#ifndef TINTA_ZEN_MODE_QT_H
#define TINTA_ZEN_MODE_QT_H

#include <QWidget>

#include "window_placement.h"

// THE QT ARM OF ZEN MODE: showFullScreen() over the monitor the Win32 arm would have picked.
//
// This file names Qt types because it IS the Qt arm; include/window_placement.h names neither
// Qt's nor Windows'. The state both arms carry -- `tinta::ZenRestore` -- comes from there, which is
// what lets these two functions and include/zen_mode_win32.h's two be written against one idea of
// "the window I have to give back" instead of two.
//
// IT TAKES `QWidget&`, NOT THE APP'S OWN WINDOW CLASS, and not `App&`. Zen mode needs a top-level
// widget and nothing else -- no document, no theme, no layout -- so a test can drive both functions
// against a bare QWidget, which is the difference between this arm being a reading and being a
// screenshot somebody looked at once. Same argument tab_ghost_win32.h makes for not taking `App&`.
//
// WHY `zenFullscreenRect` AND NOT `widget.screen()->geometry()`, WHICH IS THE OBVIOUS SPELLING.
// They differ in exactly the case this tree already fixed once. Qt's `QWidget::screen()` reports
// the screen the window is ASSOCIATED with and falls back to the PRIMARY screen for a window that
// is on no screen at all; Win32's `MONITOR_DEFAULTTONEAREST` returns the NEAREST one. A window
// restored from settings onto a monitor that has since been unplugged is precisely that window --
// issue #25, the reason window_placement.h exists -- so the obvious spelling would put zen mode on
// the primary monitor where Windows puts it on the closest, silently, on the one input where
// anybody would notice. The pure function is the metric that was swept against the real API over
// 17,328 positions, so calling it is how this arm agrees with the other one.
namespace tinta {

// Captures what the window is now and takes it fullscreen over the nearest monitor.
//
// The returned state is what zenModeLeave needs and the caller must keep. Unlike the Win32 arm
// there is no failure path: Qt's geometry accessors cannot fail, and `showFullScreen()` returns
// void -- so `valid` is always true here, and that asymmetry is real rather than sloppy.
ZenRestore zenModeEnter(QWidget& window);

// Restores the window to `restore`.
//
// Returns false for a `restore` with `valid == false`, matching the Win32 arm: leaving a mode that
// was never recorded as entered must not resize the window to a zeroed rectangle.
bool zenModeLeave(QWidget& window, const ZenRestore& restore);

}  // namespace tinta

#endif  // TINTA_ZEN_MODE_QT_H
