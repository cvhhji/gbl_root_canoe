#include "SuperFbMenu.h"
#include "SuperFbFont.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>

STATIC CONST struct {
  CONST CHAR16 *English;
  CONST CHAR16 *Chinese;
} mText[] = {
  { L"Boot Menu", L"启动菜单" },
  { L"Advanced", L"高级选项" },
  { L"Power Off", L"关机" },
  { L"Restart", L"重启" },
  { L"Back", L"返回" },
  { L"Language", L"语言 / Language" },
  { L"Choose the menu language.", L"选择菜单语言。" },
  { L"Chinese text is unavailable on this display", L"当前显示设备不支持中文" },
  { L"Language changed for this boot; could not save", L"语言已切换，但未能保存，重启后将恢复" },
  { L"Boot failed", L"启动失败" },
  { L"Submenu too deep", L"子菜单层数过多" },
  { L"Press power to continue.\r\n", L"按电源键继续。\r\n" },
  { L"Vol Up/Down: move   Power: select", L"音量键：移动   电源键：选择" },
  { L"Vol Up/Down: move   Power: open", L"音量键：移动   电源键：打开" },
  { L"FASTBOOT MODE\r\n", L"FASTBOOT 模式\r\n" },
  { L"Booting %s\r\n", L"正在启动 %s\r\n" },
  { L"Powering off...", L"正在关机..." },
  { L"Restarting...", L"正在重启..." },
  { L"Entering Boot Menu\r\n", L"正在进入启动菜单\r\n" },
  { L"Entering %s\r\n", L"正在进入%s\r\n" },
  { L"  No boot entries found.\r\n", L"  未找到启动项。\r\n" },
  { L"    ... %u more\r\n", L"    ... 还有 %u 项\r\n" },
  { L"Read only", L"只读" },
  { L"Read Write", L"读写" },
  { L"read-only", L"只读" },
  { L"read-write", L"读写" },
  { L"Mount %s how?", L"选择 %s 的访问方式" },
  { L"USB Mass Storage", L"USB 大容量存储" },
  { L"USB Mass Storage >", L"USB 大容量存储 >" },
  { L"Could not wrap partition", L"无法创建分区映射" },
  { L"Could not wrap LUN", L"无法创建 LUN 映射" },
  { L"Could not wrap container", L"无法创建容器映射" },
  { L"Could not start mass storage", L"无法启动 USB 大容量存储" },
  { L"Partition: %s (%s)", L"分区：%s（%s）" },
  { L"Whole LUN %u (%s)", L"整个 LUN %u（%s）" },
  { L"Export a partition", L"导出分区" },
  { L"Export a partition >", L"导出分区 >" },
  { L"Export a LUN", L"导出 LUN" },
  { L"Export a LUN >", L"导出 LUN >" },
  { L"Export a Virtual Disk", L"导出虚拟磁盘" },
  { L"Export a Virtual Disk >", L"导出虚拟磁盘 >" },
  { L"No storage LUNs found", L"未找到存储 LUN" },
  { L"Pick the LUN holding the partition.", L"选择分区所在的 LUN。" },
  { L"Pick the partition to export.", L"选择要导出的分区。" },
  { L"Pick the LUN to export whole.", L"选择要完整导出的 LUN。" },
  { L"Partition Picker", L"选择分区" },
  { L"No partitions on this LUN", L"此 LUN 中没有分区" },
  { L"No efisp.fat is mounted", L"未挂载 efisp.fat" },
  { L"Pick the mounted container to export.", L"选择要导出的已挂载容器。" },
  { L"Could not withdraw the container", L"无法卸载容器" },
  { L"Container unavailable", L"容器不可用" },
  { L"%s (%s)", L"%s（%s）" },
  { L"Virtual Disks", L"虚拟磁盘" },
  { L"Disk", L"磁盘" },
  { L"Disk %s", L"磁盘 %s" },
  { L"Volumes", L"存储卷" },
  { L"Volumes >", L"存储卷 >" },
  { L"Volume", L"存储卷" },
  { L"No file system volumes found", L"未找到文件系统卷" },
  { L"Pick a storage group.", L"选择存储设备。" },
  { L"No volumes in this group", L"此设备中没有存储卷" },
  { L"Pick a volume to browse.", L"选择要浏览的存储卷。" },
  { L"Expose device storage to the host over USB.", L"通过 USB 将设备存储连接到电脑。" },
  { L"Enter Fastboot", L"进入 Fastboot" },
  { L"EFI Driver", L"EFI 驱动" },
  { L"Load", L"加载" },
  { L"Driver load failed", L"驱动加载失败" },
  { L"Driver loaded", L"驱动已加载" },
  { L"Boot (temporary)", L"临时启动" },
  { L"Add to BootMenu", L"添加到启动菜单" },
  { L"Cannot address that file", L"无法定位该文件" },
  { L"EFI Application", L"EFI 应用" },
  { L"Could not save entry", L"无法保存启动项" },
  { L"Added to boot menu", L"已添加到启动菜单" },
  { L"Out of memory", L"内存不足" },
  { L"Cannot read directory", L"无法读取目录" },
  { L"    (directory has more than %u entries; rest not shown)\r\n", L"    （目录超过 %u 项，其余未显示）\r\n" },
  { L"Not an EFI application", L"此文件不是 EFI 应用" },
  { L"File Browser", L"文件浏览器" },
  { L"Volume %u", L"存储卷 %u" },
  { L"Volume %u: %s", L"存储卷 %u：%s" },
  { L"EFI Program Selector", L"选择 EFI 程序" },
  { L"Choose a volume to browse.", L"选择要浏览的存储卷。" },
  { L"The host may now mount the disk.", L"电脑现在可以挂载此磁盘。" },
  { L"  Size: %Lu bytes (%Lu MiB)\r\n", L"  大小：%Lu 字节（%Lu MiB）\r\n" },
  { L"  Volume Down ends this session.\r\n", L"  按音量下键结束本次连接。\r\n" },
  { L"Volume Down: stop export", L"音量下键：停止导出" }
};

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mGraphics;
STATIC BOOLEAN mInitialized;
STATIC BOOLEAN mChinese;
STATIC UINTN mScale;
STATIC UINTN mColumn;
STATIC UINTN mRow;
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL mPixels[64 * 64];

