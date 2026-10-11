/*
 * RAM-backed fake-GPT wrapper (and one-to-one pass-through wrapper) around
 * BlockIo extents. See SuperFbSynthDisk.h.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbSynthDisk.h"

#include <Uefi/UefiGpt.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

/* One entry of 128 bytes, entry array sized as the spec's 128 entries so
 * every host-side parser finds the layout it expects. */
#define SFB_SYNTH_ENTRY_BYTES  128
#define SFB_SYNTH_ENTRY_COUNT  128
#define SFB_SYNTH_ENTRIES_BYTES  (SFB_SYNTH_ENTRY_BYTES * SFB_SYNTH_ENTRY_COUNT)
#define SFB_SYNTH_GPT_HDR_SIZE  92

/* Linux filesystem data: a benign default for a partition whose real type
 * GUID the caller did not supply. */
STATIC CONST EFI_GUID mSfbSynthDefaultTypeGuid = {
  0x0fc63daf, 0x8483, 0x4772,
  { 0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4 }
};
/* Fixed GUIDs for the synthesized table; only need to be stable within one
 * export session. */
STATIC CONST EFI_GUID mSfbSynthDiskGuid = {
  0x63616e6f, 0x6578, 0x706f,
  { 0x72, 0x74, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 }
};
STATIC CONST EFI_GUID mSfbSynthPartGuid = {
  0x63616e6f, 0x6578, 0x706f,
  { 0x72, 0x74, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00 }
};

#define SFB_SYNTH_MEDIA_ID  0x53594E54  /* 'SYNT' */

/* ---- little-endian stores into the RAM tables ---------------------------- */

STATIC
VOID
SfbSynthPut32 (IN UINT8 *At, IN UINT32 Value)
{
  At[0] = (UINT8)(Value);
  At[1] = (UINT8)(Value >> 8);
  At[2] = (UINT8)(Value >> 16);
  At[3] = (UINT8)(Value >> 24);
}

STATIC
VOID
SfbSynthPut64 (IN UINT8 *At, IN UINT64 Value)
{
  SfbSynthPut32 (At, (UINT32)Value);
  SfbSynthPut32 (At + 4, (UINT32)(Value >> 32));
}

/* Forwarded I/O must honour the backing device's alignment requirement; bounce
 * through an aligned allocation when the caller's buffer does not. */
