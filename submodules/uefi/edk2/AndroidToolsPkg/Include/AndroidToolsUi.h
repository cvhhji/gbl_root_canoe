/** @file
 *  Console menu UI for AndroidToolsPkg, modeled on the super-fastboot boot
 *  menu (SuperFbMenu). Three keys drive everything: volume up and volume down
 *  move the cursor, and power confirms.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __ANDROID_TOOLS_UI_H__
#define __ANDROID_TOOLS_UI_H__

#include <Uefi.h>

typedef enum {
  AtKeyTimeout = 0,
  AtKeyUp,
  AtKeyDown,
  AtKeySelect
} AT_KEY;

/* Rows of list content a screen shows before it starts scrolling. */
#define AT_VISIBLE_ROWS  12

/**
  Announce the menu and wait for the launching key to be released, then drain
  the input queue. Call once at application entry, before the first AtUiRunMenu,
  so a power press held through LoadImage/StartImage does not auto-confirm the
  first entry.
**/
VOID
AtUiEnterMenu (
  IN CONST CHAR16 *Title
  );

/**
  Wait for a key. TimeoutMs of 0 waits indefinitely. Returns AtKeyTimeout when
  the timer elapses.
**/
AT_KEY
AtUiWaitForKey (
  IN UINT32 TimeoutMs
  );

/** Clear the screen and print a title (and optional subtitle). **/
VOID
AtUiBeginScreen (
  IN CONST CHAR16 *Title,
  IN CONST CHAR16 *Subtitle OPTIONAL,
  IN UINTN ContentRows
  );

/** Print a footer line. **/
VOID
AtUiEndScreen (
  IN CONST CHAR16 *Footer
  );

/** Print one menu row, highlighted when Selected. **/
VOID
AtUiDrawRow (
  IN BOOLEAN       Selected,
  IN CONST CHAR16 *Marker,
  IN CONST CHAR16 *Text
  );

/** First row of the visible window, chosen to keep Cursor inside it. **/
UINTN
AtUiWindowStart (
  IN UINTN Cursor,
  IN UINTN Count,
  IN UINTN Rows
  );

/** Move a cursor by one step, wrapping at the ends. **/
VOID
AtUiMoveCursor (
  IN OUT UINTN *Cursor,
  IN UINTN     Count,
  IN AT_KEY    Key
  );

/**
  Clear the screen and show a single centered message (e.g. "Restarting...").
**/
VOID
AtUiShowMessage (
  IN CONST CHAR16 *Text
  );

/**
  Report a failure and hold the screen until the user acknowledges it.
**/
VOID
AtUiReportStatus (
  IN CONST CHAR16 *What,
  IN EFI_STATUS    Status
  );

/**
  Run a menu of Count text items under Title. Volume up/down move the cursor,
  power selects. Returns EFI_SUCCESS and writes the chosen index to *Selected,
  or EFI_TIMEOUT. Footer is printed below the list.
**/
EFI_STATUS
AtUiRunMenu (
  IN  CONST CHAR16  *Title,
  IN  CONST CHAR16  **Items,
  IN  UINTN          Count,
  OUT UINTN         *Selected,
  IN  CONST CHAR16  *Footer OPTIONAL
  );

CONST CHAR16 *
AtUiText (IN CONST CHAR16 *Text);

UINTN EFIAPI
AtUiPrint (IN CONST CHAR16 *Format, ...);

UINTN EFIAPI
AtUiFormat (OUT CHAR16 *Buffer, IN UINTN BufferSize, IN CONST CHAR16 *Format, ...);

#endif /* __ANDROID_TOOLS_UI_H__ */
