#ifndef TINTA_I18N_H
#define TINTA_I18N_H

#include <string>

// Language indices are stable identifiers persisted in settings.ini.
// Order matters: indices are stored on disk, so never renumber existing
// entries — only append new ones.
//   0 = English (en)         — fallback when a translation is missing
//   1 = Simplified Chinese (zh-CN)
//   2 = Japanese (ja)
//   3 = Korean (ko)
constexpr int LANG_COUNT = 4;
constexpr int LANG_INDEX_EN = 0;
constexpr int LANG_INDEX_DEFAULT = -1;  // Follow the system UI language

struct Language {
    const wchar_t* nativeName;   // Shown in the chooser, e.g. "简体中文"
    const wchar_t* englishName;  // Auxiliary subtitle, e.g. "Chinese (Simplified)"
    // Preferred font family for this language's native name. Empty = theme
    // default. CJK fonts are already in the fallback chain, but using the
    // native font here makes the chooser itself feel localized.
    const wchar_t* nativeFont;
};

extern const Language LANGUAGES[LANG_COUNT];

// The strings that name the file manager have one key per OS, because the file
// manager has a different name on each: Explorer, the Finder, and no single name on
// Linux. Callers ask for these rather than a literal key. Windows keeps the original
// keys, so a languages.ini that amends ctx.reveal there still applies.
#if defined(_WIN32)
constexpr const char* I18N_KEY_REVEAL = "ctx.reveal";
constexpr const char* I18N_KEY_OPEN_IN_TABS_HINT = "settings.open_in_tabs.hint";
#elif defined(__APPLE__)
constexpr const char* I18N_KEY_REVEAL = "ctx.reveal.finder";
constexpr const char* I18N_KEY_OPEN_IN_TABS_HINT = "settings.open_in_tabs.hint.finder";
#else
constexpr const char* I18N_KEY_REVEAL = "ctx.reveal.file_manager";
constexpr const char* I18N_KEY_OPEN_IN_TABS_HINT = "settings.open_in_tabs.hint.file_manager";
#endif

// The zen-mode width hint names the zen key, and on macOS that is ⌃⌘F: F11 is a media key on a
// default Mac keyboard and never reaches the app (include/menu_bar_qt.h).
#if defined(__APPLE__)
constexpr const char* I18N_KEY_ZEN_WIDTH_HINT = "settings.reading_width_full.hint.macos";
#else
constexpr const char* I18N_KEY_ZEN_WIDTH_HINT = "settings.reading_width_full.hint";
#endif

struct App;

// Resolve a translation key for the given language index.
// Falls back to English when the requested language lacks an entry, then to
// the key itself (never returns null).
const wchar_t* tr(int langIndex, const char* key);

// Convenience overload using app.currentLanguageIndex.
const wchar_t* tr(const App& app, const char* key);

// The label of a GitHub alert, kind 1..5 (note, tip, important, warning, caution), in the UI
// language. The viewer and both exporters prefix their own icon; this is the one place the
// label comes from, so the three cannot drift into English again.
const wchar_t* alertLabel(int langIndex, int kind);

// Detect the user's preferred UI language via GetUserPreferredUILanguages.
// Returns one of the LANG_INDEX_* values (always English as the last resort).
int detectSystemLanguage();

// Validates / clamps an arbitrary index into the [0, languageCount()) range.
// Returns LANG_INDEX_EN for out-of-range values.
int clampLanguageIndex(int index);

// Dynamic language registry: the compiled languages plus any [section] of
// %APPDATA%\Tinta\languages.ini that carries translations (name= labels
// it). Indices are runtime-only — persist the id string, not the index.
int languageCount();
const wchar_t* languageNameAt(int index);
std::string languageIdAt(int index);
int languageIndexById(const std::string& id);   // -1 when unknown
int languageCompiledColumn(int index);          // -1 for ini-only languages
// False for roster languages that have no translations yet — these are
// hidden from the Settings dropdown until languages.ini fills them in
bool languageHasTranslations(int index);
void loadLanguageOverrides();
// Seeds a translator template (common-language roster, every key commented
// with its English text) on first use, then opens the file
void openLanguagesIniFile();

#endif // TINTA_I18N_H
