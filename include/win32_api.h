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
// Those three sites -- main_d2d.cpp's WM_APP_UPDATE_CHECK post from the update-check
// thread, render.cpp:2433, search.cpp:122 (line
// numbers re-measured 2026-09-15; the previous three had drifted and pointed at a
// bare brace, a SizeU() argument and an rfind() -- the ROUTING added and removed no
// lines, so they had been stale for some spans already) -- are
// EXACTLY the three that already test the return value and `delete` on rejection,
// because Windows can refuse a post when a queue is full or a window is gone. That
// correspondence is measured, not assumed, and it is what makes false the right
// answer: the existing error path does precisely the correct thing off Windows,
// with no new code and no leak. (The fourth cast, input.cpp's WM_SETTINGCHANGE post in
// the settings-command switch, passes a string
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

// --- Mouse cursor shape -------------------------------------------------------
//
// Set the mouse cursor to one of the four shapes this tree uses.
//
// THIS ONE ENTERS ON THE FIRST ARM OF THE RULE ABOVE BY A WIDE MARGIN, and the
// margin is why it takes a SHAPE instead of a handle. The figures that first made
// that argument were "58 SetCursor, 13 LoadCursor, 10 HCURSOR and 14 IDC_*, spread
// over four files -- input.cpp 52 SetCursor alone, editor.cpp 6, plus LoadCursor in
// main_d2d.cpp". A seam taking an HCURSOR would have moved one call and left
// HCURSOR, LoadCursor and every IDC_* constant in the caller, which is most of what
// makes those files unreadable off Windows. Only four distinct shapes are ever
// asked for, so the shape is the thing to name.
//
// THOSE FIGURES WERE STALE AND ARE REPLACED RATHER THAN DELETED, because what they
// reconcile TO is the useful part. Re-measured 2026-09-20 across src/ and include/
// with comments stripped, after input.cpp was routed through here: 4 SetCursor, 5
// LoadCursor, 4 HCURSOR and 5 IDC_* remain tree-wide. Four of each are the four
// lines of this seam's own implementation below, and every single one under src/ is
// gone; the two ODD ENTRIES are main_d2d.cpp's LoadCursor/IDC_ARROW pair, which
// registers a window class rather than setting a hover shape and is therefore out
// of this seam's scope rather than missed by it.
//
// THE 5s ARE A CORRECTION TO THIS PARAGRAPH'S FIRST DRAFT, which read "4 LoadCursor
// ... 4 IDC_* ... zero of any of them anywhere under src/" and then named the
// main_d2d.cpp pair as an exception in the same breath -- a sentence that
// contradicted itself by arithmetic, because an exception under src/ is not
// compatible with zero under src/. The cause is worth recording since it is the
// same one that made the figures above stale: the replacement numbers were WRITTEN
// BEFORE BEING MEASURED, from the shape of the fix rather than from a count. The
// four decompositions that do hold are 4+0, 4+1, 4+0 and 4+1.
//
// The old figures reconcile against the new ones exactly: 58 was input.cpp's 52
// plus editor.cpp's 6, both now zero, and the 4 counted here are the seam's own
// implementation, which the original measurement was taken before writing. That the
// arithmetic closes is what proves the drift is two conversions rather than a
// counting error -- and a figure in a comment rots with no diff to point at it,
// which is why this paragraph re-derives rather than adjusts.
//
// THE STATIC CACHES MOVED IN HERE, AND THAT IS THE ONE THING IN THIS SLICE THAT
// IS NOT A STRAIGHT CARRY -- so it is stated rather than left to be discovered.
// Callers were inconsistent: editor.cpp:2688 and :2711-2713 cached in a
// `static HCURSOR`, while :2661 and :2667 called LoadCursor afresh on every
// mouse-move. Both spellings are correct, because LoadCursor on a system cursor
// returns a SHARED handle that the caller neither owns nor destroys -- so the
// consolidation changes no lifetime and no observable behaviour, and the only
// difference is that two per-mouse-move lookups became one-time ones. If that
// mattered it would be a bug fix; it does not, which is why it is recorded here
// as a consolidation and nowhere else as an improvement.
//
// Off Windows this does nothing, and it is a REAL gap rather than a cosmetic one:
// the split-pane handle and the text area stop announcing that they are draggable
// or clickable. The Qt path is QWidget::setCursor with Qt::ArrowCursor,
// PointingHandCursor, IBeamCursor and SplitHCursor -- a direct four-way mapping
// that needs a widget the App struct does not hold yet, which is the same
// blocker setWindowTitle records above.
enum class CursorShape { Arrow, Hand, IBeam, SizeWE };

