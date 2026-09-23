#include "tab_tearoff_win32.h"

#ifdef _WIN32

#include <cwchar>
#include <iterator>

// Both bodies below are src/tabs.cpp's, moved 2026-09-23 with `hwnd` renamed `self` and the
// duplicated send made one function. See the header for why this file exists.

namespace tinta {

namespace {

void handTabTo(HWND target, const std::string& path) {
    COPYDATASTRUCT data;
    data.dwData = 1;
    data.cbData = (DWORD)path.size() + 1;
    data.lpData = (void*)path.c_str();
    SendMessageW(target, WM_COPYDATA, 0, (LPARAM)&data);
    SetForegroundWindow(target);
}

}  // namespace

TabDropTarget tabDropHandOff(HWND self, int screenX, int screenY, const std::string& path) {
    POINT screen = {screenX, screenY};
    HWND under = WindowFromPoint(screen);
    HWND root = under ? GetAncestor(under, GA_ROOT) : nullptr;
    wchar_t className[16] = {};
    if (root && root != self &&
        GetClassNameW(root, className, (int)std::size(className)) &&
        wcscmp(className, L"Tinta") == 0) {
        handTabTo(root, path);
        return TabDropTarget::OtherWindow;
    }
    if (root == self) return TabDropTarget::ThisWindow;
    return TabDropTarget::Nowhere;
}

bool tabMergeIntoWindowUnderCursor(HWND self, const std::string& path) {
    POINT cursor;
    if (!GetCursorPos(&cursor)) return false;
    HWND target = nullptr;
    // Top-level Tinta windows in z-order; the first one under the cursor
    // (other than us) is the drop candidate
    for (HWND w = FindWindowExW(nullptr, nullptr, L"Tinta", nullptr); w;
         w = FindWindowExW(nullptr, w, L"Tinta", nullptr)) {
        if (w == self || !IsWindowVisible(w) || IsIconic(w)) continue;
        RECT r;
        if (!GetWindowRect(w, &r) || !PtInRect(&r, cursor)) continue;
        // Only the strip band counts as a drop zone; anywhere lower is
        // an ordinary overlapping window placement
        UINT targetDpi = GetDpiForWindow(w);
        if (cursor.y > r.top + MulDiv(46, (int)targetDpi, 96)) break;
        target = w;
        break;
    }
    if (!target) return false;
    handTabTo(target, path);
    return true;
}

}  // namespace tinta

#endif  // _WIN32
