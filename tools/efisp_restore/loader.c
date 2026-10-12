#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Protocol/PartitionInfo.h>

#define IMAGE_LIMIT (32U * 1024U * 1024U)

static const EFI_GUID block_guid = EFI_BLOCK_IO_PROTOCOL_GUID;
static const EFI_GUID record_guid = {
    0xfe2555be, 0xd716, 0x4686, {0xb9, 0xd0, 0x79, 0xdb, 0x59, 0x21, 0xb7, 0x0d}
};
static const EFI_GUID info_guid = {
    0x8cf2f62c, 0xbc9b, 0x4821, {0x80, 0x8d, 0xec, 0x9e, 0xc4, 0x21, 0xa1, 0xa0}
};
static const CHAR16 partition_name[] = {'e', 'f', 'i', 's', 'p', 0};

static UINT16 read16(const UINT8 *p) {
    return (UINT16)p[0] | ((UINT16)p[1] << 8);
}

static UINT32 read32(const UINT8 *p) {
    return (UINT32)p[0] | ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

static BOOLEAN is_efisp(const CHAR16 *name) {
    for (UINTN i = 0; i < sizeof(partition_name) / sizeof(partition_name[0]); ++i)
        if (name[i] != partition_name[i]) return FALSE;
    return TRUE;
}

static UINTN pe_size(const UINT8 *data, UINTN length) {
    if (length < 64 || read16(data) != 0x5a4d) return 0;
    UINTN pe = read32(data + 60);
    if (pe > length - 24 || read32(data + pe) != 0x4550 ||
        read16(data + pe + 4) != 0xaa64) return 0;
    UINTN count = read16(data + pe + 6);
    UINTN optional = read16(data + pe + 20);
    if (!count || count > 96 || optional < 112 ||
        optional > length - pe - 24) return 0;
    UINTN opt = pe + 24;
    if (read16(data + opt) != 0x20b || read16(data + opt + 68) != 10) return 0;
    UINTN table = opt + optional;
    if (count > (length - table) / 40) return 0;
    UINTN result = read32(data + opt + 60);
    if (result < table + count * 40 || result > length) return 0;
    for (UINTN i = 0; i < count; ++i) {
        const UINT8 *section = data + table + i * 40;
        UINTN size = read32(section + 16);
        UINTN offset = read32(section + 20);
        if (offset > length || size > length - offset) return 0;
        if (size && offset + size > result) result = offset + size;
    }
    if (optional >= 152 && read32(data + opt + 108) > 4) {
        UINTN offset = read32(data + opt + 144);
        UINTN size = read32(data + opt + 148);
        if (offset > length || size > length - offset) return 0;
        if (size && offset + size > result) result = offset + size;
    }
    return result;
}

void efisp_load(EFI_BOOT_SERVICES *bs, EFI_HANDLE parent) {
    if (!bs || !parent || !bs->LocateHandleBuffer || !bs->HandleProtocol ||
        !bs->FreePool || !bs->AllocatePages || !bs->FreePages ||
        !bs->LoadImage || !bs->StartImage || !bs->UnloadImage) return;
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;
    EFI_STATUS status = bs->LocateHandleBuffer(ByProtocol, (EFI_GUID *)&block_guid,
                                             NULL, &count, &handles);
    if (EFI_ERROR(status) || !handles) return;
    EFI_BLOCK_IO_PROTOCOL *selected = NULL;
    UINTN matches = 0;
    if (count <= 4096) {
        for (UINTN i = 0; i < count; ++i) {
            EFI_PARTITION_ENTRY *record = NULL;
            EFI_PARTITION_INFO_PROTOCOL *info = NULL;
            EFI_BLOCK_IO_PROTOCOL *block = NULL;
            status = bs->HandleProtocol(handles[i], (EFI_GUID *)&record_guid,
                                        (VOID **)&record);
            BOOLEAN match = !EFI_ERROR(status) && record && is_efisp(record->PartitionName);
            if (!match) {
                status = bs->HandleProtocol(handles[i], (EFI_GUID *)&info_guid,
                                            (VOID **)&info);
                match = !EFI_ERROR(status) && info && info->Type == PARTITION_TYPE_GPT &&
                        is_efisp(info->Info.Gpt.PartitionName);
            }
            if (!match) continue;
            status = bs->HandleProtocol(handles[i], (EFI_GUID *)&block_guid,
                                        (VOID **)&block);
            if (EFI_ERROR(status) || !block || !block->Media || !block->ReadBlocks ||
                !block->Media->MediaPresent || block->Media->RemovableMedia ||
                !block->Media->LogicalPartition) continue;
            selected = block;
            ++matches;
        }
    }
    bs->FreePool(handles);
    if (matches != 1) return;
    EFI_BLOCK_IO_MEDIA *media = selected->Media;
    UINTN block_size = media->BlockSize;
    if (block_size < 512 || block_size > 65536 || (block_size & (block_size - 1)) ||
        media->IoAlign > EFI_PAGE_SIZE ||
        (media->IoAlign && (media->IoAlign & (media->IoAlign - 1))) ||
        media->LastBlock >= IMAGE_LIMIT / block_size) return;
    UINTN length = (UINTN)(media->LastBlock + 1) * block_size;
    UINTN pages = EFI_SIZE_TO_PAGES(length);
    EFI_PHYSICAL_ADDRESS memory = 0;
    status = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &memory);
    if (EFI_ERROR(status)) return;
    VOID *buffer = (VOID *)(UINTN)memory;
    status = selected->ReadBlocks(selected, media->MediaId, 0, length, buffer);
    EFI_HANDLE image = NULL;
    if (!EFI_ERROR(status)) {
        UINTN size = pe_size(buffer, length);
        if (size) {
            status = bs->LoadImage(FALSE, parent, NULL, buffer, size, &image);
            bs->FreePages(memory, pages);
            if (!EFI_ERROR(status) && image) {
                UINTN exit_size = 0;
                CHAR16 *exit_data = NULL;
                bs->StartImage(image, &exit_size, &exit_data);
                if (exit_data) bs->FreePool(exit_data);
            }
            if (image) bs->UnloadImage(image);
            return;
        }
    }
    bs->FreePages(memory, pages);
}
