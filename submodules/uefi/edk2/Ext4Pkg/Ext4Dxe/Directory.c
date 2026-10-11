/**
  @file Directory related routines

  Copyright (c) 2021 Pedro Falcato All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "Ext4Dxe.h"

/**
   Retrieves the filename of the directory entry and converts it to UTF-16/UCS-2

   @param[in]      Entry   Pointer to a EXT4_DIR_ENTRY.
   @param[out]      Ucs2FileName   Pointer to an array of CHAR16's, of size EXT4_NAME_MAX + 1.

   @retval EFI_SUCCESS   Unicode collation was successfully initialised.
   @retval !EFI_SUCCESS  Failure.
*/
EFI_STATUS
Ext4GetUcs2DirentName (
  IN EXT4_DIR_ENTRY *Entry,
  OUT CHAR16 Ucs2FileName[EXT4_NAME_MAX + 1]
  )
{
  CHAR8  Utf8NameBuf[EXT4_NAME_MAX + 1];

  CopyMem (Utf8NameBuf, Entry->name, Entry->name_len);

  Utf8NameBuf[Entry->name_len] = '\0';

  // TODO: Use RedfishPkg's UTF-8 translation routines.
  // This only handles ASCII which is incorrect
  return AsciiStrToUnicodeStrS (Utf8NameBuf, Ucs2FileName, EXT4_NAME_MAX + 1);
}

/**
   Retrieves a directory entry.

   @param[in]      Directory   Pointer to the opened directory.
   @param[in]      NameUnicode Pointer to the UCS-2 formatted filename.
   @param[in]      Partition   Pointer to the ext4 partition.
   @param[out]     Result      Pointer to the destination directory entry.

   @return The result of the operation.
*/
EFI_STATUS
Ext4RetrieveDirent (
  IN EXT4_FILE *File,
  IN CONST CHAR16 *Name,
  IN EXT4_PARTITION *Partition,
  OUT EXT4_DIR_ENTRY *res
  )
{
  EFI_STATUS  Status;
  CHAR8       *Buf;
  UINT64      Off;
  EXT4_INODE  *Inode;
  UINT64      DirInoSize;
  UINT32      BlockRemainder;

  Status = EFI_NOT_FOUND;
  Buf    = AllocatePool (Partition->BlockSize);

  if(Buf == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Off = 0;

  Inode = File->Inode;
  DirInoSize = EXT4_INODE_SIZE (Inode);

  DivU64x32Remainder (DirInoSize, Partition->BlockSize, &BlockRemainder);
  if(BlockRemainder != 0) {
    // Directory inodes need to have block aligned sizes
    return EFI_VOLUME_CORRUPTED;
  }

  while(Off < DirInoSize) {
    UINTN  Length;

    Length = Partition->BlockSize;

    Status = Ext4Read (Partition, File, Buf, Off, &Length);

    if (Status != EFI_SUCCESS) {
      FreePool (Buf);
      return Status;
    }

    for(CHAR8 *b = Buf; b < Buf + Partition->BlockSize; ) {
      EXT4_DIR_ENTRY  *Entry;
      UINTN           RemainingBlock;

      Entry = (EXT4_DIR_ENTRY *)b;
      ASSERT (Entry->rec_len != 0);

      RemainingBlock = Partition->BlockSize - (b - Buf);

      if(Entry->name_len > RemainingBlock || Entry->rec_len > RemainingBlock) {
        // Corrupted filesystem
        // TODO: Do the proper ext4 corruption detection thing and dirty the filesystem.
        FreePool (Buf);
        return EFI_VOLUME_CORRUPTED;
      }

      // Ignore names bigger than our limit.

      /* Note: I think having a limit is sane because:
        1) It's nicer to work with.
        2) Linux and a number of BSDs also have a filename limit of 255.
      */
      if(Entry->name_len > EXT4_NAME_MAX) {
        continue;
      }

      // Unused entry
      if(Entry->inode == 0) {
        b += Entry->rec_len;
        continue;
      }

      CHAR16  Ucs2FileName[EXT4_NAME_MAX + 1];

      Status = Ext4GetUcs2DirentName (Entry, Ucs2FileName);

      /* In theory, this should never fail.
       * In reality, it's quite possible that it can fail, considering filenames in
       * Linux (and probably other nixes) are just null-terminated bags of bytes, and don't
       * need to form valid ASCII/UTF-8 sequences.
       */
      if (EFI_ERROR (Status)) {
        // If we error out, skip this entry
        // I'm not sure if this is correct behaviour, but I don't think there's a precedent here.
        b += Entry->rec_len;
        continue;
      }

      if (Entry->name_len == StrLen (Name) &&
          !Ext4StrCmpInsensitive (Ucs2FileName, (CHAR16 *)Name)) {
        UINTN  ToCopy;

        ToCopy = Entry->rec_len > sizeof (EXT4_DIR_ENTRY) ?
                 sizeof (EXT4_DIR_ENTRY) :
                 Entry->rec_len;

        CopyMem (res, Entry, ToCopy);
        FreePool (Buf);
        return EFI_SUCCESS;
      }

      b += Entry->rec_len;
    }

    Off += Partition->BlockSize;
  }

  FreePool (Buf);
  return EFI_NOT_FOUND;
}

/**
   Opens a file using a directory entry.

   @param[in]      Partition   Pointer to the ext4 partition.
   @param[in]      OpenMode    Mode in which the file is supposed to be open.
   @param[out]     OutFile     Pointer to the newly opened file.
   @param[in]      Entry       Directory entry to be used.

   @retval EFI_STATUS          Result of the operation
*/
EFI_STATUS
Ext4OpenDirent (
  IN  EXT4_PARTITION *Partition,
  IN  UINT64 OpenMode,
  OUT EXT4_FILE **OutFile,
  IN  EXT4_DIR_ENTRY *Entry
  )
{
  EFI_STATUS  Status;
  CHAR16      FileName[EXT4_NAME_MAX + 1];
  EXT4_FILE   *File;

  File = AllocateZeroPool (sizeof (EXT4_FILE));

  if (File == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Error;
  }

  Status = Ext4GetUcs2DirentName (Entry, FileName);

  if (EFI_ERROR (Status)) {
    goto Error;
  }

  File->FileName = AllocateZeroPool (StrSize (FileName));

  if (!File->FileName) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Error;
  }

  Status = Ext4InitExtentsMap (File);

  if (EFI_ERROR (Status)) {
    goto Error;
  }

  // This should not fail.
  StrCpyS (File->FileName, EXT4_NAME_MAX + 1, FileName);

  File->InodeNum = Entry->inode;

  Ext4SetupFile (File, (EXT4_PARTITION *)Partition);

  Status = Ext4ReadInode (Partition, Entry->inode, &File->Inode);

  if (EFI_ERROR (Status)) {
    goto Error;
  }

  *OutFile = File;

  return EFI_SUCCESS;

Error:
  if (File != NULL) {
    if (File->FileName != NULL) {
      FreePool (File->FileName);
    }

    if (File->ExtentsMap != NULL) {
      OrderedCollectionUninit (File->ExtentsMap);
    }

    FreePool (File);
  }

  return Status;
}

