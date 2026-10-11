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
// The settings functions after them (slice 11) are here for the same reason.

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
// The narrowness is the point and it is not laziness: Settings has 34 fields and a placement
// reads four of them. The theme fields go to applySavedTheme below, windowMaximized to
// showPlaced, and the rest -- the keymap and language included -- to applySavedPreferences
// (settings.h).
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

// --stats and --tabbed: two App fields, set before the window exists, as WinMain sets them right
// after the parse. Refused by name on this arm until 2026-10-06, each citing src/input.cpp, which
// had been linked into qinta since the key router (2026-09-25).
void applyLaunchFlags(App& app, const LaunchOptions& options);

// --new and --edit, entered AFTER the window exists, as WinMain enters them after its first
// frame: --new opens an untitled quick note, --edit puts the loaded document in the editor.
// --new wins when both are given, which is WinMain's `if (quickNote) ... else if (edit)`.
// These are what make the two self-relaunches (src/window_launch_qt.cpp) land where the
// Windows ones do.
void enterLaunchMode(App& app, const LaunchOptions& options);

// THE THEME FIELDS settings.ini HOLDS, LOADED BECAUSE THIS ARM WRITES THEM. The theme picker's
// persistThemeChoice and the close save both read-modify-write these four, so an arm that
// wrote them without loading them put App's defaults over the user's saved light/dark pair --
// on Windows in the settings.ini tinta itself reads. The start index is WinMain's rule: an
// index past the registry (a deleted custom theme) falls back to slot 0. With follow-system on
// the start index is the one `systemPrefersLight` picks, as WinMain's autoThemeIndex does.
void applySavedTheme(App& app, const Settings& settings, bool systemPrefersLight);

// This arm's reading of the system appearance, WinMain's AppsUseLightTheme counterpart. On Qt
// 6.5+ it is QStyleHints::colorScheme, and Unknown reads as light the way a missing registry
// value does. Below 6.5 there is no such property, so it is the palette's window lightness --
// a reading of what the platform theme drew, taken when asked, with no change notification.
bool systemPrefersLight();

// show(), or showMaximized() when the saved window was maximized -- windowMaximized is one of
// the fields the close save writes, so it is loaded for the same reason as the theme.
void showPlaced(QWidget& window, bool maximized);

// WM_DESTROY's save: storePreferences (settings.h) for everything App holds -- the theme, the
// preferences, the reading position and the session -- then the maximized flag and, unless
// maximized or full screen, where the widget's rectangle is not the one to restore, the
// geometry, as the inverse of applyPlacement: frame position, client size.
void saveClosingState(const App& app, const QWidget& window);

// THE LAST SESSION, WinMain's rule: reopened on a plain launch -- no file argument, not a quick
// note, a cascaded new window, an edit-mode launch or a harness switch -- and only with "Open
// files in tabs" on, since with it off a window holds one document. Paths that no longer exist
// are dropped, and `active` follows its path through the filtering. Empty means no session.
std::vector<std::string> sessionToRestore(const LaunchOptions& options, const Settings& settings,
                                          int& active);

// The launch document's path, made absolute against the current directory. A file named on the
// command line is stored in the session and the recent files, and a relative one stored as
// given only resolves again from the same directory: started from the Start menu or a file
// manager, qinta dropped it as missing (found 2026-10-12 in a real settings.ini). Empty stays empty.
std::string absoluteLaunchPath(const std::string& path);

// Whether this window owns the session the close save writes: not a cascaded new window and not
// a quick note, as in WinMain. Those keep what is on disk (storePreferences).
bool ownsSession(const LaunchOptions& options);

// After the launch document has loaded: its saved reading position and, when it differs, its
// zoom, as openDocumentInViewer does for an in-app open -- except under --edit, which opens at
// the top, as in WinMain. A file named on the command line also goes to the head of the recent
// files, unless this is a harness run (--printpages, --export*): a headless export is not the
// user opening a document, and WinMain recording it is how a scripted run came to rewrite the
// user's settings. Then the text formats, if the zoom they were built at (installQtBackends,
// before any setting was read) is no longer the one in force.
void restoreLaunchDocument(App& app, const Settings& settings, const LaunchOptions& options,
                           bool namedOnCommandLine);

}  // namespace tinta

#endif  // TINTA_QT_LAUNCH_H
