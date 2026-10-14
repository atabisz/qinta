#ifndef TINTA_KEYBOARD_H
#define TINTA_KEYBOARD_H

// MODIFIER-KEY STATE, AT A SEAM WITH TWO REAL IMPLEMENTATIONS.
//
// This is deliberately NOT in win32_api.h, and the reason is that header's own
// preamble: "nothing here pretends to work ... off Windows these are no-ops or
// honest failures". That is the right home for a call with no non-Windows meaning.
// It is the wrong home for this one, because GetKeyState has a GENUINE equivalent:
// QGuiApplication::keyboardModifiers(). So this follows platform_paths.h instead --
// a declaration here, one .cpp per platform, and CMake picking the file, which is
// how the tree already says "both answers are real" rather than "one is a stand-in".
//
// The Qt mapping is settled by census, not by preference. All 17 GetKeyState sites
// in the tree read a modifier at the time an input event is being handled, and none
// of them uses GetAsyncKeyState. Qt splits exactly that way:
// QGuiApplication::keyboardModifiers() is the event-time state and matches
// GetKeyState; queryKeyboardModifiers() is the live hardware query and matches the
// GetAsyncKeyState this tree never calls. Picking the wrong one of that pair would
// compile, run, and be subtly wrong only while keys are moving -- so the fact that
// the census settles it is worth more than it looks.
//
// TWO PREDICATES, NOT A MODIFIER BITFIELD, by the same measurement argument that
// gave requestRepaint no rectangle: the 17 sites name exactly two keys, VK_SHIFT
// (11 sites) and VK_CONTROL (6). Neither Alt nor Meta appears, and no site tests a
// combination. A bitfield would be a wider API than anything asks for, and the
// first site that genuinely needs Alt should have to come here and say so.

namespace platform {

// Is the high bit of a Windows key-state word set?
//
// THIS IS SPLIT OUT FROM THE CALLS ON PURPOSE, and the purpose is testability. The
// two platform calls below cannot be asserted to say anything interesting in a test: no
// key is held while a test binary runs, so both correctly return false, and "returns
// false" is equally satisfied by a stub that ignores the keyboard entirely. That
// weakness was written down before this seam was built, as the reason it was
// scheduled second.
//
// It is narrowed here rather than accepted. The DECISION GetKeyState's callers make
// is not the syscall, it is the bit test: `(state & 0x8000) != 0`, repeated
// identically at all 17 sites, distinguishing "held down now" from the LOW bit,
// which is the sticky toggle state of Caps Lock and friends. That predicate is pure,
// so it can be asserted exhaustively on both platforms, and a stub cannot pass it.
// What remains untestable is only the syscall itself -- a much smaller claim than the
// whole seam, which is the point of the split.
//
// The parameter is `short`, not `unsigned short`, because GetKeyState returns SHORT
// and this must be bit-for-bit the expression it replaces. With a key held, the high
// bit is set and the value is NEGATIVE; integral promotion to int sign-extends, bit
// 15 survives, and the mask sees it. Taking unsigned short would give the same answer
// for every input, but it would no longer be the same expression, and matching the
// original exactly is the whole contract of a portability seam.
constexpr bool modifierHeld(short keyState) {
    return (keyState & 0x8000) != 0;
}

// A SECOND PREDICATE WAS WRITTEN HERE AND REMOVED, and the removal is recorded because
// it is the same rule this slice applied to somebody else's declarations.
//
// `keyToggled(short)` returned `(keyState & 0x0001) != 0` -- the LOW bit, which is the
// sticky toggle state of Caps Lock, Num Lock and Scroll Lock. Its own comment said "no
// site in this tree reads it. Named here, unused, purely so the distinction is
// documented", which is speculative coverage stated out loud. include/keymap.h refuses
// exactly that in the same commit, twice: ten VK_ codes were admitted because
// src/tableedit.cpp names each one, and two arrow-key static_asserts were deleted for
// having no dependency behind them. A rule applied to declarations I inherited and not
// to declarations I wrote would be a preference.
//
// Two things sharpened it beyond mere unusedness. Nothing anywhere reads the toggle bit
// -- `GetKeyState[^;]*& *0x0*1` matches zero sites. And Qt cannot answer the question at
// all: keyboardModifiers() reports modifiers, not lock state, so a portable-looking
// predicate for the toggle bit would be a portability claim the target stack cannot
// honour. That is worse than dead code, because it reads as available.
//
// modifierHeld stays, and the contrast is the useful part. It is not speculative: it is
// the Windows backend's OWN decode logic, hoisted out of the syscall so that a Linux run
// can assert the expression Windows depends on. keyToggled was neither backend's logic
// nor any caller's question. The distinction the deleted function existed to document is
// documented above, in the comment on modifierHeld, which is where prose belongs.

// Was Shift held when the event now being handled was generated?
bool isShiftDown();

// Was Ctrl held when the event now being handled was generated?
bool isCtrlDown();

// THE EVENT'S OWN ANSWER, FOR THE LENGTH OF ONE HANDLER. An input layer that holds the event
// opens one of these before calling into src/input.cpp, and while it is open both predicates
// above answer from it instead of from the platform call. The Qt backend, now the only one, honours it.
//
// Needed because neither platform call reliably sees a SYNTHESISED event. GetKeyState reads
// the thread's real key state and sees none of them. QGuiApplication::keyboardModifiers() is
// half right, measured on Qt 6.4.2 with a standalone probe: a sent KEY event updates it, a
// sent mouse or wheel event does not -- so a wheel after a sent Ctrl+J still reads Ctrl. And
// qinta on Windows linked keyboard_win32.cpp until 2026-10-07, so a scope only the Qt backend
// honoured would have fixed one host of two. The Qt window opens one from event->modifiers() for every event it
// routes, which makes the promise above true for sendEvent too; until this existed, slices 2
// and 3 kept Ctrl+O and the wheel out of the router and dropped the checks that would have
// measured them.
//
// Two bools and not a Qt::KeyboardModifiers, because this header was compiled by the Windows
// build with no Qt on the include path until 2026-10-07, and by the same measurement that made
// the predicates two: nothing asks about any other key. tinta's WinMain never opened one, so on
// that stack the predicates were exactly the GetKeyState calls they had been.
//
// A scope rather than a setter, because the answer is only true while the handler runs; each
// scope restores the one it displaced, so nesting is safe.
class EventModifierScope {
public:
    EventModifierScope(bool shift, bool ctrl) : previous_(current_), shift_(shift), ctrl_(ctrl) {
        current_ = this;
    }
    ~EventModifierScope() { current_ = previous_; }

    EventModifierScope(const EventModifierScope&) = delete;
    EventModifierScope& operator=(const EventModifierScope&) = delete;

    // The innermost open scope, or nullptr. Read by the two backends and by nothing else.
    static const EventModifierScope* current() { return current_; }
    bool shift() const { return shift_; }
    bool ctrl() const { return ctrl_; }

private:
    // inline: one pointer for the whole program, defined here, so neither backend owns it.
    static inline const EventModifierScope* current_ = nullptr;
    const EventModifierScope* previous_;
    bool shift_;
    bool ctrl_;
};

} // namespace platform

#endif // TINTA_KEYBOARD_H
