/*
 * Console UI for the super-fastboot boot menu.
 *
 * Three keys drive everything: volume up and volume down move the cursor, and
 * power confirms.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMenu.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/ShutdownServices.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <AndroidToolsUi.h>
#include <Protocol/SimpleTextIn.h>

/* Keeps the translation unit legal when the feature is compiled out. */
CONST CHAR8 *gSfbMenuModuleTag = "SuperFbMenu";

#define SFB_ATTR_NORMAL    EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK)
#define SFB_ATTR_SELECTED  EFI_TEXT_ATTR (EFI_BLACK, EFI_LIGHTGRAY)
#define SFB_ATTR_TITLE     EFI_TEXT_ATTR (EFI_WHITE, EFI_BLACK)

SFB_KEY
SfbWaitForKey (IN UINT32 TimeoutMs)
{
  EFI_STATUS     Status;
  EFI_EVENT      TimerEvent = NULL;
  EFI_EVENT      WaitList[2];
  UINTN          WaitCount;
  UINTN          EventIndex;
  EFI_INPUT_KEY  Key;
  SFB_KEY        Result = SfbKeyTimeout;

  if (TimeoutMs != 0) {
    Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &TimerEvent);
    if (EFI_ERROR (Status)) {
      TimerEvent = NULL;
    } else {
      /* Boot services timers count in 100ns units. */
      Status = gBS->SetTimer (TimerEvent, TimerRelative,
                              (UINT64)TimeoutMs * 10000);
      if (EFI_ERROR (Status)) {
        gBS->CloseEvent (TimerEvent);
        TimerEvent = NULL;
      }
    }
  }

  WaitList[0] = gST->ConIn->WaitForKey;
  WaitCount = 1;
  if (TimerEvent != NULL) {
    WaitList[1] = TimerEvent;
    WaitCount = 2;
  }

  while (TRUE) {
    Status = gBS->WaitForEvent (WaitCount, WaitList, &EventIndex);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: WaitForEvent failed: %r\n", Status));
      break;
    }

    if (EventIndex == 1) {
      break;
    }

    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (EFI_ERROR (Status)) {
      continue;
    }

    /*
     * On the handset the Qualcomm keypad driver reports the volume keys as
     * SCAN_UP and SCAN_DOWN, and power arrives as a carriage return.
     *
     * Anything left over counts as confirm: on a three-key handset there is
     * nothing else it can be, so the menu stays usable even if a platform
     * reports power differently from what is expected here.
     */
    if (Key.ScanCode == SCAN_UP) {
      Result = SfbKeyUp;
    } else if (Key.ScanCode == SCAN_DOWN) {
      Result = SfbKeyDown;
    } else {
      DEBUG ((EFI_D_VERBOSE, "SFB: confirm key scan=0x%x char=0x%x\n",
              Key.ScanCode, Key.UnicodeChar));
      Result = SfbKeySelect;
    }
    break;
  }

  if (TimerEvent != NULL) {
    gBS->CloseEvent (TimerEvent);
  }

  return Result;
}

/* ---- drawing ------------------------------------------------------------ */

STATIC CONST CHAR16*
SfbGetFileName (IN CONST CHAR16 *Path)
{
  CONST CHAR16 *FileName = Path;
  while (*Path != L'\0') {
    if (*Path == L'\\') FileName = Path + 1;
    Path++;
  }
  return FileName;
}

STATIC BOOLEAN
SfbStrCaseEqual (IN CONST CHAR16 *Str1, IN CONST CHAR16 *Str2)
{
  while (*Str1 && *Str2) {
    CHAR16 c1 = (*Str1 >= L'a' && *Str1 <= L'z') ? *Str1 - 0x20 : *Str1;
    CHAR16 c2 = (*Str2 >= L'a' && *Str2 <= L'z') ? *Str2 - 0x20 : *Str2;
    if (c1 != c2) return FALSE;
    Str1++;
    Str2++;
  }
  return *Str1 == L'\0' && *Str2 == L'\0';
}

