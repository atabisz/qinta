#ifndef TINTA_KEYMAP_H
#define TINTA_KEYMAP_H

#include <string>
#include <utility>
#include <vector>

// Virtual-key codes are the internal key representation. On Windows they come
// from <windows.h>; elsewhere the same numeric values are reproduced so keymap
// logic and its tests build unchanged off Windows. Values read from the Windows
// SDK's WinUser.h.
//
// The asserts below need BOTH builds to mean anything: on Windows they check the
// literals against the SDK's own macros, and off Windows they check these
// fallbacks against the same literals. Neither build alone proves the fallbacks
// match Windows; run together they do.
//
// BOTH HALVES RUN AGAIN, so these asserts are live. An earlier version of this
// comment warned that only the Windows half executed anywhere, because the CI job
// that built the other one was removed on 2026-09-10. That is no longer true: the
// portable Linux build compiles keymap.cpp (it is listed in
// tests/portable-sources.txt, whose manifest check verifies in both directions),
// so every assert below is evaluated with _WIN32 undefined on each of those runs.
//
// The warning is corrected rather than deleted because it failed in the dangerous
// direction: telling a reader that a guard is dead invites removing it. If the
// portable build is ever dropped again, restore the warning -- and note that no
// single-platform build can prove these values, since the Windows half checks the
// literals against the SDK and the other half checks the fallbacks against the
// same literals. Only the two together close the loop.
// The block is kept in ASCENDING VALUE order, which is a legibility decision with a
// purpose: the question a reader actually has here is "is the code I need present
// off Windows", and a sorted list answers it by scanning. The ten codes added on
// 2026-09-15 were merged into the order rather than appended as a second block for
// that reason -- the eight original VALUES are unchanged, and the static_asserts
// below are what proves that, since every one of them pins a literal against the
// SDK on the Windows build.
//
// The ten new codes are not speculative coverage. Each is named by src/tableedit.cpp,
// which was the last src/ file whose ONLY off-Windows errors were missing key codes:
// 12 g++ errors, 10 of them a VK_ constant and 2 the GetKeyState call that
// include/keyboard.h now owns. Adding a code with no site behind it was refused for
// the same reason six scalars were refused from win32_types.h -- a declaration with
// no error behind it is a claim nothing tests.
#ifdef _WIN32
#include <windows.h>
#else
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_DELETE 0x2E
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_OEM_2 0xBF
#endif
static_assert(VK_BACK == 0x08, "VK_BACK must match the Windows virtual-key code");
static_assert(VK_TAB == 0x09, "VK_TAB must match the Windows virtual-key code");
static_assert(VK_RETURN == 0x0D, "VK_RETURN must match the Windows virtual-key code");
static_assert(VK_SHIFT == 0x10, "VK_SHIFT must match the Windows virtual-key code");
static_assert(VK_CONTROL == 0x11, "VK_CONTROL must match the Windows virtual-key code");
static_assert(VK_ESCAPE == 0x1B, "VK_ESCAPE must match the Windows virtual-key code");
static_assert(VK_SPACE == 0x20, "VK_SPACE must match the Windows virtual-key code");
static_assert(VK_END == 0x23, "VK_END must match the Windows virtual-key code");
static_assert(VK_HOME == 0x24, "VK_HOME must match the Windows virtual-key code");
static_assert(VK_LEFT == 0x25, "VK_LEFT must match the Windows virtual-key code");
static_assert(VK_RIGHT == 0x27, "VK_RIGHT must match the Windows virtual-key code");
static_assert(VK_DOWN == 0x28, "VK_DOWN must match the Windows virtual-key code");
static_assert(VK_DELETE == 0x2E, "VK_DELETE must match the Windows virtual-key code");
static_assert(VK_F1 == 0x70, "VK_F1 must match the Windows virtual-key code");
static_assert(VK_F2 == 0x71, "VK_F2 must match the Windows virtual-key code");
static_assert(VK_F11 == 0x7A, "VK_F11 must match the Windows virtual-key code");
static_assert(VK_F12 == 0x7B, "VK_F12 must match the Windows virtual-key code");
static_assert(VK_OEM_2 == 0xBF, "VK_OEM_2 must match the Windows virtual-key code");
// F1..F12 are contiguous, which keymap.cpp relies on for its F-key arithmetic
static_assert(VK_F12 - VK_F1 == 11, "F-key codes must stay contiguous");
// The F-key assert above is the ONLY relationship assert here, and that is a measured
// decision rather than an omission. It exists because src/keymap.cpp really does the
// arithmetic -- `VK_F1 + n - 1` at :17 and `key - VK_F1 + 1` at :27 -- so contiguity
// is a contract some code depends on, not merely a fact about the numbers.
//
// Two matching asserts for the arrow keys (0x25..0x28) were written here and then
// REMOVED, because both halves of their justification turned out to be false. Nothing
// in the tree does arrow-key arithmetic: all nine sites are plain `case VK_LEFT:`
// labels in annotations.cpp, editor.cpp and input.cpp. And the stated reason -- that a
// transposed pair would slip past the literal asserts, since each value would still
// match itself -- is simply wrong. Swapping VK_LEFT and VK_RIGHT was tried, and the
// two LITERAL asserts fired immediately, because after a swap VK_LEFT is 0x27 and no
// longer equals its own literal. Predicted 2 failures from the contiguity asserts,
// measured 4, and the extra 2 were the literal asserts doing the job I had just
// claimed they could not do.
//
// Recorded rather than quietly dropped, because the removal is the point: an assert
// with no dependency behind it is the same defect as an off-Windows key code with no
// call site behind it, which the block above refuses by name. Applying that rule to
// someone else's declarations and not to my own would make it a preference.

