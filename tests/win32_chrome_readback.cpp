#include "win32_chrome_readback.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

namespace tinta {

// The raw attribute numbers, as src/window_chrome_win32.cpp falls back to them for older SDKs:
// 20 is DWMWA_USE_IMMERSIVE_DARK_MODE, 35 DWMWA_CAPTION_COLOR.
DwmChromeReading readDwmChrome(std::uintptr_t window) {
    DwmChromeReading reading;
    HWND hwnd = reinterpret_cast<HWND>(window);
    BOOL dark = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
        reading.darkRead = true;
        reading.dark = dark != FALSE;
    }
    COLORREF caption = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, 35, &caption, sizeof(caption)))) {
        reading.captionRead = true;
        reading.caption = caption;
    }
    return reading;
}

}  // namespace tinta

#else  // !_WIN32

namespace tinta {

DwmChromeReading readDwmChrome(std::uintptr_t) { return DwmChromeReading{}; }

}  // namespace tinta

#endif  // _WIN32