/**
   Opens a file.

   @param[in]      Directory   Pointer to the opened directory.
   @param[in]      Name        Pointer to the UCS-2 formatted filename.
   @param[in]      Partition   Pointer to the ext4 partition.
   @param[in]      OpenMode    Mode in which the file is supposed to be open.
   @param[out]     OutFile     Pointer to the newly opened file.

   @return Result of the operation.
*/
EFI_STATUS
Ext4OpenFile (
  IN  EXT4_FILE *Directory,
  IN  CONST CHAR16 *Name,
  IN  EXT4_PARTITION *Partition,
  IN  UINT64 OpenMode,
  OUT EXT4_FILE **OutFile
  )
{
  EXT4_DIR_ENTRY  Entry;
  EFI_STATUS      Status;

  Status = Ext4RetrieveDirent (Directory, Name, Partition, &Entry);

  if (EFI_ERROR (Status)) {
    return Status;
  }

  // EFI requires us to error out on ".." opens for the root directory
  if (Entry.inode == Directory->InodeNum) {
    return EFI_NOT_FOUND;
  }

  return Ext4OpenDirent (Partition, OpenMode, OutFile, &Entry);
}

EFI_STATUS EFIAPI
Ext4OpenVolume (
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Partition, EFI_FILE_PROTOCOL **Root
  )
{
  EXT4_INODE  *RootInode;
  EFI_STATUS  Status;

  Status = Ext4ReadInode ((EXT4_PARTITION *)Partition, 2, &RootInode);

  if(EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "[ext4] Could not open root inode - status %x\n", Status));
    return Status;
  }

  EXT4_FILE  *RootDir = AllocateZeroPool (sizeof (EXT4_FILE));

  if(!RootDir) {
    FreePool (RootInode);
    return EFI_OUT_OF_RESOURCES;
  }

  // The filename will be "\"(null terminated of course)
  RootDir->FileName = AllocateZeroPool (2 * sizeof (CHAR16));

  if (!RootDir->FileName) {
    FreePool (RootDir);
    FreePool (RootInode);
    return EFI_OUT_OF_RESOURCES;
  }

  RootDir->FileName[0] = L'\\';

  RootDir->Inode    = RootInode;
  RootDir->InodeNum = 2;

  if (EFI_ERROR (Ext4InitExtentsMap (RootDir))) {
    FreePool (RootDir->FileName);
    FreePool (RootInode);
    FreePool (RootDir);
    return EFI_OUT_OF_RESOURCES;
  }

  Ext4SetupFile (RootDir, (EXT4_PARTITION *)Partition);
  *Root = &RootDir->Protocol;

  return EFI_SUCCESS;
}

