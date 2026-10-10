// The built-in palette table and the themes.ini round-trip, built and run off
// Windows.
//
// WHY THIS TARGET EXISTS
//
// src/themes.cpp is 451 lines of pure data plus an ini reader and writer, and it
// touches no App member at all -- yet until 2026-09-10 it could not compile
// anywhere but Windows, because `#include "app.h"` on its first line pulled in
// windows.h, d2d1.h, dwrite.h and wincodec.h. The block it actually wanted
// (D2DTheme and the theme registry) moved to include/theme_data.h; this target is
// the proof that what remains needs no Windows.
//
// Three things in this file were MSVC-only until that move and are checked here
// rather than asserted in a comment:
//   - std::ofstream/ifstream given a bare std::wstring path. MSVC accepts it as
//     an extension; libstdc++ has no such constructor. Now a
//     std::filesystem::path.
//   - _wcsicmp, an MSVC extension with no POSIX spelling, used to decide whether
//     saveCustomTheme replaces an existing theme or appends a new one. Now
//     themeNamesEqual.
//   - hexColor, which duplicated the shift-and-mask in tinta::colorFromHex and
//     now delegates to it.
//
// Runs in portable mode via tests/run_portable_test.cmake, which copies the
// binary somewhere scratch and puts a settings.ini beside it. Without that,
// saveCustomTheme below would rewrite the user's real %APPDATA%\Tinta\themes.ini
// and destroy their custom themes -- so this test refuses to run unless it can
// prove the config home is the scratch directory.

#include "theme_data.h"
#include "config_dir.h"
#include "platform_paths.h"

#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

void checkNear(float actual, float expected, const char* message) {
    check(actual > expected - 0.0001f && actual < expected + 0.0001f, message);
}

// Hand-computed rather than run through hexColor, so this compares the unpack
// against arithmetic done independently of the code under test.
float channel(int byteValue) { return byteValue / 255.0f; }

} // namespace

