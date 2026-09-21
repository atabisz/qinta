// Exercises include/repaint_sink.h and the off-Windows arm of
// win32_api.h::requestRepaint.
//
// WHAT WAS UNTESTABLE BEFORE THIS FILE. requestRepaint is the tree's single repaint
// seam -- 277 call sites under src/ -- and off Windows its whole body was
// `(void)hwnd;`. Nothing could observe that, because a no-op and a working repaint
// are indistinguishable from the caller's side: the call returns, the state really
// does mutate, and the compiler says nothing. That is the shape this tree keeps
// cataloguing, and it sat in the one header whose opening rule forbids it.
//
// WHY THE SEAM ITSELF IS ONLY CALLED OFF WINDOWS HERE, AND WHY THAT IS NOT A HOLE.
// The Windows arm is InvalidateRect(hwnd, nullptr, FALSE), and this test has no
// window to hand it. Passing nullptr is not a harmless stand-in: InvalidateRect
// with a NULL hWnd invalidates and redraws EVERY window on the desktop, so a test
// that did it would flash the whole machine as a side effect of running the suite.
// So the requestRepaint call below is guarded, and the guard is COUNTED rather than
// silent -- the totals printed at the end differ per platform on purpose, and a
// platform where the guarded block vanished entirely would show it in the count
// rather than still reporting "all assertions passed".
//
// The Windows arm is not left unexamined either: it is unchanged by this slice, and
// ISC-13's whole job is the Windows suite staying at full count, which is a stronger
// instrument for "InvalidateRect still runs" than anything this file could assert
// without a window.

#include "win32_api.h"
#include "repaint_sink.h"

#include <cstdio>

static int failures = 0;
// COUNTED, for the reason geometry_tests.cpp states: this file has a block behind
// #ifdef _WIN32, so "how many assertions ran" is a real question here.
static int checks = 0;

static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

int main() {
    // --- The slot starts empty, and an empty slot is not an error -------------
    //
    // The Windows build, every other test binary and a console run all have no
    // sink, and a repaint request in those has genuinely nothing to do. This must
    // therefore be a safe no-op rather than a crash or a diagnostic.
    check(!tinta::repaintSinkInstalled(), "no sink is installed at startup");
    tinta::deliverRepaintRequest();
    check(true, "delivering with no sink installed is a safe no-op");

    // --- A registered sink receives the request -------------------------------
    int calls = 0;
    tinta::setRepaintSink([&calls]() { ++calls; });
    check(tinta::repaintSinkInstalled(), "setRepaintSink installs the sink");

    tinta::deliverRepaintRequest();
    // EXACTLY one, not "at least one". A seam that delivered twice per request
    // would coalesce invisibly in Qt and pass a `calls > 0` check, so the
    // assertion is on the delta rather than on the direction.
    check(calls == 1, "one delivery calls the sink exactly once");
    tinta::deliverRepaintRequest();
    tinta::deliverRepaintRequest();
    check(calls == 3, "three deliveries call the sink exactly three times");

    // --- Installing a second sink REPLACES the first --------------------------
    //
    // A slot implemented as a list would pass every assertion above and then fire
    // a destroyed window's closure for the rest of the process. This is the check
    // that separates the two implementations.
    int secondCalls = 0;
    const int callsBeforeReplace = calls;
    tinta::setRepaintSink([&secondCalls]() { ++secondCalls; });
    tinta::deliverRepaintRequest();
    check(secondCalls == 1, "the replacement sink receives the request");
    check(calls == callsBeforeReplace, "the replaced sink receives nothing further");

    // --- The real seam, off Windows -------------------------------------------
#ifndef _WIN32
    // THIS IS THE ASSERTION THE FILE EXISTS FOR. Everything above tests
    // repaint_sink.h in isolation, which a correct sink with a seam still wired to
    // `(void)hwnd` would pass in full. Only this reaches the function the 277 call
    // sites actually call.
    //
    // nullptr is not a stand-in for a window here -- off Windows it is what every
    // one of those sites passes, because App::hwnd is assigned at exactly one site
    // in the tree and that site is in the Windows-only D2D window creation.
    const int callsBeforeSeam = secondCalls;
    requestRepaint(nullptr);
    check(secondCalls == callsBeforeSeam + 1,
          "requestRepaint(nullptr) off Windows reaches the registered sink exactly once");

    // And it must stop reaching it once the window is gone, because the sink
    // captures that window. A request after clearRepaintSink() that still fired
    // would be a use-after-free the moment a real QtAppWindow is destroyed.
    tinta::clearRepaintSink();
    check(!tinta::repaintSinkInstalled(), "clearRepaintSink empties the slot");
    const int callsAfterClear = secondCalls;
    requestRepaint(nullptr);
    check(secondCalls == callsAfterClear,
          "requestRepaint after clearRepaintSink reaches nothing");
#else
    // Deliberately not calling requestRepaint: see the header comment. The clear
    // half is still asserted, so the Windows arm of this file is not vacuous.
    tinta::clearRepaintSink();
    check(!tinta::repaintSinkInstalled(), "clearRepaintSink empties the slot");
    std::printf("note: requestRepaint itself is not called on Windows -- "
                "InvalidateRect with a NULL hWnd would redraw every window on the desktop\n");
#endif

    std::printf("%d/%d repaint sink assertions passed\n", checks - failures, checks);
    std::printf("%s\n", failures == 0 ? "all repaint sink assertions passed"
                                      : "repaint sink assertions FAILED");
    return failures == 0 ? 0 : 1;
}