/**
   Validates a directory entry.

   @param[in]      Dirent      Pointer to the directory entry.

   @retval TRUE          Valid directory entry.
           FALSE         Invalid directory entry.
*/
STATIC
BOOLEAN
Ext4ValidDirent (
  IN CONST EXT4_DIR_ENTRY *Dirent
  )
{
  UINTN  RequiredSize = Dirent->name_len + EXT4_MIN_DIR_ENTRY_LEN;

  if (Dirent->rec_len < RequiredSize) {
    DEBUG ((EFI_D_ERROR, "[ext4] dirent size %lu too small (compared to %lu)\n", Dirent->rec_len, RequiredSize));
    return FALSE;
  }

  // Dirent sizes need to be 4 byte aligned
  if (Dirent->rec_len % 4) {
    return FALSE;
  }

  return TRUE;
}

/**
   Reads a directory entry.

   @param[in]      Partition   Pointer to the ext4 partition.
   @param[in]      File        Pointer to the open directory.
   @param[out]     Buffer      Pointer to the output buffer.
   @param[in]      Offset      Initial directory position.
   @param[in out] OutLength    Pointer to a UINTN that contains the length of the buffer,
                               and the length of the actual EFI_FILE_INFO after the call.

   @return Result of the operation.
*/
EFI_STATUS
Ext4ReadDir (
  IN EXT4_PARTITION *Partition,
  IN EXT4_FILE *File,
  OUT VOID *Buffer,
  IN UINT64 Offset,
  IN OUT UINTN *OutLength
  )
{
  DEBUG ((EFI_D_VERBOSE, "[ext4] Ext4ReadDir offset %lu\n", Offset));
  EXT4_INODE      *DirIno;
  EFI_STATUS      Status;
  UINT64          DirInoSize;
  UINTN           Len;
  UINT32          BlockRemainder;
  EXT4_DIR_ENTRY  Entry;

  DirIno     = File->Inode;
  Status     = EFI_SUCCESS;
  DirInoSize = Ext4InodeSize (DirIno);

  DivU64x32Remainder (DirInoSize, Partition->BlockSize, &BlockRemainder);
  if(BlockRemainder != 0) {
    // Directory inodes need to have block aligned sizes
    return EFI_VOLUME_CORRUPTED;
  }

  while(TRUE) {
    EXT4_FILE  *TempFile;

    TempFile = NULL;

    // We (try to) read the maximum size of a directory entry at a time
    // Note that we don't need to read any padding that may exist after it.
    Len    = sizeof (Entry);
    Status = Ext4Read (Partition, File, &Entry, Offset, &Len);

    if (EFI_ERROR (Status)) {
      goto Out;
    }

 #if 0
      DEBUG ((EFI_D_INFO, "[ext4] Length read %lu, offset %lu\n", Len, Offset));
 #endif

    if (Len == 0) {
      *OutLength = 0;
      Status     = EFI_SUCCESS;
      goto Out;
    }

    if (Len < EXT4_MIN_DIR_ENTRY_LEN) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Out;
    }

    // Invalid directory entry length
    if (!Ext4ValidDirent (&Entry)) {
      DEBUG ((EFI_D_ERROR, "[ext4] Invalid dirent at offset %lu\n", Offset));
      Status = EFI_VOLUME_CORRUPTED;
      goto Out;
    }

    DEBUG ((EFI_D_VERBOSE, "[ext4] dirent size %lu\n", Entry.rec_len));

    if (Entry.inode == 0) {
      // When inode = 0, it's unused
      Offset += Entry.rec_len;
      continue;
    }

    Status = Ext4OpenDirent (Partition, EFI_FILE_MODE_READ, &TempFile, &Entry);

    if (EFI_ERROR (Status)) {
      goto Out;
    }

    // TODO: Is this needed?
    if (!StrCmp (TempFile->FileName, L".") || !StrCmp (TempFile->FileName, L"..")) {
      Offset += Entry.rec_len;
      Ext4CloseInternal (TempFile);
      continue;
    }

 #if 0
      DEBUG ((EFI_D_INFO, "[ext4] Listing file %s\n", TempFile->FileName));
 #endif

    Status = Ext4GetFileInfo (TempFile, Buffer, OutLength);
    if (!EFI_ERROR (Status)) {
      File->Position = Offset + Entry.rec_len;
    }

    Ext4CloseInternal (TempFile);

    break;
  }

  Status = EFI_SUCCESS;
Out:
  return Status;
}