STATIC
EFI_STATUS
SfbSynthForward (IN EFI_BLOCK_IO_PROTOCOL *Backing,
                 IN BOOLEAN               Write,
                 IN UINT64                BackingBlock,
                 IN VOID                  *Buffer,
                 IN UINTN                 Bytes)
{
  EFI_STATUS  Status;
  UINTN       Align = Backing->Media->IoAlign;
  VOID        *Bounce = NULL;
  VOID        *IoBuffer = Buffer;

  if (Align < 2) {
    Align = 1;
  }
  if (((UINTN)Buffer % Align) != 0) {
    Bounce = AllocateAlignedPages (EFI_SIZE_TO_PAGES (Bytes),
                                   (UINTN)(Align > 8 ? Align : 8));
    if (Bounce == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    IoBuffer = Bounce;
    if (Write) {
      CopyMem (Bounce, Buffer, Bytes);
    }
  }

  if (Write) {
    Status = Backing->WriteBlocks (Backing, Backing->Media->MediaId,
                                   (EFI_LBA)BackingBlock, Bytes, IoBuffer);
  } else {
    Status = Backing->ReadBlocks (Backing, Backing->Media->MediaId,
                                  (EFI_LBA)BackingBlock, Bytes, IoBuffer);
    if (!EFI_ERROR (Status) && Bounce != NULL) {
      CopyMem (Buffer, Bounce, Bytes);
    }
  }

  if (Bounce != NULL) {
    FreeAlignedPages (Bounce, EFI_SIZE_TO_PAGES (Bytes));
  }

  return Status;
}

/* ---- BlockIo callbacks --------------------------------------------------- */

STATIC
EFI_STATUS
EFIAPI
SfbSynthReset (IN EFI_BLOCK_IO_PROTOCOL *This, IN BOOLEAN ExtendedVerification)
{
  SFB_SYNTH_DISK  *Disk = (SFB_SYNTH_DISK *)This;

  (VOID)ExtendedVerification;
  if (Disk == NULL || Disk->Backing == NULL) {
    return EFI_DEVICE_ERROR;
  }
  return Disk->Backing->Reset (Disk->Backing, FALSE);
}

STATIC
EFI_STATUS
SfbSynthCheck (IN SFB_SYNTH_DISK *Disk,
               IN UINT32         MediaId,
               IN UINT64         Lba,
               IN UINTN          BufferSize,
               IN VOID           *Buffer,
               IN UINTN          *Blocks)
{
  if (Buffer == NULL || BufferSize == 0 ||
      (BufferSize % Disk->BlockSize) != 0) {
    return EFI_INVALID_PARAMETER;
  }
  *Blocks = BufferSize / Disk->BlockSize;
  if (Lba + *Blocks > Disk->TotalBlocks) {
    return EFI_INVALID_PARAMETER;
  }
  if (MediaId != Disk->Media.MediaId) {
    return EFI_MEDIA_CHANGED;
  }
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SfbSynthReadBlocks (IN EFI_BLOCK_IO_PROTOCOL *This,
                    IN UINT32                MediaId,
                    IN EFI_LBA               Lba,
                    IN UINTN                 BufferSize,
                    OUT VOID                 *Buffer)
{
  SFB_SYNTH_DISK  *Disk = (SFB_SYNTH_DISK *)This;
  EFI_STATUS      Status;
  UINT64          FirstLba = (UINT64)Lba;
  UINTN           Blocks;
  UINTN           Index;
  UINT8           *Out = Buffer;
  UINT64          LastLba = Disk->TotalBlocks - 1;
  UINT64          BackupEntries = LastLba - Disk->EntryBlocks;
  UINT64          DataEnd = Disk->DataStartLba + Disk->DataBlocks;

  Status = SfbSynthCheck (Disk, MediaId, FirstLba, BufferSize, Buffer, &Blocks);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  /* Whole-LUN pass-through and requests entirely inside the data area are
   * one contiguous forwarding; both spaces are contiguous runs. */
  if (Disk->PassThrough ||
      (FirstLba >= Disk->DataStartLba && FirstLba + Blocks <= DataEnd)) {
    UINT64  DataOffset = Disk->PassThrough ? FirstLba
                                           : FirstLba - Disk->DataStartLba;
    return SfbSynthForward (Disk->Backing, FALSE,
                            Disk->FirstDataBlock + DataOffset,
                            Buffer, BufferSize);
  }

  /* Mixed request: serve each block from its own region. */
  for (Index = 0; Index < Blocks; Index++) {
    UINT64  Cur = FirstLba + Index;
    UINT8   *Dest = Out + (UINTN)Index * Disk->BlockSize;

    if (Cur == 0) {
      CopyMem (Dest, Disk->Mbr, Disk->BlockSize);
    } else if (Cur == 1) {
      CopyMem (Dest, Disk->Header, Disk->BlockSize);
    } else if (Cur == LastLba) {
      CopyMem (Dest, Disk->BackupHeader, Disk->BlockSize);
    } else if (Cur >= 2 && Cur < 2 + Disk->EntryBlocks) {
      CopyMem (Dest,
               Disk->Entries + (UINTN)(Cur - 2) * Disk->BlockSize,
               Disk->BlockSize);
    } else if (Cur >= BackupEntries && Cur < LastLba) {
      CopyMem (Dest,
               Disk->Entries + (UINTN)(Cur - BackupEntries) * Disk->BlockSize,
               Disk->BlockSize);
    } else {
      UINT64  DataOffset = Cur - Disk->DataStartLba;
      Status = SfbSynthForward (Disk->Backing, FALSE,
                                Disk->FirstDataBlock + DataOffset,
                                Dest, Disk->BlockSize);
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
  }

  return EFI_SUCCESS;
}

/*
 * The protective contract of this wrapper: a host write aimed at the RAM GPT
 * region is accepted and dropped, in both mount modes. That is what keeps a
 * host-side "initialize disk" from stamping over the exported extent's first
 * blocks. Only the data area reaches the backing device.
 */
STATIC
EFI_STATUS
EFIAPI
SfbSynthWriteBlocks (IN EFI_BLOCK_IO_PROTOCOL *This,
                     IN UINT32                MediaId,
                     IN EFI_LBA               Lba,
                     IN UINTN                 BufferSize,
                     IN VOID                  *Buffer)
{
  SFB_SYNTH_DISK  *Disk = (SFB_SYNTH_DISK *)This;
  EFI_STATUS      Status;
  UINT64          FirstLba = (UINT64)Lba;
  UINTN           Blocks;
  UINTN           Index;
  CONST UINT8     *In = Buffer;
  UINT64          LastLba = Disk->TotalBlocks - 1;
  UINT64          BackupEntries = LastLba - Disk->EntryBlocks;
  UINT64          DataEnd = Disk->DataStartLba + Disk->DataBlocks;

  if (Disk->Media.ReadOnly) {
    /*
     * Drop the write and report success. Windows automounts every volume it
     * recognises on an attached disk and fires writes (dirty bits, journal
     * replay markers) even on one it should treat as write-protected, and
     * the resident MSD driver does not translate a failed BlkIo write into
     * a clean SCSI error - the host then blocks forever waiting for the
     * bulk transfer the driver never finishes, which presents as
     * DiskGenius/Disk Management hanging on attach. A silently swallowed
     * write cannot hang anything; the read-only choice means nothing was
     * meant to persist anyway. Media.ReadOnly stays set so drivers that do
     * honour it still report write-protect through SCSI mode sense.
     */
    return EFI_SUCCESS;
  }
  Status = SfbSynthCheck (Disk, MediaId, FirstLba, BufferSize, Buffer, &Blocks);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Disk->PassThrough ||
      (FirstLba >= Disk->DataStartLba && FirstLba + Blocks <= DataEnd)) {
    UINT64  DataOffset = Disk->PassThrough ? FirstLba
                                           : FirstLba - Disk->DataStartLba;
    return SfbSynthForward (Disk->Backing, TRUE,
                            Disk->FirstDataBlock + DataOffset,
                            Buffer, BufferSize);
  }

  for (Index = 0; Index < Blocks; Index++) {
    UINT64        Cur = FirstLba + Index;
    CONST UINT8   *Src = In + (UINTN)Index * Disk->BlockSize;

    if (Cur == 0 || Cur == 1 || Cur == LastLba ||
        (Cur >= 2 && Cur < 2 + Disk->EntryBlocks) ||
        (Cur >= BackupEntries && Cur < LastLba)) {
      /* RAM GPT region: swallowed, never forwarded. */
      continue;
    }
    {
      UINT64  DataOffset = Cur - Disk->DataStartLba;
      Status = SfbSynthForward (Disk->Backing, TRUE,
                                Disk->FirstDataBlock + DataOffset,
                                (VOID *)Src, Disk->BlockSize);
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SfbSynthFlush (IN EFI_BLOCK_IO_PROTOCOL *This)
{
  SFB_SYNTH_DISK  *Disk = (SFB_SYNTH_DISK *)This;

  /* Nothing a read-only export ever dropped needs flushing, and a flushed
   * write-protected device is another vendor error path worth avoiding. */
  if (Disk->Media.ReadOnly) {
    return EFI_SUCCESS;
  }
  if (Disk->Backing->FlushBlocks == NULL) {
    return EFI_SUCCESS;
  }
  return Disk->Backing->FlushBlocks (Disk->Backing);
}

/* ---- construction -------------------------------------------------------- */

STATIC
EFI_STATUS
SfbSynthDiskInitCommon (IN EFI_BLOCK_IO_PROTOCOL *Backing,
                        OUT SFB_SYNTH_DISK       **Disk)
{
  SFB_SYNTH_DISK  *Out;

  if (Backing == NULL || Backing->Media == NULL ||
      !Backing->Media->MediaPresent) {
    return EFI_UNSUPPORTED;
  }
  if (Backing->Media->BlockSize < 512) {
    return EFI_UNSUPPORTED;
  }

  Out = AllocateZeroPool (sizeof (*Out));
  if (Out == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Out->Backing = Backing;
  Out->BlockSize = Backing->Media->BlockSize;

  Out->BlockIo.Revision = EFI_BLOCK_IO_PROTOCOL_REVISION;
  Out->BlockIo.Media = &Out->Media;
  Out->BlockIo.Reset = SfbSynthReset;
  Out->BlockIo.ReadBlocks = SfbSynthReadBlocks;
  Out->BlockIo.WriteBlocks = SfbSynthWriteBlocks;
  Out->BlockIo.FlushBlocks = SfbSynthFlush;

  *Disk = Out;
  return EFI_SUCCESS;
}

/*
 * Fill one 92-byte GPT header into Hdr's (zeroed) block. MyLba/AltLba and the
 * entry-array LBA differ between the primary and backup headers; the CRC is
 * computed over each header's own bytes with the CRC field zero, per spec.
 */
STATIC
VOID
SfbSynthFillHeader (IN SFB_SYNTH_DISK *Disk,
                    IN UINT8          *Hdr,
                    IN UINT64         MyLba,
                    IN UINT64         AltLba,
                    IN UINT64         EntriesLba)
{
  UINT32  Crc;

  CopyMem (Hdr, "EFI PART", 8);
  SfbSynthPut32 (Hdr + 8, 0x00010000);
  SfbSynthPut32 (Hdr + 12, SFB_SYNTH_GPT_HDR_SIZE);
  SfbSynthPut32 (Hdr + 16, 0);
  SfbSynthPut64 (Hdr + 24, MyLba);
  SfbSynthPut64 (Hdr + 32, AltLba);
  SfbSynthPut64 (Hdr + 40, Disk->DataStartLba);
  SfbSynthPut64 (Hdr + 48, Disk->DataStartLba + Disk->DataBlocks - 1);
  CopyGuid ((EFI_GUID *)(Hdr + 56), &mSfbSynthDiskGuid);
  SfbSynthPut64 (Hdr + 72, EntriesLba);
  SfbSynthPut32 (Hdr + 80, SFB_SYNTH_ENTRY_COUNT);
  SfbSynthPut32 (Hdr + 84, SFB_SYNTH_ENTRY_BYTES);
  SfbSynthPut32 (Hdr + 88,
                 CalculateCrc32 (Disk->Entries,
                                 SFB_SYNTH_ENTRY_COUNT * SFB_SYNTH_ENTRY_BYTES));
  Crc = CalculateCrc32 (Hdr, SFB_SYNTH_GPT_HDR_SIZE);
  SfbSynthPut32 (Hdr + 16, Crc);
}

STATIC
VOID
SfbSynthFillTables (IN SFB_SYNTH_DISK *Disk,
                    IN CONST EFI_GUID *TypeGuid,
                    IN CONST CHAR16   *Name)
{
  EFI_PARTITION_ENTRY  *Entry;
  UINT64               LastLba = Disk->TotalBlocks - 1;
  UINT64               BackupEntries = LastLba - Disk->EntryBlocks;

  /* Protective MBR: one 0xEE entry spanning the disk (clamped to 32 bits),
   * boot signature at the end of the block. */
  Disk->Mbr[446] = 0x00;
  Disk->Mbr[447] = 0x00;
  Disk->Mbr[448] = 0x02;
  Disk->Mbr[450] = 0xEE;
  Disk->Mbr[451] = 0xFE;
  Disk->Mbr[452] = 0xFF;
  Disk->Mbr[453] = 0xFF;
  SfbSynthPut32 (Disk->Mbr + 454, 1);
  SfbSynthPut32 (Disk->Mbr + 458,
                 (UINT32)MIN (Disk->TotalBlocks - 1, 0xFFFFFFFF));
  Disk->Mbr[510] = 0x55;
  Disk->Mbr[511] = 0xAA;

  /* Partition entry 0: the exported extent. EFI_PARTITION_ENTRY's memory
   * layout is the on-disk GPT entry byte-for-byte on this (little-endian)
   * platform, so filling the struct and copying it out is the encoding. */
  Entry = (EFI_PARTITION_ENTRY *)Disk->Entries;
  CopyGuid (&Entry->PartitionTypeGUID,
            (TypeGuid != NULL) ? TypeGuid : &mSfbSynthDefaultTypeGuid);
  CopyGuid (&Entry->UniquePartitionGUID, &mSfbSynthPartGuid);
  Entry->StartingLBA = Disk->DataStartLba;
  Entry->EndingLBA = Disk->DataStartLba + Disk->DataBlocks - 1;
  Entry->Attributes = 0;
  SetMem (Entry->PartitionName, sizeof (Entry->PartitionName), 0);
  if (Name != NULL) {
    UINTN  Len = StrLen (Name);
    if (Len > (sizeof (Entry->PartitionName) / sizeof (CHAR16)) - 1) {
      Len = (sizeof (Entry->PartitionName) / sizeof (CHAR16)) - 1;
    }
    CopyMem (Entry->PartitionName, Name, Len * sizeof (CHAR16));
  }

  /* Primary header at LBA1 points its alternate and entry array forward;
   * the backup header at the last LBA mirrors both, per spec. */
  SfbSynthFillHeader (Disk, Disk->Header, 1, LastLba, 2);
  SfbSynthFillHeader (Disk, Disk->BackupHeader, LastLba, 1, BackupEntries);
}

EFI_STATUS
SfbSynthDiskCreate (IN EFI_BLOCK_IO_PROTOCOL *Backing,
                    IN UINT64                FirstDataBlock,
                    IN UINT64                DataBlocks,
                    IN CONST EFI_GUID        *TypeGuid OPTIONAL,
                    IN CONST CHAR16          *Name OPTIONAL,
                    IN BOOLEAN               ReadOnly,
                    OUT SFB_SYNTH_DISK       **Disk)
{
  SFB_SYNTH_DISK  *Out;
  EFI_STATUS      Status;
  UINT32          BlockSize;

  if (Disk == NULL || Backing == NULL || Backing->Media == NULL ||
      DataBlocks == 0) {
    return EFI_INVALID_PARAMETER;
  }

  Status = SfbSynthDiskInitCommon (Backing, &Out);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  BlockSize = Out->BlockSize;

  Out->FirstDataBlock = FirstDataBlock;
  Out->DataBlocks = DataBlocks;
  Out->EntryBlocks =
    (UINT32)((SFB_SYNTH_ENTRIES_BYTES + BlockSize - 1) / BlockSize);
  Out->DataStartLba = 2 + Out->EntryBlocks;
  Out->TotalBlocks = Out->DataStartLba + DataBlocks + Out->EntryBlocks + 1;

  Out->Mbr = AllocateZeroPool (BlockSize);
  Out->Header = AllocateZeroPool (BlockSize);
  Out->BackupHeader = AllocateZeroPool (BlockSize);
  Out->Entries = AllocateZeroPool ((UINTN)Out->EntryBlocks * BlockSize);
  if (Out->Mbr == NULL || Out->Header == NULL || Out->BackupHeader == NULL ||
      Out->Entries == NULL) {
    SfbSynthDiskDestroy (Out);
    return EFI_OUT_OF_RESOURCES;
  }

  Out->Media.MediaId = SFB_SYNTH_MEDIA_ID;
  Out->Media.RemovableMedia = FALSE;
  Out->Media.MediaPresent = TRUE;
  Out->Media.LogicalPartition = FALSE;
  Out->Media.ReadOnly = (BOOLEAN)(ReadOnly || Backing->Media->ReadOnly ||
                                  Backing->WriteBlocks == NULL);
  Out->Media.WriteCaching = Backing->Media->WriteCaching;
  Out->Media.BlockSize = BlockSize;
  Out->Media.LastBlock = Out->TotalBlocks - 1;
  Out->Media.IoAlign = 1;  /* the wrapper bounces to the backing alignment */

  SfbSynthFillTables (Out, TypeGuid, Name);

  *Disk = Out;
  return EFI_SUCCESS;
}

EFI_STATUS
SfbPassDiskCreate (IN EFI_BLOCK_IO_PROTOCOL *Backing,
                   IN BOOLEAN               ReadOnly,
                   OUT SFB_SYNTH_DISK       **Disk)
{
  SFB_SYNTH_DISK  *Out;
  EFI_STATUS      Status;

  if (Disk == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = SfbSynthDiskInitCommon (Backing, &Out);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Out->PassThrough = TRUE;
  Out->FirstDataBlock = 0;
  Out->DataBlocks = Backing->Media->LastBlock + 1;
  Out->EntryBlocks = 0;
  Out->DataStartLba = 0;
  Out->TotalBlocks = Out->DataBlocks;

  Out->Media.MediaId = SFB_SYNTH_MEDIA_ID;
  Out->Media.RemovableMedia = FALSE;
  Out->Media.MediaPresent = TRUE;
  Out->Media.LogicalPartition = FALSE;
  Out->Media.ReadOnly = (BOOLEAN)(ReadOnly || Backing->Media->ReadOnly ||
                                  Backing->WriteBlocks == NULL);
  Out->Media.WriteCaching = Backing->Media->WriteCaching;
  Out->Media.BlockSize = Out->BlockSize;
  Out->Media.LastBlock = Out->TotalBlocks - 1;
  Out->Media.IoAlign = 1;

  *Disk = Out;
  return EFI_SUCCESS;
}

VOID
SfbSynthDiskDestroy (IN SFB_SYNTH_DISK *Disk)
{
  if (Disk == NULL) {
    return;
  }
  if (Disk->Mbr != NULL) {
    FreePool (Disk->Mbr);
  }
  if (Disk->Header != NULL) {
    FreePool (Disk->Header);
  }
  if (Disk->BackupHeader != NULL) {
    FreePool (Disk->BackupHeader);
  }
  if (Disk->Entries != NULL) {
    FreePool (Disk->Entries);
  }
  FreePool (Disk);
}