VOID
SfbBeginScreen (IN CONST CHAR16 *Title, IN CONST CHAR16 *Subtitle, IN UINTN ContentRows)
{
  UINTN Rows = ContentRows + 3 + SfbTextLines (Title) +
                 SfbTextLines (Subtitle) +
                 SfbTextLines (L"Vol Up/Down: move   Power: select");
  AtUiDisableWatchdogs ();
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  SfbClearScreen (Rows);
  SfbPrint (L"%s\r\n", SfbText (Title));
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  if (Subtitle != NULL) {
    SfbPrint (L"%s\r\n", SfbText (Subtitle));
  }
  SfbPrint (L"\r\n");
}

VOID
SfbEndScreen (IN CONST CHAR16 *Footer)
{
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  SfbPrint (L"\r\n%s\r\n", SfbText (Footer));
}

VOID EFIAPI
SfbDrawRow (IN BOOLEAN Selected, IN CONST CHAR16 *Marker, IN CONST CHAR16 *Text)
{
  CHAR16 Visible[SFB_PATH_CHARS + 4];
  UINTN Columns = SfbTextColumns ();
  UINTN Prefix = StrLen (Marker) + 3;
  UINTN Width = 0;
  UINTN Index = 0;

  if (Columns > Prefix + 4) {
    Columns -= Prefix + 1;
    while (Text[Index] != L'\0' && Index < ARRAY_SIZE (Visible) - 4) {
      UINTN CharacterWidth = (Text[Index] < 0x80) ? 1 : 2;
      if (Width + CharacterWidth > Columns) {
        break;
      }
      Width += CharacterWidth;
      Visible[Index] = Text[Index];
      Index++;
    }
    if (Text[Index] != L'\0') {
      while (Index != 0 && Width + 3 > Columns) {
        Index--;
        Width -= (Visible[Index] < 0x80) ? 1 : 2;
      }
      Visible[Index] = L'\0';
      StrCatS (Visible, ARRAY_SIZE (Visible), L"...");
    } else {
      Visible[Index] = L'\0';
    }
    Text = Visible;
  }
  gST->ConOut->SetAttribute (gST->ConOut,
                             Selected ? SFB_ATTR_SELECTED : SFB_ATTR_NORMAL);
  SfbPrint (L"%s %s %s", Selected ? L">" : L" ", Marker, Text);
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  SfbPrint (L"\r\n");
}

/*
 * First row of the visible window, keeping the cursor inside it. Lists longer
 * than the window scroll rather than overflow the console.
 */
UINTN
SfbWindowStart (IN UINTN Cursor, IN UINTN Count, IN UINTN Rows)
{
  if (Count <= Rows) {
    return 0;
  }
  if (Cursor < Rows / 2) {
    return 0;
  }
  if (Cursor > Count - 1 - (Rows - Rows / 2 - 1)) {
    return Count - Rows;
  }

  return Cursor - Rows / 2;
}

VOID
SfbMoveCursor (IN OUT UINTN *Cursor, IN UINTN Count, IN SFB_KEY Key)
{
  if (Count == 0) {
    *Cursor = 0;
    return;
  }

  if (Key == SfbKeyUp) {
    *Cursor = (*Cursor == 0) ? Count - 1 : *Cursor - 1;
  } else if (Key == SfbKeyDown) {
    *Cursor = (*Cursor + 1 >= Count) ? 0 : *Cursor + 1;
  }
}

/* Report a failure and hold the screen until the user acknowledges it. */
VOID
SfbReportStatus (IN CONST CHAR16 *What, IN EFI_STATUS Status)
{
  CHAR16 StatusText[80];
  UnicodeSPrint (StatusText, sizeof (StatusText), L"%r", Status);
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  SfbPrint (L"\r\n%s: %s\r\n", SfbText (What), SfbText (StatusText));
  SfbPrint (L"Press power to continue.\r\n");
  SfbWaitForKey (0);
}

/*
 * Hand the screen over to fastboot. The menu is the last thing that draws
 * before control leaves for the fastboot loop, which prints nothing of its own
 * until a host connects, so without this the user would be staring at a boot
 * menu that no longer responds to anything.
 */
VOID
SfbShowFastbootMode (VOID)
{
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  SfbClearScreen (4);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  SfbPrint (L"FASTBOOT MODE\r\n");

  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
}

/*
 * Clear the menu away and announce the launch. The loaded image prints nothing
 * of its own until it takes over, so without this the boot menu would linger on
 * screen through the load.
 */
