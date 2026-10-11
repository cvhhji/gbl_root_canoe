/* Read the same platform identity used by the mass-storage producer. Never
 * fabricate a shared serial: it cannot bind a reviewed operation to a phone. */
#include "UsbSerial.h"
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/EFIUsbfnIo.h>

EFI_STATUS
SfbUsbSerial (CHAR8 *Output, UINTN Capacity)
{
  EFI_USBFN_IO_PROTOCOL *Usb = NULL;
  EFI_STATUS Status;
  CHAR16 Value[64] = {0};
  UINTN Bytes = sizeof (Value);
  UINTN Count;
  UINTN Index;
  BOOLEAN Nonzero = FALSE;
  if (Output == NULL || Capacity == 0) return EFI_INVALID_PARAMETER;
  Output[0] = '\0';
  Status = gBS->LocateProtocol (&gEfiUsbfnIoProtocolGuid, NULL, (VOID **)&Usb);
  if (EFI_ERROR (Status)) return Status;
  if (Usb == NULL || Usb->GetDeviceInfo == NULL) return EFI_UNSUPPORTED;
  Status = Usb->GetDeviceInfo (Usb, EfiUsbDeviceInfoSerialNumber, &Bytes, Value);
  if (EFI_ERROR (Status)) return Status;
  if (Bytes == 0 || Bytes > sizeof (Value) || (Bytes % sizeof (CHAR16)) != 0)
    return EFI_COMPROMISED_DATA;
  Count = Bytes / sizeof (CHAR16);
  /* USBFn may include a final terminator; the USB string itself does not. */
  if (Value[Count - 1] == 0) Count--;
  if (Count == 0) return EFI_NOT_FOUND;
  if (Count >= Capacity) return EFI_BUFFER_TOO_SMALL;
  for (Index = 0; Index < Count; Index++) {
    CHAR16 Ch = Value[Index];
    if (!((Ch >= '0' && Ch <= '9') || (Ch >= 'A' && Ch <= 'Z') ||
          (Ch >= 'a' && Ch <= 'z') || Ch == '-' || Ch == '_' || Ch == '.'))
      return EFI_COMPROMISED_DATA;
    if (Ch != '0') Nonzero = TRUE;
  }
  if (!Nonzero) return EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) Output[Index] = (CHAR8)Value[Index];
  Output[Count] = '\0';
  return EFI_SUCCESS;
}
