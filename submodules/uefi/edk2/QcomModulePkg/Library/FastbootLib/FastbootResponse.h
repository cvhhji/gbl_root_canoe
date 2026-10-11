/* Bounded fastboot response framing. SPDX-License-Identifier: BSD-3-Clause */
#ifndef CANOE_FASTBOOT_RESPONSE_H
#define CANOE_FASTBOOT_RESPONSE_H

#include <Uefi.h>

/* The four-byte status is included in the protocol's 64-byte packet limit.
 * The extra local byte is only a string terminator; it is never transmitted. */
#define SFB_FASTBOOT_RESPONSE_BYTES 64u
#define SFB_FASTBOOT_PAYLOAD_BYTES (SFB_FASTBOOT_RESPONSE_BYTES - 4u)

UINTN SfbFastbootEncodeResponse (
  CONST CHAR8 Code[4], CONST CHAR8 *Reason,
  CHAR8 Response[SFB_FASTBOOT_RESPONSE_BYTES + 1u]);

typedef VOID (*SFB_FASTBOOT_EMIT_INFO) (CONST CHAR8 *Payload);

/* Emit the complete name:value as consecutive INFO payloads. Ordinary short
 * variables keep their single-packet shape; longer values are never lost. */
VOID SfbFastbootGetVarInfo (
  CONST CHAR8 *Name, CONST CHAR8 *Value, SFB_FASTBOOT_EMIT_INFO Emit);

#endif
