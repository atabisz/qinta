#ifndef TINTA_WINDOW_CHROME_WIN32_H
#define TINTA_WINDOW_CHROME_WIN32_H

// WINDOWS ONLY: paints a tinta::WindowChrome onto a native window through DWM. One body for
// both Windows stacks -- src/d2d_init.cpp's applyWindowChrome hands it app.hwnd, and
// src/qt_window.cpp's chrome sink hands it the widget's winId(), which under the windows
// platform plugin is the same kind of top-level HWND. Neither caller holds its own copy of
// the four attribute calls, so the Qt title bar cannot drift from the Direct2D one.
//
// Not in tests/portable-sources.txt and never will be: there is no DWM off Windows, and the
// Linux/macOS gap is recorded at src/theme_qt.cpp. Named and guarded like the tree's other
// Windows halves (zen_mode_win32, tab_ghost_win32): the .cpp is one `#ifdef _WIN32` from line 3
// to its last line, so off Windows it is an empty translation unit, which the non-portable
// census reads as `vacuous` and README's backends row picks up by its `_win32` stem.

#ifdef _WIN32

#include "win32_types.h"
#include "window_sink.h"

void applyDwmWindowChrome(HWND hwnd, const tinta::WindowChrome& chrome);

#endif  // _WIN32

#endif  // TINTA_WINDOW_CHROME_WIN32_H
