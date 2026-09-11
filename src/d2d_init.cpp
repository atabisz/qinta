#include "d2d_init.h"
#include "tabs.h"
#include "math_render.h"
#include "utils.h"

#include <objbase.h>
#include <dwmapi.h>

#include <algorithm>

// Older SDK headers may lack these
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

void applyWindowChrome(App& app) {
    if (!app.hwnd) return;

    auto toColorref = [](const tinta::Color& c) {
        return RGB((BYTE)(c.r * 255.0f + 0.5f),
                   (BYTE)(c.g * 255.0f + 0.5f),
                   (BYTE)(c.b * 255.0f + 0.5f));
    };

    // Windows 10 fallback: at least pick the right caption variant
    BOOL dark = app.theme.isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(app.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &dark, sizeof(dark));

    // Windows 11: exact theme colors (silently rejected on older builds).
    // The frame tint matches the drawn tab strip, so the window border and
    // any DWM-drawn transition flash blend with the custom caption.
    COLORREF caption = toColorref(tabStripBackground(app));
    DwmSetWindowAttribute(app.hwnd, DWMWA_CAPTION_COLOR,
                          &caption, sizeof(caption));
    DwmSetWindowAttribute(app.hwnd, DWMWA_BORDER_COLOR,
                          &caption, sizeof(caption));
    COLORREF text = toColorref(app.theme.text);
    DwmSetWindowAttribute(app.hwnd, DWMWA_TEXT_COLOR,
                          &text, sizeof(text));
}

bool initD2D(App& app) {
    auto t0 = Clock::now();

    // Initialize COM (required for WIC image loading)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &app.d2dFactory);
    if (FAILED(hr)) return false;

    app.metrics.d2dInitUs = usElapsed(t0);
    t0 = Clock::now();

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&app.dwriteFactory));
    if (app.dwriteFactory) {
        app.dwriteFactory->CreateTextAnalyzer(&app.textAnalyzer);
        // Seated HERE and not from App::seatSeamWrappers(), which has zero callers --
        // the live paths are the individual seaters, called from this file. Adding a
        // line to the aggregate would have compiled clean, left app.textFactory null
        // forever, and handed every migrated site a null factory.
        app.seatTextFactory();
    }
    if (FAILED(hr)) return false;

    app.metrics.dwriteInitUs = usElapsed(t0);

    // Initialize WIC for image loading
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&app.wicFactory));
    // WIC failure is non-fatal (images just won't render)

    return true;
}

