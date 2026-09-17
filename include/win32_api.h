#ifndef TINTA_WIN32_API_H
#define TINTA_WIN32_API_H

// THE WIN32 API CALLS TINTA MAKES, AT A NAMED SEAM.
//
// win32_types.h is the layer below and holds TYPES only, with an explicit note
// that it will never hold functions. This is where the functions live, and the
// distinction is the whole point: a type can be stood in for off Windows and be
// genuinely equivalent, whereas a CALL cannot. Every function here does something
// on Windows and something visibly different off it, and each one says which.
//
// The rule for adding one: it must be a call the tree makes at more than one site,
// or one whose off-Windows behaviour needs explaining. A one-off call belongs
// guarded at its own site, where the reader can see it, rather than hidden behind a
// name that suggests it works everywhere.
//
// NOTHING HERE PRETENDS TO WORK. Off Windows these are no-ops or honest failures,
// never silent successes, because a silent success is the failure this tree keeps
// cataloguing: it turns a missing implementation into a wrong picture or a leak
// instead of a compile error or a visible gap. Where a caller already handles
// failure, the off-Windows path FAILS rather than no-ops, so that the caller's
// existing error handling does the right thing for free.

#include "win32_types.h"

// --- Repaint ------------------------------------------------------------------
//
// Ask the window to repaint everything at the next paint opportunity.
//
// This takes no rectangle and no erase flag, and that is a measurement rather than
// a simplification. All 277 repaint sites in the tree passed exactly
// `nullptr, FALSE`: 244 spelled `InvalidateRect(hwnd, ...)` and 33
// `InvalidateRect(app.hwnd, ...)`, differing only in how they name the window. Not
// one passed a rectangle, and not one asked for a background erase. So the seam has
// exactly the shape the tree actually uses, and a future site that genuinely needs
// a partial invalidation will have to add a parameter here and thereby say so.
//
// TENSE CORRECTED 2026-09-15: all 277 now call this seam, and no `InvalidateRect`
// remains anywhere under src/. The 244/33 split survives verbatim as the seam's own
// argument -- 244 `requestRepaint(hwnd)`, 33 `requestRepaint(app.hwnd)` -- so the
// figures above are still live and were re-measured, not assumed. Worth recording
// WHY they were nearly "fixed": a mid-pass census of the sites still spelling the
// raw call read 227/20, and 227/20 looks like a correction to 244/33 while actually
// measuring a different population -- the unrouted REMAINDER, the other 30 having
// been routed in earlier spans. 227+17 = 244 and 20+13 = 33. A partial count and a
// stale count are indistinguishable by shape; only the total, 277, separates them.
//
// Off Windows this does nothing, because there is no window and no paint message
// to schedule. That is a gap, not a lie: the Qt path will implement it as
// QWidget::update(), which is the same request with the same coalescing semantics.
inline void requestRepaint(HWND hwnd) {
#ifdef _WIN32
    InvalidateRect(hwnd, nullptr, FALSE);
#else
    (void)hwnd;
#endif
}

// --- Window title -------------------------------------------------------------
//
// Set the window's caption text.
//
// THIS ONE ENTERS ON THE SECOND ARM OF THE RULE ABOVE, NOT THE FIRST, and it is
// worth saying so because the first arm would exclude it: SetWindowTextW is named
// at exactly ONE site in the tree, utils.cpp::updateWindowTitle, and the rule says
// a one-off call belongs guarded at its own site. Two things decided otherwise.
//
// Its off-Windows behaviour needs explaining, which is what the second arm is for.
// And guarding it at its own site would mean utils.cpp keeping a conditional
// windows.h -- the exact include this slice exists to remove from that file. The
// tree already reaches windows.h through app.h on Windows, so a call routed here
// costs the caller nothing, whereas a call guarded in place costs it the header.
//
// Off Windows this does nothing, so the title computed just above the call is
// discarded. That is a visible gap of the same kind requestRepaint carries -- the
// window keeps whatever caption it was given -- and not a wrong picture: no state
// is corrupted and nothing is leaked. The Qt path is QWidget::setWindowTitle,
// which is the same request; reaching it needs a window handle the App struct
// does not hold yet (App has only `HWND hwnd`, and QtAppWindow is a QWidget on the
// Qt side), so wiring it is that slice's job rather than a line this one can write.
//
// const wchar_t* rather than const std::wstring& on purpose: it keeps this header
// free of <string>, and every caller already has a c_str() to hand.
inline void setWindowTitle(HWND hwnd, const wchar_t* title) {
#ifdef _WIN32
    SetWindowTextW(hwnd, title);
#else
    (void)hwnd;
    (void)title;
#endif
}

