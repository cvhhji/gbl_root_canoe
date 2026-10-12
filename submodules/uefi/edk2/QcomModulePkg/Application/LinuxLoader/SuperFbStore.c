/*
 * Persistent settings for the super-fastboot boot menu.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMenu.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Guid/Gpt.h>
#include <Protocol/BlockIo.h>
#include <Protocol/PartitionInfo.h>

/* Keeps the translation unit legal when the feature is compiled out. */
CONST CHAR8 *gSfbStoreModuleTag = "SuperFbStore";

#define SFB_STORE_BYTES  (SFB_STORE_SLOT_BYTES * SFB_STORE_SLOTS)

/* Refuse anything too small to have the megabyte of slack we were promised. */
#define SFB_STORE_MIN_PARTITION_BYTES  SIZE_1MB

/* Sanity bounds on a GPT header before its contents are believed. */
#define SFB_GPT_MAX_ENTRIES     512
#define SFB_GPT_MIN_ENTRY_SIZE  128
#define SFB_GPT_MAX_ENTRY_SIZE  4096

typedef struct {
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  /* Absolute byte offset of the first store record on BlockIo's media. */
  UINT64                 Offset;
  BOOLEAN                Resolved;
  BOOLEAN                Failed;
} SFB_STORE_LOCATION;

STATIC SFB_STORE_LOCATION  mSfbStore = { NULL, 0, FALSE, FALSE };

/* ---- finding the EFI System Partition ----------------------------------- */

/*
 * Read Blocks starting at Lba into a freshly allocated, IoAlign-correct buffer.
 * Caller releases it with FreeAlignedPages () and the same page count.
 */
STATIC
EFI_STATUS
SfbReadBlocks (IN EFI_BLOCK_IO_PROTOCOL  *BlockIo,
               IN EFI_LBA                Lba,
               IN UINTN                  Blocks,
               OUT VOID                  **Buffer,
               OUT UINTN                 *Pages)
{
  EFI_STATUS  Status;
  UINTN       Bytes;
  UINTN       Alignment;

  *Buffer = NULL;
  *Pages = 0;

  Bytes = Blocks * BlockIo->Media->BlockSize;
  Alignment = (BlockIo->Media->IoAlign > 1) ? BlockIo->Media->IoAlign : 8;

  *Pages = EFI_SIZE_TO_PAGES (Bytes);
  *Buffer = AllocateAlignedPages (*Pages, Alignment);
  if (*Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, Lba,
                                Bytes, *Buffer);
  if (EFI_ERROR (Status)) {
    FreeAlignedPages (*Buffer, *Pages);
    *Buffer = NULL;
    *Pages = 0;
  }

  return Status;
}

/* The GPT name the platform gives its EFI System Partition. */
#define SFB_ESP_PARTITION_NAME  L"efisp"

#define SFB_ESP_NO_MATCH    0
#define SFB_ESP_BY_TYPE     1
#define SFB_ESP_BY_NAME     2

/*
 * How well an entry answers to "the ESP". PartitionName is a fixed-width field
 * and is not required to be NUL terminated, so it is copied out before being
 * compared as a string.
 */
STATIC
UINTN
SfbRankEspEntry (IN CONST EFI_PARTITION_ENTRY *Entry)
{
  CHAR16  Name[ARRAY_SIZE (Entry->PartitionName) + 1];

  CopyMem (Name, Entry->PartitionName, sizeof (Entry->PartitionName));
  Name[ARRAY_SIZE (Entry->PartitionName)] = L'\0';

  if (StrCmp (Name, SFB_ESP_PARTITION_NAME) == 0) {
    return SFB_ESP_BY_NAME;
  }

  /* Fallback for a table that names it something else: the type GUID is what
   * makes a partition an ESP in the first place. */
  if (CompareGuid (&Entry->PartitionTypeGUID, &gEfiPartTypeSystemPartGuid)) {
    return SFB_ESP_BY_TYPE;
  }

  return SFB_ESP_NO_MATCH;
}

