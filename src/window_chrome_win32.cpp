#include "window_chrome_win32.h"

#ifdef _WIN32

#include <dwmapi.h>

// Older SDK headers may lack these
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

// Moved out of src/d2d_init.cpp's applyWindowChrome unchanged, bar the colours arriving
// already chosen (include/theme_apply.h's windowChromeFor).
void applyDwmWindowChrome(HWND hwnd, const tinta::WindowChrome& chrome) {
    if (!hwnd) return;

    auto toColorref = [](const tinta::Color& c) {
        return RGB((BYTE)(c.r * 255.0f + 0.5f),
                   (BYTE)(c.g * 255.0f + 0.5f),
                   (BYTE)(c.b * 255.0f + 0.5f));
    };

    // Windows 10 fallback: at least pick the right caption variant
    BOOL dark = chrome.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &dark, sizeof(dark));

    // Windows 11: exact theme colors (silently rejected on older builds).
    // The frame tint matches the drawn tab strip, so the window border and
    // any DWM-drawn transition flash blend with the custom caption.
    COLORREF caption = toColorref(chrome.caption);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR,
                          &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR,
                          &caption, sizeof(caption));
    COLORREF text = toColorref(chrome.text);
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR,
                          &text, sizeof(text));
}

#endif  // _WIN32
