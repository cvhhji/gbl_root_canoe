/**
  @file Common header for the driver

  Copyright (c) 2021 Pedro Falcato All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef _EXT4_H
#define _EXT4_H

#include <Uefi.h>

#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>
#include <Guid/FileSystemVolumeLabelInfo.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include <Protocol/DiskIo2.h>
#include <Protocol/SimpleFileSystem.h>

#include <Library/PcdLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/OrderedCollectionLib.h>

#include "Ext4Disk.h"

#define EXT4_NAME_MAX  255

#define EXT4_DRIVER_VERSION  0x0000

/**
   Opens an ext4 partition and installs the Simple File System protocol.

   @param[in]        DeviceHandle     Handle to the block device.
   @param[in]        DiskIo           Pointer to an EFI_DISK_IO_PROTOCOL.
   @param[in opt]    DiskIo2          Pointer to an EFI_DISK_IO2_PROTOCOL, if supported.
   @param[in]        BlockIo          Pointer to an EFI_BLOCK_IO_PROTOCOL.

   @retval EFI_SUCCESS      The opening was successful.
           !EFI_SUCCESS     Opening failed.
 */
EFI_STATUS
Ext4OpenPartition (
  IN EFI_HANDLE DeviceHandle,
  IN EFI_DISK_IO_PROTOCOL *DiskIo,
  IN OPTIONAL EFI_DISK_IO2_PROTOCOL *DiskIo2,
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo
  );

typedef struct _Ext4File EXT4_FILE;

typedef struct _Ext4_PARTITION {
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL    Interface;
  EFI_DISK_IO_PROTOCOL               *DiskIo;
  EFI_DISK_IO2_PROTOCOL              *DiskIo2;
  EFI_BLOCK_IO_PROTOCOL              *BlockIo;

  EXT4_SUPERBLOCK                    SuperBlock;
  BOOLEAN                            Unmounting;

  UINT32                             FeaturesIncompat;
  UINT32                             FeaturesCompat;
  UINT32                             FeaturesRoCompat;
  UINT32                             InodeSize;
  UINT32                             BlockSize;
  BOOLEAN                            ReadOnly;
  UINT64                             NumberBlockGroups;
  EXT4_BLOCK_NR                      NumberBlocks;

  EXT4_BLOCK_GROUP_DESC              *BlockGroups;
  UINT32                             DescSize;
  EXT4_FILE                          *Root;

  UINT32                             InitialSeed;

  // log2(Entries) = log2(BlockSize/4)
  // Needed for indirect block operations.
  UINT16                             EntryShift;
} EXT4_PARTITION;

/**
   Opens and parses the superblock.

   @param[out]     Partition Partition structure to fill with filesystem details.
   @retval EFI_SUCCESS       Parsing was succesful and the partition is a
                             valid ext4 partition.
 */
EFI_STATUS
Ext4OpenSuperblock (
  OUT EXT4_PARTITION *Partition
  );

/**
   Retrieves the EFI_BLOCK_IO_PROTOCOL of the partition.

   @param[in]     Partition  Pointer to the opened ext4 partition.
   @return The Block IO protocol associated with the partition.
 */
STATIC inline
EFI_BLOCK_IO_PROTOCOL *
Ext4BlockIo (
  EXT4_PARTITION *Partition
  )
{
  return Partition->BlockIo;
}

/**
   Retrieves the EFI_DISK_IO_PROTOCOL of the partition.

   @param[in]     Partition  Pointer to the opened ext4 partition.
   @return The Disk IO protocol associated with the partition.
 */
STATIC inline
EFI_DISK_IO_PROTOCOL *
Ext4DiskIo (
  EXT4_PARTITION *Partition
  )
{
  return Partition->DiskIo;
}

/**
   Retrieves the EFI_DISK_IO2_PROTOCOL of the partition.

   @param[in]     Partition  Pointer to the opened ext4 partition.
   @return The Disk IO 2 protocol associated with the partition, or NULL if
           not supported.
 */
STATIC inline
EFI_DISK_IO2_PROTOCOL *
Ext4DiskIo2 (
  EXT4_PARTITION *Partition
  )
{
  return Partition->DiskIo2;
}