/*
 * Look for the EFI System Partition in Disk's GPT. On success the partition's
 * last byte, exclusive, is returned as an absolute offset on Disk.
 *
 * The named partition wins over a merely ESP-typed one, so a table holding both
 * still resolves to the one the platform means.
 */
STATIC
EFI_STATUS
SfbFindEspInGpt (IN EFI_BLOCK_IO_PROTOCOL *Disk, OUT UINT64 *PartitionEnd)
{
  EFI_STATUS                   Status;
  EFI_PARTITION_TABLE_HEADER   *Header = NULL;
  UINT8                        *Entries = NULL;
  UINTN                        HeaderPages = 0;
  UINTN                        EntryPages = 0;
  UINTN                        BlockSize = Disk->Media->BlockSize;
  UINTN                        EntryBytes;
  UINTN                        EntryBlocks;
  UINTN                        Index;
  UINTN                        Best;

  *PartitionEnd = 0;

  Status = SfbReadBlocks (Disk, PRIMARY_PART_HEADER_LBA, 1,
                          (VOID **)&Header, &HeaderPages);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EFI_NOT_FOUND;

  if (Header->Header.Signature != EFI_PTAB_HEADER_ID ||
      Header->NumberOfPartitionEntries == 0 ||
      Header->NumberOfPartitionEntries > SFB_GPT_MAX_ENTRIES ||
      Header->SizeOfPartitionEntry < SFB_GPT_MIN_ENTRY_SIZE ||
      Header->SizeOfPartitionEntry > SFB_GPT_MAX_ENTRY_SIZE ||
      (Header->SizeOfPartitionEntry % 8) != 0) {
    goto Done;
  }

  EntryBytes = (UINTN)Header->NumberOfPartitionEntries *
               Header->SizeOfPartitionEntry;
  EntryBlocks = (EntryBytes + BlockSize - 1) / BlockSize;

  Status = SfbReadBlocks (Disk, Header->PartitionEntryLBA, EntryBlocks,
                          (VOID **)&Entries, &EntryPages);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Status = EFI_NOT_FOUND;
  Best = SFB_ESP_NO_MATCH;

  for (Index = 0; Index < Header->NumberOfPartitionEntries; Index++) {
    CONST EFI_PARTITION_ENTRY  *Entry =
      (CONST EFI_PARTITION_ENTRY *)(Entries +
                                    Index * Header->SizeOfPartitionEntry);
    UINTN  Rank = SfbRankEspEntry (Entry);

    if (Rank <= Best) {
      continue;
    }

    if (Entry->EndingLBA < Entry->StartingLBA ||
        Entry->EndingLBA > Disk->Media->LastBlock) {
      continue;
    }

    if ((Entry->EndingLBA - Entry->StartingLBA + 1) * BlockSize <
        SFB_STORE_MIN_PARTITION_BYTES) {
      DEBUG ((EFI_D_ERROR, "SFB: ESP candidate too small for the store\n"));
      continue;
    }

    *PartitionEnd = (Entry->EndingLBA + 1) * BlockSize;
    Status = EFI_SUCCESS;
    Best = Rank;

    /* Nothing outranks the partition the platform actually named. */
    if (Best == SFB_ESP_BY_NAME) {
      break;
    }
  }

Done:
  if (Entries != NULL) {
    FreeAlignedPages (Entries, EntryPages);
  }
  FreeAlignedPages (Header, HeaderPages);

  return Status;
}

/*
 * Second way in, for platforms whose storage stack hands out partitions but no
 * readable GPT: ask the partition handles themselves what they are.
 */
