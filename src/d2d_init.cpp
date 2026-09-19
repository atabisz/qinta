#include "d2d_init.h"
#include "tabs.h"
#include "math_render.h"
#include "text_formats.h"
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
        // No CreateTextAnalyzer here any more. It used to run on EVERY start, for a
        // COM object only the non-ASCII line-breaking slow path ever touched;
        // D2DTextFactory now creates one lazily behind TextFactory::AnalyzeLineBreaks.
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
    if (app.hwnd) {
        requestRepaint(app.hwnd);
    }
}

// THE NATIVE measureText OVERLOAD WAS HERE AND IS DELETED. It took an IDWriteTextFormat*
// and measured through dwriteFactory->CreateTextLayout, and its four callers were the
// space-width caches in updateTextFormats; those went through the seam when the eleven
// document formats stopped being native, which orphaned it. The one remaining measureText
// is the portable overload in src/text_utils_app.cpp, taking tinta::TextFormat*, and all
// 40 call sites in the tree resolve to it.
//
// Worth recording HOW the deletion was cleared, because "no callers" was a grep and a grep
// is not a usage census: the function was removed temporarily on 3645c8d and the whole tree
// rebuilt, which linked clean across 51 link actions with all four d2d_init.h-including test
// targets recompiled. The compiler agreeing is the evidence; the grep only chose where to
// point it.

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
    // src/qt_init.cpp had a second one that this file could not see.
    //
    // The reason this is a deletion rather than a port: installQtTextFormats already built
    // exactly these sixteen objects, with the same families, sizes and weights, through
    // tinta::TextFactory. Two parallel copies of a table of numbers, one of them naming
    // DirectWrite. Its own comment said a number disagreeing with this file "is a
    // divergence, not a Qt decision", which names the hazard without removing it.
    //
    // It is safe to call before the fallback chain below exists: a fallback is attached to
    // a LAYOUT, not to a format, and the handoff further down runs unconditionally on
    // every call for exactly that reason.
    installDocumentTextFormats(app);

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

    // Hand the chain to the text factory so a layout can attach it WITHOUT render.cpp
    // holding a DirectWrite interface. App still owns and releases it -- the factory
    // borrows, which is why this is a raw pointer and not a transfer.
    //
    // OUTSIDE the `if (!app.fontFallback)` block above on purpose. That block is a
    // build-once guard, so on the second and every later call to updateTextFormats it
    // does nothing -- and a factory reseated in between (a device loss recreates the
    // backend) would never be told. Setting it unconditionally here costs one store per
    // format update and cannot go stale.
    //
    // The static_cast is safe because app.textFactory is seated only from
    // App::seatTextFactory, which constructs a D2DTextFactory and nothing else. This is
    // the same downcast-with-a-stated-reason shape as nativeTextLayout, one layer up.
    if (app.textFactory) {
        static_cast<D2DTextFactory*>(app.textFactory)->setFontFallback(app.fontFallback);
    }

    // The chrome formats, through the shared portable function -- and NOT followed by a
    // seating call any more. See the tombstone below for why that pairing had to break in
    // one commit rather than two.
    installChromeFormats(app);
    app.appliedZoomFactor = app.zoomFactor;
    app.layoutDirty = true;
}