void applyTheme(App& app, int themeIndex) {
    if (themeIndex < 0 || themeIndex >= themeCount()) return;

    const D2DTheme& newTheme = themeAt(themeIndex);

    // If the fonts are unchanged the existing text formats are identical —
    // skip recreating ~47 IDWriteTextFormat objects. Colors are baked into
    // the cached layout runs, so a relayout is still needed.
    bool sameFonts = app.nativeTextFormat &&
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
    if (app.hwnd) {
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
}

void updateTextFormats(App& app) {
    // Editor line layouts hold the old format/size
    app.clearEditorLineLayoutCache();
    // Math boxes were laid out against the old theme font
    mathClearCache();
    // Release existing formats
    app.textFormatStorage.reset(); app.nativeTextFormat = nullptr; app.textFormat = nullptr;
    app.headingFormatStorage.reset(); app.nativeHeadingFormat = nullptr; app.headingFormat = nullptr;
    app.codeFormatStorage.reset(); app.nativeCodeFormat = nullptr; app.codeFormat = nullptr;
    app.boldFormatStorage.reset(); app.nativeBoldFormat = nullptr; app.boldFormat = nullptr;
    app.italicFormatStorage.reset(); app.nativeItalicFormat = nullptr; app.italicFormat = nullptr;
    app.boldItalicFormatStorage.reset(); app.nativeBoldItalicFormat = nullptr; app.boldItalicFormat = nullptr;
    app.codeBoldFormatStorage.reset(); app.nativeCodeBoldFormat = nullptr; app.codeBoldFormat = nullptr;
    app.codeItalicFormatStorage.reset(); app.nativeCodeItalicFormat = nullptr; app.codeItalicFormat = nullptr;
    app.codeBoldItalicFormatStorage.reset(); app.nativeCodeBoldItalicFormat = nullptr; app.codeBoldItalicFormat = nullptr;
    app.supSubFormatStorage.reset(); app.nativeSupSubFormat = nullptr; app.supSubFormat = nullptr;
    for (int i = 0; i < 6; ++i) {
        app.headingFormatsStorage[i].reset();
        app.nativeHeadingFormats[i] = nullptr;
        app.headingFormats[i] = nullptr;
    }

    // Create text formats with current zoom and theme fonts
    float scale = app.contentScale * app.zoomFactor;
    float fontSize = 16.0f * scale;
    float codeSize = 14.0f * scale;

    const wchar_t* fontFamily = app.theme.fontFamily;
    const wchar_t* codeFont = app.theme.codeFontFamily;

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &app.nativeTextFormat);

    app.dwriteFactory->CreateTextFormat(themeHeadingFont(app.theme), nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        28.0f * scale, L"en-us", &app.nativeHeadingFormat);

    app.dwriteFactory->CreateTextFormat(codeFont, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        codeSize, L"en-us", &app.nativeCodeFormat);

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &app.nativeBoldFormat);

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &app.nativeItalicFormat);

    // Inline spans nest, so every weight/style combination needs a format:
    // ***both***, **`code`**, *`code`*
    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &app.nativeBoldItalicFormat);

    app.dwriteFactory->CreateTextFormat(codeFont, nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        codeSize, L"en-us", &app.nativeCodeBoldFormat);

    app.dwriteFactory->CreateTextFormat(codeFont, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL,
        codeSize, L"en-us", &app.nativeCodeItalicFormat);

    app.dwriteFactory->CreateTextFormat(codeFont, nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL,
        codeSize, L"en-us", &app.nativeCodeBoldItalicFormat);

    // Small format for ^superscript^ / ~subscript~ spans
    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize * 0.68f, L"en-us", &app.nativeSupSubFormat);

    // Heading formats by level: the theme's heading face, which inherits
    // the main font unless the theme sets headingfont= (#155). Abyss pins
    // its headings to Segoe UI in data - bolding its Light body family
    // would render faux-bold.
    const wchar_t* headingFont = themeHeadingFont(app.theme);
    float headingSizes[] = {32, 26, 22, 18, 16, 14};
    for (int i = 0; i < 6; i++) {
        app.dwriteFactory->CreateTextFormat(headingFont, nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            headingSizes[i] * scale, L"en-us", &app.nativeHeadingFormats[i]);
    }

    // Set consistent baseline alignment for all formats
    if (app.nativeTextFormat) app.nativeTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.nativeHeadingFormat) app.nativeHeadingFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.nativeCodeFormat) app.nativeCodeFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.nativeBoldFormat) app.nativeBoldFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.nativeItalicFormat) app.nativeItalicFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.nativeBoldItalicFormat) app.nativeBoldItalicFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.nativeCodeBoldFormat) app.nativeCodeBoldFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.nativeCodeItalicFormat) app.nativeCodeItalicFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.nativeCodeBoldItalicFormat) app.nativeCodeBoldItalicFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.nativeSupSubFormat) app.nativeSupSubFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    for (auto* fmt : app.nativeHeadingFormats) {
        if (fmt) fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    // Cache space widths
    if (app.nativeTextFormat) app.spaceWidthText = measureText(app, L" ", app.nativeTextFormat);
    if (app.nativeBoldFormat) app.spaceWidthBold = measureText(app, L" ", app.nativeBoldFormat);
    if (app.nativeItalicFormat) app.spaceWidthItalic = measureText(app, L" ", app.nativeItalicFormat);
    if (app.nativeCodeFormat) app.spaceWidthCode = measureText(app, L" ", app.nativeCodeFormat);

    // Build font fallback chain for emoji and CJK support
    if (!app.fontFallback) {
        IDWriteFactory2* factory2 = nullptr;
        if (SUCCEEDED(app.dwriteFactory->QueryInterface(__uuidof(IDWriteFactory2),
                reinterpret_cast<void**>(&factory2)))) {
            IDWriteFontFallbackBuilder* builder = nullptr;
            if (SUCCEEDED(factory2->CreateFontFallbackBuilder(&builder))) {
                // --- Japanese kana: Japanese fonts first ---
                const wchar_t* jpFamilies[] = {
                    L"Yu Gothic UI", L"Meiryo", L"Microsoft YaHei UI"
                };
                DWRITE_UNICODE_RANGE jpRanges[] = {
                    { 0x3040, 0x309F },    // Hiragana
                    { 0x30A0, 0x30FF },    // Katakana
                    { 0x3190, 0x319F },    // Kanbun annotation marks
                    { 0x31F0, 0x31FF },    // Katakana phonetic extensions
                    { 0x32D0, 0x32FF },    // Circled katakana + era names (㋐ ㋿)
                    { 0x3300, 0x3357 },    // Squared katakana words (㌔ ㌘)
                    { 0xFF65, 0xFF9F },    // Halfwidth katakana
                };
                builder->AddMapping(jpRanges, 7, jpFamilies, 3);

                // --- Korean: Korean font first ---
                const wchar_t* krFamilies[] = {
                    L"Malgun Gothic", L"Microsoft YaHei UI", L"Yu Gothic UI"
                };
                DWRITE_UNICODE_RANGE krRanges[] = {
                    { 0x1100, 0x11FF },    // Hangul Jamo
                    { 0x3130, 0x318F },    // Hangul compatibility Jamo
                    { 0x3200, 0x321E },    // Parenthesized Hangul (㈀ ㈜)
                    { 0x3260, 0x327F },    // Circled Hangul (㉠ ㉻)
                    { 0xAC00, 0xD7AF },    // Hangul syllables
                };
                builder->AddMapping(krRanges, 5, krFamilies, 3);

                // --- CJK ideographs: ordered by the Windows UI language ---
                // Han unification shares code points across languages but
                // the glyph shapes differ, so whichever family sits first
                // decides the variant every ideograph renders with. The
                // default keeps Microsoft YaHei UI first (Chinese variants
                // and a consistent Regular weight instead of Yu Gothic UI's
                // visually-heavier strokes); Japanese and Korean systems
                // reorder so kanji/hanja match the kana or hangul around
                // them instead of coming out Simplified-Chinese (#155).
                const wchar_t* cjkZh[] = {
                    L"Microsoft YaHei UI", L"Yu Gothic UI", L"Meiryo", L"Malgun Gothic"
                };
                const wchar_t* cjkJa[] = {
                    L"Yu Gothic UI", L"Meiryo", L"Microsoft YaHei UI", L"Malgun Gothic"
                };
                const wchar_t* cjkKo[] = {
                    L"Malgun Gothic", L"Microsoft YaHei UI", L"Yu Gothic UI", L"Meiryo"
                };
                WORD uiLang = PRIMARYLANGID(GetUserDefaultUILanguage());
                const wchar_t** cjkFamilies =
                    uiLang == LANG_JAPANESE ? cjkJa
                    : uiLang == LANG_KOREAN ? cjkKo
                                            : cjkZh;
                DWRITE_UNICODE_RANGE cjkRanges[] = {
                    { 0x2E80, 0x303F },    // CJK radicals, Kangxi, CJK symbols & punctuation
                    { 0x3100, 0x312F },    // Bopomofo
                    { 0x31A0, 0x31EF },    // Bopomofo extended + CJK strokes
                    { 0x3220, 0x325F },    // Parenthesized/circled ideographs (㈠ ㊿)
                    { 0x3280, 0x32CF },    // Circled ideographs + months (㊀ ㋀)
                    { 0x3358, 0x33FF },    // CJK compatibility: units (㎜ ㎡ ㏄)
                    { 0x3400, 0x4DBF },    // CJK extension A
                    { 0x4DC0, 0x4DFF },    // Yijing hexagrams
                    { 0x4E00, 0x9FFF },    // CJK unified ideographs
                    { 0xF900, 0xFAFF },    // CJK compatibility ideographs
                    { 0xFE10, 0xFE1F },    // Vertical forms (CJK punctuation)
                    { 0xFE30, 0xFE4F },    // CJK compatibility forms
                    { 0xFF00, 0xFF64 },    // Fullwidth forms (Latin, punctuation, etc.)
                    { 0xFFA0, 0xFFEF },    // Halfwidth/fullwidth forms (Hangul + rest)
                    { 0x20000, 0x2FA1F },  // CJK extensions B-F
                };
                builder->AddMapping(cjkRanges, 15, cjkFamilies, 4);

                // Emoji/symbol fallback for everything else
                const wchar_t* emojiFamilies[] = {
                    L"Segoe UI Emoji", L"Segoe UI Symbol"
                };
                DWRITE_UNICODE_RANGE fullRange = { 0x0000, 0x10FFFF };
                builder->AddMapping(&fullRange, 1, emojiFamilies, 2);

                // Chain the system fallback so scripts not covered above
                // (Arabic, Thai, ...) still resolve instead of rendering tofu
                IDWriteFontFallback* systemFallback = nullptr;
                if (SUCCEEDED(factory2->GetSystemFontFallback(&systemFallback))) {
                    builder->AddMappings(systemFallback);
                    systemFallback->Release();
                }

                builder->CreateFontFallback(&app.fontFallback);
                builder->Release();
            }
            factory2->Release();
        }
    }

    updateOverlayFormats(app);
    app.seatTextFormats();
    app.appliedZoomFactor = app.zoomFactor;
    app.layoutDirty = true;
}

