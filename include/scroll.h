#ifndef TINTA_SCROLL_H
#define TINTA_SCROLL_H

// THE DOCUMENT SCROLL POLICY, IN A PORTABLE FILE, BECAUSE IT IS THE ONE THING
// STANDING IN FRONT OF README.md's HEADLINE BASIS.
//
// qinta has drawn a document since the argv[1] slice and you could not move it, so
// every file longer than the window showed its top and refused the rest. That is why
// basis F reads 0/3 and not because nothing renders.
//
// WHY A NEW FILE RATHER THAN THESE LINES INSIDE src/qt_window.cpp. Three reasons, in
// order of how much they cost to get wrong:
//
//   1. src/input.cpp already holds this policy and does not compile off Windows. Its
//      numbers -- 60 device-independent pixels a wheel notch, 50 an arrow key, 80% of
//      the window a page -- are the product's behaviour, not defaults, and a second
//      copy inside a Qt-only file would be a divergence with no test that can see it.
//      Here there is ONE place for those numbers and input.cpp can route to it when it
//      ports. That is the same argument loadDocumentFromPath was written under, and it
//      is the argument this tree keeps having to re-learn: main_d2d.cpp's file loader
//      is two lambdas inside WinMain, so nothing could reuse it and the Qt side had to
//      write a new one.
//   2. A Qt-only file cannot be exercised by a test that links no Qt. This one can.
//   3. The CLAMP is the part with a defect in it, and it wants to be in exactly one
//      place. app.contentHeight is only known after layoutDocument has run, so before
//      the first paint every maximum is zero and every scroll clamps to the top. That
//      is correct and it is also surprising, so it is asserted rather than described.
//
// THE NUMBERS ARE COPIED, NOT CHOSEN, and the sites are named so a reviewer can check
// them, BY `case` LABEL RATHER THAN BY LINE: src/input.cpp's handleMouseWheel document
// branch (wheel, 60); in handleKeyDown, `case VK_DOWN`/`case 'J'` and
// `case VK_UP`/`case 'K'` (arrows, 50), `case VK_NEXT`/`case VK_SPACE` (page,
// height * 0.8), `case VK_HOME`, and `case VK_END` (after ensureLayoutComplete). The
// horizontal step is 60 as well, from handleMouseWheel's Shift branch and
// `case VK_LEFT`/`case VK_RIGHT`.
//
// THE LINE NUMBERS THAT STOOD HERE ARE GONE ON PURPOSE, not refreshed. They read :888,
// :3964, :3985, :3286, :3998, :4004, :881 and :3972, and every one of them was wrong by
// 2026-09-20 -- not because the scroll code moved, but because input.cpp changed ABOVE
// them. That is the third of the three ways a line citation rots, it is the one no
// amount of care at the citing site prevents, and the fix is to cite the thing the
// claim is about: a `case` label is greppable and does not drift.
//
// WHAT IS DELIBERATELY NOT HERE. Ctrl+wheel zoom, which handleMouseWheel routes to
// applyZoomDelta -- zoom rescales the scroll position to keep content anchored and
// lives in the same file as the rest of the zoom state, so lifting one branch of it
// would split a coupled thing. Smooth scrolling is also absent: the Windows code sets
// targetScrollY and then assigns scrollY from it in the same breath at every site, so
// the animation the two members imply is not switched on, and reproducing that
// faithfully means writing both members. BOTH ARE WRITTEN, for that reason -- a seam
// preserves behaviour rather than improving it, and input.cpp reads targetScrollY in
// eleven other places.
//
// ONE DELIBERATE DIVERGENCE FROM src/input.cpp, ASSERTED RATHER THAN FOOTNOTED.
// input.cpp's arrow-key sites clamp the HORIZONTAL offset at zero only -- `targetScrollX
// += dpi(60)` in `case VK_RIGHT` has no upper bound -- and relies on
// src/paint_document.cpp to
// clamp `scrollX` at paint time. The visible pixels are therefore right on both stacks,
// but on Windows `targetScrollX` keeps growing past the end of the content, so after
// holding Right at the right-hand edge the first several Lefts do nothing. Here the
// clamp is applied to both axes at every site. That is a behaviour CHANGE and it is the
// one place in this file that is not a copy, so `qt_entry_tests` asserts the clamped
// value directly: a divergence nothing measures is indistinguishable from a mistake.

struct App;

namespace tinta {

// Clamp both axes into range and mirror the targets onto the live values. Every
// function below ends with this, and the Windows sites all end with the same two
// lines open-coded -- see src/input.cpp's arrow-key cases and handleMouseWheel.
//
// Public because a caller that sets app.targetScrollY itself still needs the clamp,
// and because a test can assert it in isolation.
void clampDocumentScroll(App& app);

// One wheel notch is 60 device-independent pixels. `notches` is POSITIVE FOR WHEEL
// UP, matching GET_WHEEL_DELTA_WPARAM/WHEEL_DELTA on Windows and
// QWheelEvent::angleDelta().y()/120 in Qt, and it is a float because both platforms
// report fractions of a notch from a trackpad.
void scrollDocumentByNotches(App& app, float notches);
void scrollDocumentHorizontallyByNotches(App& app, float notches);

// One arrow-key step: 50 device-independent pixels vertically, 60 horizontally.
// `direction` is +1 for down/right and -1 for up/left; anything else is a no-op
// rather than an assertion, because the caller is an event handler.
void scrollDocumentByStep(App& app, int direction);
void scrollDocumentHorizontallyByStep(App& app, int direction);

// 80% of the window height. Reads app.height, so a caller on a stack that syncs the
// viewport inside its paint path must sync it before calling this or page down moves
// zero on the first keystroke.
void scrollDocumentByPage(App& app, int direction);

void scrollDocumentToTop(App& app);

// Calls ensureLayoutComplete first, because jump-to-bottom needs the FINAL content
// height and layout is incremental -- input.cpp's `case VK_END` does the same, with the same
// comment, and it is the only scroll target that cannot be computed from what is
// already laid out.
void scrollDocumentToBottom(App& app);

}  // namespace tinta

#endif // TINTA_SCROLL_H