/**
   Retrieves the media ID of the partition.

   @param[in]     Partition  Pointer to the opened ext4 partition.
   @return The media ID associated with the partition.
 */
STATIC inline
UINT32
Ext4MediaId (
  EXT4_PARTITION *Partition
  )
{
  return Partition->BlockIo->Media->MediaId;
}

/**
   Reads from the partition's disk using the DISK_IO protocol.

   @param[in]  Partition      Pointer to the opened ext4 partition.
   @param[out] Buffer         Pointer to a destination buffer.
   @param[in]  Length         Length of the destination buffer.
   @param[in]  Offset         Offset, in bytes, of the location to read.

   @return Success status of the disk read.
 */
EFI_STATUS
Ext4ReadDiskIo (
  IN EXT4_PARTITION *Partition,
  OUT VOID *Buffer,
  IN UINTN Length,
  IN UINT64 Offset
  );

/**
   Reads blocks from the partition's disk using the DISK_IO protocol.

   @param[in]  Partition      Pointer to the opened ext4 partition.
   @param[out] Buffer         Pointer to a destination buffer.
   @param[in]  NumberBlocks   Length of the read, in filesystem blocks.
   @param[in]  BlockNumber    Starting block number.

   @return Success status of the read.
 */
EFI_STATUS
Ext4ReadBlocks (
  IN  EXT4_PARTITION *Partition,
  OUT VOID *Buffer,
  IN  UINTN NumberBlocks,
  IN  EXT4_BLOCK_NR BlockNumber
  );

/**
   Allocates a buffer and reads blocks from the partition's disk using the DISK_IO protocol.
   This function is deprecated and will be removed in the future.

   @param[in]  Partition      Pointer to the opened ext4 partition.
   @param[in]  NumberBlocks   Length of the read, in filesystem blocks.
   @param[in]  BlockNumber    Starting block number.

   @return Buffer allocated by AllocatePool, or NULL if some part of the process
           failed.
 */
VOID *
Ext4AllocAndReadBlocks (
  IN EXT4_PARTITION *Partition,
  IN UINTN NumberBlocks,
  IN EXT4_BLOCK_NR BlockNumber
  );

/**
   Checks if the opened partition has the 64-bit feature (see EXT4_FEATURE_INCOMPAT_64BIT).

   @param[in]  Partition      Pointer to the opened ext4 partition.

   @return TRUE if EXT4_FEATURE_INCOMPAT_64BIT is enabled, else FALSE.
 */
STATIC inline
BOOLEAN
Ext4Is64Bit (
  IN CONST EXT4_PARTITION *Partition
  )
{
  return Partition->FeaturesIncompat & EXT4_FEATURE_INCOMPAT_64BIT;
}

/**
   Composes an EXT4_BLOCK_NR safely, from two halfs.

   @param[in]  Partition      Pointer to the opened ext4 partition.
   @param[in]  Low            Low half of the block number.
   @param[in]  High           High half of the block number.

   @return The block number formed by Low, and if 64 bit is enabled, High.
 */
STATIC inline
EXT4_BLOCK_NR
Ext4MakeBlockNumberFromHalfs (
  IN CONST EXT4_PARTITION *Partition,
  IN UINT32 Low,
  IN UINT32 High
  )
{
  // High might have garbage if it's not a 64 bit filesystem
  return Ext4Is64Bit (Partition) ? Low | ((UINT64)High << 32) : Low;
}

/**
   Retrieves a block group descriptor of the ext4 filesystem.

   @param[in]  Partition      Pointer to the opened ext4 partition.
   @param[in]  BlockGroup    Block group number.

   @return A pointer to the block group descriptor.
 */
STATIC inline
EXT4_BLOCK_GROUP_DESC *
Ext4GetBlockGroupDesc (
  IN EXT4_PARTITION *Partition,
  IN UINT32 BlockGroup
  )
{
  // Maybe assert that the block group nr isn't a nonsense number?
  return (EXT4_BLOCK_GROUP_DESC *)((CHAR8 *)Partition->BlockGroups + BlockGroup * Partition->DescSize);
}

/**
   Reads an inode from disk.

   @param[in]    Partition  Pointer to the opened partition.
   @param[in]    InodeNum   Number of the desired Inode
   @param[out]   OutIno     Pointer to where it will be stored a pointer to the read inode.

   @return Status of the inode read.
 */
