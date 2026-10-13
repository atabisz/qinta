#ifndef TINTA_WINDOW_CHROME_WIN32_H
#define TINTA_WINDOW_CHROME_WIN32_H

// WINDOWS ONLY: paints a tinta::WindowChrome onto a native window through DWM. Its one caller
// is src/qt_window.cpp's chrome sink, which hands it the widget's winId() -- under the windows
// platform plugin a top-level HWND. It was one body for both Windows stacks until the retired
// src/d2d_init.cpp, whose applyWindowChrome handed it app.hwnd, was deleted in 0b39ae6; neither
// caller held its own copy of the four attribute calls, so the two title bars could not drift.
//
// Not in tests/portable-sources.txt and never will be: there is no DWM off Windows, and the
// Linux/macOS gap is recorded at src/theme_qt.cpp. Named and guarded like the Windows halves
// zen_mode_win32 and tab_ghost_win32 were before 0b39ae6: the .cpp is one `#ifdef _WIN32` from line 3
// to its last line, so off Windows it is an empty translation unit, which the non-portable
// census reads as `vacuous` and README's backends row picks up by its `_win32` stem.

#ifdef _WIN32

#include "win32_types.h"
#include "window_sink.h"

void applyDwmWindowChrome(HWND hwnd, const tinta::WindowChrome& chrome);

#endif  // _WIN32

#endif  // TINTA_WINDOW_CHROME_WIN32_H