void updateOverlayFormats(App& app) {
    app.releaseOverlayFormats();

    float scale = app.contentScale;

    // Search overlay format
    app.dwriteFactory->CreateTextFormat(app.theme.fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        16.0f * scale, L"en-us", &app.nativeSearchTextFormat);

    // Signal chips: subtitles, badges, and the tiny state labels
    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        10.0f * scale, L"en-us", &app.nativeSignalSmallFormat);
    if (app.nativeSignalSmallFormat) {
        app.nativeSignalSmallFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    // Theme chooser formats
    app.dwriteFactory->CreateTextFormat(L"Segoe UI Light", nullptr,
        DWRITE_FONT_WEIGHT_LIGHT, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        28.0f * scale, L"en-us", &app.nativeThemeTitleFormat);

    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.0f * scale, L"en-us", &app.nativeThemeHeaderFormat);

    // Theme preview formats (3 per theme, several distinct font families) are
    // created lazily by ensureThemePreviewFormats when the chooser first opens
    // — they were ~30 CreateTextFormat calls on the startup critical path.
    // releaseOverlayFormats() above already cleared them; they rebuild at the
    // current scale on next use.

    // Stats overlay format: fixed UI size so zooming the document cannot
    // overflow the fixed stats box
    app.dwriteFactory->CreateTextFormat(app.theme.codeFontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f * scale, L"en-us", &app.nativeStatsFormat);

    // Folder browser format
    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f * scale, L"en-us", &app.nativeFolderBrowserFormat);
    if (app.nativeFolderBrowserFormat) {
        // Entries render on fixed-height rows: wrapping would overlap the next
        // row, so keep everything single-line and trim with an ellipsis
        app.nativeFolderBrowserFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* ellipsis = nullptr;
        app.dwriteFactory->CreateEllipsisTrimmingSign(app.nativeFolderBrowserFormat, &ellipsis);
        if (ellipsis) {
            app.nativeFolderBrowserFormat->SetTrimming(&trimming, ellipsis);
            ellipsis->Release();
        }
    }

    // TOC formats
    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f * scale, L"en-us", &app.nativeTocFormatBold);
    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.0f * scale, L"en-us", &app.nativeTocFormat);
    auto configureTocFormat = [&](IDWriteTextFormat* format) {
        if (!format) return;
        // TOC entries occupy fixed-height rows. Wrapping would paint into the
        // following entry, so trim long headings to one line.
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* ellipsis = nullptr;
        if (SUCCEEDED(app.dwriteFactory->CreateEllipsisTrimmingSign(format, &ellipsis)) &&
            ellipsis) {
            format->SetTrimming(&trimming, ellipsis);
            ellipsis->Release();
        }
    };
    configureTocFormat(app.nativeTocFormatBold);
    configureTocFormat(app.nativeTocFormat);

    // Editor text format (monospace, same size as body)
    float editorScale = app.contentScale * app.zoomFactor;
    float editorFontSize = 14.0f * editorScale;
    app.dwriteFactory->CreateTextFormat(app.theme.codeFontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        editorFontSize, L"en-us", &app.nativeEditorTextFormat);
    if (app.nativeEditorTextFormat) {
        app.nativeEditorTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        // Measure actual monospace character width and the font's natural
        // line metrics (before uniform spacing overrides them below)
        float naturalHeight = 0.0f, naturalBaseline = 0.0f;
        IDWriteTextLayout* measureLayout = nullptr;
        app.dwriteFactory->CreateTextLayout(L"M", 1, app.nativeEditorTextFormat,
            10000.0f, 100.0f, &measureLayout);
        if (measureLayout) {
            DWRITE_TEXT_METRICS metrics{};
            measureLayout->GetMetrics(&metrics);
            app.editorCharWidth = metrics.widthIncludingTrailingWhitespace;
            DWRITE_LINE_METRICS lm{};
            UINT32 lineCount = 0;
            if (SUCCEEDED(measureLayout->GetLineMetrics(&lm, 1, &lineCount)) &&
                lineCount >= 1) {
                naturalHeight = lm.height;
                naturalBaseline = lm.baseline;
            }
            measureLayout->Release();
        }
        // The editor positions every visual row on a fontSize*1.5 grid, but
        // by default each layout spaces its wrapped rows at the fonts' own
        // (tighter) line height - so a long wrapped paragraph drew shorter
        // than the rows it occupies, leaving a blank band before the next
        // line that grows with paragraph length, and skewing click/caret/
        // arrow math inside it (#181). Uniform spacing pins layout rows to
        // the grid; the baseline centers the natural line box in the cell.
        float editorLineHeight = editorFontSize * 1.5f;
        float editorBaseline = (naturalHeight > 0.0f && naturalBaseline > 0.0f)
            ? naturalBaseline + (editorLineHeight - naturalHeight) * 0.5f
            : editorFontSize * 1.2f;
        app.nativeEditorTextFormat->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
            editorLineHeight, editorBaseline);
    }

    // Slim line-number gutter (design t11): small mono digits,
    // right-aligned against the gutter's inner edge
    app.dwriteFactory->CreateTextFormat(L"Consolas", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 9.5f * editorScale, L"en-us",
        &app.nativeEditorGutterFormat);
    if (app.nativeEditorGutterFormat) {
        app.nativeEditorGutterFormat->SetTextAlignment(
            DWRITE_TEXT_ALIGNMENT_TRAILING);
        app.nativeEditorGutterFormat->SetParagraphAlignment(
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        app.nativeEditorGutterFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // Seating is idempotent, so calling it here as well as at the end of
    // updateTextFormats costs nothing and means this function is safe to call
    // on its own -- which it is, from the overlay and DPI paths.
    app.seatTextFormats();
}

void ensureThemePreviewFormats(App& app) {
    if (!app.themePreviewFormats.empty()) return;

    float scale = app.contentScale;
    app.themePreviewFormats.resize(themeCount());
    for (int i = 0; i < themeCount(); i++) {
        const D2DTheme& t = themeAt(i);
        app.dwriteFactory->CreateTextFormat(t.fontFamily, nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            14.0f * scale, L"en-us", &app.themePreviewFormats[i].nativeName);
        app.dwriteFactory->CreateTextFormat(t.fontFamily, nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            11.0f * scale, L"en-us", &app.themePreviewFormats[i].nativePreview);
        app.dwriteFactory->CreateTextFormat(t.codeFontFamily, nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            10.0f * scale, L"en-us", &app.themePreviewFormats[i].nativeCode);
    }

    // After the resize(), never before: the wrappers live inside the vector's
    // elements, so seating earlier would leave them pointing into a buffer that
    // resize() then reallocated.
    app.seatTextFormats();
}

void createTypography(App& app) {
    // Release existing typography objects
    if (app.bodyTypography) { app.bodyTypography->Release(); app.bodyTypography = nullptr; }
    if (app.codeTypography) { app.codeTypography->Release(); app.codeTypography = nullptr; }

    // Body typography - standard ligatures, kerning, contextual alternates
    app.dwriteFactory->CreateTypography(&app.bodyTypography);
    if (app.bodyTypography) {
        app.bodyTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES, 1});
        app.bodyTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_KERNING, 1});
        app.bodyTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_ALTERNATES, 1});
    }

    // Code typography - programming ligatures (for fonts like Cascadia Code, Fira Code)
    app.dwriteFactory->CreateTypography(&app.codeTypography);
    if (app.codeTypography) {
        app.codeTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES, 1});
        app.codeTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_DISCRETIONARY_LIGATURES, 1});
    }
}