EFI_STATUS
Ext4ReadInode (
  IN  EXT4_PARTITION *Partition,
  IN  EXT4_INO_NR InodeNum,
  OUT EXT4_INODE **OutIno
  );

/**
   Converts blocks to bytes.

   @param[in]    Partition  Pointer to the opened partition.
   @param[in]    Block      Block number/number of blocks.

   @return The number of bytes.
 */
STATIC inline
UINT64
Ext4BlockToByteOffset (
  IN CONST EXT4_PARTITION *Partition,
  IN EXT4_BLOCK_NR Block
  )
{
  return Partition->BlockSize * Block;
}

/**
   Reads from an EXT4 inode.
   @param[in]      Partition     Pointer to the opened EXT4 partition.
   @param[in]      File          Pointer to the opened file.
   @param[out]     Buffer        Pointer to the buffer.
   @param[in]      Offset        Offset of the read.
   @param[in out]  Length        Pointer to the length of the buffer, in bytes.
                                 After a succesful read, it's updated to the number of read bytes.

   @return Status of the read operation.
*/
EFI_STATUS
Ext4Read (
  IN     EXT4_PARTITION *Partition,
  IN     EXT4_FILE *File,
  OUT    VOID *Buffer,
  IN     UINT64 Offset,
  IN OUT UINTN *Length
  );

/**
   Retrieves the size of the inode.

   @param[in]    Inode      Pointer to the ext4 inode.

   @return The size of the inode, in bytes.
 */
STATIC inline
UINT64
Ext4InodeSize (
  CONST EXT4_INODE *Inode
  )
{
  return ((UINT64)Inode->i_size_hi << 32) | Inode->i_size_lo;
}

/**
   Retrieves an extent from an EXT4 inode.
   @param[in]      Partition     Pointer to the opened EXT4 partition.
   @param[in]      File          Pointer to the opened file.
   @param[in]      LogicalBlock  Block number which the returned extent must cover.
   @param[out]     Extent        Pointer to the output buffer, where the extent will be copied to.

   @retval EFI_SUCCESS        Retrieval was succesful.
   @retval EFI_NO_MAPPING     Block has no mapping.
*/
EFI_STATUS
Ext4GetExtent (
  IN  EXT4_PARTITION *Partition,
  IN  EXT4_FILE *File,
  IN  EXT4_BLOCK_NR LogicalBlock,
  OUT EXT4_EXTENT *Extent
  );

struct _Ext4File {
  EFI_FILE_PROTOCOL     Protocol;
  EXT4_INODE            *Inode;
  EXT4_INO_NR           InodeNum;

  UINT64                OpenMode;
  UINT64                Position;

  EXT4_PARTITION        *Partition;
  CHAR16                *FileName;

  ORDERED_COLLECTION    *ExtentsMap;
};

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
  IN  EXT4_FILE *Directory,
  IN  CONST CHAR16 *NameUnicode,
  IN  EXT4_PARTITION *Partition,
  OUT EXT4_DIR_ENTRY *Result
  );

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
  );

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
  );

/**
   Allocates a zeroed inode structure.
   @param[in]      Partition     Pointer to the opened EXT4 partition.

   @return Pointer to the allocated structure, from the pool,
           with size Partition->InodeSize.
*/
EXT4_INODE *
Ext4AllocateInode (
  IN EXT4_PARTITION *Partition
  );

// Part of the EFI_SIMPLE_FILE_SYSTEM_PROTOCOL

EFI_STATUS EFIAPI
Ext4OpenVolume (
  IN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Partition,
  IN EFI_FILE_PROTOCOL **Root
  );

// End of EFI_SIMPLE_FILE_SYSTEM_PROTOCOL

/**
   Sets up the protocol and metadata of a file that is being opened.

   @param[in out]        File        Pointer to the file.
   @param[in]            Partition   Pointer to the opened partition.
 */
VOID
Ext4SetupFile (
  IN OUT EXT4_FILE *File,
  IN EXT4_PARTITION *Partition
  );

/**
   Closes a file.

   @param[in]        File        Pointer to the file.

   @return Status of the closing of the file.
 */
