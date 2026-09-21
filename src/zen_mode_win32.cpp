#include "zen_mode_win32.h"

#ifdef _WIN32

namespace tinta {
namespace {

// The SDK struct, rebuilt from the two fields ZenRestore carries.
//
// `length` is mandatory and SetWindowPlacement fails without it. `flags` stays 0, which is what
// drops `WPF_SETMINPOSITION` -- and dropping it is why `ptMinPosition` can be left zeroed rather
// than remembered: without that flag the field is ignored, so carrying it would have been carrying
// a value nothing reads. `ptMaxPosition` is recomputed by Windows per monitor.
WINDOWPLACEMENT placementFrom(const ZenRestore& restore) {
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(WINDOWPLACEMENT);
    wp.showCmd = restore.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    wp.rcNormalPosition.left = restore.normal.x;
    wp.rcNormalPosition.top = restore.normal.y;
    wp.rcNormalPosition.right = restore.normal.x + restore.normal.width;
    wp.rcNormalPosition.bottom = restore.normal.y + restore.normal.height;
    return wp;
}

}  // namespace

ZenRestore zenModeEnter(HWND hwnd) {
    ZenRestore restore;

    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(WINDOWPLACEMENT);
    if (!GetWindowPlacement(hwnd, &wp)) return restore;  // valid stays false

    restore.normal.x = wp.rcNormalPosition.left;
    restore.normal.y = wp.rcNormalPosition.top;
    restore.normal.width = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    restore.normal.height = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    restore.maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    restore.valid = true;

    // rcMonitor and NOT rcWork: zen mode covers the taskbar on purpose. See zenFullscreenRect in
    // window_placement.h, which says the same thing for the arm that cannot make this call.
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        // The window is remembered but not moved. Reporting valid state for a mode we failed to
        // enter would let F11 again shrink a normal window to a remembered rectangle.
        restore.valid = false;
        return restore;
    }

    SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    return restore;
}

bool zenModeLeave(HWND hwnd, const ZenRestore& restore) {
    if (!restore.valid) return false;

    // Style first, then placement, then the frame recalculation with no move and no resize --
    // the shipped order. SetWindowPlacement before the style change would size the window to fit a
    // frame it does not have yet.
    SetWindowLongW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    WINDOWPLACEMENT wp = placementFrom(restore);
    SetWindowPlacement(hwnd, &wp);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    return true;
}

}  // namespace tinta

#endif  // _WIN32
