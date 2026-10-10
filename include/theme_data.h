#ifndef TINTA_THEME_DATA_H
#define TINTA_THEME_DATA_H

// The theme record and the theme registry, with no platform dependency.
//
// WHY THIS EXISTS
//
// This is the third block to come out of app.h by the same route as
// config_dir.h and settings_data.h: a struct that is pure data, but was
// reachable only through a header that includes windows.h, d2d1.h, dwrite.h and
// wincodec.h. Every consumer of a theme colour therefore inherited the whole
// Windows GUI stack.
//
// D2DTheme could not move until 2026-09-10, and settings_data.h says so in its
// own header comment: its colour members were D2D1_COLOR_F, a Windows SDK type,
// so the struct was Windows-only by its members even though nothing about a
// palette is. Replacing those with tinta::Color across the tree (1,929 sites,
// commit 0e54418) is what made this extraction possible, and it is why the block
// moved on that day and not before.
//
// The name D2DTheme is kept deliberately. It is wrong now -- there is nothing
// Direct2D about this struct any more -- but it appears at 200+ sites across 18
// files, and renaming it would bury the one interesting thing in this diff (the
// header moved; the type did not change) under a tree-wide rename. Rename it
// when something else is already touching those files.
//
// NOTHING here may include windows.h, app.h, or any Direct2D/DirectWrite header.
// src/themes.cpp depends on that -- it is in tests/portable-sources.txt and a
// non-Windows compiler reads it on every probe run -- and so does every future
// non-Windows build.
//
// On the wide strings: theme names and font families are wchar_t, matching the
// rest of the tree. That is not a portability problem in itself (wchar_t is
// standard, and 4 bytes rather than 2 off Windows), and it is not this header's
// job to prejudge the string migration. See the note in platform_paths.h.

#include <cstdint>
#include <optional>
#include <string>

#include "geometry.h"

// Default body/heading font, per platform. Every built-in theme in
// src/themes.cpp used to hardcode a literal "Segoe UI" ("Segoe UI Light" for
// Abyss's body) regardless of platform -- fine on Windows, where it ships, but
// on Qt/macOS and Qt/Linux that name resolves only through Qt's cross-platform
// alias table, built lazily on first lookup at a measured ~50ms cost and
// logged as "Populating font family aliases". These name REAL, installed
// families on each platform instead, so the lookup succeeds directly. Kept
// here rather than file-local to src/themes.cpp so tests/theme_data_tests.cpp
// can assert against the same source of truth instead of duplicating the
// platform conditional with its own literal, which would silently drift.
// The same rule covers the chrome: every site that pinned "Segoe UI" for UI text
// takes kDefaultFont / kDefaultFontLight, and every site that pinned "Consolas"
// takes kDefaultCodeFont, so no family Qt has to alias is named on startup.
//
// The monospace default is kDefaultCodeFont, and the per-theme choices go through
// platformCodeFont() below rather than being replaced by it. Consolas / Cascadia
// Code / Cascadia Mono are a deliberate Windows distinction between themes, so
// Windows keeps them; none of the three ships with macOS or a stock Linux, which
// is where the alias cost came back after the body fix -- measured 2026-10-10 on
// macOS 26.6.2: "Populating font family aliases took 35 ms ... missing font
// family "Cascadia Code"", from Midnight, the default theme's code face.
#if defined(_WIN32)
inline constexpr const wchar_t* kDefaultFont = L"Segoe UI";
inline constexpr const wchar_t* kDefaultFontLight = L"Segoe UI Light";
inline constexpr const wchar_t* kDefaultCodeFont = L"Consolas";
#elif defined(__APPLE__)
inline constexpr const wchar_t* kDefaultFont = L"Helvetica Neue";
inline constexpr const wchar_t* kDefaultFontLight = L"Helvetica Neue Light";
inline constexpr const wchar_t* kDefaultCodeFont = L"Menlo";
#else
// [DEFERRED-VERIFY]: not measured against a real Linux host -- this session
// has none to probe. DejaVu Sans ships with the core fontconfig packages on
// effectively every mainstream distro, but whether it avoids the same
// alias-population cost "Segoe UI" paid is unconfirmed. It has no distinct
// Light-weight family, so kDefaultFontLight reuses the same name.
inline constexpr const wchar_t* kDefaultFont = L"DejaVu Sans";
inline constexpr const wchar_t* kDefaultFontLight = L"DejaVu Sans";
// [DEFERRED-VERIFY], for the same reason: DejaVu Sans Mono ships in the same
// package as DejaVu Sans.
inline constexpr const wchar_t* kDefaultCodeFont = L"DejaVu Sans Mono";
#endif

