/*
 * USB mass-storage export session over the platform's EFI_USB_MSD_PROTOCOL.
 *
 * Ported from the 7.x SuperFbMassStorage.c session core, minus the managed
 * container, lease and bundled-variant machinery: this build exports plain
 * BlockIo disks (a whole UFS LUN, or a partition behind a RAM-backed fake GPT)
 * through the resident platform driver.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbUsbMsd.h"
#include "SuperFbMenu.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleTextIn.h>

/*
 * Consecutive EventHandler errors that end a session. Only reached when the
 * vendor stack is answering nothing at all; it exists so a device whose
 * console is unavailable still has a way out, since cancelling needs a key.
 * Any single non-error poll resets the run, so a blip cannot trip it.
 */
#define SFB_MSC_MAX_CONSECUTIVE_ERRORS  100000u

/* UsbfnIo is declared in QcomModulePkg.dec; the GUID is spelled out here so
 * this translation unit needs no DEC additions. */
STATIC CONST EFI_GUID mSfbUsbfnIoProtocolGuid = {
  0x32d2963a, 0xfe5d, 0x4f30,
  { 0xb6, 0x33, 0x6e, 0x5d, 0xc5, 0x58, 0x03, 0xcc }
};
STATIC CONST EFI_GUID mSfbUsbMsdProtocolGuid = {
  0xc8591faf, 0xdbcc, 0x479e,
  { 0x9e, 0xf2, 0xfd, 0x08, 0x5b, 0xc3, 0x7b, 0xc7 }
};
/* The vendor UsbConfigDxe event group that brings the device controller up. */
STATIC CONST EFI_GUID mSfbInitUsbControllerGuid = {
  0x1c0cffce, 0xfc8d, 0x4e44,
  { 0x8c, 0x78, 0x9c, 0x9e, 0x5b, 0x53, 0x0d, 0x36 }
};

STATIC
VOID
EFIAPI
SfbMsdDummyNotify (IN EFI_EVENT Event, IN VOID *Context)
{
  (VOID)Event;
  (VOID)Context;
}

/*
 * The MSD driver's StartDevice locates EFI_USBFN_IO_PROTOCOL, which does not
 * exist until the platform USB controller has been initialised. Fastboot does
 * that on entry, so an export entered from fastboot inherits a live stack; a
 * menu export on a normal boot does not, and StartDevice would fail with
 * EFI_NOT_FOUND. Signalling the vendor's InitUsbController event group brings
 * the device controller up; it claims no gadget and installs no descriptors,
 * so this is a no-op wherever USB is already up, and it needs no matching
 * release when the export ends.
 */
STATIC
VOID
SfbMsdEnsureUsbStack (VOID)
{
  EFI_STATUS Status;
  EFI_EVENT  Event;
  VOID       *Protocol = NULL;

  Status = gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbfnIoProtocolGuid, NULL,
                                &Protocol);
  if (!EFI_ERROR (Status)) {
    return;
  }

  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                               SfbMsdDummyNotify, NULL,
                               (EFI_GUID *)&mSfbInitUsbControllerGuid,
                               &Event);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: Usb controller init event not signaled: %r\n",
            Status));
    return;
  }
  gBS->SignalEvent (Event);
  gBS->CloseEvent (Event);
}

/*
 * Discard anything already queued. The chooser screen that precedes the
 * export hands over with the operator's confirm keystroke, and sometimes its
 * trailing event, still in the queue; a cancel test that accepted these would
 * abort the session before the host ever saw the device.
 */
STATIC
VOID
SfbMsdDrainKeys (VOID)
{
  EFI_INPUT_KEY Key;
  UINTN         Drained = 0;

  if (gST == NULL || gST->ConIn == NULL) {
    return;
  }
  while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
    Drained++;
  }
  if (Drained != 0) {
    DEBUG ((EFI_D_VERBOSE, "SFB: msc drained %u stale keys\n",
            (UINT32)Drained));
  }
}

/*
 * Cancel on volume-down only. "Any key" made the confirm press that started
 * the session cancel it, and it also meant a stray keypress could yank a disk
 * out from under a host mid-write.
 */
STATIC
BOOLEAN
SfbMsdCancelled (VOID)
{
  EFI_INPUT_KEY Key;

  if (gST == NULL || gST->ConIn == NULL) {
    return FALSE;
  }
  if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
    return FALSE;
  }
  return (BOOLEAN)(Key.ScanCode == SCAN_DOWN);
}

