#ifndef TINTA_QT_LAUNCH_H
#define TINTA_QT_LAUNCH_H

#include <vector>

#include "launch_options.h"
#include "settings_data.h"
#include "window_placement.h"

// THE QT ARM'S LAUNCH DECISIONS, PULLED OUT OF main() SO A TEST CAN REACH THEM.
//
// src/main_qt.cpp's whole design claim is "a main() with no logic in it has nothing to test,
// which is the point -- the logic is next door where a test can reach it". Wiring the launch
// options into it put logic back: two steps that turn parsed options and loaded settings into
// a placed window, both of which can be WRONG in ways a green build does not notice.
//
// Specifically, `placementRequestFrom` is nine field copies, and a field copy is the classic
// silent defect -- swap posX and posY and a window appears somewhere plausible, on the wrong
// axis, in a build that compiles and runs. Nine copies inside main() are nine copies nothing
// can assert on. Here, tests/qt_entry_tests.cpp asserts on them AND on the geometry the real
// QtAppWindow ends up with, which are two different claims: one that the request was built
// right, one that the request reached the window.
//
// THIS IS NOT A SECOND DECISION. Neither of the first two functions decides anything about
// placement -- that is tinta::choosePlacement, shared with WinMain, and it is deliberately
// not called from here. They supply its INPUTS from the two sources only the Qt arm has: Qt's
// screen list and this arm's parse. Putting the decision here as well would recreate exactly
// the two-chains problem that made the shared parse necessary.
//
// THE OTHER TWO ARE THE APPLY HALF, and they are here for a sharper reason than tidiness:
// a test that holds its own copy of `resize(); if (not sentinel) move();` cannot fail on a
// defect planted in main()'s copy. The tree has paid for that shape before -- memory
// `a-reproduction-is-not-an-instrument`. So main() calls these and the test calls the same
// two functions, and what is left uncovered in main() is only which arguments it passes.

struct App;
class QWidget;

namespace tinta {

// Qt's screen list as the placement seam's monitor list.
//
// TWO RECTANGLES PER MONITOR, and the pair is load-bearing rather than thorough:
// QScreen::geometry() is what a saved window is hit-tested against, availableGeometry() is
// what it is clamped into, and the two differ by the taskbar. Collapsing them would silently
// move any window sitting inside the taskbar strip -- a behaviour change disguised as a
// simplification. window_placement.h names these two calls as the counterparts it expects
// from a Qt caller, so this function is that paragraph made executable.
//
// Requires a live QGuiApplication; returns an empty list if the platform reports no screens,
// which choosePlacement already treats as "do not clamp" rather than as an error.
std::vector<MonitorArea> monitorsFromQt();

// Settings' four geometry fields plus the three placement options, and nothing else.
//
// The narrowness is the point and it is not laziness: Settings has thirty fields and the
// other twenty-six reach consumers that are still Windows-only, so copying them would set
// state nothing on this arm reads. These four have a portable consumer, which is why they are
// the four.
//
// `savedX`/`savedY` may be kWindowPositionUseDefault and that value is passed through
// unchanged -- it means "nothing has ever saved a position", and choosePlacement returns it
// so the caller can let the window manager choose instead of inventing a coordinate.
PlacementRequest placementRequestFrom(const LaunchOptions& options, const Settings& settings);

// Put a chosen placement on a real widget: always the size, the position only if it is one.
//
// THE SENTINEL SURVIVES INTO HERE, which is why choosePlacement returns it rather than
// resolving it. kWindowPositionUseDefault means "nothing has ever saved a position", and the
// right answer to that is to let the window manager place the window -- so this does NOT call
// move() in that case. Resolving the sentinel inside the placement seam would have forced it
// to invent a coordinate, and inventing one is exactly what Qt does better.
//
// Takes a QWidget rather than a QtAppWindow because nothing here needs the app window: the
// narrower parameter is what lets a test drive it, and it is also the honest type.
void applyPlacement(QWidget& window, const WindowRect& placement);

// --light: the one appearance flag that needs nothing Windows-only.
//
// Three fields that have to move TOGETHER, which is the reason this is a function and not
// three lines in main(). The index comes from theme_data.h so WinMain and the Qt arm cannot
// disagree about which slot "light" means, and `darkMode` is READ OFF the chosen theme rather
// than assumed false -- assuming it is how a renamed or reordered theme table turns a light
// window into a light window drawn with dark-mode decisions.
void applyLightMode(App& app);

}  // namespace tinta

#endif  // TINTA_QT_LAUNCH_H
