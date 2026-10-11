/**
  @file EFI_FILE_PROTOCOL implementation for EXT4

  Copyright (c) 2021 Pedro Falcato All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "Ext4Dxe.h"

#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>

/**
   Duplicates a file structure.

   @param[in]        Original    Pointer to the original file.

   @return Pointer to the new file structure.
 */
STATIC
EXT4_FILE *
Ext4DuplicateFile (
  IN CONST EXT4_FILE *Original
  );

/**
   Gets the next path segment.

   @param[in]        Path        Pointer to the rest of the path.
   @param[out]       PathSegment Pointer to the buffer that will hold the path segment.
                                 Note: It's necessarily EXT4_NAME_MAX +1 long.
   @param[out]       Length      Pointer to the UINTN that will hold the length of the path segment.

   @retval !EFI_SUCCESS          The path segment is too large(> EXT4_NAME_MAX).
 */
STATIC
EFI_STATUS
GetPathSegment (
  IN CONST CHAR16 *Path, OUT CHAR16 *PathSegment, OUT UINTN *Length
  )
{
  CONST CHAR16  *Start = Path;
  CONST CHAR16  *End   = Path;

  // The path segment ends on a backslash or a null terminator
  for( ; *End != L'\0' && *End != L'\\'; End++) {
  }

  *Length = End - Start;

  return StrnCpyS (PathSegment, EXT4_NAME_MAX, Start, End - Start);
}

#define EXT4_INO_PERM_READ_OWNER   0400
#define EXT4_INO_PERM_WRITE_OWNER  0200

BOOLEAN
Ext4ApplyPermissions (
  IN OUT EXT4_FILE *File, UINT64 OpenMode
  )
{
  UINT16  NeededPerms = 0;

  if((OpenMode & EFI_FILE_MODE_READ) != 0) {
    NeededPerms |= EXT4_INO_PERM_READ_OWNER;
  }

  if((OpenMode & EFI_FILE_MODE_WRITE) != 0) {
    NeededPerms |= EXT4_INO_PERM_WRITE_OWNER;
  }

  if((File->Inode->i_mode & NeededPerms) != NeededPerms) {
    return FALSE;
  }

  File->OpenMode = OpenMode;

  return TRUE;
}