EFI_STATUS
SfbUsbMsdExportBlkIo (IN EFI_BLOCK_IO_PROTOCOL *BlkIo,
                      IN CONST CHAR16          *Title,
                      IN CONST CHAR16          *Detail,
                      IN UINT64                Bytes)
{
  EFI_STATUS            Status;
  EFI_STATUS            CleanupStatus;
  SFB_USB_MSD_PROTOCOL  *Msd = NULL;
  BOOLEAN               Cancelled = FALSE;
  BOOLEAN               HostEjected = FALSE;
  UINT32                Consecutive = 0;

  if (BlkIo == NULL || BlkIo->Media == NULL || !BlkIo->Media->MediaPresent) {
    return EFI_NO_MEDIA;
  }

  /* An export can sit idle for as long as the operator leaves it up; the
   * default 5-minute boot watchdog must not kill the device mid-session. */
  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  SfbMsdEnsureUsbStack ();

  Status = gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbMsdProtocolGuid, NULL,
                                (VOID **)&Msd);
  if (EFI_ERROR (Status) || Msd == NULL) {
    DEBUG ((EFI_D_ERROR, "SFB: no resident USB MSD driver: %r\n", Status));
    return EFI_NOT_FOUND;
  }
  if (Msd->AssignBlkIoHandle == NULL || Msd->StopDevice == NULL ||
      Msd->StartDevice == NULL || Msd->EventHandler == NULL) {
    return EFI_UNSUPPORTED;
  }

  if (BlkIo->FlushBlocks != NULL) {
    (VOID)BlkIo->FlushBlocks (BlkIo);
  }

  Status = Msd->AssignBlkIoHandle (Msd, BlkIo, 0);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: msc assign lun0: %r\n", Status));
    return Status;
  }

  /*
   * Draw after assigning the LUN and before draining keys. The chooser's
   * confirm keystroke is still queued here, so the screen must be visible
   * before the drain hands control to the export loop.
   */
  SfbBeginScreen ((Title != NULL) ? Title : L"USB Mass Storage",
                  L"The host may now mount the disk.");
  if (Detail != NULL && Detail[0] != L'\0') {
    Print (L"  %s\r\n", Detail);
  }
  if (Bytes != 0) {
    Print (L"  Size: %Lu bytes (%Lu MiB)\r\n", Bytes, Bytes / (1024 * 1024));
  }
  Print (L"\r\n");
  Print (L"  Volume Down ends this session.\r\n");
  SfbEndScreen (L"Volume Down: stop export");

  /* Nothing queued may reach the cancel test. */
  SfbMsdDrainKeys ();

  Status = Msd->StartDevice (Msd);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: msc start: %r\n", Status));
    /* StartDevice may have partially claimed the shared gadget before
     * reporting failure; stop it so a failed start cannot leave the gadget
     * half-owned. */
    (VOID)Msd->StopDevice (Msd);
    (VOID)Msd->AssignBlkIoHandle (Msd, NULL, 0);
    return Status;
  }

  /*
   * Pump the handler first and test for cancel second, which is the order the
   * Mu-Silicium reference client uses. The first poll then lands before any
   * console access, which is where it is needed: the host begins enumerating
   * the moment StartDevice returns and Linux scans one second later.
   *
   * The return value is counted but a single error does not end the session:
   * the vendor header documents EventHandler's error returns as "?" and the
   * reference client discards the value entirely. Only an unbroken run of
   * errors ends it. EFI_MEDIA_CHANGED is tested before the error arm: the
   * driver reports a host eject (SCSI START STOP UNIT with LOEJ) with that
   * status, and it must not be counted as a stalled poll.
   */
  while (TRUE) {
    Status = Msd->EventHandler (Msd);

    if (Status == EFI_MEDIA_CHANGED) {
      HostEjected = TRUE;
      break;
    }
    if (EFI_ERROR (Status)) {
      Consecutive++;
      if (Consecutive >= SFB_MSC_MAX_CONSECUTIVE_ERRORS) {
        break;
      }
    } else {
      Consecutive = 0;
    }

    if (SfbMsdCancelled ()) {
      Cancelled = TRUE;
      break;
    }
  }

  CleanupStatus = Msd->StopDevice (Msd);
  if (Msd->AssignBlkIoHandle != NULL) {
    (VOID)Msd->AssignBlkIoHandle (Msd, NULL, 0);
  }

  /* Volume Down itself is consumed by the cancel test, but its trailing
   * events and anything pressed while the host was mounting are still queued,
   * and the next screen receives them as its own input. */
  SfbMsdDrainKeys ();

  DEBUG ((EFI_D_INFO,
          "SFB: msc session ending=%a cleanup=%r\n",
          HostEjected ? "host-eject" : (Cancelled ? "volume-down" : "gave-up"),
          CleanupStatus));

  if (HostEjected) {
    return EFI_MEDIA_CHANGED;
  }
  return Cancelled ? EFI_ABORTED : (EFI_ERROR (CleanupStatus) ? CleanupStatus
                                                             : EFI_SUCCESS);
}
