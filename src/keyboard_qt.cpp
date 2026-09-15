#include "keyboard.h"

#include <QGuiApplication>

// The Qt half of include/keyboard.h, and the reason that header is a seam rather than
// an entry in win32_api.h: this is a real implementation of the same idea, not a
// stand-in that returns false so the build proceeds.
//
// keyboardModifiers() and NOT queryKeyboardModifiers(). The two differ only while
// keys are physically moving, which makes the wrong choice invisible in any test and
// wrong in exactly the situation the code exists for. The census in keyboard.h settles
// it: every one of the 17 sites this replaces reads a modifier while handling an input
// event, and not one uses GetAsyncKeyState, whose Qt counterpart is the query form.
//
// modifierHeld does not appear here, and that asymmetry is correct rather than an
// oversight. It decodes a Windows key-state WORD; Qt hands over a typed flag set with
// no such word behind it, so reaching for the bit test would mean manufacturing a
// Windows-shaped value in order to take it apart again. Both files answer the same
// question; only one of them has a bitfield to decode.
//
// With no QGuiApplication constructed -- which is the case in a plain test binary --
// these return false. That is Qt's documented behaviour for the cached state and it is
// the honest answer: nothing is held, because there is no window system connection to
// hold anything. It is also why the assertions that matter in tests/keyboard_tests.cpp
// are on modifierHeld and not on these two.
namespace platform {

bool isShiftDown() {
    return QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
}

bool isCtrlDown() {
    return QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
}

} // namespace platform