STATIC
EFI_STATUS
SfbFindEspByPartitionInfo (OUT EFI_BLOCK_IO_PROTOCOL **BlockIo,
                           OUT UINT64                *PartitionEnd)
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count = 0;
  UINTN       Index;

  *BlockIo = NULL;
  *PartitionEnd = 0;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPartitionInfoProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    return EFI_NOT_FOUND;
  }

  Status = EFI_NOT_FOUND;

  for (Index = 0; Index < Count; Index++) {
    EFI_PARTITION_INFO_PROTOCOL  *Info = NULL;
    EFI_BLOCK_IO_PROTOCOL        *Candidate = NULL;
    UINT64                       Bytes;

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index],
                                        &gEfiPartitionInfoProtocolGuid,
                                        (VOID **)&Info)) ||
        Info->Type != PARTITION_TYPE_GPT ||
        !CompareGuid (&Info->Info.Gpt.PartitionTypeGUID,
                      &gEfiPartTypeSystemPartGuid)) {
      continue;
    }

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index],
                                        &gEfiBlockIoProtocolGuid,
                                        (VOID **)&Candidate)) ||
        Candidate->Media == NULL || !Candidate->Media->MediaPresent) {
      continue;
    }

    Bytes = (Candidate->Media->LastBlock + 1) * Candidate->Media->BlockSize;
    if (Bytes < SFB_STORE_MIN_PARTITION_BYTES) {
      continue;
    }

    /* Addressed relative to the partition, so its end is the media's end. */
    *BlockIo = Candidate;
    *PartitionEnd = Bytes;
    Status = EFI_SUCCESS;
    break;
  }

  FreePool (Handles);

  return Status;
}

/* Resolve once and remember the answer, good or bad. */
STATIC
EFI_STATUS
SfbResolveStore (VOID)
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count = 0;
  UINTN       Index;
  UINT64      PartitionEnd = 0;

  if (mSfbStore.Resolved) {
    return EFI_SUCCESS;
  }
  if (mSfbStore.Failed) {
    return EFI_NOT_FOUND;
  }

  /* Whole disks first: reading the GPT ourselves works even where nothing has
   * published partition metadata. */
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid,
                                    NULL, &Count, &Handles);
  if (!EFI_ERROR (Status) && Handles != NULL) {
    for (Index = 0; Index < Count; Index++) {
      EFI_BLOCK_IO_PROTOCOL  *Disk = NULL;

      if (EFI_ERROR (gBS->HandleProtocol (Handles[Index],
                                          &gEfiBlockIoProtocolGuid,
                                          (VOID **)&Disk)) ||
          Disk->Media == NULL ||
          Disk->Media->LogicalPartition ||
          !Disk->Media->MediaPresent) {
        continue;
      }

      if (!EFI_ERROR (SfbFindEspInGpt (Disk, &PartitionEnd))) {
        mSfbStore.BlockIo = Disk;
        break;
      }
    }

    FreePool (Handles);
  }

  if (mSfbStore.BlockIo == NULL) {
    Status = SfbFindEspByPartitionInfo (&mSfbStore.BlockIo, &PartitionEnd);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: no EFI System Partition found\n"));
      mSfbStore.Failed = TRUE;
      return EFI_NOT_FOUND;
    }
  }

  if (mSfbStore.BlockIo->Media->ReadOnly) {
    DEBUG ((EFI_D_ERROR, "SFB: ESP media is read only\n"));
    /* Still usable for reads, so this is not a resolution failure. */
  }

  mSfbStore.Offset = PartitionEnd - SFB_STORE_BYTES;
  mSfbStore.Resolved = TRUE;

  DEBUG ((EFI_D_INFO, "SFB: store at offset 0x%lx (block size %u)\n",
          mSfbStore.Offset, (UINT32)mSfbStore.BlockIo->Media->BlockSize));

  return EFI_SUCCESS;
}

/* ---- record access ------------------------------------------------------ */

/*
 * The store is not block aligned in general - a 4 KiB sector device puts both
 * records inside one block - so every access works on the whole run of blocks
 * that covers it, and a write is a read-modify-write of that run.
 *
 * On success *Buffer holds the blocks, *Skip is where the store starts inside
 * them, and *Lba / *Blocks describe where they came from.
 */
