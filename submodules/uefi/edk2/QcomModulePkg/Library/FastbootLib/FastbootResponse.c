/* Bounded fastboot response framing. SPDX-License-Identifier: BSD-3-Clause */
#include "FastbootResponse.h"

UINTN
SfbFastbootEncodeResponse (
  CONST CHAR8 Code[4], CONST CHAR8 *Reason,
  CHAR8 Response[SFB_FASTBOOT_RESPONSE_BYTES + 1u])
{
  UINTN Length = 4;
  UINTN Index;

  for (Index = 0; Index < 4; ++Index) Response[Index] = Code[Index];
  if (Reason != NULL) {
    while (*Reason != '\0' && Length < SFB_FASTBOOT_RESPONSE_BYTES)
      Response[Length++] = *Reason++;
  }
  Response[Length] = '\0';
  return Length;
}

VOID
SfbFastbootGetVarInfo (
  CONST CHAR8 *Name, CONST CHAR8 *Value, SFB_FASTBOOT_EMIT_INFO Emit)
{
  CONST CHAR8 *Parts[3];
  CHAR8 Payload[SFB_FASTBOOT_PAYLOAD_BYTES + 1u];
  UINTN Length = 0;
  UINTN Part;

  Parts[0] = Name == NULL ? "" : Name;
  Parts[1] = ":";
  Parts[2] = Value == NULL ? "" : Value;
  for (Part = 0; Part < 3; ++Part) {
    CONST CHAR8 *Next = Parts[Part];
    while (*Next != '\0') {
      Payload[Length++] = *Next++;
      if (Length == SFB_FASTBOOT_PAYLOAD_BYTES) {
        Payload[Length] = '\0';
        Emit (Payload);
        Length = 0;
      }
    }
  }
  if (Length != 0) {
    Payload[Length] = '\0';
    Emit (Payload);
  }
}
