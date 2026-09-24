#ifndef TINTA_ZEN_MODE_WIN32_H
#define TINTA_ZEN_MODE_WIN32_H

#if defined(_WIN32) && !defined(TINTA_QT_UI)

#include <windows.h>

#include "window_placement.h"

// THE WIN32 ARM OF ZEN MODE: a borderless WS_POPUP sized to one whole monitor.
//
// This file is allowed to name `HWND` because it IS the Windows arm; include/window_placement.h,
// which owns the state both arms carry, names neither `HWND` nor `QWidget`. Same split as
// include/tab_ghost_win32.h against include/tab_ghost.h, for the same reason: laundering the
// handle through `void*` would buy a clean grep at the cost of the type safety that makes the
// rule worth having.
//
// WHAT THIS IS, AND WHAT IT IS NOT. It is a MOVE. Every style bit, every `SWP_*` flag and the
// order of the two restore calls are the ones that shipped inside `toggleZenMode` in
// src/input.cpp, unchanged -- so a `git diff` of this file against those lines shows relocation
// and nothing else. That matters more than it sounds: a changed flag here would be
// indistinguishable from a porting mistake, and the only witness zen mode has is somebody
// pressing F11.
//
// THE ONE THING DELIBERATELY NOT SHARED WITH THE QT ARM, stated because its absence looks like an
// oversight. `window_placement.h` exposes `zenFullscreenRect`, which computes the covered
// rectangle from a monitor list -- and this arm does not call it. It asks the OS instead, with
// `MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)`, exactly as the shipped code did. The pure
// function is a DEFINITION of "nearest" that was swept against that API and made to agree with it,
// not a reproduction of it: `window_placement.h` says so at length, and the residue is 18 genuine
// ties whose Win32 order is undocumented. Substituting our definition for the OS's answer on the
// arm that already had the OS's answer would be a behaviour change wearing a refactor's clothes.
// The Qt arm has no equivalent single call, which is what the pure function is for.
namespace tinta {

// Captures what the window is now and takes it borderless over the nearest monitor.
//
// The returned state is what zenModeLeave needs and the caller must keep -- it is `App::zenRestore`
// at the only call site. `valid` is false if the placement could not be read, in which case the
// window is left alone rather than expanded over a monitor it could not remember leaving: a
// fullscreen window with no way back is worse than an F11 that did nothing.
ZenRestore zenModeEnter(HWND hwnd);

// Puts the frame back and restores the window to `restore`.
//
// A `restore` with `valid == false` is a no-op and returns false, which is the honest answer to
// "leave a mode you were never recorded as entering" -- the alternative is SetWindowPlacement on a
// zeroed struct, which is a 0x0 window at the origin that looks exactly like a crash.
bool zenModeLeave(HWND hwnd, const ZenRestore& restore);

}  // namespace tinta

#endif  // _WIN32

#endif  // TINTA_ZEN_MODE_WIN32_H
