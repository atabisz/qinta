#include "theme_apply.h"
#include "platform_paths.h"
#include "text_qt.h"

#include <QFontDatabase>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

// The Qt arms of include/theme_apply.h's three backend hooks. The Windows arms WERE in
// src/d2d_init.cpp, which 0b39ae6 deleted with the Direct2D stack -- so there is no second
// arm to be linked against any more, and these three are the only definitions in the tree.

// Handed to the window, which owns the native frame: include/window_sink.h's chrome member.
// This used to be `{}` under "A REAL GAP, not a design: the title bar keeps the platform's
// colours when the theme changes", and it is now HALF closed. On Windows the widget is a real
// HWND under the windows platform plugin, so src/qt_window.cpp paints the chrome with the same
// DWM calls as the Direct2D stack (src/window_chrome_win32.cpp).
//
// ON LINUX AND macOS THE GAP STANDS. Qt has no portable caption-colour API and the frame
// belongs to the window manager. QStyleHints::setColorScheme is not the fix: it is app-wide,
// and qt_launch's systemPrefersLight reads that same scheme to decide follow-system mode, so
// setting it from the theme would feed the theme back into its own input.
void applyWindowChrome(App& app) {
    tinta::deliverWindowChrome(windowChromeFor(app));
}

namespace {

// THE THREE IDEOGRAPH ORDERINGS, one group per CJK language, each group ordered
// Windows face -> macOS face -> Linux faces.
//
// Han unification shares code points across Chinese, Japanese and Korean while the glyph
// SHAPES differ, so whichever family resolves first decides the variant every ideograph in
// the document renders with. That is the whole of #155: on a Japanese system kanji must come
// out of a Japanese face instead of a Simplified-Chinese one, and hanja out of a Korean face
// on a Korean system.
//
// The deleted DirectWrite arm named only the four Windows families it needed
// (`git show 0b39ae6^:src/d2d_init.cpp`). Porting just those would have left macOS and Linux
// exactly where they were, which is why each group carries all three platforms' faces: a
// family the host does not have costs nothing, because Qt skips it when it resolves a glyph.
const wchar_t* const kSimplifiedChinese[] = {
    L"Microsoft YaHei UI", L"Microsoft YaHei",            // Windows
    L"PingFang SC", L"Heiti SC",                          // macOS
    L"Noto Sans CJK SC", L"Noto Sans SC", L"Source Han Sans SC", L"WenQuanYi Micro Hei",
};
const wchar_t* const kJapanese[] = {
    L"Yu Gothic UI", L"Meiryo",                           // Windows
    L"Hiragino Sans", L"Hiragino Kaku Gothic ProN",       // macOS
    L"Noto Sans CJK JP", L"Noto Sans JP", L"Source Han Sans JP", L"IPAexGothic",
};
const wchar_t* const kKorean[] = {
    L"Malgun Gothic",                                     // Windows
    L"Apple SD Gothic Neo",                               // macOS
    L"Noto Sans CJK KR", L"Noto Sans KR", L"Source Han Sans KR", L"NanumGothic",
};

// The tail, for everything the three groups do not cover. The Windows arm spelled this as a
// full-range mapping of Segoe UI Emoji and Segoe UI Symbol with the system fallback chained
// behind it; Qt's own per-glyph resolution IS that chained system fallback, so only the
// emoji faces have to be named.
const wchar_t* const kEmoji[] = {
    L"Segoe UI Emoji", L"Apple Color Emoji", L"Noto Color Emoji", L"Segoe UI Symbol",
};

template <size_t N>
void appendGroup(std::vector<std::wstring>& out, const wchar_t* const (&group)[N]) {
    for (const wchar_t* family : group) out.push_back(family);
}

// The primary subtag of a BCP 47 tag, lowercased: "ja-JP" and "ja-Jpan-JP" both give "ja".
// Underscore as well as hyphen because a POSIX locale spells it "ja_JP", and
// platform::preferredUiLanguages normalises on its own but a caller (the test) may not.
std::wstring primarySubtag(const std::wstring& tag) {
    std::wstring primary;
    for (wchar_t ch : tag) {
        if (ch == L'-' || ch == L'_' || ch == L'.' || ch == L'@') break;
        primary.push_back(static_cast<wchar_t>(
            (ch >= L'A' && ch <= L'Z') ? ch - L'A' + L'a' : ch));
    }
    return primary;
}

}  // namespace

