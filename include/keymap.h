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
// The asserts below need BOTH builds to mean anything, which is precisely why
// the portable-core CI job exists: on Windows they check the literals against
// the SDK's own macros, and off Windows they check these fallbacks against the
// same literals. Neither build alone proves the fallbacks match Windows; run
// together they do. A fallback that drifts therefore fails the Linux/macOS job,
// and a literal that drifts fails the Windows one.
#ifdef _WIN32
#include <windows.h>
#else
#define VK_TAB 0x09
#define VK_SPACE 0x20
#define VK_DOWN 0x28
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_OEM_2 0xBF
#endif
static_assert(VK_TAB == 0x09, "VK_TAB must match the Windows virtual-key code");
static_assert(VK_SPACE == 0x20, "VK_SPACE must match the Windows virtual-key code");
static_assert(VK_DOWN == 0x28, "VK_DOWN must match the Windows virtual-key code");
static_assert(VK_F1 == 0x70, "VK_F1 must match the Windows virtual-key code");
static_assert(VK_F2 == 0x71, "VK_F2 must match the Windows virtual-key code");
static_assert(VK_F11 == 0x7A, "VK_F11 must match the Windows virtual-key code");
static_assert(VK_F12 == 0x7B, "VK_F12 must match the Windows virtual-key code");
static_assert(VK_OEM_2 == 0xBF, "VK_OEM_2 must match the Windows virtual-key code");
// F1..F12 are contiguous, which keymap.cpp relies on for its F-key arithmetic
static_assert(VK_F12 - VK_F1 == 11, "F-key codes must stay contiguous");

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
