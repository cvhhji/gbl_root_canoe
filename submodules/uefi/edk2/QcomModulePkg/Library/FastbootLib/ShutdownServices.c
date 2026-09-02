/** @file
 *
 *  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
 *
 *  This program and the accompanying materials
 *  are licensed and made available under the terms and conditions of the BSD
 *License
 *  which accompanies this distribution.  The full text of the license may be
 *found at
 *  http://opensource.org/licenses/bsd-license.php
 *
 *  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR
 *IMPLIED.
 */

 /*
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted (subject to the limitations in the
 *  disclaimer below) provided that the following conditions are met:
 *
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials provided
 *        with the distribution.
 *
 *      * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *        contributors may be used to endorse or promote products derived
 *        from this software without specific prior written permission.
 *
 *  NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 *  GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 *  HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 *   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 *  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 *  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 *  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ShutdownServices.h"

#include <FastbootLib/FastbootCmds.h>
#include <Guid/ArmMpCoreInfo.h>
#include <Guid/FileInfo.h>
#include <Guid/GlobalVariable.h>
#include <Library/ArmLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/HobLib.h>
#include <Library/LinuxLoaderLib.h>
#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/EFIDisplayPwr.h>

STATIC
EFI_STATUS
ClearResetReason ()
{
  EFI_RESETREASON_PROTOCOL *RstReasonIf;
  EFI_STATUS Status = gBS->LocateProtocol (&gEfiResetReasonProtocolGuid, NULL,
                                           (VOID **)&RstReasonIf);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Error locating the reset reason protocol\n"));
    return Status;
  }

  if (RstReasonIf->Revision >= EFI_RESETREASON_PROTOCOL_REVISION)
    RstReasonIf->ClearResetReason (RstReasonIf);
  return Status;
}

STATIC
EFI_STATUS
DisplayTurnOff(VOID)
{
  EFI_DISPLAY_POWER_PROTOCOL* DisplayPower = NULL;
  EFI_GUID Guid = {0x7BFA4293, 0x7AA4, 0x4375, {0xB6, 0x3C, 0xB6, 0xAA, 0xB7, 0x86, 0xC4, 0x3C}};

  EFI_STATUS Status = gBS->LocateProtocol(
    &Guid,
    NULL,
    (VOID**)&DisplayPower
  );

  if (EFI_ERROR(Status) || DisplayPower == NULL) {
    DEBUG((DEBUG_WARN,
      "DisplayTurnOff: LocateProtocol failed: %r\n", Status));
    return EFI_NOT_READY;
  }

  Status = DisplayPower->SetDisplayPowerState(0, 0);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR,
      "DisplayTurnOff: SetDisplayPowerState failed: %r\n", Status));
    return Status;
  }

  DEBUG((DEBUG_INFO, "DisplayTurnOff: done\n"));

  return EFI_SUCCESS;
}

VOID
RebootDevice (UINT8 RebootReason)
{
  ResetDataType ResetData;
  EFI_STATUS Status = EFI_INVALID_PARAMETER;

  WaitForFlashFinished ();
  DisplayTurnOff();

  StrnCpyS (ResetData.DataBuffer, ARRAY_SIZE (ResetData.DataBuffer),
            (CONST CHAR16 *)STR_RESET_PARAM, ARRAY_SIZE (STR_RESET_PARAM) - 1);
  ResetData.Bdata = RebootReason;
  if (RebootReason == NORMAL_MODE)
    Status = EFI_SUCCESS;

  if (RebootReason == EMERGENCY_DLOAD)
    gRT->ResetSystem (EfiResetPlatformSpecific, EFI_SUCCESS,
                      StrSize ((CONST CHAR16 *)STR_RESET_PLAT_SPECIFIC_EDL),
                      STR_RESET_PLAT_SPECIFIC_EDL);

  gRT->ResetSystem (EfiResetCold, Status, sizeof (ResetDataType),
                    (VOID *)&ResetData);
}

VOID ShutdownDevice (VOID)
{
  EFI_STATUS Status = EFI_INVALID_PARAMETER;

  WaitForFlashFinished ();
  ClearResetReason ();
  DisplayTurnOff();

  gRT->ResetSystem (EfiResetShutdown, Status, 0, NULL);

  /* Flow never comes here and is fatal if it comes here.*/
  ASSERT (0);
}
