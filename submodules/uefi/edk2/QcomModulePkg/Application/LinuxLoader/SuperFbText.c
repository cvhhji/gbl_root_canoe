#include "SuperFbMenu.h"
#include "SuperFbFont.h"
#include <BootMenuText.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>

STATIC CONST struct {
  CONST CHAR16 *English;
  CONST CHAR16 *Chinese;
} mText[] = {
  { L"Android", L"安卓" },
  { L"Android backup", L"安卓备份" },
  { L"Android Tools", L"安卓工具" },
  { L"Reboot Tools", L"重启工具" },
  { L"BL Tools", L"BL 工具" },
  { L"ARB Tools", L"防回滚工具" },
  { L"Custom entry", L"自定义启动项" },
  { L"Enter EFI Program Selector", L"选择 EFI 程序" },
  { L"Language / 语言", L"Language / 语言" },
  { L"Boot Menu", L"启动菜单" },
  { L"FASTBOOT MODE", L"FASTBOOT 模式" },
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
  { L"Volume Down: stop export", L"音量下键：停止导出" },
  { L"Exit to menu", L"返回菜单" },
  { L"Returning to Boot Menu...", L"正在返回启动菜单..." },
  { L"Reboot to Fastbootd", L"重启到 Fastbootd" },
  { L"Reboot to Bootloader", L"重启到 Bootloader" },
  { L"Reboot to Recovery", L"重启到 Recovery" },
  { L"Reboot to System", L"重启到系统" },
  { L"Rebooting to Fastbootd...", L"正在重启到 Fastbootd..." },
  { L"Rebooting to Bootloader...", L"正在重启到 Bootloader..." },
  { L"Rebooting to Recovery...", L"正在重启到 Recovery..." },
  { L"Rebooting to System...", L"正在重启到系统..." },
  { L"Vol+/- move, power select", L"音量键：移动   电源键：选择" },
  { L"Vol+/- scroll, power to return", L"音量键：翻页   电源键：返回" },
  { L"Write BCB", L"写入启动控制信息" },
  { L"Cancelled", L"已取消" },
  { L"State unchanged", L"状态未改变" },
  { L"%s - writes DeviceInfo. May cause data loss.", L"%s：写入 DeviceInfo，可能导致数据丢失。" },
  { L"\r\nPower = confirm   Vol+/- = cancel\r\n", L"\r\n电源键：确认   音量键：取消\r\n" },
  { L"Applying...", L"正在应用..." },
  { L"Write DeviceInfo", L"写入设备信息" },
  { L"Read DeviceInfo", L"读取设备信息" },
  { L"Done. Reboot for the change to take effect.", L"已完成，重启后生效。" },
  { L"Unlock Device", L"解锁设备" },
  { L"Lock Device", L"锁定设备" },
  { L"Unlock Critical", L"解锁关键分区" },
  { L"Lock Critical", L"锁定关键分区" },
  { L"DeviceInfo not initialized", L"设备信息尚未初始化" },
  { L"BL Tools  Unlock:%s  Crit:%s", L"BL 工具  设备解锁：%s  关键分区解锁：%s" },
  { L"on", L"是" },
  { L"off", L"否" },
  { L"Alloc", L"分配内存" },
  { L"Slot %2u: 0x%016lx", L"槽位 %2u：0x%016lx" },
  { L"All rollback slots are 0", L"所有防回滚槽位的值均为 0" },
  { L"ARB Rollback Index (non-zero)", L"防回滚索引（非零项）" },
  { L"Reset ARB Index", L"重置防回滚索引" },
  { L"WARNING: this writes to the TEE and may lose keys.\r\n", L"警告：此操作会写入 TEE，可能导致密钥丢失。\r\n" },
  { L"\r\n   Confirm %u/5\r\n", L"\r\n   第 %u/5 次确认\r\n" },
  { L"Reset cancelled", L"已取消重置" },
  { L"Resetting ARB index...", L"正在重置防回滚索引..." },
  { L"DeviceInfo magic", L"设备信息标识" },
  { L"ARB index reset complete", L"防回滚索引已重置" },
  { L"Get ARB Value", L"查看防回滚值" },
  { L"Reset ARB Value", L"重置防回滚值" },
  { L"virtual(LUN%u:%s:%u)", L"虚拟磁盘（LUN%u：%s：%u）" },
  { L"virtual(efisp.fat)", L"虚拟磁盘（efisp.fat）" },
  { L"Menu", L"菜单" },
  { L"Success", L"成功" },
  { L"Warning Unknown Glyph", L"警告：未知字形" },
  { L"Warning Delete Failure", L"警告：删除失败" },
  { L"Warning Write Failure", L"警告：写入失败" },
  { L"Warning Buffer Too Small", L"警告：缓冲区过小" },
  { L"Warning Stale Data", L"警告：数据已过期" },
  { L"Load Error", L"加载错误" },
  { L"Invalid Parameter", L"参数无效" },
  { L"Unsupported", L"不支持" },
  { L"Bad Buffer Size", L"缓冲区大小错误" },
  { L"Buffer Too Small", L"缓冲区过小" },
  { L"Not Ready", L"尚未就绪" },
  { L"Device Error", L"设备错误" },
  { L"Write Protected", L"写保护" },
  { L"Out of Resources", L"资源不足" },
  { L"Volume Corrupt", L"文件系统损坏" },
  { L"Volume Full", L"存储空间已满" },
  { L"No Media", L"未插入存储介质" },
  { L"Media changed", L"存储介质已变更" },
  { L"Not Found", L"未找到" },
  { L"Access Denied", L"访问被拒绝" },
  { L"No Response", L"无响应" },
  { L"No mapping", L"无可用映射" },
  { L"Time out", L"操作超时" },
  { L"Not started", L"尚未启动" },
  { L"Already started", L"已启动" },
  { L"Aborted", L"已中止" },
  { L"Protocol Error", L"协议错误" },
  { L"Security Violation", L"安全验证失败" },
  { L"ICMP Error", L"ICMP 错误" },
  { L"TFTP Error", L"TFTP 错误" },
  { L"Incompatible Version", L"版本不兼容" },
  { L"CRC Error", L"CRC 校验错误" },
  { L"End of Media", L"已到存储介质末尾" },
  { L"End of File", L"已到文件末尾" },
};

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mGraphics;
STATIC BOOLEAN mInitialized;
STATIC BOOLEAN mChinese;
STATIC UINTN mScale;
STATIC UINTN mColumn;
STATIC UINTN mRow;
STATIC UINTN mLeft;
STATIC UINTN mTop;
STATIC UINTN mWidth;
STATIC UINTN mHeight;
STATIC CHAR8 mLanguageId[40];
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL mPixels[64 * 64];

