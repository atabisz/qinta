// Exercises include/timer_sink.h and the off-Win32 arms of win32_api.h's armTimer and
// disarmTimer.
//
// Same instrument shape as tests/repaint_sink_tests.cpp, and read that file for why the
// seam itself is only called off Windows here: the Windows arm is SetTimer, and
// SetTimer(NULL, ...) does not fail -- it creates a THREAD timer with a system-chosen id,
// so a test calling it with no window would leak a live timer rather than exercise
// anything. The guarded block is counted, so the totals differ per platform on purpose.
//
// What the Qt window's sink does with a request -- one QTimer per id, restarted on
// re-arm -- is asserted in tests/qt_entry_tests.cpp, which has a window and an event loop.
// This file asserts only that each request reaches the sink with its id and interval
// intact, and that nothing installed is a safe no-op.
//
// AND include/post_sink.h, the window sink postWindowMessage reaches, asserted here rather
// than in a binary of its own because it is the same instrument over the same shape -- a
// new target would move the README's test-target bases for one header. Its one property
// the timer sink lacks is asserted too: a post from a SECOND thread arrives intact, which
// is the only way the two portable senders ever post.

#include "win32_api.h"
#include "post_sink.h"
#include "timer_sink.h"

#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

struct Recorded {
    char op;  // 'a' arm, 'd' disarm
    unsigned id;
    unsigned intervalMs;
};

int main() {
    check(!tinta::timerSinkInstalled(), "no timer sink is installed at startup");
    tinta::deliverTimerArm(5, 80);
    tinta::deliverTimerDisarm(5);
    check(true, "arming and disarming with no sink installed is a safe no-op");

    std::vector<Recorded> log;
    tinta::TimerSink sink;
    sink.arm = [&log](unsigned id, unsigned ms) { log.push_back({'a', id, ms}); };
    sink.disarm = [&log](unsigned id) { log.push_back({'d', id, 0}); };
    tinta::setTimerSink(std::move(sink));
    check(tinta::timerSinkInstalled(), "setTimerSink installs both halves");

    tinta::deliverTimerArm(5, 80);
    tinta::deliverTimerDisarm(9);
    // EXACT sequence, not "something arrived": a sink that swapped the two halves, or
    // dropped the interval, would pass a count check.
    check(log.size() == 2, "two requests reach the sink exactly twice");
    check(log.size() == 2 && log[0].op == 'a' && log[0].id == 5 && log[0].intervalMs == 80,
          "the arm arrives as arm(5, 80)");
    check(log.size() == 2 && log[1].op == 'd' && log[1].id == 9,
          "the disarm arrives as disarm(9), on the disarm half");

    // A half-installed sink is not installed: the window always sets both, and a caller
    // asking timerSinkInstalled() wants to know whether BOTH calls go somewhere.
    tinta::TimerSink half;
    half.arm = [](unsigned, unsigned) {};
    tinta::setTimerSink(std::move(half));
    check(!tinta::timerSinkInstalled(), "a sink with only arm set does not report installed");
    tinta::deliverTimerDisarm(3);
    check(true, "and its missing disarm half is a safe no-op");

#ifndef _WIN32
    // THE REAL SEAM. Off the Win32 stack armTimer and disarmTimer must reach the sink --
    // before this file they were `(void)` casts, and every timer the tree armed on qinta
    // was dropped with nothing able to observe it.
    log.clear();
    tinta::TimerSink real;
    real.arm = [&log](unsigned id, unsigned ms) { log.push_back({'a', id, ms}); };
    real.disarm = [&log](unsigned id) { log.push_back({'d', id, 0}); };
    tinta::setTimerSink(std::move(real));
    armTimer(nullptr, 8, 16);
    disarmTimer(nullptr, 8);
    check(log.size() == 2 && log[0].op == 'a' && log[0].id == 8 && log[0].intervalMs == 16 &&
              log[1].op == 'd' && log[1].id == 8,
          "armTimer(nullptr, 8, 16) and disarmTimer(nullptr, 8) reach the sink off Win32");

    // canArmTimer, the guard app.h's blink and notification helpers ask. Off Win32 it
    // follows the SINK, never the handle: a null HWND is the only one qinta has, so a
    // guard on the handle is the `if (app.hwnd)` that kept both timers dead.
    check(canArmTimer(nullptr), "canArmTimer(nullptr) is true while a sink is installed");
    tinta::clearTimerSink();
    check(!canArmTimer(nullptr), "and false once the sink is cleared");
    tinta::TimerSink again;
    again.arm = [&log](unsigned id, unsigned ms) { log.push_back({'a', id, ms}); };
    again.disarm = [&log](unsigned id) { log.push_back({'d', id, 0}); };
    tinta::setTimerSink(std::move(again));
#endif

    tinta::clearTimerSink();
    check(!tinta::timerSinkInstalled(), "clearTimerSink empties the slot");
    const size_t before = log.size();
    tinta::deliverTimerArm(5, 80);
    check(log.size() == before, "and a cleared sink receives nothing");

    // --- The post sink ----------------------------------------------------------
    struct Posted {
        unsigned message;
        std::uintptr_t wParam;
        std::intptr_t lParam;
        std::thread::id thread;
    };
    std::vector<Posted> posted;
    check(!tinta::postSinkInstalled(), "no post sink is installed at startup");
    check(!tinta::deliverPostedMessage(7, 1, 2),
          "and a post with none is REFUSED, so an owning sender keeps its delete");
    // Only message 7 is taken, as the Qt window takes only the two it has handlers for.
    tinta::setPostSink([&posted](unsigned message, std::uintptr_t wParam, std::intptr_t lParam) {
        if (message != 7) return false;
        posted.push_back({message, wParam, lParam, std::this_thread::get_id()});
        return true;
    });
    check(tinta::postSinkInstalled(), "setPostSink installs it");
    check(tinta::deliverPostedMessage(7, 11, 22) && posted.size() == 1 &&
              posted[0].wParam == 11 && posted[0].lParam == 22,
          "an accepted post arrives with both parameters intact");
    check(!tinta::deliverPostedMessage(8, 1, 2) && posted.size() == 1,
          "a message the sink does not take is refused and never recorded");
    bool fromThread = false;
    std::thread worker([&fromThread]() { fromThread = tinta::deliverPostedMessage(7, 33, -44); });
    worker.join();
    check(fromThread && posted.size() == 2 && posted[1].lParam == -44 &&
              posted[1].thread != std::this_thread::get_id(),
          "a post from a second thread is accepted, runs on that thread, lParam's sign intact");
#ifndef _WIN32
    // The seam itself, off Windows only for the reason this file's header gives SetTimer:
    // PostMessageW(NULL, ...) does not fail, it posts a thread message nobody reads.
    check(canPostWindowMessage(nullptr) && postWindowMessage(nullptr, 7, 55, 66) &&
              posted.size() == 3 && posted[2].wParam == 55,
          "postWindowMessage's off-Win32 arm reaches the installed post sink");
#endif
    tinta::clearPostSink();
    check(!tinta::postSinkInstalled() && !tinta::deliverPostedMessage(7, 1, 2),
          "clearPostSink empties the slot and a post is refused again");
#ifndef _WIN32
    check(!canPostWindowMessage(nullptr) && !postWindowMessage(nullptr, 7, 1, 2),
          "and the seam answers false with it cleared -- the no-window answer from before");
#endif

    std::printf("timer_sink checks=%d failures=%d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