STATIC
EFI_STATUS
SfbMapStoreBlocks (OUT VOID      **Buffer,
                   OUT UINTN     *Pages,
                   OUT EFI_LBA   *Lba,
                   OUT UINTN     *Blocks,
                   OUT UINTN     *Skip)
{
  EFI_STATUS  Status;
  UINTN       BlockSize;

  Status = SfbResolveStore ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  BlockSize = mSfbStore.BlockIo->Media->BlockSize;

  *Lba = mSfbStore.Offset / BlockSize;
  *Skip = (UINTN)(mSfbStore.Offset % BlockSize);
  *Blocks = (*Skip + SFB_STORE_BYTES + BlockSize - 1) / BlockSize;

  return SfbReadBlocks (mSfbStore.BlockIo, *Lba, *Blocks, Buffer, Pages);
}

EFI_STATUS
SfbStoreRead (IN UINTN Slot, OUT CHAR8 *Out, IN UINTN OutBytes)
{
  EFI_STATUS  Status;
  VOID        *Buffer = NULL;
  UINTN       Pages = 0;
  EFI_LBA     Lba = 0;
  UINTN       Blocks = 0;
  UINTN       Skip = 0;
  CONST CHAR8 *Record;
  UINTN       Index;

  if (Slot >= SFB_STORE_SLOTS || OutBytes == 0) {
    return EFI_INVALID_PARAMETER;
  }

  Out[0] = '\0';

  Status = SfbMapStoreBlocks (&Buffer, &Pages, &Lba, &Blocks, &Skip);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Record = (CONST CHAR8 *)Buffer + Skip + Slot * SFB_STORE_SLOT_BYTES;

  /* Never-written media is whatever the flash was left as, so nothing beyond
   * the record's own bounds is trusted and the copy is terminated by us. */
  for (Index = 0;
       Index < SFB_STORE_SLOT_BYTES && Index < OutBytes - 1;
       Index++) {
    CHAR8  Ch = Record[Index];

    if (Ch == '\0' || Ch == '\r' || Ch == '\n') {
      break;
    }
    /* Printable 7-bit only: a record is text by definition, and this keeps
     * garbage from reaching the console or the path handling. */
    if (Ch < 0x20 || (UINT8)Ch > 0x7e) {
      Index = 0;
      break;
    }
    Out[Index] = Ch;
  }

  Out[Index] = '\0';

  FreeAlignedPages (Buffer, Pages);

  return EFI_SUCCESS;
}

EFI_STATUS
SfbStoreWrite (IN UINTN Slot, IN CONST CHAR8 *Text)
{
  EFI_STATUS  Status;
  VOID        *Buffer = NULL;
  UINTN       Pages = 0;
  EFI_LBA     Lba = 0;
  UINTN       Blocks = 0;
  UINTN       Skip = 0;
  CHAR8       *Record;
  UINTN       Length;

  if (Slot >= SFB_STORE_SLOTS) {
    return EFI_INVALID_PARAMETER;
  }

  Length = AsciiStrLen (Text);
  if (Length >= SFB_STORE_SLOT_BYTES) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Status = SfbMapStoreBlocks (&Buffer, &Pages, &Lba, &Blocks, &Skip);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (mSfbStore.BlockIo->Media->ReadOnly) {
    FreeAlignedPages (Buffer, Pages);
    return EFI_WRITE_PROTECTED;
  }

  /* Only this slot changes; the other one and the rest of the blocks are put
   * back exactly as they were read. */
  Record = (CHAR8 *)Buffer + Skip + Slot * SFB_STORE_SLOT_BYTES;
  ZeroMem (Record, SFB_STORE_SLOT_BYTES);
  CopyMem (Record, Text, Length);

  Status = mSfbStore.BlockIo->WriteBlocks (mSfbStore.BlockIo,
                                           mSfbStore.BlockIo->Media->MediaId,
                                           Lba,
                                           Blocks *
                                             mSfbStore.BlockIo->Media->BlockSize,
                                           Buffer);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: store write failed: %r\n", Status));
  } else {
    /* Flushing matters: the user may power the device off the moment the menu
     * hands over to the image it just recorded. */
    Status = mSfbStore.BlockIo->FlushBlocks (mSfbStore.BlockIo);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: store flush failed: %r\n", Status));
    }
  }

  FreeAlignedPages (Buffer, Pages);

  return Status;
}