EFI_STATUS
Ext4CloseInternal (
  IN EXT4_FILE *File
  );

// Part of the EFI_FILE_PROTOCOL

EFI_STATUS EFIAPI
Ext4Open (
  IN EFI_FILE_PROTOCOL        *This,
  OUT EFI_FILE_PROTOCOL       **NewHandle,
  IN CHAR16                   *FileName,
  IN UINT64                   OpenMode,
  IN UINT64                   Attributes
  );

EFI_STATUS EFIAPI
Ext4Close (
  IN EFI_FILE_PROTOCOL *This
  );

EFI_STATUS EFIAPI
Ext4Delete (
  IN EFI_FILE_PROTOCOL  *This
  );

EFI_STATUS
EFIAPI
Ext4ReadFile (
  IN EFI_FILE_PROTOCOL        *This,
  IN OUT UINTN                *BufferSize,
  OUT VOID                    *Buffer
  );

EFI_STATUS
EFIAPI
Ext4WriteFile (
  IN EFI_FILE_PROTOCOL        *This,
  IN OUT UINTN                *BufferSize,
  IN VOID                    *Buffer
  );

EFI_STATUS
EFIAPI
Ext4GetPosition (
  IN EFI_FILE_PROTOCOL        *This,
  OUT UINT64                  *Position
  );

EFI_STATUS
EFIAPI
Ext4SetPosition (
  IN EFI_FILE_PROTOCOL        *This,
  IN UINT64                   Position
  );

EFI_STATUS
EFIAPI
Ext4GetInfo (
  IN EFI_FILE_PROTOCOL        *This,
  IN EFI_GUID                 *InformationType,
  IN OUT UINTN                *BufferSize,
  OUT VOID                    *Buffer
  );

// EFI_FILE_PROTOCOL implementation ends here.

/**
   Checks if a file is a directory.
   @param[in]      File          Pointer to the opened file.

   @return TRUE if file is a directory.
*/
BOOLEAN
Ext4FileIsDir (
  IN CONST EXT4_FILE *File
  );

/**
   Checks if a file is a regular file.
   @param[in]      File          Pointer to the opened file.

   @return BOOLEAN         TRUE if file is a regular file.
*/
BOOLEAN
Ext4FileIsReg (
  IN CONST EXT4_FILE *File
  );

// In EFI we can't open FIFO pipes, UNIX sockets, character/block devices since these concepts are
// at the kernel level and are OS dependent.

/**
   Checks if a file is openable.
   @param[in]      File    Pointer to the file trying to be opened.


   @return TRUE if file is openable. A file is considered openable if
           it's a regular file or a directory, since most other file types
           don't make sense under UEFI.
*/
STATIC inline
BOOLEAN
Ext4FileIsOpenable (
  IN CONST EXT4_FILE *File
  )
{
  return Ext4FileIsReg (File) || Ext4FileIsDir (File);
}

#define Ext4InodeHasField(Inode, \
                          Field)  (Inode->i_extra_isize + EXT4_GOOD_OLD_INODE_SIZE >= OFFSET_OF (EXT4_INODE, Field) + \
                                   sizeof (((EXT4_INODE *)NULL)->Field))

/**
   Calculates the physical space used by a file.
   @param[in]      File          Pointer to the opened file.

   @return Physical space used by a file, in bytes.
*/
UINT64
Ext4FilePhysicalSpace (
  EXT4_FILE *File
  );

/**
   Gets the file's last access time.
   @param[in]      File   Pointer to the opened file.
   @param[out]     Time   Pointer to an EFI_TIME structure.
*/
VOID
Ext4FileATime (
  IN EXT4_FILE *File,
  OUT EFI_TIME *Time
  );

/**
   Gets the file's last (data) modification time.
   @param[in]      File   Pointer to the opened file.
   @param[out]     Time   Pointer to an EFI_TIME structure.
*/
VOID
Ext4FileMTime (
  IN EXT4_FILE *File,
  OUT EFI_TIME *Time
  );

/**
   Gets the file's creation time, if possible.
   @param[in]      File   Pointer to the opened file.
   @param[out]     Time   Pointer to an EFI_TIME structure.
                          In the case where the the creation time isn't recorded,
                          Time is zeroed.
*/
VOID
Ext4FileCreateTime (
  IN EXT4_FILE *File, OUT EFI_TIME *Time
  );