VOID
SfbInitializeText (VOID)
{
  CHAR8 Saved[16];
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Pixel;

  if (mInitialized) {
    return;
  }
  mInitialized = TRUE;
  if (EFI_ERROR (gBS->HandleProtocol (gST->ConsoleOutHandle,
                                      &gEfiGraphicsOutputProtocolGuid,
                                      (VOID **)&mGraphics))) {
    mGraphics = NULL;
    if (EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL,
                                         (VOID **)&mGraphics))) {
      mGraphics = NULL;
    }
  }
  if (mGraphics == NULL || mGraphics->Mode == NULL ||
      mGraphics->Mode->Info == NULL || mGraphics->Blt == NULL ||
      mGraphics->Mode->Info->HorizontalResolution < 320 ||
      mGraphics->Mode->Info->VerticalResolution < 480 ||
      EFI_ERROR (mGraphics->Blt (mGraphics, &Pixel, EfiBltVideoToBltBuffer,
                                  0, 0, 0, 0, 1, 1, 0))) {
    mGraphics = NULL;
    return;
  }
  mScale = MIN (4, MIN (mGraphics->Mode->Info->HorizontalResolution / 360,
                         mGraphics->Mode->Info->VerticalResolution / 480));
  if (mScale == 0) {
    mScale = 1;
  }
  if (!EFI_ERROR (SfbStoreRead (SFB_STORE_LANGUAGE, Saved, sizeof (Saved)))) {
    mChinese = (BOOLEAN)(AsciiStrCmp (Saved, "SFBL1|zh") == 0);
  }
}

BOOLEAN
SfbChineseAvailable (VOID)
{
  SfbInitializeText ();
  return (BOOLEAN)(mGraphics != NULL);
}

BOOLEAN
SfbIsChinese (VOID)
{
  SfbInitializeText ();
  return mChinese;
}

UINTN
SfbTextColumns (VOID)
{
  UINTN Columns = 80;
  UINTN Rows;

  SfbInitializeText ();
  if (mGraphics != NULL) {
    return mGraphics->Mode->Info->HorizontalResolution / (8 * mScale);
  }
  gST->ConOut->QueryMode (gST->ConOut, gST->ConOut->Mode->Mode, &Columns, &Rows);
  return Columns;
}

EFI_STATUS
SfbSetChinese (IN BOOLEAN Chinese)
{
  SfbInitializeText ();
  if (Chinese && mGraphics == NULL) {
    return EFI_UNSUPPORTED;
  }
  mChinese = Chinese;
  return SfbStoreWrite (SFB_STORE_LANGUAGE, Chinese ? "SFBL1|zh" : "SFBL1|en");
}

CONST CHAR16 *
SfbText (IN CONST CHAR16 *Text)
{
  UINTN Index;

  if (!SfbIsChinese () || Text == NULL) {
    return Text;
  }
  for (Index = 0; Index < ARRAY_SIZE (mText); Index++) {
    if (StrCmp (Text, mText[Index].English) == 0) {
      return mText[Index].Chinese;
    }
  }
  return Text;
}

STATIC CONST SFB_GLYPH *
SfbFindGlyph (IN CHAR16 Character)
{
  UINTN Low = 0;
  UINTN High = ARRAY_SIZE (mGlyphs);

  while (Low < High) {
    UINTN Middle = Low + (High - Low) / 2;
    if (mGlyphs[Middle].Character < Character) {
      Low = Middle + 1;
    } else {
      High = Middle;
    }
  }
  if (Low < ARRAY_SIZE (mGlyphs) && mGlyphs[Low].Character == Character) {
    return &mGlyphs[Low];
  }
  return &mGlyphs[L'?' - L' '];
}

STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL
SfbColor (IN UINTN Color)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Pixel;
  UINT8 Level = (Color & 8) ? 0xff : 0xaa;

  Pixel.Red = (Color & 4) ? Level : 0;
  Pixel.Green = (Color & 2) ? Level : 0;
  Pixel.Blue = (Color & 1) ? Level : 0;
  if (Color == EFI_DARKGRAY) {
    Pixel.Red = Pixel.Green = Pixel.Blue = 0x55;
  }
  Pixel.Reserved = 0;
  return Pixel;
}

VOID
SfbClearScreen (VOID)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Background;

  SfbInitializeText ();
  gST->ConOut->ClearScreen (gST->ConOut);
  mColumn = 0;
  mRow = 0;
  if (mGraphics != NULL) {
    Background = SfbColor ((gST->ConOut->Mode->Attribute >> 4) & 7);
    mGraphics->Blt (mGraphics, &Background, EfiBltVideoFill, 0, 0, 0, 0,
                     mGraphics->Mode->Info->HorizontalResolution,
                     mGraphics->Mode->Info->VerticalResolution, 0);
  }
}

STATIC EFI_STATUS
SfbNewLine (VOID)
{
  UINTN Height = mGraphics->Mode->Info->VerticalResolution;
  UINTN Width = mGraphics->Mode->Info->HorizontalResolution;
  UINTN Line = 20 * mScale;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Background;
  EFI_STATUS Status;

  mColumn = 0;
  mRow += Line;
  if (mRow + Line <= Height) {
    return EFI_SUCCESS;
  }
  Status = mGraphics->Blt (mGraphics, NULL, EfiBltVideoToVideo,
                            0, Line, 0, 0, Width, Height - Line, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  mRow = Height - Line;
  Background = SfbColor ((gST->ConOut->Mode->Attribute >> 4) & 7);
  return mGraphics->Blt (mGraphics, &Background, EfiBltVideoFill,
                          0, 0, 0, mRow, Width, Line, 0);
}

STATIC EFI_STATUS
SfbOutputText (IN CONST CHAR16 *Text)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Foreground;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Background;
  UINTN Attribute = gST->ConOut->Mode->Attribute;
  EFI_STATUS Status;

  Foreground = SfbColor (Attribute & 15);
  Background = SfbColor ((Attribute >> 4) & 7);
  for (; *Text != L'\0'; Text++) {
    CONST SFB_GLYPH *Glyph;
    UINTN Width;
    UINTN Height = 16 * mScale;
    UINTN X;
    UINTN Y;

    if (*Text == L'\r') {
      mColumn = 0;
      continue;
    }
    if (*Text == L'\n') {
      Status = SfbNewLine ();
      if (EFI_ERROR (Status)) {
        return Status;
      }
      continue;
    }
    Width = ((*Text < 0x80) ? 8 : 16) * mScale;
    if (mColumn + Width > mGraphics->Mode->Info->HorizontalResolution) {
      Status = SfbNewLine ();
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
    Glyph = SfbFindGlyph (*Text);
    for (Y = 0; Y < Height; Y++) {
      for (X = 0; X < Width; X++) {
        mPixels[Y * Width + X] =
          (Glyph->Rows[Y / mScale] & (0x8000 >> (X / mScale))) ?
          Foreground : Background;
      }
    }
    Status = mGraphics->Blt (mGraphics, mPixels, EfiBltBufferToVideo,
                              0, 0, mColumn, mRow, Width, Height, 0);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    mColumn += Width;
  }
  return EFI_SUCCESS;
}

UINTN
EFIAPI
SfbPrint (IN CONST CHAR16 *Format, ...)
{
  CHAR16 Buffer[1024];
  VA_LIST Marker;
  UINTN Length;

  VA_START (Marker, Format);
  Length = UnicodeVSPrint (Buffer, sizeof (Buffer), SfbText (Format), Marker);
  VA_END (Marker);
  if (mGraphics == NULL) {
    gST->ConOut->OutputString (gST->ConOut, Buffer);
  } else if (EFI_ERROR (SfbOutputText (Buffer))) {
    mGraphics = NULL;
    mChinese = FALSE;
    gST->ConOut->ClearScreen (gST->ConOut);
    gST->ConOut->OutputString (gST->ConOut, L"Display drawing failed. English console restored.\r\n");
    return 0;
  }
  return Length;
}

UINTN
EFIAPI
SfbFormat (OUT CHAR16 *Buffer, IN UINTN BufferSize, IN CONST CHAR16 *Format, ...)
{
  VA_LIST Marker;
  UINTN Length;

  VA_START (Marker, Format);
  Length = UnicodeVSPrint (Buffer, BufferSize, SfbText (Format), Marker);
  VA_END (Marker);
  return Length;
}
