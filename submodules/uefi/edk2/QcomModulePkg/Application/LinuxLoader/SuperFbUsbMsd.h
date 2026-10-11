/*
 * USB mass-storage export over the platform's EFI_USB_MSD_PROTOCOL.
 *
 * The driver behind that protocol owns everything above BlockIo: the
 * descriptor set, enumeration, EP0 class requests (Get Max LUN, BOT reset),
 * the BOT state machine and the SCSI decoder. The client assigns a BlockIo to
 * a LUN slot, starts the device and pumps events. It is the same stack
 * fastboot uses.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_USB_MSD_H__
#define __SUPER_FB_USB_MSD_H__

#include <Uefi.h>
#include <Protocol/BlockIo.h>

/*
 * EFI_USB_MSD_PROTOCOL, mirrored from the public Mu-Silicium EFIUsbMsd.h and
 * verified field-for-field against the driver's own EFIUsbMsdPeripheral.h
 * (revision 0x00010003).
 */
typedef struct _SFB_USB_MSD_PROTOCOL SFB_USB_MSD_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *SFB_USB_MSD_ASSIGN_BLK_IO) (
  IN SFB_USB_MSD_PROTOCOL  *This,
  IN EFI_BLOCK_IO_PROTOCOL *BlkIo,
  IN UINT32                Lun
  );

typedef
EFI_STATUS
(EFIAPI *SFB_USB_MSD_QUERY_MAX_LUN) (
  IN SFB_USB_MSD_PROTOCOL *This,
  OUT UINT8               *Count
  );

typedef
EFI_STATUS
(EFIAPI *SFB_USB_MSD_EVENT_HANDLER) (IN SFB_USB_MSD_PROTOCOL *This);

typedef
EFI_STATUS
(EFIAPI *SFB_USB_MSD_START_DEVICE) (IN SFB_USB_MSD_PROTOCOL *This);

typedef
EFI_STATUS
(EFIAPI *SFB_USB_MSD_STOP_DEVICE) (IN SFB_USB_MSD_PROTOCOL *This);

struct _SFB_USB_MSD_PROTOCOL {
  UINT32                     Revision;
  SFB_USB_MSD_ASSIGN_BLK_IO  AssignBlkIoHandle;
  SFB_USB_MSD_QUERY_MAX_LUN  QueryMaxLun;
  SFB_USB_MSD_EVENT_HANDLER  EventHandler;
  SFB_USB_MSD_START_DEVICE   StartDevice;
  SFB_USB_MSD_STOP_DEVICE    StopDevice;
  VOID                       *GetDeviceSpeed;
  VOID                       *UnmountHandle;
  VOID                       *MountHandle;
  VOID                       *FindPartitions;
};

/*
 * Export one BlockIo disk to the connected PC as USB mass-storage LUN 0 and
 * run the session until it ends. Title names the screen; Detail is one extra
 * line under it (partition/lun name). Bytes is the exported size shown to the
 * operator (0 prints no size line).
 *
 * Returns EFI_MEDIA_CHANGED when the host ended the session itself (SCSI
 * START STOP UNIT with LOEJ; only some drivers report it), EFI_ABORTED when
 * the operator stopped it with Volume Down, EFI_SUCCESS when the session gave
 * up on its own after a sustained run of unanswered polls, and anything else
 * is the platform error from a session that could not be started at all.
 */
EFI_STATUS
SfbUsbMsdExportBlkIo (IN EFI_BLOCK_IO_PROTOCOL *BlkIo,
                      IN CONST CHAR16          *Title,
                      IN CONST CHAR16          *Detail,
                      IN UINT64                Bytes);

#endif /* __SUPER_FB_USB_MSD_H__ */