/**
   Initialises Unicode collation, which is needed for case-insensitive string comparisons
   within the driver (a good example of an application of this is filename comparison).

   @param[in]      DriverHandle    Handle to the driver image.

   @retval EFI_SUCCESS   Unicode collation was successfully initialised.
   @retval !EFI_SUCCESS  Failure.
*/
EFI_STATUS
Ext4InitialiseUnicodeCollation (
  EFI_HANDLE DriverHandle
  );

/**
   Does a case-insensitive string comparison. Refer to EFI_UNICODE_COLLATION_PROTOCOL's StriColl
   for more details.

   @param[in]      Str1   Pointer to a null terminated string.
   @param[in]      Str2   Pointer to a null terminated string.

   @retval 0   Str1 is equivalent to Str2.
   @retval >0  Str1 is lexically greater than Str2.
   @retval <0  Str1 is lexically less than Str2.
*/
INTN
Ext4StrCmpInsensitive (
  IN CHAR16                                 *Str1,
  IN CHAR16                                 *Str2
  );

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
  );

/**
   Retrieves information about the file and stores it in the EFI_FILE_INFO format.

   @param[in]      File           Pointer to an opened file.
   @param[out]     Info           Pointer to a EFI_FILE_INFO.
   @param[in out]  BufferSize     Pointer to the buffer size

   @return Status of the file information request.
*/
EFI_STATUS
Ext4GetFileInfo (
  IN     EXT4_FILE *File,
  OUT    EFI_FILE_INFO *Info,
  IN OUT UINTN *BufferSize
  );

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
  );

/**
   Initialises the (empty) extents map, that will work as a cache of extents.

   @param[in]      File        Pointer to the open file.

   @return Result of the operation.
*/
EFI_STATUS
Ext4InitExtentsMap (
  IN EXT4_FILE *File
  );

/**
   Frees the extents map, deleting every extent stored.

   @param[in]      File        Pointer to the open file.
*/
VOID
Ext4FreeExtentsMap (
  IN EXT4_FILE *File
  );

/**
   Calculates the CRC32c checksum of the given buffer.

   @param[in]      Buffer        Pointer to the buffer.
   @param[in]      Length        Length of the buffer, in bytes.
   @param[in]      InitialValue  Initial value of the CRC.

   @return The CRC32c checksum.
*/
UINT32
CalculateCrc32c (
  IN CONST VOID *Buffer,
  IN UINTN Length,
  IN UINT32 InitialValue
  );

/**
   Calculates the checksum of the given buffer.
   @param[in]      Partition     Pointer to the opened EXT4 partition.
   @param[in]      Buffer        Pointer to the buffer.
   @param[in]      Length        Length of the buffer, in bytes.
   @param[in]      InitialValue  Initial value of the CRC.

   @return The checksum.
*/
UINT32
Ext4CalculateChecksum (
  IN CONST EXT4_PARTITION *Partition,
  IN CONST VOID *Buffer,
  IN UINTN Length,
  IN UINT32 InitialValue
  );

/**
   Calculates the checksum of the given inode.
   @param[in]      Partition     Pointer to the opened EXT4 partition.
   @param[in]      Inode         Pointer to the inode.
   @param[in]      InodeNum      Inode number.

   @return The checksum.
*/
UINT32
Ext4CalculateInodeChecksum (
  IN CONST EXT4_PARTITION *Partition,
  IN CONST EXT4_INODE *Inode,
  IN EXT4_INO_NR InodeNum
  );

/**
   Checks if the checksum of the inode is correct.
   @param[in]      Partition     Pointer to the opened EXT4 partition.
   @param[in]      Inode         Pointer to the inode.
   @param[in]      InodeNum      Inode number.

   @return TRUE if checksum is correct, FALSE if there is corruption.
*/
BOOLEAN
Ext4CheckInodeChecksum (
  IN CONST EXT4_PARTITION *Partition,
  IN CONST EXT4_INODE *Inode,
  IN EXT4_INO_NR InodeNum
  );

/**
   Unmounts and frees an ext4 partition.

   @param[in]        Partition        Pointer to the opened partition.

   @return Status of the unmount.
 */
