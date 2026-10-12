#ifndef __BOOT_MENU_TEXT_H__
#define __BOOT_MENU_TEXT_H__

#include <Uefi.h>

#define BOOT_MENU_TEXT_GUID \
  { 0xeb52b973, 0xcc25, 0x48e8, { 0x91, 0x4d, 0x3a, 0x61, 0x26, 0xb5, 0x1c, 0x04 } }

typedef struct {
  UINT32 Revision;
  CONST CHAR16 *(EFIAPI *Translate)(IN CONST CHAR16 *Text);
  UINTN (EFIAPI *Lines)(IN CONST CHAR16 *Text);
  VOID (EFIAPI *Clear)(IN UINTN Rows);
  VOID (EFIAPI *Output)(IN CONST CHAR16 *Text);
  VOID (EFIAPI *DrawRow)(IN BOOLEAN Selected, IN CONST CHAR16 *Marker,
                        IN CONST CHAR16 *Text);
} BOOT_MENU_TEXT;

#endif
