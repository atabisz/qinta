#include "i18n.h"
#include "app.h"

#include <cstring>
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

bool contains(const wchar_t* haystack, const wchar_t* needle) {
    return std::wstring(haystack).find(needle) != std::wstring::npos;
}

} // namespace

int main() {
    // Registry + built-in override seeding (a user languages.ini may add
    // languages on a dev machine, but can only add translations, so every
    // positive assertion below holds regardless of local state).
    loadLanguageOverrides();

    int en = languageIndexById("en");
    int zh = languageIndexById("zh");
    int de = languageIndexById("de");
    int fr = languageIndexById("fr");
    int it = languageIndexById("it");
    int ja = languageIndexById("ja");
    int ko = languageIndexById("ko");
    check(en == 0, "en is the first registry entry");
    check(zh == 1, "zh is the second registry entry");
    check(de >= 0, "de is registered");
    check(fr >= 0, "fr is registered");
    check(it >= 0, "it is registered");
    check(ja >= 0, "ja is registered");
    check(ko >= 0, "ko is registered");

    // The Settings dropdown lists exactly the languages that carry
    // translations; the shipped set must always qualify.
    check(languageHasTranslations(en), "en counts as translated");
    check(languageHasTranslations(zh), "zh counts as translated");
    check(languageHasTranslations(de), "de ships built-in translations");
    check(languageHasTranslations(fr), "fr ships built-in translations");
    check(languageHasTranslations(it), "it ships built-in translations");
    check(languageHasTranslations(ja), "ja ships built-in translations");
    check(languageHasTranslations(ko), "ko ships built-in translations");

    // formatArg: the %1$s substitution title.plain and title.dirty use
    check(formatArg(L"Qinta - %1$s", L"a.md") == L"Qinta - a.md", "formatArg fills %1$s");
    check(formatArg(L"%1$s and %1$s", L"x") == L"x and x", "formatArg fills every %1$s");
    check(formatArg(L"Qinta", L"a.md") == L"Qinta", "a pattern without %1$s is unchanged");
    check(formatArg(L"%1$s", L"%1$s") == L"%1$s", "a value holding %1$s is not re-substituted");
    check(formatArg(nullptr, L"a").empty(), "a null pattern formats to empty");
    check(formatArg(tr(en, "title.dirty"), L"a.md") == L"Qinta - * a.md",
          "title.dirty formats to the caption updateWindowTitle used to spell");

    // Alias normalization onto compiled languages
    check(languageIndexById("zh-CN") == zh, "zh-CN aliases to zh");
    check(languageIndexById("EN-us") == en, "en-US aliases to en");

    // Spot-check the shipped strings (a local languages.ini could override
    // these keys in theory; the template it seeds keeps every key commented)
    check(std::wstring(tr(de, "settings.title")) == L"Einstellungen",
          "German settings title");
    check(std::wstring(tr(fr, "settings.title")) == L"Param\u00E8tres",
          "French settings title");
    check(std::wstring(tr(it, "ctx.settings")) == L"Impostazioni",
          "Italian settings menu item");
    check(std::wstring(tr(ja, "settings.language")) == L"\u8A00\u8A9E",
          "Japanese language setting label");
    check(std::wstring(tr(ko, "settings.language")) == L"\uC5B8\uC5B4",
          "Korean language setting label");
    check(std::wstring(tr(ja, "toast.copied")) == L"\u30B3\u30D4\u30FC\u3057\u307E\u3057\u305F\uFF01",
          "Japanese copy toast");
    check(std::wstring(tr(ko, "toast.copied")) == L"\uBCF5\uC0AC\uD588\uC2B5\uB2C8\uB2E4!",
          "Korean copy toast");
    check(std::wstring(tr(zh, "theme.editor.title")) == L"\u65B0\u5EFA\u4E3B\u9898",
          "Chinese theme editor title");
    check(std::wstring(tr(ja, "theme.editor.save")) == L"\u30C6\u30FC\u30DE\u3092\u4FDD\u5B58",
          "Japanese theme editor save action");
    check(std::wstring(tr(ko, "theme.editor.color.background")) == L"\uBC30\uACBD",
          "Korean theme editor color label");
    check(std::wstring(tr(de, "theme.editor.cancel")) == L"Abbrechen",
          "German theme editor cancel action");
    check(std::wstring(tr(ja, "codeblock.copy")) == L"\u30B3\u30D4\u30FC",
          "Japanese code block copy button");
    check(std::wstring(tr(ko, "codeblock.copied")) == L"\uBCF5\uC0AC\uD588\uC2B5\uB2C8\uB2E4!",
          "Korean code block copied toast");
    check(std::wstring(tr(ja, "ctx.settings")) == L"\u8A2D\u5B9A",
          "Japanese context menu settings");
    check(std::wstring(tr(ko, "confirm.discard")) == L"\uBCC0\uACBD \uBC84\uB9AC\uAE30",
          "Korean discard confirmation");

    // Keys left out of the built-in table fall back to English
    check(std::wstring(tr(de, "title.no_file")) == L"Qinta",
          "untranslated key falls back to English");

    // Quick-note strings (Ctrl+N) ship translated too
    check(std::wstring(tr(de, "title.untitled")) == L"Unbenannt",
          "German untitled title");
    check(std::wstring(tr(it, "help.general.new_note")) == L"Nuova nota rapida",
          "Italian quick-note help row");

    // search.match_count is the one runtime-printf'd string: the fixed
    // argument order (%d current, %zu total) must survive translation
    for (int lang : {de, fr, it}) {
        const wchar_t* fmt = tr(lang, "search.match_count");
        check(contains(fmt, L"%d"), "match_count keeps %d");
        check(contains(fmt, L"%zu"), "match_count keeps %zu");
    }

    // --- the App overload -------------------------------------------------
    //
    // tr(const App&, key) is the whole of src/i18n_app.cpp, and until this block
    // existed nothing executed a line of that file on any platform: it sat in
    // portable_compile_only, where a compiler accepting it was the only evidence
    // there was. Every one of the 169 tr(app, "key") call sites across the tree
    // goes through it.
    //
    // What makes it worth asserting is the failure mode, not the size. The
    // function's entire job is to READ app.currentLanguageIndex and forward it.
    // Break that read -- forward a constant, forward the wrong member, forward the
    // system language instead of the chosen one -- and the app compiles, runs,
    // draws every string, and is silently monolingual. There is no error to see.
    // So the assertions below are all differential: the same key through the same
    // object must change answer when the index changes, and must agree exactly
    // with the index overload it delegates to.
    App app;

    check(app.currentLanguageIndex == 0,
          "a default-constructed App starts on the first registry entry");
    check(std::wstring(tr(app, "settings.title")) ==
              std::wstring(tr(en, "settings.title")),
          "the App overload on a default App agrees with tr(en, ...)");

    // The differential core. Same object, same key, index moved: a forwarding
    // function that ignored the member would return the same string three times.
    app.currentLanguageIndex = de;
    const std::wstring titleDe = tr(app, "settings.title");
    app.currentLanguageIndex = fr;
    const std::wstring titleFr = tr(app, "settings.title");
    app.currentLanguageIndex = ja;
    const std::wstring titleJa = tr(app, "settings.language");

    check(titleDe == L"Einstellungen",
          "the App overload reads currentLanguageIndex (German)");
    check(titleFr == L"Param\u00E8tres",
          "the App overload reads currentLanguageIndex (French)");
    check(titleJa == L"\u8A00\u8A9E",
          "the App overload reads currentLanguageIndex (Japanese)");
    check(titleDe != titleFr,
          "moving currentLanguageIndex changes the answer, so the member is not ignored");

    // Agreement with the overload it delegates to, across every shipped language
    // and several keys. This is the forwarding contract stated directly: for any
    // index the App overload must be indistinguishable from the index overload.
    // Asserted as pointer-equality-free string comparison on purpose -- the table
    // returns stable pointers today and that is not the contract being pinned.
    for (int lang : {en, zh, de, fr, it, ja, ko}) {
        app.currentLanguageIndex = lang;
        for (const char* key : {"settings.title", "ctx.settings", "toast.copied",
                                "codeblock.copy", "title.untitled"}) {
            check(std::wstring(tr(app, key)) == std::wstring(tr(lang, key)),
                  "tr(app, key) agrees with tr(app.currentLanguageIndex, key)");
        }
    }

    // The English fallback has to survive the extra hop. A key missing from the
    // German table resolves through the App overload exactly as it does through
    // the index one -- worth its own assertion because the fallback lives in the
    // index overload, and an App overload that cached or short-circuited would
    // break it while every present key still looked right.
    app.currentLanguageIndex = de;
    check(std::wstring(tr(app, "title.no_file")) == L"Qinta",
          "an untranslated key still falls back to English through the App overload");

    // The file manager's name, per OS. Every shipped language carries both of the
    // per-OS keys, and off Windows the key a caller is handed is NOT the Explorer
    // one in any language -- the label used to say Explorer on all three OSes. On
    // Windows the counter-case: the caller's key IS the Explorer one, unchanged.
    for (int lang : {en, zh, de, fr, it, ja, ko}) {
        for (const char* key : {"ctx.reveal.finder", "ctx.reveal.file_manager",
                                "settings.open_in_tabs.hint.finder",
                                "settings.open_in_tabs.hint.file_manager"}) {
            // tr returns the key itself when no table has it.
            check(std::wstring(tr(lang, key)) != std::wstring(key, key + std::strlen(key)),
                  "every shipped language resolves the per-OS file-manager keys");
            if (lang != en)
                check(std::wstring(tr(lang, key)) != std::wstring(tr(en, key)),
                      "the per-OS file-manager keys are translated, not English fallbacks");
        }
        const bool sameReveal =
            std::wstring(tr(lang, I18N_KEY_REVEAL)) == std::wstring(tr(lang, "ctx.reveal"));
        const bool sameHint = std::wstring(tr(lang, I18N_KEY_OPEN_IN_TABS_HINT)) ==
                              std::wstring(tr(lang, "settings.open_in_tabs.hint"));
        // The macOS menu bar's View and Find and Replace, and the zen hint that names ⌃⌘F there.
        // menu.file is not here: Italian's File is the English word, so "translated" cannot hold.
        for (const char* key : {"menu.view", "menu.find_replace",
                                "settings.reading_width_full.hint.macos"}) {
            check(std::wstring(tr(lang, key)) != std::wstring(key, key + std::strlen(key)),
                  "every shipped language resolves the macOS menu and zen-hint keys");
            if (lang != en)
                check(std::wstring(tr(lang, key)) != std::wstring(tr(en, key)),
                      "the macOS menu and zen-hint keys are translated, not English fallbacks");
        }
        const bool sameZenHint = std::wstring(tr(lang, I18N_KEY_ZEN_WIDTH_HINT)) ==
                                 std::wstring(tr(lang, "settings.reading_width_full.hint"));
#ifdef __APPLE__
        check(!sameZenHint, "on macOS the zen width hint does not name F11");
#else
        check(sameZenHint, "off macOS the zen width hint keeps its F11 key");
#endif
#ifdef _WIN32
        check(sameReveal && sameHint, "Windows keeps the Explorer labels");
#else
        check(!sameReveal, "off Windows the reveal label does not name Explorer");
        check(!sameHint, "off Windows the open-in-tabs hint does not name Explorer");
#endif
    }

    if (failures == 0) {
        // The count is printed on success, which this test did not do for its
        // whole life: it said "i18n tests passed" and nothing else, so a block
        // compiled out, a loop that never entered, or the whole App section
        // silently dropped would all have read as a clean pass. A green run with
        // no number is indistinguishable from a green run of nothing.
        std::cout << "i18n: all " << checks << " assertions passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s) out of " << checks << " assertions\n";
    return 1;
}
