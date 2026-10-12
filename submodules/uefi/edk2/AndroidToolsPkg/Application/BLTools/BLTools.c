/** @file
 *  BLTools - a standalone UEFI tool launched from the super-fastboot boot
 *  menu. Reads the DeviceInfo blob (via the Verified Boot protocol, ported from
 *  the r32 tree) and controls the bootloader unlock state: the is_unlocked and
 *  is_unlock_critical flags that verified boot consults at every boot.
 *
 *  The two flags are not independent. Android's bootloader requires that a
 *  critically-unlocked device also be unlocked, so BLTools enforces the
 *  invariant on every toggle:
 *    - enabling unlock_critical forces is_unlocked on as well; and
 *    - disabling is_unlocked forces is_unlock_critical off as well
 *      (the contrapositive: unlock=false forbids unlock_critical=true).
 *  This is the same invariant the r32 DeviceInfoInit writes and that the
 *  SetDeviceUnlockValue path (QcomModulePkg/Library/BootLib/DeviceInfo.c)
 *  assumes; persisting an unlock_critical=true / unlock=false blob would
 *  confuse verified boot, so the write path clamps it a second time as a guard.
 *
 *  Like ArbTools, the app is self-contained: it reads/writes the whole blob
 *  through VBRwDeviceState (a TEE-backed write), refuses to touch a blob whose
 *  magic does not match, and gates the write behind a deliberate confirmation.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "AtDevInfo.h"
#include "AndroidToolsUi.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof (a) / sizeof ((a)[0]))
#endif

/* ---- ported r32 DeviceInfo helpers (same as ArbTools) ------------------- */
/*
 * Self-contained per the package convention: each app ports the r32 read/write
 * wrapper it needs rather than sharing a library. These are byte-identical to
 * ArbTools' so the on-disk access semantics match exactly.
 */

EFI_STATUS
AtDevInfoRead (
  OUT DeviceInfo *DevInfo
  )
{
  QCOM_VERIFIEDBOOT_PROTOCOL *VbIntf;
  EFI_STATUS                 Status;

  if (DevInfo == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = gBS->LocateProtocol (&gEfiQcomVerifiedBootProtocolGuid, NULL,
                                (VOID **)&VbIntf);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "BL: VB protocol not found: %r\n", Status));
    return Status;
  }

  return VbIntf->VBRwDeviceState (VbIntf, READ_CONFIG, (UINT8 *)DevInfo,
                                  (UINT32)sizeof (DeviceInfo));
}

