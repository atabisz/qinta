#include "win32_monitor_dump.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace tinta {
namespace {

BOOL CALLBACK collectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM param) {
    MONITORINFO info;
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(handle, &info)) {
        auto* out = reinterpret_cast<std::vector<MonitorArea>*>(param);
        MonitorArea area;
        area.bounds = {info.rcMonitor.left, info.rcMonitor.top,
                       info.rcMonitor.right - info.rcMonitor.left,
                       info.rcMonitor.bottom - info.rcMonitor.top};
        area.work = {info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left,
                     info.rcWork.bottom - info.rcWork.top};
        out->push_back(area);
    }
    // TRUE continues the enumeration. Returning FALSE on a failed GetMonitorInfoW would stop it
    // early and hand back a SHORTER list, which reads as "this host has fewer displays" rather
    // than as an error -- the silent-instrument shape, so the failed monitor is skipped and the
    // count difference is what the caller compares.
    return TRUE;
}

}  // namespace

std::vector<MonitorArea> monitorsFromWin32() {
    std::vector<MonitorArea> monitors;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor,
                        reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

const char* processDpiAwareness() {
    const DPI_AWARENESS_CONTEXT context = GetThreadDpiAwarenessContext();
    switch (GetAwarenessFromDpiAwarenessContext(context)) {
        case DPI_AWARENESS_PER_MONITOR_AWARE:
            // PER_MONITOR and PER_MONITOR_V2 both report as PER_MONITOR_AWARE through this
            // enum -- V2 is a context, not an awareness level -- so the two are asked apart
            // by comparing the context itself.
            return AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
                       ? "per-monitor-v2"
                       : "per-monitor";
        case DPI_AWARENESS_SYSTEM_AWARE:
            return "system";
        case DPI_AWARENESS_UNAWARE:
            return "unaware";
        default:
            return "unknown";
    }
}

}  // namespace tinta

#else  // !_WIN32

namespace tinta {

std::vector<MonitorArea> monitorsFromWin32() { return {}; }
const char* processDpiAwareness() { return "n/a"; }

}  // namespace tinta

#endif  // _WIN32
