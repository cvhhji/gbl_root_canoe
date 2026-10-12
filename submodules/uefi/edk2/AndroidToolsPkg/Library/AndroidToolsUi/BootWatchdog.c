#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/EFIKernelInterface.h>

#include "AndroidToolsUi.h"

typedef EFI_STATUS (EFIAPI *DISABLE_WATCHDOG)(VOID);

typedef struct {
  UINT64 Revision;
  VOID *Enable;
  DISABLE_WATCHDOG Disable;
} QCOM_WATCHDOG;

typedef struct {
  UINT64 Revision;
  VOID *Reserved[2];
  DISABLE_WATCHDOG Disable;
} PHOENIX_WATCHDOG;

STATIC EFI_GUID mQcomWatchdogGuid = {
  0x6f8b0fa0, 0x034f, 0x47a4, { 0x8c, 0x7a, 0xbc, 0xec, 0x55, 0xb4, 0x1c, 0x64 }
};
STATIC EFI_GUID mPhoenixGuid = {
  0x7d2a39f3, 0x0f8c, 0x47a0, { 0x9b, 0x51, 0xf2, 0x69, 0xb4, 0xba, 0xf9, 0x93 }
};
STATIC EFI_GUID mPhoenixStoppedGuid = {
  0x45e62fa1, 0xd17a, 0x45b6, { 0xa1, 0x9c, 0xf5, 0x04, 0x61, 0xe6, 0xd8, 0x29 }
};
STATIC BOOLEAN mPhoenixStopped;

VOID
AtUiDisableWatchdogs (VOID)
{
  EFI_STATUS Status;
  EFI_STATUS HardwareStatus = EFI_UNSUPPORTED;
  QCOM_WATCHDOG *Watchdog = NULL;
  EFI_KERNEL_PROTOCOL *Kernel = NULL;
  PHOENIX_WATCHDOG *Phoenix = NULL;
  EFI_HANDLE MarkerHandle = NULL;
  VOID *Marker = NULL;

  Status = gBS->SetWatchdogTimer (0, 0, 0, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Boot: cannot disable UEFI watchdog: %r\n", Status));
  }

  Status = gBS->LocateProtocol (&mQcomWatchdogGuid, NULL, (VOID **)&Watchdog);
  if (!EFI_ERROR (Status) && Watchdog != NULL &&
      Watchdog->Revision >= 0x00010000 && Watchdog->Revision <= 0x00010002 &&
      Watchdog->Disable != NULL) {
    HardwareStatus = Watchdog->Disable ();
    if (EFI_ERROR (HardwareStatus)) {
      DEBUG ((DEBUG_ERROR, "Boot: cannot disable Qcom watchdog: %r\n", HardwareStatus));
    }
  }

  if (EFI_ERROR (HardwareStatus)) {
    Status = gBS->LocateProtocol (&gEfiKernelProtocolGuid, NULL, (VOID **)&Kernel);
    if (!EFI_ERROR (Status) && Kernel != NULL &&
        Kernel->Version >= EFI_KERNEL_PROTOCOL_VER_WDOG_INTF &&
        Kernel->WDog != NULL && Kernel->WDog->WdogDisable != NULL) {
      Kernel->WDog->WdogDisable ();
    }
  }

  if (mPhoenixStopped) {
    return;
  }
  Status = gBS->LocateProtocol (&mPhoenixStoppedGuid, NULL, &Marker);
  if (!EFI_ERROR (Status)) {
    mPhoenixStopped = TRUE;
    return;
  }

  Status = gBS->LocateProtocol (&mPhoenixGuid, NULL, (VOID **)&Phoenix);
  if (EFI_ERROR (Status) || Phoenix == NULL) {
    return;
  }
  if (Phoenix->Revision != 1 || Phoenix->Disable == NULL) {
    DEBUG ((DEBUG_ERROR, "Boot: unsupported Phoenix watchdog interface\n"));
    return;
  }

  Status = Phoenix->Disable ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Boot: cannot stop Phoenix boot timer: %r\n", Status));
    return;
  }
  mPhoenixStopped = TRUE;
  Status = gBS->InstallProtocolInterface (&MarkerHandle, &mPhoenixStoppedGuid,
                                          EFI_NATIVE_INTERFACE, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Boot: cannot mark Phoenix timer stopped: %r\n", Status));
  }
}
