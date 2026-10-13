#ifndef TINTA_TESTS_WIN32_MONITOR_DUMP_H
#define TINTA_TESTS_WIN32_MONITOR_DUMP_H

#include <vector>

#include "window_placement.h"

// THE WIN32 HALF OF ISC-17'S DIFFERENTIAL, IN ITS OWN TRANSLATION UNIT ON PURPOSE.
//
// `tinta::monitorsFromQt()` (src/qt_launch.cpp) is what the Qt arm feeds to choosePlacement. The
// question ISC-17 asks is whether the Win32 arm's own enumeration -- EnumDisplayMonitors plus
// GetMonitorInfoW, which is what the retired src/main_d2d.cpp's placement path always used -- describes the
// SAME displays. A port that silently changes which rectangles the placement decision is made over
// has changed the decision, and every green in the portable placement suite would stay green through
// it, because that suite is handed its monitors as data.
//
// WHY A SEPARATE FILE and not a `#ifdef _WIN32` block inside tests/qt_entry_tests.cpp: that file is
// 1,900 lines of Qt, and <windows.h> arrives with a macro set (`min`, `max`, `interface`, ...) that
// is actively hostile to headers included after it. Keeping the Win32 include in a TU whose only job
// is this enumeration means the hazard has nowhere to reach.
namespace tinta {

// The live displays as Win32 reports them, in EnumDisplayMonitors order. `bounds` is
// MONITORINFO::rcMonitor and `work` is rcWork -- deliberately the same pairing monitorsFromQt()
// builds out of QScreen::geometry()/availableGeometry(), so the two lists are comparable
// element-by-element rather than needing a translation nobody can check.
//
// Returns an empty vector off Windows, where there is no second enumeration to compare against and
// the criterion's "or the disagreement is stated" branch does not apply.
std::vector<MonitorArea> monitorsFromWin32();

// The calling process's DPI awareness, as a short literal for printing: "per-monitor-v2",
// "per-monitor", "system", "unaware", or "unknown". THIS IS NOT DECORATION. Win32 reports monitor
// rectangles in PHYSICAL pixels to a per-monitor-aware process and in virtualised ones otherwise,
// while Qt 6 reports QScreen::geometry() in LOGICAL pixels -- physical divided by the screen's
// devicePixelRatio. So the two enumerations can only be compared as equal where every ratio is 1,
// and an agreement read without knowing that is an agreement that expires the moment somebody
// scales a display. The retired src/main_d2d.cpp called SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)
// at WinMain entry; this reports what the TEST process ended up with rather than assuming it matches.
const char* processDpiAwareness();

}  // namespace tinta

#endif  // TINTA_TESTS_WIN32_MONITOR_DUMP_H
