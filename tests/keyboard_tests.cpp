// Assertions for include/keyboard.h -- the modifier-key seam.
//
// WHAT THIS TEST CAN AND CANNOT PROVE, stated first because the honest answer is
// mixed and was known before the seam was built.
//
// It CANNOT prove much about isShiftDown() and isCtrlDown(). No key is held while a
// test binary runs, so both correctly return false, and "returns false" is equally
// satisfied by a stub that never consults the keyboard at all. That weakness is the
// documented reason this seam was scheduled after the CRT one, and no amount of test
// writing removes it: proving those two right needs a human holding a key down.
//
// It CAN prove the part that actually decides behaviour. Every one of the 17 call
// sites this seam replaces made the same decision -- `(state & 0x8000) != 0` -- and
// getting THAT wrong is the realistic bug, because the neighbouring low bit is the
// sticky Caps-Lock-style toggle. Reading the toggle bit instead would make Shift+Tab
// depend on whether Caps Lock happened to be on. modifierHeld is pure, so it is
// asserted over its ENTIRE input domain below, on both platforms.
//
// Worth being precise about what that buys on Windows in particular: modifierHeld is
// the Windows backend's own decode step, not a portable convenience. So these
// assertions are a Linux run proving the expression the WINDOWS half depends on --
// which is the direction of travel this seam architecture usually runs the other way.
//
// The exhaustive sweep deliberately does NOT compare against `(v & 0x8000) != 0`,
// which would be the implementation restating itself and would pass no matter which
// bit the implementation read. It compares against an INDEPENDENT characterisation:
// a `short` with bit 15 set is exactly a `short` that is negative. Two different ways
// of saying the same thing is what makes the agreement mean something.
//
// THIS FILE LOST HALF ITS ASSERTIONS ON PURPOSE, from 131,086 to 65,545. It used to
// sweep a second predicate, keyToggled, over the same domain against "is odd". That
// function has been deleted -- include/keyboard.h says why -- and the assertions went
// with it. Nothing was weakened: the claim they were supposed to support is that
// modifierHeld reads bit 15 and NOT the adjacent toggle bit, and the sweep below
// already settles that on its own. An implementation reading bit 0 disagrees with
// `(s < 0)` on 32,768 of the 65,536 values. The two named checks on 0x0001 and 0x8001
// pin the same thing by hand. A halved count that proves the same thing is the honest
// reading, and it is recorded here because a count falling is otherwise indistinguishable
// from coverage being lost.

#include "keyboard.h"

#include <iostream>
#include <limits>

int failures = 0;
long long checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

int main() {
    using platform::modifierHeld;

    // --- the shapes Windows actually documents -------------------------------
    // GetKeyState returns the high bit set while a key is down, and the low bit as
    // the toggle state. These four spell out the whole contract in the form a reader
    // of the Windows docs would recognise, before the sweep generalises it. The middle
    // two are the ones that matter: 0x8001 is held WITH Caps Lock on and must read as
    // held, 0x0001 is Caps Lock on with nothing held and must not.
    check(modifierHeld(static_cast<short>(0x8000)), "0x8000: key down");
    check(modifierHeld(static_cast<short>(0x8001)), "0x8001: key down and toggled");
    check(!modifierHeld(static_cast<short>(0x0001)), "0x0001: toggled but NOT down");
    check(!modifierHeld(static_cast<short>(0x0000)), "0x0000: neither");

    // The value a real GetKeyState hands over for a held key, spelled as the signed
    // quantity the function actually returns rather than as a hex mask. This is the
    // form the seam has to cope with, and the reason keyboard.h takes `short` and not
    // `unsigned short`: sign extension has to leave bit 15 where the mask can see it.
    check(modifierHeld(std::numeric_limits<short>::min()),
          "the most negative short is a held key (0x8000 as GetKeyState returns it)");
    check(modifierHeld(static_cast<short>(-1)),
          "-1 (all bits set) is a held key");
    check(!modifierHeld(std::numeric_limits<short>::max()),
          "the largest positive short (0x7FFF) is NOT held, though every other bit is");

    // --- exhaustive over the whole domain ------------------------------------
    // All 65,536 values, each checked against an independent formulation. A mismatch
    // is reported once with the offending value, rather than 65,536 times, so a broken
    // implementation produces a readable failure instead of a wall of output -- but the
    // COUNT below still includes every comparison, which is what makes the printed
    // number a fingerprint of the sweep having actually run.
    //
    // This is the assertion an implementation reading the wrong bit fails, and it fails
    // it loudly: `(keyState & 0x0001) != 0` disagrees with `(s < 0)` on exactly half the
    // domain, so the report would name 32768.
    int heldMismatch = 0;
    short firstHeldBad = 0;
    for (int v = -32768; v <= 32767; ++v) {
        const short s = static_cast<short>(v);
        // bit 15 set <=> negative, for a two's-complement short
        if (modifierHeld(s) != (s < 0)) {
            if (heldMismatch == 0) firstHeldBad = s;
            heldMismatch++;
        }
        checks++;
    }
    if (heldMismatch != 0) {
        std::cerr << "FAIL: modifierHeld disagrees with (s < 0) on " << heldMismatch
                  << " of 65536 values, first at " << firstHeldBad << '\n';
        failures++;
    }

    // --- the platform halves, and the limit of what they can be asked -------
    // NOT a proof that either backend works. Nothing is held, so false is the right
    // answer and a stub returning false unconditionally passes these two as well.
    // They are here for what they DO prove, which is not nothing: that the seam
    // resolves at link time on this platform, that the chosen backend file was
    // actually compiled in, and that calling it does not crash. On Windows that
    // means GetKeyState through user32; off Windows it means
    // QGuiApplication::keyboardModifiers() with no QGuiApplication constructed,
    // which Qt answers from cached state rather than by failing.
    check(!platform::isShiftDown(), "no Shift is held while a test binary runs");
    check(!platform::isCtrlDown(), "no Ctrl is held while a test binary runs");

    // --- the event scope, which a stub CAN fail ------------------------------
    // Unlike the two checks above, these go through the linked backend with an answer that
    // is not false, so a backend that ignores EventModifierScope fails them on whichever
    // platform it is. The two keys are set to DIFFERENT values in each scope, so a backend
    // that swapped them, or read one scope's shift as the other's ctrl, fails too.
    {
        platform::EventModifierScope outer(true, false);
        check(platform::isShiftDown() && !platform::isCtrlDown(),
              "an open scope answers for the event: Shift held, Ctrl not");
        {
            platform::EventModifierScope inner(false, true);
            check(!platform::isShiftDown() && platform::isCtrlDown(),
                  "a nested scope replaces the outer one's answer");
        }
        check(platform::isShiftDown() && !platform::isCtrlDown(),
              "closing the nested scope restores the outer one, not the platform call");
    }
    check(!platform::isShiftDown() && !platform::isCtrlDown(),
          "with every scope closed the answer is the platform's again");
    check(platform::EventModifierScope::current() == nullptr,
          "and no scope is left installed");

    if (failures == 0) {
        std::cout << "keyboard: all " << checks << " assertions passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s) out of " << checks << " assertions\n";
    return 1;
}