EFI_STATUS
Ext4UnmountAndFreePartition (
  IN EXT4_PARTITION *Partition
  );

/**
   Checks if the checksum of the block group descriptor is correct.
   @param[in]      Partition       Pointer to the opened EXT4 partition.
   @param[in]      BlockGroupDesc  Pointer to the block group descriptor.
   @param[in]      BlockGroupNum   Number of the block group.

   @return TRUE if checksum is correct, FALSE if there is corruption.
*/
BOOLEAN
Ext4VerifyBlockGroupDescChecksum (
  IN CONST EXT4_PARTITION *Partition,
  IN CONST EXT4_BLOCK_GROUP_DESC *BlockGroupDesc,
  IN UINT32 BlockGroupNum
  );

/**
   Calculates the checksum of the block group descriptor.
   @param[in]      Partition       Pointer to the opened EXT4 partition.
   @param[in]      BlockGroupDesc  Pointer to the block group descriptor.
   @param[in]      BlockGroupNum   Number of the block group.

   @return The checksum.
*/
UINT16
Ext4CalculateBlockGroupDescChecksum (
  IN CONST EXT4_PARTITION *Partition,
  IN CONST EXT4_BLOCK_GROUP_DESC *BlockGroupDesc,
  IN UINT32 BlockGroupNum
  );

/**
   Verifies the existance of a particular RO compat feature set.
   @param[in]      Partition           Pointer to the opened EXT4 partition.
   @param[in]      RoCompatFeatureSet  Feature set to test.

   @return TRUE if all features are supported, else FALSE.
*/
STATIC inline
BOOLEAN
Ext4HasRoCompat (
  IN CONST EXT4_PARTITION *Partition,
  IN UINT32 RoCompatFeatureSet
  )
{
  return (Partition->FeaturesRoCompat & RoCompatFeatureSet) == RoCompatFeatureSet;
}

/**
   Verifies the existance of a particular compat feature set.
   @param[in]      Partition           Pointer to the opened EXT4 partition.
   @param[in]      RoCompatFeatureSet  Feature set to test.

   @return TRUE if all features are supported, else FALSE.
*/
STATIC inline
BOOLEAN
Ext4HasCompat (
  IN CONST EXT4_PARTITION *Partition,
  IN UINT32 CompatFeatureSet
  )
{
  return (Partition->FeaturesCompat & CompatFeatureSet) == CompatFeatureSet;
}

/**
   Verifies the existance of a particular compat feature set.
   @param[in]      Partition           Pointer to the opened EXT4 partition.
   @param[in]      RoCompatFeatureSet  Feature set to test.

   @return TRUE if all features are supported, else FALSE.
*/
STATIC inline
BOOLEAN
Ext4HasIncompat (
  IN CONST EXT4_PARTITION *Partition,
  IN UINT32 IncompatFeatureSet
  )
{
  return (Partition->FeaturesIncompat & IncompatFeatureSet) == IncompatFeatureSet;
}

// Note: Might be a good idea to provide generic Ext4Has$feature() through macros.

/**
   Checks if metadata_csum is enabled on the partition.
   @param[in]      Partition           Pointer to the opened EXT4 partition.
   @param[in]      RoCompatFeatureSet  Feature set to test.

   @return TRUE if the feature is supported, else FALSE.
*/
STATIC inline
BOOLEAN
Ext4HasMetadataCsum (
  IN CONST EXT4_PARTITION *Partition
  )
{
  return Ext4HasRoCompat (Partition, EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);
}

/**
   Checks if gdt_csum is enabled on the partition.
   @param[in]      Partition           Pointer to the opened EXT4 partition.
   @param[in]      RoCompatFeatureSet  Feature set to test.

   @return TRUE if the feature is supported, else FALSE.
*/
STATIC inline
BOOLEAN
Ext4HasGdtCsum (
  IN CONST EXT4_PARTITION *Partition
  )
{
  return Ext4HasRoCompat (Partition, EXT4_FEATURE_RO_COMPAT_GDT_CSUM);
}

/* Header bounds must be validated before the checksum locates its tail. */
BOOLEAN Ext4CheckExtentChecksum (CONST EXT4_EXTENT_HEADER *Header, CONST EXT4_FILE *File);

#endif