VOID
SfbShowBootingScreen (IN CONST CHAR16 *Name,
                      IN CONST CHAR16 *FilePath,
                      IN BOOLEAN       ClearScreen)
{
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  /*
   * An unattended default boot must not blank whatever is already on screen
   * (typically the boot splash): only clear when the launch came from the menu,
   * where the menu itself is what needs clearing away.
   */
  if (ClearScreen) {
    SfbClearScreen (4);
  }
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  if (FilePath != NULL) {
    CONST CHAR16 *FileName = SfbGetFileName (FilePath);
    if (!SfbStrCaseEqual (FileName, L"boot.efi")) {
      SfbPrint (L"Booting %s\r\n", (Name != NULL && Name[0] != L'\0') ? SfbText (Name) : L"...");
    }
  } else {
    SfbPrint (L"Booting %s\r\n", (Name != NULL && Name[0] != L'\0') ? SfbText (Name) : L"...");
  }

  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
}

/*
 * Announce a power action (Power Off / Restart) and leave the message on
 * screen while the reset takes effect. Neither action returns, so the screen is
 * the last thing the user sees.
 */
VOID
SfbShowActionScreen (IN CONST CHAR16 *Text)
{
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  SfbClearScreen (4);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  SfbPrint (L"%s\r\n", SfbText (Text));

  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
}

/*
 * Seconds to hold on the "Entering Boot Menu" screen before the menu starts
 * taking input. Long enough that a volume key held from power-on has been
 * released, so it does not immediately move the menu cursor.
 */
#define SFB_ENTER_MENU_DELAY_S  3

VOID
SfbShowEnteringMenu (VOID)
{
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  SfbClearScreen (4);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  SfbPrint (L"Entering Boot Menu\r\n");

  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);

  /* Wait for the key to be released... */
  gBS->Stall (SFB_ENTER_MENU_DELAY_S * 1000 * 1000);

  /* ...then drop anything typed or held during the wait so it does not leak
   * into the menu as a spurious keypress. */
  gST->ConIn->Reset (gST->ConIn, FALSE);
}

/* ---- shared screens ------------------------------------------------------- */

/*
 * Seconds to hold on an "Entering <submenu>" screen before the submenu starts
 * taking input. Short of the root menu's three-second hold on purpose: the
 * parent's confirm keystroke is already consumed, this only covers a key still
 * held or its trailing repeat events.
 */
#define SFB_ENTERING_SCREEN_DELAY_S  1

VOID
SfbShowEnteringScreen (IN CONST CHAR16 *What)
{
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  SfbClearScreen (4);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  SfbPrint (L"Entering %s\r\n", (What != NULL) ? SfbText (What) : L"...");

  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);

  /* Wait for the key to be released, then drop anything typed or held during
   * the wait so it does not leak into the submenu as a spurious keypress. */
  gBS->Stall (SFB_ENTERING_SCREEN_DELAY_S * 1000 * 1000);
  gST->ConIn->Reset (gST->ConIn, FALSE);
}

/*
 * Debounce a menu exit: the key that confirmed Back would otherwise leak into
 * the parent menu (or its trailing repeats would) and act there immediately.
 * Hold for the entering-screen delay, then drop everything queued.
 */
VOID
SfbDebounceMenuExit (VOID)
{
  gBS->Stall (SFB_ENTERING_SCREEN_DELAY_S * 1000 * 1000);
  gST->ConIn->Reset (gST->ConIn, FALSE);
}

/* ---- boot menu ---------------------------------------------------------- */

STATIC
VOID
SfbDrawMenu (IN CONST SFB_MENU_STATE *Menu,
             IN UINTN                Cursor,
             IN CONST CHAR16         *Title)
{
  UINTN  Start;
  UINTN  Index;
  UINTN  Last;

  SfbBeginScreen (Title, NULL, MIN (Menu->Count, SFB_VISIBLE_ROWS) +
                   (Menu->Count > SFB_VISIBLE_ROWS ? 1 : 0));

  if (Menu->Count == 0) {
    SfbPrint (L"  No boot entries found.\r\n");
  }

  Start = SfbWindowStart (Cursor, Menu->Count, SFB_VISIBLE_ROWS);
  Last = Start + SFB_VISIBLE_ROWS;
  if (Last > Menu->Count) {
    Last = Menu->Count;
  }

  for (Index = Start; Index < Last; Index++) {
    CONST SFB_BOOT_ENTRY  *Entry = &Menu->Entry[Index];
    CONST CHAR16          *Marker = (Index == Menu->DefaultIndex) ? L"*" : L" ";

    /* Submenu rows get a trailing '>' so it is obvious they open another list
     * rather than launch an image. */
    if (Entry->Kind == SfbEntrySubmenu) {
      CHAR16  Text[SFB_DESC_CHARS + 4];

      SfbFormat (Text, sizeof (Text), L"%s >", SfbText (Entry->Desc));
      SfbDrawRow ((BOOLEAN)(Index == Cursor), Marker, Text);
    } else {
      SfbDrawRow ((BOOLEAN)(Index == Cursor), Marker,
                   SfbText (Entry->Desc));
    }
  }

  if (Last < Menu->Count) {
    SfbPrint (L"    ... %u more\r\n", (UINT32)(Menu->Count - Last));
  }

  SfbEndScreen (L"Vol Up/Down: move   Power: select");
}