// Every device bitmap dies with the render target that made it, so a target
// swap has to drop them all. This is APP policy, not device policy: which
// caches exist, and how a dropped entry gets refilled, has nothing to do with
// which backend is in use. It lives apart from the creation code below for
// that reason -- when creation moves behind a per-backend factory, this stays
// exactly where it is.
//
// Entries are ERASED, not just nulled: getOrLoadImage returns any existing
// entry as-is, so a kept-but-empty entry would render as the alt-text
// placeholder forever instead of reloading. In-flight downloads keep their
// entry — the arriving result creates its bitmap on whatever target exists
// then.
static void invalidateDeviceBitmaps(App& app) {
    app.imageCacheBytes = 0;
    for (auto it = app.imageCache.begin(); it != app.imageCache.end();) {
        if (it->second.bitmap) { it->second.bitmap->Release(); it->second.bitmap = nullptr; }
        it->second.bytes = 0;
        if (it->second.pending) { ++it; }
        else { it = app.imageCache.erase(it); }
    }
    // The title-bar icon is a device bitmap too; the strip recreates it
    if (app.nativeTitleIconBitmap) {
        app.nativeTitleIconBitmap->Release();
        app.nativeTitleIconBitmap = nullptr;
    }
    if (app.nativeStartPageIconBitmap) {
        app.nativeStartPageIconBitmap->Release();
        app.nativeStartPageIconBitmap = nullptr;
    }
}

