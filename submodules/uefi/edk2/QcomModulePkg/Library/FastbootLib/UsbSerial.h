/* Platform USB identity shared by the descriptor and fastboot variables. */
#ifndef CANOE_USB_SERIAL_H
#define CANOE_USB_SERIAL_H
#include <Uefi.h>
EFI_STATUS SfbUsbSerial (CHAR8 *Output, UINTN Capacity);
#endif