/*
 * Run a submenu defined by the ENTRIES file at EntriesPath on Volume. The file
 * is parsed exactly like the root BOOTENTRIES, and may itself contain further
 * '%' submenu rows; Depth bounds the nesting so a chain of files that points at
 * one another cannot recurse without limit. The submenu state is heap-allocated
 * (a single SFB_MENU_STATE is ~17 KB) so deep nesting stays off the call stack.
 *
 * Returns when the user picks the trailing "Back" row, or when the file could
 * not be built at all; the caller then redraws its own menu.
 */
STATIC
VOID
SfbRunSubMenu (IN EFI_HANDLE   Volume,
               IN CONST CHAR16 *EntriesPath,
               IN CONST CHAR16 *Title,
               IN UINTN        Depth)
{
  SFB_MENU_STATE  *Menu = NULL;
  UINTN           Cursor = 0;
  BOOLEAN         Rebuild = TRUE;
  SFB_KEY         Key;
  EFI_STATUS      Status;

  Menu = AllocateZeroPool (sizeof (*Menu));
  if (Menu == NULL) {
    return;
  }
  Menu->DefaultIndex = SFB_NO_INDEX;

  while (TRUE) {
    UINTN  Chosen;

    if (Rebuild) {
      SfbFreeMenu (Menu);
      Status = SfbBuildSubMenu (Menu, Volume, EntriesPath);
      if (EFI_ERROR (Status)) {
        SfbReportStatus (Title, Status);
        break;
      }
      Cursor = 0;
      Rebuild = FALSE;
    }

    SfbDrawMenu (Menu, Cursor, Title);

    /* Same input model as the root menu: volume keys move, power confirms. */
    Key = SfbWaitForKey (0);
    if (Key == SfbKeyTimeout) {
      continue;
    }

    if (Key == SfbKeyUp || Key == SfbKeyDown) {
      SfbMoveCursor (&Cursor, Menu->Count, Key);
      continue;
    }

    if (Menu->Count == 0) {
      continue;
    }

    Chosen = Cursor;
    switch (Menu->Entry[Chosen].Kind) {
    case SfbEntryBack:
      /* Returning to the parent: debounce the confirming key first. */
      SfbDebounceMenuExit ();
      goto done;

    case SfbEntrySubmenu:
      if (Depth >= SFB_MAX_SUBMENU_DEPTH) {
        SfbReportStatus (L"Submenu too deep", EFI_BUFFER_TOO_SMALL);
      } else {
        SfbRunSubMenu (Menu->Entry[Chosen].Volume,
                       Menu->Entry[Chosen].Path,
                       Menu->Entry[Chosen].Desc,
                       Depth + 1);
      }
      /* Media may have changed while the child menu was open. */
      Rebuild = TRUE;
      break;

    case SfbEntryEfiFile:
    default:
      Status = SfbLaunchEntry (&Menu->Entry[Chosen], TRUE, TRUE);//Entries in submenu never defaults
      if (EFI_ERROR (Status)) {
        SfbReportStatus (L"Boot failed", Status);
      }
      Rebuild = TRUE;
      break;
    }
  }

done:
  SfbFreeMenu (Menu);
  FreePool (Menu);
}

