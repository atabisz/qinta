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

#include "win32_api.h"
#include "timer_sink.h"

#include <cstdio>
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

    std::printf("timer_sink checks=%d failures=%d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