int main() {
    namespace fs = std::filesystem;

    // --- Isolation guard, not a nicety ---------------------------------------
    //
    // saveCustomTheme() rewrites themes.ini in the config home. If portable mode
    // were not active that would be the user's real one. Proving isolation by
    // comparing tintaConfigDir() against the executable's own directory is
    // stronger than looking for a marker file: portable mode is DEFINED as "the
    // config home is the exe's folder", so this asserts the property itself
    // rather than one of its causes.
    const std::wstring configDir = tintaConfigDir();
    const std::wstring exeDir = platform::executableDirectory();
    check(!configDir.empty(), "tintaConfigDir resolves a config home");
    check(!exeDir.empty(), "platform::executableDirectory resolves");
    if (configDir.empty() || exeDir.empty() || fs::path(configDir) != fs::path(exeDir)) {
        std::cerr << "FAIL: config home is not the executable directory, so portable\n"
                     "      mode is not active. Refusing to run: this test writes\n"
                     "      themes.ini and would clobber a real configuration.\n"
                     "      Run it through tests/run_portable_test.cmake.\n";
        return 2;
    }
    check(true, "portable mode is active, so themes.ini writes are isolated");

    // --- The built-in table --------------------------------------------------
    check(THEME_COUNT == 10, "there are 10 built-in themes, 5 light and 5 dark");

    // Names and fonts. wcscmp rather than == because these are const wchar_t*.
    check(std::wcscmp(THEMES[0].name, L"Paper") == 0, "theme 0 is Paper");
    check(std::wcscmp(THEMES[9].name, L"Abyss") == 0, "theme 9 is Abyss");
    check(!THEMES[0].isDark, "Paper is a light theme");
    check(THEMES[9].isDark, "Abyss is a dark theme");

    int darkCount = 0;
    for (int i = 0; i < THEME_COUNT; i++) {
        if (THEMES[i].isDark) darkCount++;
        check(THEMES[i].name != nullptr && THEMES[i].name[0] != L'\0',
              "every built-in theme has a non-empty name");
        check(THEMES[i].fontFamily != nullptr && THEMES[i].fontFamily[0] != L'\0',
              "every built-in theme names a main font");
        check(THEMES[i].codeFontFamily != nullptr && THEMES[i].codeFontFamily[0] != L'\0',
              "every built-in theme names a monospace font");
    }
    check(darkCount == 5, "exactly 5 of the 10 built-ins are dark");

    // --- hexColor unpacking --------------------------------------------------
    //
    // Paper's background is written hexColor(0xF5F1E8) in the table. hexColor now
    // delegates to tinta::colorFromHex, so this is checking that the delegation
    // did not change which byte lands in which channel -- the failure a wrong
    // shift produces is a plausible-looking colour, not an obvious one.
    checkNear(THEMES[0].background.r, channel(0xF5), "Paper background red is 0xF5/255");
    checkNear(THEMES[0].background.g, channel(0xF1), "Paper background green is 0xF1/255");
    checkNear(THEMES[0].background.b, channel(0xE8), "Paper background blue is 0xE8/255");
    checkNear(THEMES[0].background.a, 1.0f, "hexColor leaves the table's colours opaque");
    checkNear(THEMES[6].background.r, channel(0x28), "Dracula background red is 0x28/255");
    checkNear(THEMES[9].background.r, 0.0f, "Abyss background is pure black");

    // --- themeAt clamping ---------------------------------------------------
    check(&themeAt(0) == &THEMES[0], "themeAt(0) is the first built-in");
    check(&themeAt(9) == &THEMES[9], "themeAt(9) is the last built-in");
    check(&themeAt(-1) == &THEMES[0], "themeAt clamps a negative index to theme 0");
    check(&themeAt(9999) == &THEMES[0], "themeAt clamps an out-of-range index to theme 0");

    // --- themeHeadingFont inheritance (#155) --------------------------------
    //
    // Paper leaves headingFontFamily null, meaning "headings use font=", so
    // setting font= alone restyles the whole document. Abyss sets both.
    check(THEMES[0].headingFontFamily == nullptr, "Paper leaves headingFontFamily null");
    check(std::wcscmp(themeHeadingFont(THEMES[0]), kDefaultFont) == 0,
          "a null headingFontFamily inherits the main font");
    check(std::wcscmp(themeHeadingFont(THEMES[9]), kDefaultFont) == 0,
          "an explicit headingFontFamily is used as given");
    check(std::wcscmp(THEMES[9].fontFamily, kDefaultFontLight) == 0,
          "...and is distinct from that theme's main font, so the test can tell them apart");

    // --- code faces per platform (platformCodeFont) -------------------------
    //
    // Off Windows no built-in may name Consolas or Cascadia: Qt aliases a missing
    // family on first lookup and logs the cost (theme_data.h). On Windows the themes
    // keep their distinct faces, so the mapping must not have flattened them.
#if defined(_WIN32)
    check(std::wcscmp(THEMES[0].codeFontFamily, L"Consolas") == 0,
          "Paper keeps Consolas on Windows");
    check(std::wcscmp(THEMES[5].codeFontFamily, L"Cascadia Code") == 0,
          "Midnight keeps Cascadia Code on Windows");
    check(std::wcscmp(THEMES[9].codeFontFamily, L"Cascadia Mono") == 0,
          "Abyss keeps Cascadia Mono on Windows");
#else
    for (int i = 0; i < THEME_COUNT; i++) {
        check(std::wcscmp(THEMES[i].codeFontFamily, kDefaultCodeFont) == 0,
              "every built-in code face is kDefaultCodeFont off Windows");
    }
#endif

    // --- themeInlineCodeColor (#197) ----------------------------------------
    check(!THEMES[0].inlineCode.has_value(), "no built-in sets an inlineCode override");
    checkNear(themeInlineCodeColor(THEMES[0]).r, THEMES[0].code.r,
              "a missing inlineCode inherits the code colour");

    // --- themes.ini round-trip ----------------------------------------------
    //
    // This is the part that exercises the wide-path streams, the UTF-8 seam and
    // the hex parse/format pair. Start from a clean slate: the scratch directory
    // is fresh per configure, but loadCustomThemes() first so the registry state
    // is known rather than assumed.
    loadCustomThemes();
    check(themeCount() == THEME_COUNT,
          "no custom themes are registered before this test writes any");

    D2DTheme custom = THEMES[0];
    custom.isDark = true;
    custom.background = hexColor(0x2E3440);
    custom.text = hexColor(0xD8DEE9);
    custom.inlineCode = hexColor(0x88C0D0);
    const int savedIndex = saveCustomTheme(custom, L"Nordish", L"Fira Sans", L"Fira Code");
    check(savedIndex == THEME_COUNT, "saveCustomTheme returns the new theme's registry index");
    check(themeCount() == THEME_COUNT + 1, "the custom theme is registered");

    const fs::path iniPath = fs::path(configDir) / "themes.ini";
    check(fs::exists(iniPath), "saveCustomTheme wrote themes.ini through a wide path");

    // Re-read from disk. This is the assertion that the writer and the reader
    // agree, which is the only claim a round-trip can actually settle.
    loadCustomThemes();
    check(themeCount() == THEME_COUNT + 1, "the custom theme survives a reload");
    const D2DTheme& reloaded = themeAt(THEME_COUNT);
    check(std::wcscmp(reloaded.name, L"Nordish") == 0, "the reloaded theme keeps its name");
    check(reloaded.isDark, "the reloaded theme keeps dark=1");
    check(std::wcscmp(reloaded.fontFamily, L"Fira Sans") == 0, "the reloaded theme keeps font=");
    check(std::wcscmp(reloaded.codeFontFamily, L"Fira Code") == 0,
          "the reloaded theme keeps codefont=");
    checkNear(reloaded.background.r, channel(0x2E), "background survives hex write and re-parse");
    checkNear(reloaded.background.g, channel(0x34), "...green channel too");
    checkNear(reloaded.background.b, channel(0x40), "...and blue");
    checkNear(reloaded.text.r, channel(0xD8), "text colour survives the round-trip");
    check(reloaded.inlineCode.has_value(), "an inlineCode override survives the round-trip");
    if (reloaded.inlineCode) {
        checkNear(reloaded.inlineCode->b, channel(0xD0), "...with the right channel values");
    }

    // headingfont= is omitted when empty, and an absent one must come back empty
    // rather than null -- bindStrings points it at an owned wstring either way,
    // and themeHeadingFont resolves empty to the main font.
    check(reloaded.headingFontFamily != nullptr,
          "an omitted headingfont= still leaves a non-null pointer");
    check(reloaded.headingFontFamily[0] == L'\0', "...and it is empty");
    check(std::wcscmp(themeHeadingFont(reloaded), L"Fira Sans") == 0,
          "an empty headingFontFamily inherits the main font, as a null one does");

    // --- Case-insensitive replacement (the _wcsicmp replacement) -------------
    //
    // Saving under a differently-cased name must REPLACE the existing theme, not
    // append a second one. If themeNamesEqual compared case-sensitively this
    // would silently grow the registry and write two [theme] sections with the
    // same name, and the reader would then load both.
    D2DTheme recased = reloaded;
    recased.background = hexColor(0x111111);
    const int recasedIndex = saveCustomTheme(recased, L"NORDISH", L"Fira Sans", L"Fira Code");
    check(recasedIndex == THEME_COUNT, "saving under a recased name returns the SAME index");
    check(themeCount() == THEME_COUNT + 1,
          "saving under a recased name replaces rather than appends");
    loadCustomThemes();
    check(themeCount() == THEME_COUNT + 1, "...and still only one section is on disk");
    checkNear(themeAt(THEME_COUNT).background.r, channel(0x11),
              "the replacement's colours are what persisted");

    // --- A hand-authored ini ------------------------------------------------
    //
    // Users write this file by hand, so the reader has to cope with a section
    // that sets a few keys and omits the rest (missing colours fall back to
    // Paper), with comments, with blank lines, and with a '#' prefix on a hex
    // value. Written directly rather than through the writer, because the writer
    // never produces this shape and it is the reader that has to be tolerant.
    {
        std::ofstream handAuthored(iniPath);
        check(handAuthored.good(), "the scratch themes.ini is writable");
        handAuthored << "; a comment line\n"
                        "\n"
                        "[theme]\n"
                        "name=Minimal\n"
                        "dark=1\n"
                        "background=#0A0B0C\n"
                        "inlinecode=FFEE00\n"
                        "unknownkey=ignored\n"
                        "malformed-line-with-no-equals\n";
    }
    loadCustomThemes();
    check(themeCount() == THEME_COUNT + 1, "the hand-authored section loaded as one theme");
    const D2DTheme& minimal = themeAt(THEME_COUNT);
    check(std::wcscmp(minimal.name, L"Minimal") == 0, "the hand-authored name loaded");
    checkNear(minimal.background.r, channel(0x0A), "a leading '#' on a hex value is accepted");
    checkNear(minimal.text.r, THEMES[0].text.r, "an omitted colour falls back to Paper's");
    check(std::wcscmp(minimal.fontFamily, kDefaultFont) == 0,
          "an omitted font= falls back to the platform default font");
    check(std::wcscmp(minimal.codeFontFamily, kDefaultCodeFont) == 0,
          "an omitted codefont= falls back to the platform default code font");
    check(minimal.inlineCode.has_value(), "inlinecode= is read for a hand-authored theme");

    // A section with no name= is not a theme and must be dropped, not registered
    // with an empty name -- the theme picker would show a blank row.
    {
        std::ofstream nameless(iniPath);
        nameless << "[theme]\ndark=1\nbackground=010203\n";
    }
    loadCustomThemes();
    check(themeCount() == THEME_COUNT, "a [theme] section with no name= is dropped");

    std::cout << (failures == 0 ? "all " : "") << checks << " theme-data assertions run, "
              << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