STATIC VOID
SfbRunLanguageMenu (VOID)
{
  UINTN Cursor = SfbIsChinese () ? 1 : 0;
  UINTN Index;
  SFB_KEY Key;
  EFI_STATUS Status;
  CONST CHAR16 *Rows[3];

  SfbShowEnteringScreen (L"Language");
  while (TRUE) {
    Rows[0] = L"English";
    Rows[1] = SfbChineseAvailable () ? L"简体中文" : L"Simplified Chinese";
    Rows[2] = SfbText (L"Back");
    SfbBeginScreen (L"Language", L"Choose the menu language.", ARRAY_SIZE (Rows));
    for (Index = 0; Index < ARRAY_SIZE (Rows); Index++) {
      SfbDrawRow ((BOOLEAN)(Index == Cursor),
                   Index == (SfbIsChinese () ? 1U : 0U) ? L"*" : L" ",
                   Rows[Index]);
    }
    SfbEndScreen (L"Vol Up/Down: move   Power: select");
    Key = SfbWaitForKey (0);
    if (Key == SfbKeyTimeout) {
      continue;
    }
    if (Key == SfbKeyUp || Key == SfbKeyDown) {
      SfbMoveCursor (&Cursor, ARRAY_SIZE (Rows), Key);
      continue;
    }
    if (Cursor == 2) {
      break;
    }
    if (Cursor == 1 && !SfbChineseAvailable ()) {
      SfbReportStatus (L"Chinese text is unavailable on this display", EFI_UNSUPPORTED);
      continue;
    }
    Status = SfbSetChinese ((BOOLEAN)(Cursor == 1));
    if (EFI_ERROR (Status)) {
      SfbReportStatus (L"Language changed for this boot; could not save", Status);
    }
    break;
  }
  SfbDebounceMenuExit ();
}

BOOLEAN
SfbRunBootMenu (VOID)
{
  SFB_MENU_STATE  Menu;
  UINTN           Cursor = 0;
  BOOLEAN         Rebuild = TRUE;
  SFB_KEY         Key;
  EFI_STATUS      Status;

  ZeroMem (&Menu, sizeof (Menu));
  Menu.DefaultIndex = SFB_NO_INDEX;

  while (TRUE) {
    UINTN  Chosen;

    if (Rebuild) {
      SfbFreeMenu (&Menu);
      SfbBuildMenu (&Menu);
      Cursor = (Menu.DefaultIndex == SFB_NO_INDEX) ? 0 : Menu.DefaultIndex;
      Rebuild = FALSE;
    }

    SfbDrawMenu (&Menu, Cursor, L"Boot Menu");

    /* The menu is purely interactive: it waits for a key indefinitely and
     * never launches anything unattended. */
    Key = SfbWaitForKey (0);
    if (Key == SfbKeyTimeout) {
      continue;
    }

    if (Key == SfbKeyUp || Key == SfbKeyDown) {
      SfbMoveCursor (&Cursor, Menu.Count, Key);
      continue;
    }

    Chosen = Cursor;

    if (Menu.Count == 0) {
      continue;
    }

    switch (Menu.Entry[Chosen].Kind) {
    case SfbEntryFastboot:
      SfbFreeMenu (&Menu);
      return TRUE;

    case SfbEntrySelector:
      SfbRunFileBrowser ();
      Rebuild = TRUE;
      break;

    case SfbEntryLanguage:
      SfbRunLanguageMenu ();
      Rebuild = TRUE;
      break;

    case SfbEntrySubmenu:
      SfbRunSubMenu (Menu.Entry[Chosen].Volume,
                     Menu.Entry[Chosen].Path,
                     Menu.Entry[Chosen].Desc,
                     1);
      /* Media may have changed while the submenu was open. */
      Rebuild = TRUE;
      break;

    case SfbEntryBack:
      /* Only submenus carry a Back row; the root menu never adds one. */
      Rebuild = TRUE;
      break;

    case SfbEntryPowerOff:
      SfbShowActionScreen (L"Powering off...");
      ShutdownDevice ();
      break;

    case SfbEntryRestart:
      SfbShowActionScreen (L"Restarting...");
      RebootDevice (NORMAL_MODE);
      break;

    case SfbEntryEfiFile:
    default:
      Status = SfbLaunchEntry (&Menu.Entry[Chosen], FALSE, TRUE);
      if (EFI_ERROR (Status)) {
        SfbReportStatus (L"Boot failed", Status);
      }
      /* Media or variables may have changed while the image ran. */
      Rebuild = TRUE;
      break;
    }
  }
}
