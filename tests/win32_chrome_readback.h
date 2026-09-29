#ifndef TINTA_TESTS_WIN32_CHROME_READBACK_H
#define TINTA_TESTS_WIN32_CHROME_READBACK_H

#include <cstdint>

// SLICE 13'S OS-SIDE READING, in its own TU for win32_monitor_dump.h's reason: <windows.h> and
// <dwmapi.h> stay out of tests/qt_entry_tests.cpp.
//
// The window's chromeCount()/lastChrome() say what the sink RECEIVED; this says what DWM now
// HOLDS for the window, which is the only reading that can fail on a sink that records and
// never paints. Each attribute carries its own `read` flag, because DwmGetWindowAttribute is
// not documented to answer for every attribute DwmSetWindowAttribute takes -- a refused read is
// a skip, never a match.
namespace tinta {

struct DwmChromeReading {
    bool darkRead = false;
    bool dark = false;
    bool captionRead = false;
    std::uint32_t caption = 0;  // COLORREF, 0x00BBGGRR
};

// `window` is QWidget::winId(). All-false off Windows.
DwmChromeReading readDwmChrome(std::uintptr_t window);

}  // namespace tinta

#endif  // TINTA_TESTS_WIN32_CHROME_READBACK_H