STATIC VOID EFIAPI SfbProtocolOutput (IN CONST CHAR16 *Text);
STATIC EFI_GUID mTextGuid = BOOT_MENU_TEXT_GUID;
STATIC EFI_HANDLE mTextHandle;
STATIC BOOT_MENU_TEXT mTextProtocol = {
  1, SfbText, SfbTextLines, SfbClearScreen, SfbProtocolOutput, SfbDrawRow
};

STATIC VOID
SfbReadLanguageId (VOID)
{
  EFI_HANDLE *Volumes = NULL;
  UINTN Count = 0;
  UINTN Index;
  UINTN Pass;

  if (EFI_ERROR (SfbLocateVolumes (&Volumes, &Count))) {
    return;
  }
  for (Pass = 0; Pass < 2 && mLanguageId[0] == '\0'; Pass++) {
    for (Index = 0; Index < Count; Index++) {
      EFI_FILE_PROTOCOL *Root = NULL;
      EFI_FILE_PROTOCOL *File = NULL;
      CHAR16 Path[40];
      CHAR8 Id[40];
      UINTN Bytes = sizeof (Id) - 1;
      UINTN Char;
      EFI_STATUS Status;

      if (SfbIsExt4Volume (Volumes[Index]) != (BOOLEAN)(Pass == 0) ||
          EFI_ERROR (SfbOpenVolumeRoot (Volumes[Index], &Root))) {
        continue;
      }
      UnicodeSPrint (Path, sizeof (Path), L"%s\\MENU.ID",
                      SfbVolumeRootPrefix (Volumes[Index]));
      Status = Root->Open (Root, &File, Path, EFI_FILE_MODE_READ, 0);
      if (!EFI_ERROR (Status)) {
        Status = File->Read (File, &Bytes, Id);
        File->Close (File);
        if (!EFI_ERROR (Status) && (Bytes == 36 || (Bytes == 37 && Id[36] == '\n'))) {
          for (Char = 0; Char < 36; Char++) {
            if (!((Id[Char] >= '0' && Id[Char] <= '9') ||
                  (Id[Char] >= 'a' && Id[Char] <= 'f') || Id[Char] == '-')) {
              break;
            }
          }
          if (Char == 36) {
            CopyMem (mLanguageId, Id, 36);
            mLanguageId[36] = '\0';
          }
        }
      }
      Root->Close (Root);
      if (mLanguageId[0] != '\0') {
        break;
      }
    }
  }
  FreePool (Volumes);
}

VOID
SfbInitializeText (VOID)
{
  CHAR8 Saved[64];
  CHAR8 Expected[64];
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Pixel;

  if (mInitialized) {
    return;
  }
  mInitialized = TRUE;
  gBS->InstallProtocolInterface (&mTextHandle, &mTextGuid,
                                  EFI_NATIVE_INTERFACE, &mTextProtocol);
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
  mScale = MIN (4, MIN (mGraphics->Mode->Info->HorizontalResolution / 480,
                         mGraphics->Mode->Info->VerticalResolution / 600));
  if (mScale == 0) {
    mScale = 1;
  }
  mWidth = (mGraphics->Mode->Info->HorizontalResolution * 9 / 10 / (8 * mScale)) * (8 * mScale);
  mHeight = 4 * 20 * mScale;
  mLeft = (mGraphics->Mode->Info->HorizontalResolution - mWidth) / 2;
  mTop = (mGraphics->Mode->Info->VerticalResolution - mHeight) / 2;
  SfbReadLanguageId ();
  if (mLanguageId[0] != '\0' &&
      !EFI_ERROR (SfbStoreRead (SFB_STORE_LANGUAGE, Saved, sizeof (Saved)))) {
    AsciiSPrint (Expected, sizeof (Expected), "SFBL2|zh|%a", mLanguageId);
    mChinese = (BOOLEAN)(AsciiStrCmp (Saved, Expected) == 0);
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
    return mWidth / (8 * mScale);
  }
  gST->ConOut->QueryMode (gST->ConOut, gST->ConOut->Mode->Mode, &Columns, &Rows);
  return MAX (Columns, 1);
}

