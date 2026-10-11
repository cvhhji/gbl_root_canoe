/*
 * RAM-backed fake-GPT wrapper around one BlockIo extent.
 *
 * A raw partition exported to a PC reads back with no partition table at LBA0,
 * so the host shows it as an uninitialized disk and offers to initialize it -
 * which would write an MBR/GPT stub straight into the partition's first
 * blocks. The wrapper prepends a GPT built in RAM: the host sees a valid
 * one-partition disk, the exported extent appears as that partition, and any
 * host write to the GPT region is swallowed before it can reach the backing
 * device.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_SYNTH_DISK_H__
#define __SUPER_FB_SYNTH_DISK_H__

#include <Uefi.h>
#include <Protocol/BlockIo.h>

typedef struct {
  /* The public interface; hand this to the USB MSD driver. */
  EFI_BLOCK_IO_PROTOCOL  BlockIo;
  EFI_BLOCK_IO_MEDIA     Media;

  /* PassThrough forwards the backing device one-to-one (whole-LUN export),
   * optionally forcing writes off; the fake-GPT layout below is unused then. */
  BOOLEAN                PassThrough;

  /* Backing storage (a partition child or a whole LUN). */
  EFI_BLOCK_IO_PROTOCOL  *Backing;
  /* Backing block index the data area starts at, and its block count. */
  UINT64                 FirstDataBlock;
  UINT64                 DataBlocks;

  /* Export-space geometry. DataStartLba .. DataStartLba + DataBlocks - 1 is
   * the data area; everything before it and the trailing backup copy are
   * served from the RAM tables below. */
  UINT64                 DataStartLba;
  UINT64                 TotalBlocks;
  UINT32                 BlockSize;

  UINT8                  *Mbr;          /* one block */
  UINT8                  *Header;       /* one block, primary (MyLBA=1) */
  UINT8                  *BackupHeader; /* one block, mirrored at the last LBA */
  UINT8                  *Entries;      /* EntryBlocks * BlockSize */
  UINT32                 EntryBlocks;
} SFB_SYNTH_DISK;

/*
 * Build a wrapper publishing [FirstDataBlock, FirstDataBlock + DataBlocks) of
 * Backing as a one-partition GPT disk. TypeGuid/Name are placed in the
 * partition entry (the real partition's own values read best on the host);
 * either may be NULL for the Linux-filesystem default and an empty name.
 *
 * ReadOnly turns the whole export read-only; in either mode the RAM GPT
 * region itself is always write-protected - a host write aimed at it is
 * accepted and dropped, so a host-side "initialize disk" can never stamp over
 * the exported extent's first blocks. The caller owns *Disk and releases it
 * with SfbSynthDiskDestroy after the export session ends.
 */
EFI_STATUS
SfbSynthDiskCreate (IN EFI_BLOCK_IO_PROTOCOL *Backing,
                    IN UINT64                FirstDataBlock,
                    IN UINT64                DataBlocks,
                    IN CONST EFI_GUID        *TypeGuid OPTIONAL,
                    IN CONST CHAR16          *Name OPTIONAL,
                    IN BOOLEAN               ReadOnly,
                    OUT SFB_SYNTH_DISK       **Disk);

/*
 * One-to-one wrapper around a whole BlockIo disk (whole-LUN export). Reads
 * pass straight through, including the real GPT at LBA0; ReadOnly forces
 * EFI_WRITE_PROTECTED on every write.
 */
EFI_STATUS
SfbPassDiskCreate (IN EFI_BLOCK_IO_PROTOCOL *Backing,
                   IN BOOLEAN               ReadOnly,
                   OUT SFB_SYNTH_DISK       **Disk);

VOID
SfbSynthDiskDestroy (IN SFB_SYNTH_DISK *Disk);

#endif /* __SUPER_FB_SYNTH_DISK_H__ */