// A built-in theme's code face: the named Windows face on Windows, the platform
// monospace everywhere else. A theme file's codefont= is the user's choice and does
// not go through this -- only the families the tree itself names.
inline constexpr const wchar_t* platformCodeFont(const wchar_t* windowsFace) {
#if defined(_WIN32)
    return windowsFace;
#else
    (void)windowsFace;
    return kDefaultCodeFont;
#endif
}

// Theme colors
struct D2DTheme {
    const wchar_t* name;
    const wchar_t* fontFamily;        // Main font
    const wchar_t* headingFontFamily; // Headings; null/empty inherits fontFamily
    const wchar_t* codeFontFamily;    // Monospace font
    bool isDark;
    tinta::Color background;
    tinta::Color text;
    tinta::Color heading;
    tinta::Color link;
    tinta::Color code;
    tinta::Color codeBackground;
    tinta::Color blockquoteBorder;
    tinta::Color accent;             // For UI elements
    // Syntax highlighting colors
    tinta::Color syntaxKeyword;
    tinta::Color syntaxString;
    tinta::Color syntaxComment;
    tinta::Color syntaxNumber;
    tinta::Color syntaxFunction;
    tinta::Color syntaxType;
    tinta::Color syntaxControlFlow;
    // Optional themes.ini override. Missing means inherit code (#197).
    std::optional<tinta::Color> inlineCode = std::nullopt;
};

inline tinta::Color themeInlineCodeColor(const D2DTheme& theme) {
    return theme.inlineCode.value_or(theme.code);
}

// Helper to create color from hex.
//
// Delegates to tinta::colorFromHex rather than repeating the shift-and-mask.
// Both spellings existed for a few hours after geometry.h landed and that is one
// unpack too many: a wrong shift still produces a plausible-looking colour, so a
// second copy is a place for the two to drift apart silently. The name stays
// because it has 200 call sites and 151 of them are the built-in theme table.
inline tinta::Color hexColor(uint32_t hex, float alpha = 1.0f) {
    return tinta::colorFromHex(hex, alpha);
}

// Themes array (defined in themes.cpp)
extern const D2DTheme THEMES[];
extern const int THEME_COUNT;

// What `-l` / `--light` selects. Named here, in the header both entry points already
// include, because the two arms apply light mode SEPARATELY -- WinMain in its own block,
// src/main_qt.cpp in its own -- and the three assignments each makes are mechanical while
// the INDEX is the actual policy. An index typed twice is the half that can drift: move
// Paper out of slot 0 and one arm goes light while the other goes dark, with nothing to
// catch it. The state changes themselves are not shared because src/themes.cpp cannot see
// App and making it able to is a heavier include change than either arm needs.
//
// Global scope, not namespace tinta, to match THEMES/THEME_COUNT/themeAt in this same
// header. Everything here is global; one namespaced name among them would be the odd one
// out and would read as a mistake at every call site.
inline constexpr int kLightModeThemeIndex = 0;  // Paper, the first light theme

// Dynamic theme registry (#82): the built-ins plus user themes loaded from
// %APPDATA%\Tinta\themes.ini. Custom themes own their strings and live at
// stable addresses for the app lifetime, so D2DTheme's const wchar_t*
// members stay valid. Indices: [0, THEME_COUNT) built-in, then customs.
struct CustomTheme {
    std::wstring name, fontFamily, headingFontFamily, codeFontFamily;
    D2DTheme theme;
};
int themeCount();
const D2DTheme& themeAt(int index);  // out-of-range clamps to theme 0

// The face headings render with: headingfont= when the theme sets one,
// else the theme's main font - so font= means the whole document (#155)
inline const wchar_t* themeHeadingFont(const D2DTheme& t) {
    return (t.headingFontFamily && t.headingFontFamily[0])
               ? t.headingFontFamily
               : t.fontFamily;
}
void loadCustomThemes();
// Adds or replaces (by name) a user theme and rewrites themes.ini.
// Returns the theme's registry index, or -1 on failure.
int saveCustomTheme(const D2DTheme& t, const std::wstring& name,
                    const std::wstring& fontFamily,
                    const std::wstring& codeFontFamily,
                    const std::wstring& headingFontFamily = L"");

#endif // TINTA_THEME_DATA_H