UINTN EFIAPI
SfbTextLines (IN CONST CHAR16 *Text)
{
  UINTN Columns = SfbTextColumns ();
  UINTN Width = 0;
  UINTN Lines = 1;

  Text = SfbText (Text);
  if (Text == NULL) {
    return 0;
  }
  for (; *Text != L'\0'; Text++) {
    UINTN CharacterWidth;
    if (*Text == L'\r') {
      continue;
    }
    if (*Text == L'\n') {
      Lines++;
      Width = 0;
      continue;
    }
    CharacterWidth = *Text < 0x80 ? 1 : 2;
    if (Width + CharacterWidth > Columns) {
      Lines++;
      Width = 0;
    }
    Width += CharacterWidth;
  }
  return Lines;
}

EFI_STATUS
SfbSetChinese (IN BOOLEAN Chinese)
{
  CHAR8 Record[64];
  SfbInitializeText ();
  if (Chinese && mGraphics == NULL) {
    return EFI_UNSUPPORTED;
  }
  mChinese = Chinese;
  if (mLanguageId[0] == '\0') {
    return EFI_NOT_FOUND;
  }
  AsciiSPrint (Record, sizeof (Record), "SFBL2|%a|%a",
                Chinese ? "zh" : "en", mLanguageId);
  return SfbStoreWrite (SFB_STORE_LANGUAGE, Record);
}

CONST CHAR16 * EFIAPI
SfbText (IN CONST CHAR16 *Text)
{
  UINTN Index;

  if (Text == NULL) {
    return Text;
  }
  if (StrCmp (Text, L"Language") == 0 || StrCmp (Text, L"Language / 语言") == 0) {
    return SfbChineseAvailable () ? L"Language / 语言" : L"Language";
  }
  if (!SfbIsChinese ()) {
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

VOID EFIAPI
SfbClearScreen (IN UINTN Rows)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Background;

  SfbInitializeText ();
  gST->ConOut->ClearScreen (gST->ConOut);
  mColumn = 0;
  mRow = 0;
  if (mGraphics != NULL) {
    mHeight = MIN (Rows * 20 * mScale,
                    mGraphics->Mode->Info->VerticalResolution / (20 * mScale) * (20 * mScale));
    mHeight = MAX (mHeight, 20 * mScale);
    mLeft = (mGraphics->Mode->Info->HorizontalResolution - mWidth) / 2;
    mTop = (mGraphics->Mode->Info->VerticalResolution - mHeight) / 2;
    Background = SfbColor ((gST->ConOut->Mode->Attribute >> 4) & 7);
    mGraphics->Blt (mGraphics, &Background, EfiBltVideoFill, 0, 0, 0, 0,
                     mGraphics->Mode->Info->HorizontalResolution,
                     mGraphics->Mode->Info->VerticalResolution, 0);
  }
}

STATIC EFI_STATUS
SfbNewLine (VOID)
{
  UINTN Height = mHeight;
  UINTN Width = mWidth;
  UINTN Line = 20 * mScale;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Background;
  EFI_STATUS Status;

  mColumn = 0;
  mRow += Line;
  if (mRow + Line <= Height) {
    return EFI_SUCCESS;
  }
  Status = mGraphics->Blt (mGraphics, NULL, EfiBltVideoToVideo,
                            mLeft, mTop + Line, mLeft, mTop, Width, Height - Line, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  mRow = Height - Line;
  Background = SfbColor ((gST->ConOut->Mode->Attribute >> 4) & 7);
  return mGraphics->Blt (mGraphics, &Background, EfiBltVideoFill,
                          0, 0, mLeft, mTop + mRow, Width, Line, 0);
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
    if (mColumn + Width > mWidth) {
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
                              0, 0, mLeft + mColumn, mTop + mRow, Width, Height, 0);
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
  SfbProtocolOutput (Buffer);
  return Length;
}

STATIC VOID EFIAPI
SfbProtocolOutput (IN CONST CHAR16 *Text)
{
  if (mGraphics == NULL) {
    gST->ConOut->OutputString (gST->ConOut, (CHAR16 *)Text);
  } else if (EFI_ERROR (SfbOutputText (Text))) {
    mGraphics = NULL;
    mChinese = FALSE;
    gST->ConOut->ClearScreen (gST->ConOut);
    gST->ConOut->OutputString (gST->ConOut, L"Display drawing failed. English console restored.\r\n");
  }
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
