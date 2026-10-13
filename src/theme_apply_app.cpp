#include "theme_apply.h"

#include <cwchar>

#include "math_render.h"
#include "settings.h"
#include "tabs.h"  // tabStripBackground
#include "text_formats.h"
#include "theme_data.h"
#include "win32_api.h"

// Moved out of src/d2d_init.cpp on 2026-09-24, bodies unchanged apart from the fallback chain
// becoming a call. Why a new file and not src/text_formats_app.cpp: applyTheme is not text
// format construction, and that file's header says construction is all it holds. The same
// split-rather-than-relax argument text_formats_app.cpp makes about text_utils_app.cpp.

void applyTheme(App& app, int themeIndex) {
    if (themeIndex < 0 || themeIndex >= themeCount()) return;

    const D2DTheme& newTheme = themeAt(themeIndex);

    // If the fonts are unchanged the existing text formats are identical —
    // skip recreating them. Colors are baked into the cached layout runs, so a
    // relayout is still needed.
    //
    // The count that used to be in this sentence ("~47 IDWriteTextFormat objects") is
    // deleted rather than corrected: sixteen of them are tinta::TextFormat now and the
    // rest are still native, so any single figure here would be wrong about one half.
    // The probe reads app.textFormat, the seam pointer, where it used to read
    // app.nativeTextFormat -- the same question ("has anything been built yet") asked of
    // the object the tree actually draws with.
    bool sameFonts = app.textFormat &&
        wcscmp(app.theme.fontFamily, newTheme.fontFamily) == 0 &&
        wcscmp(app.theme.codeFontFamily, newTheme.codeFontFamily) == 0;

    app.currentThemeIndex = themeIndex;
    app.theme = newTheme;
    app.darkMode = newTheme.isDark;

    if (sameFonts) {
        app.layoutDirty = true;
    } else {
        updateTextFormats(app);
    }

    // Title bar follows the theme
    applyWindowChrome(app);

    // Force a redraw
    if (canRepaint(app.hwnd)) {
        requestRepaint(app.hwnd);
    }
}

int followThemeIndex(const App& app, bool systemPrefersLight) {
    return systemPrefersLight ? app.lightThemeIndex : app.darkThemeIndex;
}

bool followSystemAppearance(App& app, bool systemPrefersLight) {
    if (!app.followSystemTheme) return false;
    const int want = followThemeIndex(app, systemPrefersLight);
    if (want == app.currentThemeIndex) return false;
    applyTheme(app, want);
    persistThemeChoice(app);
    return true;
}

tinta::WindowChrome windowChromeFor(const App& app) {
    tinta::WindowChrome chrome;
    chrome.dark = app.theme.isDark;
    chrome.caption = tabStripBackground(app);
    chrome.text = app.theme.text;
    return chrome;
}

void updateTextFormats(App& app) {
    // Editor line layouts hold the old format/size
    app.clearEditorLineLayoutCache();
    // Math boxes were laid out against the old theme font
    mathClearCache();
    // THE ELEVEN DOCUMENT FORMATS ARE CREATED THROUGH THE SEAM, and this one call replaces
    // 90 lines of this function: the release block, eleven dwriteFactory->CreateTextFormat
    // calls, the run of eleven SetParagraphAlignment statements and the four space-width
    // measurements. The numbers are not repeated here because they are no longer chosen
    // here -- src/text_formats_app.cpp holds the only copy, and it holds it because
    // src/qt_init.cpp had a second one that the old home of this function could not see.
    //
    // The reason this is a deletion rather than a port: installQtTextFormats already built
    // exactly these sixteen objects, with the same families, sizes and weights, through
    // tinta::TextFactory. Two parallel copies of a table of numbers, one of them naming
    // DirectWrite. Its own comment said a number disagreeing with this file "is a
    // divergence, not a Qt decision", which names the hazard without removing it.
    //
    // It is safe to call before the fallback chain exists: a fallback is attached to a
    // LAYOUT, not to a format, and installFontFallback's handoff runs unconditionally on
    // every call for exactly that reason.
    installDocumentTextFormats(app);

    // Emoji and CJK fallback, in UI-language order (#155). The DirectWrite chain was the
    // Windows arm, in src/d2d_init.cpp, deleted with the Direct2D stack; src/theme_qt.cpp is
    // the only arm now and it hands an ordered family list to the text factory.
    //
    // NOT the only caller any more: installQtBackends calls it at startup too, because this
    // function is not on the Qt startup path and the list would otherwise never be seated.
    installFontFallback(app);

    // The chrome formats, through the shared portable function. No seating call follows:
    // the retired src/d2d_init.cpp kept the tombstone for why that pairing had to break
    // (`git show 0b39ae6^:src/d2d_init.cpp`).
    installChromeFormats(app);
    app.appliedZoomFactor = app.zoomFactor;
    app.layoutDirty = true;
}