EFI_STATUS
AtDevInfoWrite (
  IN CONST DeviceInfo *DevInfo
  )
{
  QCOM_VERIFIEDBOOT_PROTOCOL *VbIntf;
  EFI_STATUS                 Status;

  if (DevInfo == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = gBS->LocateProtocol (&gEfiQcomVerifiedBootProtocolGuid, NULL,
                                (VOID **)&VbIntf);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return VbIntf->VBRwDeviceState (VbIntf, WRITE_CONFIG, (UINT8 *)DevInfo,
                                  (UINT32)sizeof (DeviceInfo));
}

/* ---- confirmation ------------------------------------------------------- */

/**
  Show Title + Warning and require one deliberate confirmation. A 1s stall and
  input flush precede the prompt so the power press that selected the action in
  the menu cannot bleed through and auto-confirm: the user must release and
  press again. Volume keys cancel.

  Returns TRUE only on a fresh power press.
**/
STATIC
BOOLEAN
BlConfirm (
  IN CONST CHAR16 *Title,
  IN CONST CHAR16 *Warning
  )
{
  AT_KEY Key;

  /* 1s: let the selecting key release, then drop anything held over so it
   * cannot confirm the prompt the instant it appears. */
  gBS->Stall (1000000);
  gST->ConIn->Reset (gST->ConIn, FALSE);

  AtUiBeginScreen (Title, NULL, 5);
  AtUiPrint (L"%s\r\n", (Warning != NULL) ? Warning : L"");
  AtUiPrint (L"\r\nPower = confirm   Vol+/- = cancel\r\n");

  Key = AtUiWaitForKey (0);
  if (Key != AtKeySelect) {
    AtUiShowMessage (L"Cancelled");
    gBS->Stall (1000000);
    return FALSE;
  }
  return TRUE;
}

/* ---- apply -------------------------------------------------------------- */

/**
  Persist a new (Unlock, Critical) state after enforcing the invariant and
  requiring confirmation. Only the two flags change; every other DeviceInfo
  field is preserved by the read-modify-write. On write failure the in-memory
  copy is rolled back so the menu keeps reflecting the on-disk state.
**/
STATIC
VOID
BlApply (
  IN OUT DeviceInfo *Info,
  IN     BOOLEAN     NewUnlock,
  IN     BOOLEAN     NewCritical,
  IN CONST CHAR16   *Action
  )
{
  BOOLEAN     OldUnlock;
  BOOLEAN     OldCritical;
  EFI_STATUS  Status;
  CHAR16      Warning[96];

  /* Second guard: never persist unlock_critical=true with unlock=false. The
   * toggles already enforce this, but clamp once more so a future caller or a
   * tampered blob cannot sneak the illegal combination through. */
  if (NewCritical && !NewUnlock) {
    NewUnlock = TRUE;
  }

  if (NewUnlock == Info->is_unlocked &&
      NewCritical == Info->is_unlock_critical) {
    AtUiShowMessage (L"State unchanged");
    AtUiWaitForKey (0);
    return;
  }

  AtUiFormat (Warning, sizeof (Warning),
                 L"%s - writes DeviceInfo. May cause data loss.",
                 AtUiText (Action));
  if (!BlConfirm (Action, Warning)) {
    return;
  }

  AtUiShowMessage (L"Applying...");

  OldUnlock   = Info->is_unlocked;
  OldCritical = Info->is_unlock_critical;
  Info->is_unlocked        = NewUnlock;
  Info->is_unlock_critical = NewCritical;

  Status = AtDevInfoWrite (Info);
  if (EFI_ERROR (Status)) {
    /* Roll back the in-memory copy; the on-disk blob was not changed. */
    Info->is_unlocked        = OldUnlock;
    Info->is_unlock_critical = OldCritical;
    AtUiReportStatus (L"Write DeviceInfo", Status);
    return;
  }

  AtUiShowMessage (L"Done. Reboot for the change to take effect.");
  AtUiWaitForKey (0);
}

/* ---- toggles ------------------------------------------------------------ */

/**
  Toggle is_unlocked. Locking the device also locks critical: unlock=false
  forces unlock_critical=false (the contrapositive of the invariant).
**/
STATIC
VOID
BlToggleUnlock (
  IN OUT DeviceInfo *Info
  )
{
  BOOLEAN NewUnlock   = !Info->is_unlocked;
  BOOLEAN NewCritical = Info->is_unlock_critical;

  if (!NewUnlock) {
    NewCritical = FALSE;
  }

  BlApply (Info, NewUnlock, NewCritical,
           NewUnlock ? L"Unlock Device" : L"Lock Device");
}

/**
  Toggle is_unlock_critical. Enabling critical forces unlock on:
  unlock_critical=true requires unlock=true.
**/
STATIC
VOID
BlToggleCritical (
  IN OUT DeviceInfo *Info
  )
{
  BOOLEAN NewCritical = !Info->is_unlock_critical;
  BOOLEAN NewUnlock   = Info->is_unlocked;

  if (NewCritical) {
    NewUnlock = TRUE;
  }

  BlApply (Info, NewUnlock, NewCritical,
           NewCritical ? L"Unlock Critical" : L"Lock Critical");
}

/* ---- entry point -------------------------------------------------------- */

EFI_STATUS
EFIAPI
BlToolsEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  DeviceInfo   Info;
  EFI_STATUS   Status;
  UINTN        Sel;
  CHAR16       Title[40];
  CHAR16       LabelUnlock[20];
  CHAR16       LabelCritical[20];
  CONST CHAR16 *Items[3];

  /*
   * The power press that selected us in the super-fastboot menu is often still
   * held when we start; AtUiEnterMenu waits for it to release and drains the
   * queue so it cannot confirm the first entry the instant the menu appears.
   */
  AtUiEnterMenu (L"BL Tools");

  Status = AtDevInfoRead (&Info);
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (L"Read DeviceInfo", Status);
    return EFI_SUCCESS;
  }

  /* Refuse to operate on an uninitialized/corrupt blob: its fields are garbage
   * and writing them back could clobber unrelated areas, so abort like ArbTools. */
  if (CompareMem (Info.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE) != 0) {
    AtUiShowMessage (L"DeviceInfo not initialized");
    AtUiWaitForKey (0);
    return EFI_SUCCESS;
  }

  while (TRUE) {
    /* The live state rides in the title bar so the action labels never
     * contradict what is actually persisted. */
    AtUiFormat (Title, sizeof (Title), L"BL Tools  Unlock:%s  Crit:%s",
                   AtUiText (Info.is_unlocked ? L"on" : L"off"),
                   AtUiText (Info.is_unlock_critical ? L"on" : L"off"));
    AtUiFormat (LabelUnlock, sizeof (LabelUnlock),
                   Info.is_unlocked ? L"Lock Device" : L"Unlock Device");
    AtUiFormat (LabelCritical, sizeof (LabelCritical),
                   Info.is_unlock_critical ? L"Lock Critical" : L"Unlock Critical");

    Items[0] = LabelUnlock;
    Items[1] = LabelCritical;
    Items[2] = L"Back";

    Status = AtUiRunMenu (Title, Items, ARRAY_SIZE (Items), &Sel,
                          L"Vol+/- move, power select");
    if (EFI_ERROR (Status)) {
      continue;
    }

    switch (Sel) {
    case 0:
      BlToggleUnlock (&Info);
      break;
    case 1:
      BlToggleCritical (&Info);
      break;
    case 2:
      /* Exit the app so control returns to the boot menu. */
      return EFI_SUCCESS;
    default:
      break;
    }
  }

  return EFI_SUCCESS;
}