EFI_STATUS EFIAPI
Ext4Open (
  IN EFI_FILE_PROTOCOL        *This,
  OUT EFI_FILE_PROTOCOL       **NewHandle,
  IN CHAR16                   *FileName,
  IN UINT64                   OpenMode,
  IN UINT64                   Attributes
  )
{
  EXT4_FILE       *Current;
  EXT4_PARTITION  *Partition;
  UINTN           Level;

  Current   = (EXT4_FILE *)This;
  Partition = Current->Partition;
  Level     = 0;

  DEBUG ((EFI_D_VERBOSE, "[ext4] Ext4Open %s\n", FileName));
  // If the path starts with a backslash, we treat the root directory as the base directory
  if(FileName[0] == L'\\') {
    FileName++;
    Current = Partition->Root;
  }

  while(FileName[0] != L'\0') {
    CHAR16      PathSegment[EXT4_NAME_MAX + 1];
    UINTN       Length;
    EXT4_FILE   *File;
    EFI_STATUS  Status;

    // Discard leading path separators
    while(FileName[0] == L'\\') {
      FileName++;
    }

    if(GetPathSegment (FileName, PathSegment, &Length) != EFI_SUCCESS) {
      return EFI_BUFFER_TOO_SMALL;
    }

    // Reached the end of the path
    if(Length == 0) {
      break;
    }

    FileName += Length;

    DEBUG ((EFI_D_VERBOSE, "[ext4] Opening %s\n", PathSegment));

    // TODO: We should look at the execute bit for permission checking on directory lookups
    // ^^ This would require a better knowledge of the path itself since we would need to know whether or not
    // we're the last token.

    // TODO: Symlinks?

    if (!Ext4FileIsDir (Current)) {
      return EFI_INVALID_PARAMETER;
    }

    Status = Ext4OpenFile (Current, PathSegment, Partition, EFI_FILE_MODE_READ, &File);

    if(EFI_ERROR (Status) && Status != EFI_NOT_FOUND) {
      return Status;
    } else if(Status == EFI_NOT_FOUND) {
      // TODO: Handle file creation
      return Status;
    }

    // Check if this is a valid file to open in EFI
    if(!Ext4FileIsOpenable (File)) {
      Ext4CloseInternal (File);
      // TODO: What status should we return?
      return EFI_ACCESS_DENIED;
    }

    if(Level != 0) {
      // Careful not to close the base directory
      Ext4CloseInternal (Current);
    }

    Level++;

    Current = File;
  }

  if (Level == 0) {
    // We opened the base directory again, so we need to duplicate the file structure
    Current = Ext4DuplicateFile (Current);
    if (Current == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
  }

  if(!Ext4ApplyPermissions (Current, OpenMode)) {
    Ext4CloseInternal (Current);
    return EFI_ACCESS_DENIED;
  }

  *NewHandle = &Current->Protocol;

  DEBUG ((EFI_D_VERBOSE, "Open successful: %s\n", Current->FileName));
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI
Ext4Close (
  IN EFI_FILE_PROTOCOL *This
  )
{
  return Ext4CloseInternal ((EXT4_FILE *)This);
}

/**
   Closes a file.

   @param[in]        File        Pointer to the file.

   @return Status of the closing of the file.
 */
EFI_STATUS
Ext4CloseInternal (
  IN EXT4_FILE *File
  )
{
  if(File == File->Partition->Root && !File->Partition->Unmounting) {
    return EFI_SUCCESS;
  }

  DEBUG ((EFI_D_VERBOSE, "[ext4] Closed file %p (inode %lu)\n",
          File, File->InodeNum));
  FreePool (File->FileName);
  FreePool (File->Inode);
  Ext4FreeExtentsMap (File);
  FreePool (File);
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI
Ext4Delete (
  IN EFI_FILE_PROTOCOL  *This
  )
{
  // TODO: When we add write support
  Ext4Close (This);
  return EFI_WARN_DELETE_FAILURE;
}

EFI_STATUS
EFIAPI
Ext4ReadFile (
  IN EFI_FILE_PROTOCOL        *This,
  IN OUT UINTN                *BufferSize,
  OUT VOID                    *Buffer
  )
{
  EXT4_FILE       *File;
  EXT4_PARTITION  *Partition;
  EFI_STATUS      Status;

  File = (EXT4_FILE *)This;
  Partition = File->Partition;

  ASSERT (Ext4FileIsOpenable (File));

  if(Ext4FileIsReg (File)) {
    Status = Ext4Read (Partition, File, Buffer, File->Position, BufferSize);
    if(Status == EFI_SUCCESS) {
      File->Position += *BufferSize;
    }

    return Status;
  } else if(Ext4FileIsDir (File)) {
    DEBUG ((EFI_D_VERBOSE, "[ext4] Reading directory\n"));
    Status = Ext4ReadDir (Partition, File, Buffer, File->Position, BufferSize);
    DEBUG ((EFI_D_VERBOSE, "[ext4] ReadDir status %r\n", Status));

    if(Status == EFI_SUCCESS) {
      DEBUG ((EFI_D_VERBOSE, "[ext4] ReadDir retlen %lu\n", *BufferSize));
    }

    return Status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Ext4WriteFile (
  IN EFI_FILE_PROTOCOL        *This,
  IN OUT UINTN                *BufferSize,
  IN VOID                    *Buffer
  )
{
  EXT4_FILE  *File = (EXT4_FILE *)This;

  if(!(File->OpenMode & EFI_FILE_MODE_WRITE)) {
    return EFI_ACCESS_DENIED;
  }

  // TODO: Add write support
  return EFI_WRITE_PROTECTED;
}

EFI_STATUS
EFIAPI
Ext4GetPosition (
  IN EFI_FILE_PROTOCOL        *This,
  OUT UINT64                  *Position
  )
{
  EXT4_FILE  *File = (EXT4_FILE *)This;

  if(Ext4FileIsDir (File)) {
    return EFI_UNSUPPORTED;
  }

  *Position = File->Position;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Ext4SetPosition (
  IN EFI_FILE_PROTOCOL        *This,
  IN UINT64                   Position
  )
{
  EXT4_FILE  *File = (EXT4_FILE *)This;

  // Only seeks to 0 (so it resets the ReadDir operation) are allowed
  if(Ext4FileIsDir (File) && Position != 0) {
    return EFI_UNSUPPORTED;
  }

  // -1 (0xffffff.......) seeks to the end of the file
  if(Position == (UINT64)- 1) {
    Position = EXT4_INODE_SIZE (File->Inode);
  }

  File->Position = Position;

  return EFI_SUCCESS;
}

/**
   Retrieves information about the file and stores it in the EFI_FILE_INFO format.

   @param[in]      File           Pointer to an opened file.
   @param[out]     Info           Pointer to a EFI_FILE_INFO.
   @param[in out]  BufferSize     Pointer to the buffer size

   @return Status of the file information request.
*/
EFI_STATUS
Ext4GetFileInfo (
  IN EXT4_FILE *File, OUT EFI_FILE_INFO *Info, IN OUT UINTN *BufferSize
  )
{
  // TODO: Get a way to set the directory entry for SetFileInfo
  UINTN  FileNameLen  = StrLen (File->FileName);
  UINTN  FileNameSize = StrSize (File->FileName);

  UINTN  NeededLength = SIZE_OF_EFI_FILE_INFO + FileNameSize;

  if(*BufferSize < NeededLength) {
    *BufferSize = NeededLength;
    return EFI_BUFFER_TOO_SMALL;
  }

  Info->FileSize     = EXT4_INODE_SIZE (File->Inode);
  Info->PhysicalSize = Ext4FilePhysicalSpace (File);
  Ext4FileATime (File, &Info->LastAccessTime);
  Ext4FileMTime (File, &Info->ModificationTime);
  Ext4FileCreateTime (File, &Info->LastAccessTime);
  Info->Attribute = 0;
  Info->Size = NeededLength;

  if(Ext4FileIsDir (File)) {
    Info->Attribute |= EFI_FILE_DIRECTORY;
  }

  *BufferSize = NeededLength;

  return StrCpyS (Info->FileName, FileNameLen + 1, File->FileName);
}

/**
   Retrieves information about the filesystem and stores it in the EFI_FILE_SYSTEM_INFO format.

   @param[in]      File           Pointer to an opened file.
   @param[out]     Info           Pointer to a EFI_FILE_SYSTEM_INFO.
   @param[in out]  BufferSize     Pointer to the buffer size

   @return Status of the file information request.
*/
STATIC
EFI_STATUS
Ext4GetFilesystemInfo (
  IN EXT4_PARTITION *Part, OUT EFI_FILE_SYSTEM_INFO *Info, IN OUT UINTN *BufferSize
  )
{
  // Length of s_volume_name + null terminator
  CHAR16         VolumeName[16 + 1];
  UINTN          VolNameLength;
  EFI_STATUS     Status;
  UINTN          NeededLength;
  EXT4_BLOCK_NR  TotalBlocks;
  EXT4_BLOCK_NR  FreeBlocks;

  VolNameLength = 0;

  // s_volume_name is only valid on dynamic revision; old filesystems don't support this
  if(Part->SuperBlock.s_rev_level == EXT4_DYNAMIC_REV) {
    Status = AsciiStrnToUnicodeStrS (
               (CONST CHAR8 *)Part->SuperBlock.s_volume_name,
               16,
               VolumeName,
               sizeof (VolumeName),
               &VolNameLength
               );

    if(Status != EFI_SUCCESS) {
      return Status;
    }
  }

  NeededLength = SIZE_OF_EFI_FILE_SYSTEM_INFO + StrSize (VolumeName);

  if(*BufferSize < NeededLength) {
    *BufferSize = NeededLength;
    return EFI_BUFFER_TOO_SMALL;
  }

  TotalBlocks = Part->NumberBlocks;

  FreeBlocks = Ext4MakeBlockNumberFromHalfs (
                 Part,
                 Part->SuperBlock.s_free_blocks_count,
                 Part->SuperBlock.s_free_blocks_count_hi
                 );

  Info->BlockSize = Part->BlockSize;
  Info->Size       = NeededLength;
  Info->ReadOnly   = Part->ReadOnly;
  Info->VolumeSize = TotalBlocks * Part->BlockSize;
  Info->FreeSpace  = FreeBlocks * Part->BlockSize;
  StrCpyS (Info->VolumeLabel, VolNameLength + 1, VolumeName);

  *BufferSize = NeededLength;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Ext4GetInfo (
  IN EFI_FILE_PROTOCOL        *This,
  IN EFI_GUID                 *InformationType,
  IN OUT UINTN                *BufferSize,
  OUT VOID                    *Buffer
  )
{
  if (CompareGuid (InformationType, &gEfiFileInfoGuid)) {
    return Ext4GetFileInfo ((EXT4_FILE *)This, Buffer, BufferSize);
  }

  if (CompareGuid (InformationType, &gEfiFileSystemInfoGuid)) {
    return Ext4GetFilesystemInfo (((EXT4_FILE *)This)->Partition, Buffer, BufferSize);
  }

  return EFI_UNSUPPORTED;
}

/**
   Duplicates a file structure.

   @param[in]        Original    Pointer to the original file.

   @return Pointer to the new file structure.
 */
STATIC
EXT4_FILE *
Ext4DuplicateFile (
  IN CONST EXT4_FILE *Original
  )
{
  EXT4_PARTITION  *Partition;
  EXT4_FILE       *File;

  Partition = Original->Partition;
  File = AllocateZeroPool (sizeof (EXT4_FILE));

  if (File == NULL) {
    return NULL;
  }

  File->Inode = Ext4AllocateInode (Partition);
  if (File->Inode == NULL) {
    FreePool (File);
    return NULL;
  }

  CopyMem (File->Inode, Original->Inode, Partition->InodeSize);

  File->FileName = AllocateZeroPool (StrSize (Original->FileName));
  if (File->FileName == NULL) {
    FreePool (File->Inode);
    FreePool (File);
    return NULL;
  }

  StrCpyS (File->FileName, StrLen (Original->FileName) + 1, Original->FileName);

  File->Position = 0;
  Ext4SetupFile (File, Partition);
  File->InodeNum = Original->InodeNum;
  File->OpenMode = 0; // Will be filled by other code

  if (!Ext4InitExtentsMap (File)) {
    FreePool (File->FileName);
    FreePool (File->Inode);
    FreePool (File);
    return NULL;
  }

  return File;
}
