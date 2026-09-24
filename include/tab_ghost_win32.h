#ifndef TINTA_TAB_GHOST_WIN32_H
#define TINTA_TAB_GHOST_WIN32_H

#if defined(_WIN32) && !defined(TINTA_QT_UI)

#include <windows.h>

#include <string>

#include "tab_ghost.h"

// THE WIN32 ARM OF THE DRAG GHOST: a layered popup card, composited by UpdateLayeredWindow.
//
// This file is allowed to name `HWND` because it IS the Windows arm; include/tab_ghost.h is not,
// and that split is the point. Everything here is mechanism -- window class, DIB section, GDI text,
// the layered blend -- and every DECISION it acts on comes from tab_ghost.h.
//
// NOTHING HERE TAKES `App&`, and that is not a stylistic preference. The code this replaces read
// `app.contentScale`, `app.theme`, `tabStripBackground(app)` and `app.tabGhostWnd` directly, so the
// only way to exercise it was to stand up an App -- which needs a real window, a theme, a document
// and a message loop. A card built from a TabGhostCard and two integers can be created, measured and
// destroyed by a test in nine lines, which is what turns "the ghost still works" from a claim into
// a reading.
namespace tinta {

// The registered window class, exposed so a test can ask the OS what it got rather than trusting
// what it asked for. `GetClassNameW` on the returned handle must report this string.
extern const wchar_t* const kTabGhostWindowClass;

// Creates the card at a screen position and shows it without activating. Returns nullptr if the
// window could not be created; the caller owns the handle and must pass it to tabGhostDestroy.
//
// `title` is std::wstring because that is what App::DocTab::title already is. Handing the portable
// core a UTF-8 copy to hand back would add two conversions per drag and lose nothing useful.
HWND tabGhostCreate(const TabGhostCard& card, const std::wstring& title, int screenX, int screenY);

// Moves an existing card. Size never changes, so this is position-only and stays topmost.
void tabGhostMove(HWND ghost, int screenX, int screenY);

// Destroys the card and nulls the handle. Takes the handle BY REFERENCE so the "destroy it and
// forget it" pair cannot come apart: the shipped code had those two statements adjacent in one
// function and repeated at three call sites, which is three chances to keep a dangling HWND.
void tabGhostDestroy(HWND& ghost);

}  // namespace tinta

#endif  // _WIN32

#endif  // TINTA_TAB_GHOST_WIN32_H
