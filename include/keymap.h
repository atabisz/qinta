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
//
// THREE MORE ON 2026-09-17, from src/editor.cpp, and one of them is worth pointing at:
// VK_UP. This block already held VK_LEFT (0x25), VK_RIGHT (0x27) and VK_DOWN (0x28) --
// THREE of the four arrow keys, with the gap at 0x26 -- because no site measured before
// today named VK_UP. editor.cpp names it once. That gap is the same
// no-site-no-declaration rule working correctly rather than an oversight, and it is
// recorded because a sorted list with one value missing from a run of four looks like a
// typo to the next reader; it was a measurement.
//
// The other two are VK_PRIOR (0x21) and VK_NEXT (0x22) -- Page Up and Page Down, the
// SDK's names, which do not resemble the keys and so are easy to mistake for each
// other. Their static_asserts pin the literals in that order deliberately: transposing
// them would compile, would pass any test that only checks a code is DEFINED, and would
// scroll the editor the wrong way. Same argument the VK_LEFT/VK_RIGHT swap experiment
// below records.
//
// THREE MORE ON 2026-09-20, from src/input.cpp, which is the file this block was always
// going to end at: VK_F4 (0x73, Ctrl+F4 closes the tab, :3562), VK_F10 (0x79, closes the
// context menu, :3447 and :3529) and VK_OEM_COMMA (0xBC, Ctrl+, opens settings, :3497,
// :3537, :3806). Six sites, three codes.
//
// Two things about this batch are worth pointing at, both of them shapes that mislead a
// reader rather than the compiler.
//
// VK_F10 has a SECOND user, src/main_d2d.cpp:1169 -- and that site is not why it is here.
// main_d2d.cpp is Windows-only by construction and takes the <windows.h> arm below, so it
// has never needed a fallback and never will. A reader who greps VK_F10, finds two sites
// and concludes the Direct2D file justified the admission would have the rule backwards:
// the code is admitted because a file DESTINED TO BE PORTABLE names it. Windows-only sites
// are invisible to this block on purpose.
//
// VK_OEM_COMMA is 0xBC and sits three below the VK_OEM_2 (0xBF) already here, in a range
// whose names say nothing about which key they are: 0xBA OEM_1, 0xBB OEM_PLUS, 0xBC
// OEM_COMMA, 0xBD OEM_MINUS, 0xBE OEM_PERIOD, 0xBF OEM_2. Picking 0xBE would compile,
// would satisfy any test that only checks the macro exists, and would open Settings on
// Ctrl+. instead of Ctrl+, -- the exact failure the VK_PRIOR/VK_NEXT note above describes,
// in a range that offers four more chances to make it. The literal static_assert is what
// catches that, on the Windows arm, against the SDK.
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
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_DELETE 0x2E
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F4 0x73
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_OEM_COMMA 0xBC
#define VK_OEM_2 0xBF
#endif
static_assert(VK_BACK == 0x08, "VK_BACK must match the Windows virtual-key code");
static_assert(VK_TAB == 0x09, "VK_TAB must match the Windows virtual-key code");
static_assert(VK_RETURN == 0x0D, "VK_RETURN must match the Windows virtual-key code");
static_assert(VK_SHIFT == 0x10, "VK_SHIFT must match the Windows virtual-key code");
static_assert(VK_CONTROL == 0x11, "VK_CONTROL must match the Windows virtual-key code");
static_assert(VK_ESCAPE == 0x1B, "VK_ESCAPE must match the Windows virtual-key code");
static_assert(VK_SPACE == 0x20, "VK_SPACE must match the Windows virtual-key code");
static_assert(VK_PRIOR == 0x21, "VK_PRIOR (Page Up) must match the Windows virtual-key code");
static_assert(VK_NEXT == 0x22, "VK_NEXT (Page Down) must match the Windows virtual-key code");
static_assert(VK_END == 0x23, "VK_END must match the Windows virtual-key code");
static_assert(VK_HOME == 0x24, "VK_HOME must match the Windows virtual-key code");
static_assert(VK_LEFT == 0x25, "VK_LEFT must match the Windows virtual-key code");
static_assert(VK_UP == 0x26, "VK_UP must match the Windows virtual-key code");
static_assert(VK_RIGHT == 0x27, "VK_RIGHT must match the Windows virtual-key code");
static_assert(VK_DOWN == 0x28, "VK_DOWN must match the Windows virtual-key code");
static_assert(VK_DELETE == 0x2E, "VK_DELETE must match the Windows virtual-key code");
static_assert(VK_F1 == 0x70, "VK_F1 must match the Windows virtual-key code");
static_assert(VK_F2 == 0x71, "VK_F2 must match the Windows virtual-key code");
static_assert(VK_F4 == 0x73, "VK_F4 must match the Windows virtual-key code");
static_assert(VK_F10 == 0x79, "VK_F10 must match the Windows virtual-key code");
static_assert(VK_F11 == 0x7A, "VK_F11 must match the Windows virtual-key code");
static_assert(VK_F12 == 0x7B, "VK_F12 must match the Windows virtual-key code");
static_assert(VK_OEM_COMMA == 0xBC,
              "VK_OEM_COMMA (the , key) must match the Windows virtual-key code -- NOT "
              "0xBE, which is VK_OEM_PERIOD and would bind Ctrl+. instead");
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