// ClearType plus custom DWrite rendering params, applied to whatever target
// was just created. Separate because this is per-device TEXT STATE and not
// construction -- a distinction worth keeping visible, since grouping these
// two calls with the creation calls is exactly the mislabel that made the
// remaining work look like six construction sites when it is four.
static void configureTextRendering(App& app) {
    // Enable high-quality text
    app.nativeRenderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    // Create custom rendering params for improved text quality
    IDWriteRenderingParams* defaultParams = nullptr;
    IDWriteRenderingParams* customParams = nullptr;

    app.dwriteFactory->CreateRenderingParams(&defaultParams);
    if (defaultParams) {
        app.dwriteFactory->CreateCustomRenderingParams(
            defaultParams->GetGamma(),
            defaultParams->GetEnhancedContrast(),
            1.0f,  // ClearType level
            defaultParams->GetPixelGeometry(),
            DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
            &customParams
        );
        defaultParams->Release();

        if (customParams) {
            app.nativeRenderTarget->SetTextRenderingParams(customParams);
            customParams->Release();
        }
    }
}

bool createRenderTarget(App& app) {
    if (app.nativeRenderTarget) {
        app.nativeRenderTarget->Release();
        app.nativeRenderTarget = nullptr;
        app.renderTarget = nullptr;
    }
    if (app.nativeBrush) {
        app.nativeBrush->Release();
        app.nativeBrush = nullptr;
        app.brush = nullptr;
    }

    invalidateDeviceBitmaps(app);

    RECT rc;
    GetClientRect(app.hwnd, &rc);

    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    rtProps.type = app.useHardwareRT ? D2D1_RENDER_TARGET_TYPE_DEFAULT
                                     : D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    rtProps.dpiX = 96.0f;
    rtProps.dpiY = 96.0f;
    rtProps.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    rtProps.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    HRESULT hr = app.d2dFactory->CreateHwndRenderTarget(
        rtProps,
        D2D1::HwndRenderTargetProperties(app.hwnd, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        &app.nativeRenderTarget
    );
    if (FAILED(hr)) return false;

    hr = app.nativeRenderTarget->CreateSolidColorBrush(tinta::colorF(1, 1, 1), &app.nativeBrush);
    if (FAILED(hr)) return false;

    // Cache device context for color emoji rendering
    if (app.deviceContext) { app.deviceContext->Release(); app.deviceContext = nullptr; }
    app.nativeRenderTarget->QueryInterface(__uuidof(ID2D1DeviceContext),
        reinterpret_cast<void**>(&app.deviceContext));

    configureTextRendering(app);

    // The drawing sites reach the device through app.renderTarget and app.brush,
    // so the wrappers have to be seated before the first frame. Both natives are
    // non-null here; every early return above leaves them null and the matching
    // seam pointer null with them, which is what callers already null-check.
    app.seatRenderTarget();
    app.seatBrush();
    app.seatDashedStrokeStyle();

    return true;
}

// System font families for the theme editor font picker (#82). Enumerated
// once per session, sorted, en-US names preferred with first-locale
// fallback so localized-only families still appear.
void enumerateSystemFontFamilies(App& app) {
    if (!app.systemFontFamilies.empty() || !app.dwriteFactory) return;
    IDWriteFontCollection* fonts = nullptr;
    if (FAILED(app.dwriteFactory->GetSystemFontCollection(&fonts)) || !fonts) return;
    UINT32 count = fonts->GetFontFamilyCount();
    app.systemFontFamilies.reserve(count);
    for (UINT32 i = 0; i < count; i++) {
        IDWriteFontFamily* family = nullptr;
        if (FAILED(fonts->GetFontFamily(i, &family)) || !family) continue;
        IDWriteLocalizedStrings* names = nullptr;
        if (SUCCEEDED(family->GetFamilyNames(&names)) && names) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            names->FindLocaleName(L"en-us", &index, &exists);
            if (!exists) index = 0;
            wchar_t name[128] = {};
            if (SUCCEEDED(names->GetString(index, name, 128)) && name[0]) {
                app.systemFontFamilies.push_back(name);
            }
            names->Release();
        }
        family->Release();
    }
    fonts->Release();
    std::sort(app.systemFontFamilies.begin(), app.systemFontFamilies.end());
}