inline void setCursorShape(CursorShape shape) {
#ifdef _WIN32
    // Loaded once per shape. See the note above on why this is safe: these are
    // shared system handles, not owned resources.
    static HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
    static HCURSOR hand = LoadCursor(nullptr, IDC_HAND);
    static HCURSOR ibeam = LoadCursor(nullptr, IDC_IBEAM);
    static HCURSOR sizewe = LoadCursor(nullptr, IDC_SIZEWE);
    switch (shape) {
        case CursorShape::Hand: SetCursor(hand); break;
        case CursorShape::IBeam: SetCursor(ibeam); break;
        case CursorShape::SizeWE: SetCursor(sizewe); break;
        case CursorShape::Arrow:
        default: SetCursor(arrow); break;
    }
#else
    (void)shape;
#endif
}

// --- Mouse capture ------------------------------------------------------------
//
// Route mouse messages to this window until the button is released, so a drag
// that leaves the window keeps tracking. The count first written here was "9
// SetCapture and 12 ReleaseCapture sites across input.cpp, editor.cpp and
// tabs.cpp", and like the cursor figures above it is replaced rather than deleted
// because the reconciliation is the evidence: editor.cpp's three of each were
// converted first, leaving 6 and 9, and input.cpp's 5 and 7 plus tabs.cpp's 1 and 2
// are exactly that remainder. Re-measured 2026-09-20 after routing both, the only
// SetCapture and ReleaseCapture calls left in the tree are the ones below.
//
// THIS WAS WRITTEN AS "THE ONE ENTRY IN THIS HEADER WHOSE OFF-WINDOWS NO-OP IS
// THE FINAL ANSWER RATHER THAN A PLACEHOLDER", and the re-measurement recorded
// below REFUTED THAT FOR ONE SITE OF NINE. The claim is kept in its original
// words because the refutation is only legible against them, and because it was
// the claim's own instruction to re-measure that produced the counter-example --
// deleting it would hide the mechanism that worked. Corrected form: the no-op is
// the final answer for EIGHT of the nine release sites and owes Qt code at one.
//
// The argument, unchanged where it holds: Qt grabs the mouse IMPLICITLY between
// press and release -- a widget that receives a press receives every subsequent
// move and the release, inside its bounds or outside them, with no call at all.
// For any site bracketed by that press and release the Qt path needs nothing and
// the empty arms below are correct rather than owed.
//
// That claim is only safe because the call sites match the implicit grab's
// bracket exactly, which was measured rather than assumed: in editor.cpp all
// three SetCapture calls are inside handleEditorMouseDown and all three
// ReleaseCapture calls inside handleEditorMouseUp. A site that captured outside
// a press -- on hover, or across a message -- would NOT be covered by Qt's
// implicit grab and would need QWidget::grabMouse, so the next file routed
// through here should re-measure that bracket rather than inherit this
// paragraph.
//
// THAT RE-MEASUREMENT HAS NOW BEEN DONE, for input.cpp and tabs.cpp, AND IT FOUND
// ONE SITE THE PARAGRAPH ABOVE DOES NOT COVER. input.cpp survives it intact, but
// NOT in the shape this sentence first claimed, and the correction is the useful
// part: all 5 of its captureMouse calls are inside handleMouseDown, and of its 7
// releases SIX are inside handleMouseUp -- the seventh is inside handleMouseDown,
// on the start-page branch, where the press lets go of its own capture up front
// because the Open action runs a modal picker whose mouse-up never reaches this
// window. That is still inside a press/release bracket, so Qt's implicit grab is
// satisfied and the conclusion holds; the arithmetic in the original sentence did
// not. THE LINE NUMBERS THAT USED TO STAND HERE ARE DELIBERATELY GONE rather than
// updated: they read `:1715-2590` and `:2592-3260` and were wrong by ~30 lines
// because input.cpp grew above them, and a span citation for a function body rots
// every time anything earlier in the file changes. The function NAMES are what the
// claim is actually about, they are greppable, and they do not drift. tabs.cpp
// does NOT. Its capture is in tabStripMouseDown and its two releases are in
// tabDragEnd and tabDragCancel; tabDragEnd is reached from handleMouseUp and is
// bracketed, but tabDragCancel is reached from handleKeyDown on VK_ESCAPE and from
// main_d2d.cpp's WM_CAPTURECHANGED. A KEYSTROKE DOES NOT END QT'S IMPLICIT GRAB, so
// on Qt an Esc there would abort the drag's state while the widget kept grabbing
// the mouse. That site needs an explicit QWidget::releaseMouse and the full
// argument is written at the site rather than here, because the two callers need
// two different Qt shapes and only one of them is a call.
//
// So the score is 8 of 9 settled and 1 owed, which is a better answer than the
// paragraph's own prediction of covered-or-bug: the exception is neither, it is a
// release keyed on a different event class entirely. Worth noting what made it
// findable -- the instruction to re-measure rather than inherit. An inherited
// paragraph would have shipped a wrong claim about a file it had never opened.
//
// THE 5-AGAINST-7 ASYMMETRY IS WHY THIS WAS WORTH MEASURING AND IS NOT A MISMATCH,
// which is the opposite of what the counts predict and therefore the finding. Two
// things produce it and neither leaves the bracket: handleMouseUp has several early
// returns and releases on each, and ONE of the releases is inside handleMouseDown
// (:2129), letting go of the capture the same press just took because the start
// page's Open action opens a modal picker whose release never reaches this window.
// A release inside a press handler is not a site the paragraph above anticipated,
// so it is named here: Qt breaks the implicit grab for a modal itself, so the
// no-op remains correct for that site too.
//
// WHAT THE RE-MEASUREMENT DID CHANGE is that a bare ReleaseCapture is not the only
// spelling in use. 6 of the 9 release sites read `if (GetCapture() == hwnd)
// ReleaseCapture();` -- a guard against releasing a capture that belongs to another
// window on the same thread, which matters here because the quick-note window is a
// second window. GetCapture was invisible to the first census because that census
// was keyed on the two calls this seam already had, which is the blind spot a
// census keyed on the shape you already fixed always has. Rather than leave 6 sites
// raw -- which would have kept ReleaseCapture AND added GetCapture to the list of
// what makes input.cpp unreadable off Windows -- the guarded form gets its own
// entry below. It qualifies on the first arm of this header's rule at 6 sites.
inline void captureMouse(HWND hwnd) {
#ifdef _WIN32
    SetCapture(hwnd);
#else
    (void)hwnd;
#endif
}