// --- Timers -------------------------------------------------------------------
//
// Moved here from app.h, which was the wrong home for them: SetTimer or KillTimer
// was named in 23 files, so this is a tree-wide seam that happened to be discovered
// while porting one header.
//
// TENSE CORRECTED 2026-09-15: that 23 is now history. Every call site is routed --
// 11 armTimer and 21 disarmTimer under src/, plus 3 and 1 in app.h's inline policy
// helpers -- and the only remaining mentions of the raw names in the whole tree are
// the two definitions below and three comments (this one, app.h's, win32_types.h's).
//
// The three inline functions in app.h that arm timers are mostly POLICY -- when the
// cursor should blink, when a notification fade needs repainting -- and only one
// statement of MECHANISM each, which is why the mechanism is what got extracted.
//
// Off Windows these do nothing, and that is accurate rather than convenient: there
// is no message loop off Windows either, so a timer that armed would have nothing
// to deliver WM_TIMER to. The Qt path is a QTimer per id.
inline void armTimer(HWND hwnd, unsigned id, unsigned intervalMs) {
#ifdef _WIN32
    SetTimer(hwnd, id, intervalMs, nullptr);
#else
    (void)hwnd;
    (void)id;
    (void)intervalMs;
#endif
}

inline void disarmTimer(HWND hwnd, unsigned id) {
#ifdef _WIN32
    KillTimer(hwnd, id);
#else
    (void)hwnd;
    (void)id;
#endif
}

// --- Posted messages ----------------------------------------------------------
//
// Post a message to a window's queue and return whether it was accepted. 19 sites
// tree-wide (before routing: 16 spelled PostMessageW, 3 relying on the PostMessage
// macro; all 19 now call this seam), and unlike the repaint population they are NOT
// uniform: nine distinct message ids
// between them -- WM_CLOSE, WM_SETTINGCHANGE, WM_SYSCOMMAND and six WM_APP_* --
// four different ways of naming the window, and four sites casting a pointer into
// lParam. So this seam keeps the full four-argument shape.
//
// OFF WINDOWS THIS RETURNS false, AND THAT IS THE INTERESTING PART. A no-op
// returning true would be the worst possible answer, because of what the lParam
// casts carry: three of those four sites hand OWNERSHIP of a `new` allocation to
// the handler, so a pretend success leaks it and silently drops the work.
//
// Those three sites -- main_d2d.cpp:1187, render.cpp:2433, search.cpp:122 (line
// numbers re-measured 2026-09-15; the previous three had drifted and pointed at a
// bare brace, a SizeU() argument and an rfind() -- the ROUTING added and removed no
// lines, so they had been stale for some spans already) -- are
// EXACTLY the three that already test the return value and `delete` on rejection,
// because Windows can refuse a post when a queue is full or a window is gone. That
// correspondence is measured, not assumed, and it is what makes false the right
// answer: the existing error path does precisely the correct thing off Windows,
// with no new code and no leak. (The fourth cast, input.cpp:696, passes a string
// literal and owns nothing, so it neither checks nor needs to.)
//
// The general lesson worth keeping: when a call already has a documented failure
// mode that callers handle, an off-Windows implementation should USE that mode
// rather than invent a third state.
inline bool postWindowMessage(HWND hwnd, unsigned message, WPARAM wParam, LPARAM lParam) {
#ifdef _WIN32
    return PostMessageW(hwnd, message, wParam, lParam) != 0;
#else
    (void)hwnd;
    (void)message;
    (void)wParam;
    (void)lParam;
    return false;
#endif
}

#endif  // TINTA_WIN32_API_H