// THE TWENTY CHROME FORMATS ARE NO LONGER BUILT HERE, and with them goes the last
// CreateTextFormat call in this tree's Windows backend. updateOverlayFormats' ten and
// ensureThemePreviewFormats' three-per-theme are installChromeFormats and
// ensureThemePreviewFormats in src/text_formats_app.cpp, portable, called by both stacks.
//
// THE DELETION WAS THE EASY HALF. What made this slice wait twice -- and the phase note of
// two earlier ISAs says so in the same words both times -- is that chrome never appears on a
// printed page, so the --printpages byte-identity rig that made the last three ports
// trustworthy is structurally blind to it. The answer was an instrument, not a bigger
// deletion: `tinta.exe --dumpchrome <file>` (src/chrome_dump_d2d.cpp) reads all forty formats
// back off the NATIVE objects through nativeTextFormat(), eleven properties each, and it
// survives this deletion because it resolves seam pointers rather than reading App's natives.
// The before-and-after of that dump is the evidence this change rests on.
//
// AND THE INSTRUMENT PAID FOR ITSELF BEFORE THE PORT RAN. Its first reading printed
// `stats family="Cascadia Code"`, contradicting four published sites that said the default
// theme's code face was Consolas -- the stated reason a Qt typography divergence was safe.
// The conclusion held; the reason and the scope estimate did not. See tests/typography_tests.cpp.
//
// WHAT WENT WITH THEM, and it had to go in the SAME commit rather than as tidy-up:
// App::seatTextFormats and App::seatFormat, plus the thirteen native chrome members they
// seated. A surviving seatTextFormats() would have called seatFormat with a null native for
// every chrome format, and seatFormat's first act on a null native is storage.reset() -- so it
// would have destroyed the formats installChromeFormats had just created, on a path that runs
// after every theme change and every zoom step. app.h said this rule when the DOCUMENT
// formats moved and said the compiler only enforces it because the native members went too.
// That is why deleting them was mandatory here and not cosmetic.
//
// THE THREE seatTextFormats() CALLS THIS FILE HELD ARE GONE FOR THE SAME REASON -- one at the
// end of updateTextFormats above, one at the end of each deleted function. The comment on the
// last of them read "Seating is idempotent, so calling it here as well... costs nothing",
// which was true and is now the wrong question: there is nothing to seat.

// THE TYPOGRAPHY SETS ARE NO LONGER BUILT HERE. createTypography is
// installTypography in src/text_formats_app.cpp, called by BOTH initialisers -- and
// the deferral that kept it in this file is worth one sentence because it expired
// silently. It read "PORTABLE AS WRITTEN, AND DELIBERATELY LEFT HERE... there is no
// portable init file yet to move it INTO", which was true when written and stopped
// being true the moment src/text_formats_app.cpp landed one slice later. A refusal
// whose premise is another file's absence has no gate on it: nothing in the tree
// re-reads it when that file arrives.
//
// The cost of the delay was not tidiness. The function had ONE caller,
// src/main_d2d.cpp, so the Qt stack ran with two empty feature sets and applied no
// OpenType features at all. A function that names no Windows type can still be
// Windows-only.
//
// THIS PARAGRAPH ENDED WITH "-- invisible, because every tag Qt honours here is on by
// default anyway", AND THAT WAS MEASURED FALSE IN THE SAME SLICE THAT WROTE IT. Invisible
// on Qt, yes, and that is not the comparison that matters: on THIS backend, printing 121
// pages across four fixtures, installing no typography differs from the shipped rendering
// on 85 of them, and installing the three body tags while dropping `kern` differs on 112.
// So the absent call was a real divergence between the two stacks, not a cosmetic one --
// which is also why deleting this function was worth a slice. The full table, including
// the mechanism I predicted and failed to confirm, is at the call site in src/qt_init.cpp.

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
        // reset(), not Release(): the entry holds a shared_ptr<tinta::Bitmap> now.
        // Worth noting what changed underneath, because the line reads the same:
        // app.layoutBitmaps used to hold raw pointers BORROWED from these entries, so
        // this release depended on a relayout landing before the next paint. Those
        // elements now keep their own reference, so the pixels survive here until the
        // relayout replaces them.
        it->second.bitmap.reset();
        it->second.bytes = 0;
        if (it->second.pending) { ++it; }
        else { it = app.imageCache.erase(it); }
    }
    // Both chrome icons are device bitmaps too; the strip and the start page each
    // recreate theirs on the next paint, from the embedded bytes rather than from a
    // Win32 resource handle. reset() for the same reason the cache entries above use it:
    // they are shared_ptr<tinta::Bitmap> holders now, so dropping this reference is the
    // whole release. THE INVALIDATION ITSELF STILL MATTERS AND IS NOT A LEFTOVER -- a
    // D2D bitmap belongs to the device that made it, so a lost device must clear these
    // whatever type holds them, or the next paint draws through a dead device bitmap.
    app.titleIconBitmap.reset();
    app.startPageIconBitmap.reset();
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
    // No seatDashedStrokeStyle() here any more, and its absence is deliberate rather
    // than an omission. App holds the dashed stroke style as one owning
    // unique_ptr<tinta::StrokeStyle> built by ensureDashedStrokeStyle, so there is no
    // native to seat -- and because this function runs on device RECREATION as well as
    // creation, a surviving call would have destroyed a live style every time the device
    // came back. The style is a factory object and outlives the device by design.
    app.seatRenderTarget();
    app.seatBrush();

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