inline void releaseMouse() {
#ifdef _WIN32
    ReleaseCapture();
#endif
}

// Release the capture only if THIS window holds it.
//
// Two calls behind one name, deliberately, because the pair is the unit: the guard
// is meaningless without the release and the release is wrong without the guard.
// ReleaseCapture() releases whatever window on the CALLING THREAD holds the mouse,
// not necessarily the one asking, so an unguarded call from a window that never
// captured would let go of a drag belonging to a sibling -- and this tree has a
// sibling, the quick-note window. Every one of the 6 sites spelled the guard by
// hand and identically, which is what makes this one call rather than two.
//
// OFF WINDOWS THIS IS THE ENTRY THAT OWES QT CODE, and it is this one rather than
// releaseMouse because the one unbracketed site in the tree spells its release
// with the guard. Five of its six callers are inside a mouse-up and need nothing;
// the sixth is tabs.cpp's tabDragCancel, reached from a keystroke and from
// WM_CAPTURECHANGED, where an empty arm leaves Qt still grabbing. So the arm below
// IS a placeholder at one call site out of six -- which is why the reason is
// written at that site, where the reader deciding what to do has the callers in
// front of them, and not only here.
inline void releaseMouseIfCaptured(HWND hwnd) {
#ifdef _WIN32
    if (GetCapture() == hwnd) ReleaseCapture();
#else
    (void)hwnd;
#endif
}

#endif  // TINTA_WIN32_API_H
