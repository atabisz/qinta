#include "keyboard.h"

#include <windows.h>

// The Windows half of include/keyboard.h. Two lines of mechanism, and they are the
// literal expression that stood at all 17 call sites before this seam existed --
// GetKeyState for the key, modifierHeld for the bit test -- so the rewire is a
// behaviour-preserving move and not an improvement in disguise.
//
// There is nothing to fail here and nothing to check: GetKeyState has no error
// return, reporting an unheld key and an unknown key identically as zero. That is
// why this file has no error handling rather than missing it.
//
// The scope check comes first for qinta, which links this file on Windows and routes Qt
// events that GetKeyState cannot see (include/keyboard.h). tinta's WinMain opens no scope, so
// on that stack the check is always false and the answer is GetKeyState's, as before.
namespace platform {

bool isShiftDown() {
    if (const EventModifierScope* scope = EventModifierScope::current()) return scope->shift();
    return modifierHeld(GetKeyState(VK_SHIFT));
}

bool isCtrlDown() {
    if (const EventModifierScope* scope = EventModifierScope::current()) return scope->ctrl();
    return modifierHeld(GetKeyState(VK_CONTROL));
}

} // namespace platform
