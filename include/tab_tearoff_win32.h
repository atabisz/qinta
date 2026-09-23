// Tab tear-off, the Windows arm: finding the other Tinta window a dragged tab was released over,
// and handing the tab's document to it.
//
// Lifted out of src/tabs.cpp 2026-09-23 so that file compiles off Windows. Both drops end in the
// same hand-off -- a WM_COPYDATA carrying the UTF-8 path, then raising the receiver -- and tabs.cpp
// spelled it twice, byte-identically; here it is written once.
//
// GUARDED, LIKE ITS SIBLINGS, AND FOR THE REASON include/utils.h gives for the relaunches: declared
// unconditionally, these would let a portable caller compile and then fail at link time. There is
// no Qt arm. Finding "the other window" is an HWND walk (WindowFromPoint, FindWindowExW, the window
// class name), and the hand-off is a Win32 message, so a Qt arm is a redesign (option C2), not a
// translation. Off Windows tabs.cpp never detaches a tab, so nothing there reaches for these.
#ifndef TINTA_TAB_TEAROFF_WIN32_H
#define TINTA_TAB_TEAROFF_WIN32_H

#ifdef _WIN32

#include <windows.h>

#include <string>

namespace tinta {

// Where a detached tab was released, as tabDragEnd needs to know it.
enum class TabDropTarget {
    OtherWindow,  // another Tinta window: the tab has been handed to it
    ThisWindow,   // back over the window it came from: snap home
    Nowhere,      // the desktop or a foreign window: the caller may tear it off
};

// A detached tab released at (screenX, screenY). If the top-level window under that point is
// another Tinta window, hands `path` to it and raises it.
TabDropTarget tabDropHandOff(HWND self, int screenX, int screenY, const std::string& path);

// A tabless window dropped with the cursor on another Tinta window's tab strip: walks the Tinta
// windows in z-order, and if the first one under the cursor (other than `self`) has the cursor in
// its strip band, hands `path` to it. Returns true if it did.
bool tabMergeIntoWindowUnderCursor(HWND self, const std::string& path);

}  // namespace tinta

#endif  // _WIN32

#endif  // TINTA_TAB_TEAROFF_WIN32_H