// A virtual key and a typed character are different namespaces. In
// particular, VK_F1 is also the Unicode value of 'p', and VK_F12 of '{'.
struct KeyBinding {
    unsigned key = 0;
    bool isChar = false;
};

struct KeyActionDef {
    const char* name;
    unsigned defaultKey;
    bool isChar;
};
inline constexpr KeyActionDef KEY_ACTIONS[] = {
    {"search", 'F', false}, {"browse", 'B', false},
    {"toc", VK_TAB, false}, {"theme", 'T', false},
    {"stats", 'S', false}, {"quit", 'Q', false},
    {"newfile", 'N', false}, {"zen", VK_F11, false},
    {"scrollup", 'K', false}, {"scrolldown", 'J', false},
    {"edit", ':', true}, {"help", '?', true},
    {"annotate", 'A', false},
};
inline constexpr int KEY_ACTION_COUNT = 13;
enum KeyActionIndex {
    KA_SEARCH = 0, KA_BROWSE, KA_TOC, KA_THEME, KA_STATS, KA_QUIT,
    KA_NEWFILE, KA_ZEN, KA_SCROLLUP, KA_SCROLLDOWN, KA_EDIT, KA_HELP,
    KA_ANNOTATE
};

struct KeyProfileDef {
    const char* id;
    KeyBinding keys[KEY_ACTION_COUNT];
};
inline constexpr KeyProfileDef KEY_PROFILES[] = {
    {"windows", {{'F'}, {'B'}, {VK_TAB}, {'T'}, {'S'}, {'Q'}, {'N'},
                 {VK_F11}, {'K'}, {'J'}, {'E'}, {VK_F1}, {'A'}}},
    {"vim", {{'/', true}, {'B'}, {VK_TAB}, {'T'}, {'S'}, {'Q'}, {'N'},
             {VK_F11}, {'K'}, {'J'}, {':', true}, {'?', true}, {'A'}}},
};
inline constexpr int KEY_PROFILE_COUNT = 2;

int keyProfileIndexById(const std::string& id);
KeyBinding parseKeyName(const std::string& value);
std::string keyIniName(KeyBinding binding);
std::wstring keyLabel(KeyBinding binding);
void resolveKeymap(KeyBinding (&keys)[KEY_ACTION_COUNT], const std::string& profile,
                   const std::vector<std::pair<std::string, std::string>>& overrides);
bool keyBindingMatches(KeyBinding binding, unsigned key, bool isChar);
// Convert to the legacy viewer command codes. Unbound characters produce
// no command; navigation virtual keys pass through unless explicitly moved.
unsigned translateActionKey(const KeyBinding* keys, unsigned key, bool isChar);

#endif