std::vector<std::wstring> cjkFallbackFamilies(const std::wstring& uiLanguageTag) {
    const std::wstring lang = primarySubtag(uiLanguageTag);
    std::vector<std::wstring> families;
    families.reserve(std::size(kSimplifiedChinese) + std::size(kJapanese) +
                     std::size(kKorean) + std::size(kEmoji));

    // Japanese and Korean lead with their own group; EVERYTHING ELSE -- including
    // Traditional Chinese, an unknown tag and an empty one -- takes the Simplified-first
    // default. That is not a gap: it is the deleted arm's own `: cjkZh` fallthrough, which
    // chose Microsoft YaHei UI first for Chinese variants and a consistent Regular weight
    // instead of Yu Gothic UI's visually-heavier strokes. A Traditional-Chinese ordering is
    // upstream #254's addition and #254 is not in this fork's history.
    if (lang == L"ja") {
        appendGroup(families, kJapanese);
        appendGroup(families, kSimplifiedChinese);
        appendGroup(families, kKorean);
    } else if (lang == L"ko") {
        appendGroup(families, kKorean);
        appendGroup(families, kSimplifiedChinese);
        appendGroup(families, kJapanese);
    } else {
        appendGroup(families, kSimplifiedChinese);
        appendGroup(families, kJapanese);
        appendGroup(families, kKorean);
    }
    appendGroup(families, kEmoji);
    return families;
}

// THE PORT OF #155, AND WHAT IT KEEPS AND DROPS IS WORTH BEING EXACT ABOUT.
//
// This used to be `{}` under "Nothing to install. QFont resolves missing glyphs per font
// from the system's fallback configuration, so there is no chain object to build." The first
// sentence was the error: Qt resolving a missing glyph is not the same as Qt resolving it in
// the UI language's order, and the order is the entire content of #155. A QFont carries a
// FAMILIES LIST and walks it in order, so the ordering has a Qt spelling after all -- it is
// just a list rather than a DirectWrite chain object.
//
// DROPPED, and stated rather than hidden: DirectWrite's builder mapped families to Unicode
// RANGES, so the deleted arm could put the Japanese faces in front for kana while leaving
// the ideograph order to the UI language independently. A families list is global to the
// font. Ordering by UI language survives; per-range ordering does not, and nothing in Qt
// offers it.
//
// The handoff is to the FACTORY, which holds the list and does not apply it -- the same
// division the Direct2D arm had (setFontFallback on D2DTextFactory, read at apply time), and
// for the reason src/render.cpp:59-80 measured: 15 sites create layouts through the factory
// and have never had a fallback, several of them MEASURE text, so a chain applied by
// CreateTextLayout would silently move measured widths. A layout gets one only where a
// caller asks, and there are four such callers.
//
// dynamic_cast rather than the Direct2D arm's static_cast, and that is a real difference:
// that one could argue app.textFactory is only ever seated from one place, but
// tests/app_fixture.h seats a counting fake through the same member. A null here is the
// fake, and a no-op is the right answer for it.
void installFontFallback(App& app) {
    auto* factory = dynamic_cast<QtTextFactory*>(app.textFactory);
    if (!factory) return;

    // THE SYSTEM UI language, not app.languageSetting. #155 is keyed on the display
    // language -- the deleted arm read GetUserDefaultUILanguage -- and drawing the
    // INTERFACE in the interface language's font is a separate upstream change (#254) with
    // its own second chain, not in this fork's history. Mixing them here would port a
    // feature under a bug fix's name.
    const std::vector<std::wstring> tags = platform::preferredUiLanguages();
    const std::wstring tag = tags.empty() ? std::wstring() : tags.front();

    QStringList families;
    for (const std::wstring& family : cjkFallbackFamilies(tag)) {
        families << QString::fromStdWString(family);
    }
    // Unconditional, where the chain BUILD on the Direct2D side was guarded by
    // `if (!app.fontFallback)`. Same reason that arm set the factory's slot outside its own
    // guard: this runs on every format rebuild, costs one list copy, and cannot go stale
    // against a reseated factory.
    factory->setFallbackFamilies(families);
}

// Same contract as the Windows arm: enumerated once, sorted, and later calls do nothing.
// QFontDatabase::families is STATIC in Qt 6, as src/text_qt.cpp records for hasFamily.
// It returns names in the application's writing system, where the Windows arm prefers the
// en-US name -- the same family can therefore be spelled differently on the two stacks.
void enumerateSystemFontFamilies(App& app) {
    if (!app.systemFontFamilies.empty()) return;
    const QStringList families = QFontDatabase::families();
    app.systemFontFamilies.reserve(static_cast<size_t>(families.size()));
    for (const QString& family : families) {
        if (!family.isEmpty()) app.systemFontFamilies.push_back(family.toStdWString());
    }
    std::sort(app.systemFontFamilies.begin(), app.systemFontFamilies.end());
}
