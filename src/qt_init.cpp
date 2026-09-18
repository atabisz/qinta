#include "qt_init.h"

#include <memory>

#include "app.h"
#include "render_qt.h"
#include "text_api.h"
#include "text_formats.h"
#include "text_qt.h"
#include "theme_data.h"
#include "utils.h"

// The first callers of Seated<>::install() in the tree. Measured before they were
// written: a scratch probe on 2026-09-16 built a QApplication + App + QWidget that
// installed a QtRenderTarget and a QtTextFactory through these two storages, linked
// with zero undefined symbols, and read the painted pixels back out of
// QWidget::grab() -- amber inside the filled rectangle, the Clear colour outside.
// Two distinct Seated<> instantiations, so one working said nothing about the other
// and both were exercised.

namespace {

// A one-line-per-format helper, because the alternative is ten three-line blocks that
// differ only in the arguments and hide a copy-paste error beautifully.
//
// THE CHROME FORMATS ONLY, now that the document formats build through
// installDocumentTextFormats. This one takes a Seated<> because the chrome formats are
// still created NATIVELY on Windows -- updateOverlayFormats has not been ported -- so
// their storage still has a native to wrap and compare. The document formats' storage is
// a plain unique_ptr. Two shapes for the same job is the honest state of a half-finished
// port rather than a design, and the second one goes when updateOverlayFormats follows.
tinta::TextFormat* makeFormat(App& app, Seated<tinta::TextFormat, IDWriteTextFormat>& storage,
                              const wchar_t* family, tinta::FontWeight weight,
                              tinta::FontStyle style, float size) {
    std::unique_ptr<tinta::TextFormat> format = app.textFactory->CreateTextFormat(
        family, weight, style, tinta::FontStretch::Normal, size, L"en-us");
    if (!format) return nullptr;
    // Every format updateOverlayFormats builds gets this too, as a separate `if (native...)`
    // statement per format. Set here instead so a new format cannot be added without it.
    format->SetParagraphAlignment(tinta::ParagraphAlignment::Near);
    return storage.install(std::move(format));
}

// THE DOCUMENT FORMATS HAVE LEFT THIS FILE. installQtTextFormats' body is now
// installDocumentTextFormats in src/text_formats_app.cpp, called by BOTH initialisers,
// and this is a plain forwarding call.
//
// THE PARAGRAPH THAT USED TO STAND HERE IS THE REASON IT MOVED, so it is quoted rather
// than deleted: "the numbers below are copied from updateTextFormats rather than chosen...
// A number that disagrees with d2d_init is a divergence, not a Qt decision." That names
// the hazard exactly and does nothing about it -- two parallel copies of a table of
// sixteen font sizes, weights and families, one per backend, with no instrument that
// would notice them drifting apart. There is one copy now.
//
// WHY THIS FUNCTION HAD TO EXIST AT ALL, and it was the finding of the slice that wrote
// it rather than a planned step: the Qt window segfaulted on the first document it was
// given, at src/render.cpp:378, dereferencing a null baseFormat that came from
// layoutHeading's app.headingFormats[levelIndex] ? ... : app.textFormat -- both null.
// Every one of these formats was created by DirectWrite in d2d_init.cpp, so off Windows
// App had a text FACTORY and no text FORMATS, and nothing in the tree said so: the
// placeholder line built its own format on the stack, and no test had ever laid out a
// document off Windows. A backtrace found it in one run; no amount of reading would have.
// Kept here because it is the evidence for why the shared function is shared, and the
// crash was on this side of the seam.
//
// THE CHROME FORMATS USED TO BE "DELIBERATELY NOT HERE" AND NOW THEY ARE BELOW, in
// installQtChromeFormats. The paragraph that stood here said they were the ~25 chrome
// formats and that creating them would be sixteen more unasserted lines; both halves
// were wrong and are corrected rather than deleted, because the reasoning was sound and
// only the arithmetic was not.
//
// The count: 25 is every IDWriteTextFormat* member in app.h, of which this function
// already covers ten plus the six-entry heading array. The chrome remainder is EIGHT
// overlay formats plus TWO editor formats plus a per-theme triple -- ten flat formats,
// not twenty-five, mirroring a pattern that already works ten times here.
//
// "Unasserted" was the load-bearing error. The chrome formats are not ten more
// CreateTextFormat calls: five of them configure wrapping, trimming, alignment or line
// spacing afterwards, and those calls are assertable without drawing a pixel of chrome
// -- an elided row is shorter than an unelided one, a right-aligned line starts further
// right, a uniform-spaced paragraph is taller. Measuring that found QtTextFormat's
// SetLineSpacing storing three values that nothing read (see include/text_qt.h's
// uniformLineHeight comment), which is precisely the defect "we will add them when
// something draws chrome" would have shipped into the first editor slice.
//
// THE SPACE WIDTHS ARE MEASURED, not defaulted, through the seam overload of measureText
// (src/text_utils_app.cpp) -- the one function that got its own translation unit for
// exactly this call. render.cpp's getSpaceWidth reads them and a zero there collapses
// every inter-word gap in the document, which draws and looks almost right, so it is the
// kind of default that would have survived a pixel count. That call moved with the body;
// the note stays because it is why the body has to measure rather than assume.
void installQtTextFormats(App& app) { installDocumentTextFormats(app); }

// A single-line row that must not wrap and must trim with an ellipsis. Three formats
// want exactly this (folder browser, TOC, TOC bold) and on Windows it is written twice
// -- once inline for the folder browser at src/d2d_init.cpp:390-401 and once as a lambda
// for the two TOC formats at :410-423. One helper here rather than two, because the
// duplication over there is what let the two copies differ: the folder browser's ignores
// the HRESULT from CreateEllipsisTrimmingSign and the TOC lambda checks it.
void configureTrimmedRow(App& app, tinta::TextFormat* format) {
    if (!format) return;

    // Order matters and it is the same order Windows uses: NoWrap first, because
    // QtTextRun's trimming branch is gated on `noWrap` (src/text_qt.cpp:165) and would
    // skip the elide entirely if the wrapping were still Wrap when the layout ran.
    // Nothing here depends on the ORDER OF THESE TWO CALLS -- both just record into
    // QtTextStyle -- but a reader checking against d2d_init should find the same shape.
    format->SetWordWrapping(tinta::WordWrapping::NoWrap);

    std::unique_ptr<tinta::InlineObject> ellipsis =
        app.textFactory->CreateEllipsisTrimmingSign(format);

    // The trimming is set EVEN IF the sign is null, which is d2d_init's behaviour and
    // not an accident: DWRITE_TRIMMING with a null sign trims without drawing an
    // ellipsis, and QtTextRun maps that to Qt::ElideNone (src/text_qt.cpp:170), so both
    // backends degrade to "cut, no dots" rather than to "no trimming at all".
    const tinta::Trimming trimming{tinta::TrimmingGranularity::Character, 0, 0};
    format->SetTrimming(trimming, ellipsis.get());

    // AFTER SetTrimming, and the vector is why this function takes App at all. See
    // App::trimmingSigns in include/app.h: DirectWrite retains the sign and Qt does
    // not, so the only caller that is correct against both backends is one that keeps
    // it alive itself.
    if (ellipsis) app.trimmingSigns.push_back(std::move(ellipsis));
}

// THE CHROME FORMATS. The Qt counterpart of updateOverlayFormats
// (src/d2d_init.cpp:346-505), and as with the document formats above, every number is
// COPIED from there rather than chosen.
//
// THE SCALE IS THE SUBTLE PART AND IT IS NOT ONE SCALE BUT TWO. The eight overlay
// formats use app.contentScale ALONE -- no zoomFactor -- and d2d_init says why at :378:
// "fixed UI size so zooming the document cannot overflow the fixed stats box". The two
// editor formats use contentScale * zoomFactor, because editor text IS document text.
// Getting this backwards would make chrome grow with Ctrl+= and look like a feature.
//
// Families: the theme's fontFamily for search, its codeFontFamily for stats and editor
// text, a literal L"Segoe UI" for signals, theme header, folder browser and both TOC
// formats, L"Segoe UI Light" for the theme title, and L"Consolas" for the gutter. THE
// LITERALS ARE COPIED VERBATIM AND THEY ARE A KNOWN DIVERGENCE OFF WINDOWS: neither
// Segoe UI nor Consolas exists on a stock Linux box, so Qt substitutes. That is the
// right thing to copy anyway -- a different family here would make the two stacks
// disagree on purpose, and the substitution is Qt's job and already happens for the
// document formats, whose theme families are equally Windows-flavoured.
void installQtChromeFormats(App& app) {
    if (!app.textFactory) return;

    // contentScale ONLY. See the paragraph above.
    const float scale = app.contentScale;
    const wchar_t* segoe = L"Segoe UI";

    using W = tinta::FontWeight;
    using S = tinta::FontStyle;

    app.searchTextFormat = makeFormat(app, app.searchTextFormatStorage,
                                      app.theme.fontFamily, W::Normal, S::Normal,
                                      16.0f * scale);

    // SemiBold, 10 DIP. The only format whose Windows site sets ParagraphAlignment
    // explicitly (d2d_init.cpp:361-363) -- makeFormat already does that for every
    // format here, which is the reason it does it there rather than at each call.
    app.signalSmallFormat = makeFormat(app, app.signalSmallFormatStorage, segoe,
                                       W::SemiBold, S::Normal, 10.0f * scale);

    app.themeTitleFormat = makeFormat(app, app.themeTitleFormatStorage,
                                      L"Segoe UI Light", W::Light, S::Normal,
                                      28.0f * scale);
    app.themeHeaderFormat = makeFormat(app, app.themeHeaderFormatStorage, segoe,
                                       W::SemiBold, S::Normal, 11.0f * scale);

    app.statsFormat = makeFormat(app, app.statsFormatStorage, app.theme.codeFontFamily,
                                 W::Normal, S::Normal, 13.0f * scale);

    app.folderBrowserFormat = makeFormat(app, app.folderBrowserFormatStorage, segoe,
                                         W::Normal, S::Normal, 13.0f * scale);
    configureTrimmedRow(app, app.folderBrowserFormat);

    // Bold at 13, plain at 12 -- the sizes DIFFER as well as the weights, which is easy
    // to miss reading d2d_init.cpp:404-409 where the two calls are adjacent.
    app.tocFormatBold = makeFormat(app, app.tocFormatBoldStorage, segoe, W::Bold,
                                   S::Normal, 13.0f * scale);
    app.tocFormat = makeFormat(app, app.tocFormatStorage, segoe, W::Normal, S::Normal,
                               12.0f * scale);
    configureTrimmedRow(app, app.tocFormatBold);
    configureTrimmedRow(app, app.tocFormat);

    // THE EDITOR FORMATS, on the OTHER scale.
    const float editorScale = app.contentScale * app.zoomFactor;
    const float editorFontSize = 14.0f * editorScale;

    app.editorTextFormat = makeFormat(app, app.editorTextFormatStorage,
                                      app.theme.codeFontFamily, W::Normal, S::Normal,
                                      editorFontSize);
    if (app.editorTextFormat) {
        app.editorTextFormat->SetWordWrapping(tinta::WordWrapping::NoWrap);

        // THE MEASUREMENT GOES THROUGH THE SEAM HERE, WHERE ON WINDOWS IT STAYS NATIVE,
        // and the asymmetry is deliberate rather than an inconsistency. d2d_init.cpp
        // explains its own choice at :433-451: at that point in that file
        // app.editorTextFormat is still null, because seatTextFormats() ran forty lines
        // earlier, so a seam call there would read null and measure nothing. Here the
        // wrapper is the thing makeFormat just returned, so there is no null to read
        // and no ordering to get wrong -- Seated<>::install() hands back the live
        // pointer. Same rule, opposite consequence.
        std::unique_ptr<tinta::TextLayout> measure =
            app.textFactory->CreateTextLayout(L"M", 1, app.editorTextFormat,
                                              10000.0f, 100.0f);
        float naturalHeight = 0.0f;
        float naturalBaseline = 0.0f;
        if (measure) {
            tinta::TextMetrics metrics{};
            measure->GetMetrics(&metrics);
            app.editorCharWidth = metrics.widthIncludingTrailingWhitespace;

            tinta::LineMetrics lm{};
            uint32_t lineCount = 0;
            if (measure->GetLineMetrics(&lm, 1, &lineCount) && lineCount >= 1) {
                naturalHeight = lm.height;
                naturalBaseline = lm.baseline;
            }
        }

        // Copied arithmetic, issue #181's fix: rows sit on a fontSize*1.5 grid and the
        // baseline centres the natural line box inside the cell. The fallback
        // (fontSize * 1.2f) is d2d_init's and fires when the metrics came back zero.
        //
        // AND THIS IS THE CALL THAT WAS A NO-OP UNTIL THIS COMMIT. QtTextFormat stored
        // all three arguments and nothing read them; QtTextRun honours them now. The
        // ORDER matters against the measurement above: the natural metrics have to be
        // read BEFORE uniform spacing overrides them, which is why d2d_init measures
        // first too and says so.
        const float editorLineHeight = editorFontSize * 1.5f;
        const float editorBaseline =
            (naturalHeight > 0.0f && naturalBaseline > 0.0f)
                ? naturalBaseline + (editorLineHeight - naturalHeight) * 0.5f
                : editorFontSize * 1.2f;
        app.editorTextFormat->SetLineSpacing(tinta::LineSpacingMethod::Uniform,
                                             editorLineHeight, editorBaseline);
    }

    // 9.5 DIP Consolas, right-aligned against the gutter's inner edge, vertically
    // centred in its row. All three of these are honoured by QtTextRun today.
    app.editorGutterFormat = makeFormat(app, app.editorGutterFormatStorage, L"Consolas",
                                        W::Normal, S::Normal, 9.5f * editorScale);
    if (app.editorGutterFormat) {
        app.editorGutterFormat->SetTextAlignment(tinta::TextAlignment::Trailing);
        app.editorGutterFormat->SetParagraphAlignment(tinta::ParagraphAlignment::Center);
        app.editorGutterFormat->SetWordWrapping(tinta::WordWrapping::NoWrap);
    }
}

// Moved out with its install half, for the reason the pair exists at all: a release that
// lives beside its creation cannot fall out of step with it. Both are now in
// src/text_formats_app.cpp and both are called from both backends -- which is also what
// gave the Windows path a release that actually releases, since the block it replaces
// nulled eleven raw IDWriteTextFormat* members without calling Release on any of them.
void releaseQtTextFormats(App& app) { releaseDocumentTextFormats(app); }

void releaseQtChromeFormats(App& app) {
    auto drop = [](tinta::TextFormat*& pointer,
                   Seated<tinta::TextFormat, IDWriteTextFormat>& storage) {
        pointer = nullptr;
        storage.install(nullptr);
    };
    drop(app.searchTextFormat, app.searchTextFormatStorage);
    drop(app.signalSmallFormat, app.signalSmallFormatStorage);
    drop(app.themeTitleFormat, app.themeTitleFormatStorage);
    drop(app.themeHeaderFormat, app.themeHeaderFormatStorage);
    drop(app.statsFormat, app.statsFormatStorage);
    drop(app.folderBrowserFormat, app.folderBrowserFormatStorage);
    drop(app.tocFormat, app.tocFormatStorage);
    drop(app.tocFormatBold, app.tocFormatBoldStorage);
    drop(app.editorTextFormat, app.editorTextFormatStorage);
    drop(app.editorGutterFormat, app.editorGutterFormatStorage);

    for (App::ThemePreviewFormats& slot : app.themePreviewFormats) {
        drop(slot.name, slot.nameStorage);
        drop(slot.preview, slot.previewStorage);
        drop(slot.code, slot.codeStorage);
    }
    // CLEARED, not just dropped: ensureQtThemePreviewFormats keys its early return on
    // the vector being EMPTY, so leaving ten seated-null slots behind would make it a
    // permanent no-op and the chooser would find null formats forever after the first
    // theme change. The Windows counterpart clears too (App::releaseOverlayFormats).
    app.themePreviewFormats.clear();

    // AFTER the formats that reference them. Nothing in either backend reads a sign
    // back, so this is not a dangling-pointer fix today -- QtTextFormat kept a bool and
    // DirectWrite holds its own reference. It is ordered this way because the reverse
    // reads as "free the thing the format points at, then the format", and the next
    // person to add a backend that DOES retain the raw pointer will read this order
    // rather than discover the rule.
    app.trimmingSigns.clear();

    // The measured width goes with the format that produced it, for the same reason the
    // space widths go with theirs above: a stale editorCharWidth against a released
    // format survives a reinstall silently, because the rebuilt format measures the same
    // until the moment a DPI or zoom change means it does not.
    app.editorCharWidth = 0.0f;
}

}  // namespace

void installQtBackends(App& app) {
    app.textFactory = app.textFactoryStorage.install(std::make_unique<QtTextFactory>());

    // THE BRUSH IS PROCESS-LIFETIME HERE AND DEVICE-LIFETIME ON WINDOWS, and the
    // asymmetry is real rather than an oversight. Direct2D's brush is created by the
    // render target (src/d2d_init.cpp calls CreateSolidColorBrush on it) and dies with
    // it, so d2d_init reseats one per device. QtBrush holds a tinta::Color and nothing
    // else -- see include/render_qt.h:203 -- so it depends on no device and one
    // instance serves every paint.
    //
    // It has to exist for the document paint pass to run at all: every drawing site in
    // src/paint_document.cpp sets a colour on app.brush before it draws, and that
    // function returns without drawing when the brush is null. Before this commit
    // nothing off Windows ever put one in App, because the placeholder line built a
    // QtBrush on its own stack.
    //
    // The initial colour is arbitrary and deliberately so: every site calls SetColor
    // first, so a wrong colour here would be invisible, which is why it is the theme's
    // text colour rather than something eye-catching that would imply it mattered.
    app.brush = app.brushStorage.install(std::make_unique<QtBrush>(app.theme.text));

    // AFTER the factory, which creates them, and after the theme is whatever it is going
    // to be: the sizes and families below are baked in at creation, exactly as on
    // Windows, where applyTheme calls updateTextFormats again on a font change. Nothing
    // on this stack changes the theme yet; when something does, it calls this pair.
    installQtTextFormats(app);

    // AND THE CHROME FORMATS, unconditionally, where Windows splits them across two
    // entry points -- and that difference is worth a sentence because it is the one place
    // this file does NOT mirror d2d_init's shape. There, updateTextFormats runs at
    // startup and updateOverlayFormats runs from a device-lost rebuild and a DPI change
    // as well, so it begins by calling app.releaseOverlayFormats() to make itself
    // idempotent. Here there is one caller and one lifetime, so the release is
    // releaseQtBackends' job and this stays a plain install.
    //
    // THE THEME PREVIEWS ARE NOT HERE, deliberately: they are ~30 formats across every
    // theme, wanted only when the chooser opens, and ensureQtThemePreviewFormats below is
    // the lazy door -- same division of labour as Windows, for the same reason.
    installQtChromeFormats(app);

    // AND THE TYPOGRAPHY, WHICH THIS STACK HAD NEVER HAD. createTypography lived in
    // src/d2d_init.cpp with one caller in WinMain, so App::bodyTypography and
    // App::codeTypography were default-constructed here -- count 0 -- and
    // QtTextLayout::SetTypography returns at its first line on an empty set. All 13 of
    // render.cpp's createLayout sites pass one of those two members, so this stack applied
    // no OpenType feature list to anything.
    //
    // THIS PARAGRAPH USED TO SAY "IT DREW THE SAME ANYWAY", and it was written before the
    // measurement that refutes it. Kept as a correction rather than replaced, because the
    // reasoning behind it is the reasoning anyone would reach for: liga and calt are
    // HarfBuzz defaults already on, and kern maps to QTextCharFormat::setFontKerning whose
    // Qt default is already true, so installing the sets moves nothing HERE. All of that is
    // still true, and it is not the whole claim.
    //
    // WHAT IT MISSED: on the Windows path typography is load-bearing, measured on 121
    // printed pages across four fixtures (README.md plus three tests/fixtures documents,
    // via --printpages, which exists only on that backend):
    //
    //   * installing NOTHING, which is what this stack did until today:  85 of 121 pages
    //     differ from the shipped rendering.
    //   * installing the three but dropping kern:                       112 of 121 differ.
    //   * adding clig, ccmp, locl, mark and mkmk to the three:            0 of 121 differ.
    //
    // So the absent call was never cosmetic. It means the Qt stack has been rendering on
    // the 85-pages-different side of that comparison for its whole life, and no instrument
    // said so because the sensitive one only runs on Windows.
    //
    // WHAT THIS LINE THEREFORE DOES AND DOES NOT FIX. It makes the two stacks agree on the
    // DATA -- both now hold the same two sets, from one definition. It does NOT make them
    // agree on the OUTPUT, and this comment should not be read as claiming parity:
    // QtTextLayout::SetTypography can act on exactly one of the three tags, and that one is
    // already Qt's default, so Qt keeps rendering with shaper defaults while Windows renders
    // with an explicitly attached set worth 85 pages of difference. That gap is now a named,
    // measured, OPEN item rather than an invisible one, and closing it is a typography
    // decision for Alex (it needs QFont::setFeature, so a Qt floor of 6.7) rather than
    // something to settle inside an init function.
    //
    // The third measurement above is the one I got wrong: I predicted that adding the
    // commonly-defaulted features back would pull the rendering toward the no-typography
    // arm, on the documented reading that SetTypography REPLACES a range's default features.
    // It changed nothing at all, so that reading does not explain the 85 pages and I have no
    // measurement that does. Left stated as an open question rather than as a mechanism.
    //
    // Asserted by tests/qt_entry_tests.cpp, whose two typography checks FAILED reading
    // count 0 before this line existed.
    //
    // Position: after the formats, though nothing orders the two -- typography is two
    // values on App and reads no format. It sits last because it is the cheapest and a
    // reader looking for "what does init do" should meet the expensive things first.
    installTypography(app);
}

// THE PER-THEME PREVIEW TRIPLE, LAZILY, exactly as ensureThemePreviewFormats does it
// (src/d2d_init.cpp:502-531). Three formats per theme and several distinct families, so
// on Windows this was ~30 CreateTextFormat calls that would otherwise sit on the startup
// critical path; it runs when the theme chooser first opens and not before.
//
// PUBLIC RATHER THAN FILE-STATIC, unlike installQtChromeFormats, and the asymmetry is
// evidence-driven rather than stylistic. Every other helper in this file has a caller
// inside it, so a test reaching installQtBackends exercises them all. This one's future
// caller is src/overlays.cpp:1467, which is 4,108 lines of unported Windows code, so
// left static it would be a function with no caller and no reading -- and a Seated<>
// wrapper vector that has never been instantiated is exactly the shape include/seated.h
// warned about when install() had zero callers in the whole tree. Declared in
// include/qt_init.h so tests/qt_entry_tests.cpp can call it and assert the triple.
void ensureQtThemePreviewFormats(App& app) {
    if (!app.textFactory) return;
    if (!app.themePreviewFormats.empty()) return;

    // contentScale alone, like every other overlay format. The chooser is chrome.
    const float scale = app.contentScale;

    using W = tinta::FontWeight;
    using S = tinta::FontStyle;

    // THE RESIZE-THEN-SEAT ORDERING IS THE WHOLE HAZARD and it is why this mirrors the
    // Windows function's structure rather than being written fresh. The wrappers live
    // INSIDE the vector's elements, so seating before the one resize() would leave them
    // pointing into a buffer resize() then reallocated. Here every format is created
    // after the resize and Seated<>::install() returns the live pointer, so there is no
    // separate seating pass to get wrong -- but the vector is still resized exactly once,
    // up front, for the same reason: a push_back per theme would reallocate under the
    // pointers already handed out.
    app.themePreviewFormats.resize(static_cast<size_t>(themeCount()));
    for (int i = 0; i < themeCount(); i++) {
        const D2DTheme& t = themeAt(i);
        App::ThemePreviewFormats& slot = app.themePreviewFormats[static_cast<size_t>(i)];
        slot.name = makeFormat(app, slot.nameStorage, t.fontFamily, W::SemiBold,
                               S::Normal, 14.0f * scale);
        slot.preview = makeFormat(app, slot.previewStorage, t.fontFamily, W::Normal,
                                  S::Normal, 11.0f * scale);
        slot.code = makeFormat(app, slot.codeStorage, t.codeFontFamily, W::Normal,
                               S::Normal, 10.0f * scale);
    }
}

void releaseQtBackends(App& app) {
    // Formats before the factory that made them. QtTextFormat does not hold a pointer
    // back into the factory today -- QFont is a value -- so this ordering is not load
    // bearing yet, and it is written this way because the reverse reads as "release the
    // owner first" to anyone who checks later.
    releaseQtTextFormats(app);
    // Which also clears the theme previews, so a release-then-reinstall rebuilds them
    // lazily rather than finding a non-empty vector of seated nulls.
    releaseQtChromeFormats(app);

    // The raw pointer goes first. If install() ran the destructor before the
    // pointer was cleared, App would hold a dangling tinta::TextFactory* for the
    // length of one statement -- harmless today and exactly the kind of ordering
    // that stops being harmless when a destructor grows.
    app.textFactory = nullptr;
    app.textFactoryStorage.install(nullptr);
    app.brush = nullptr;
    app.brushStorage.install(nullptr);
}

QtScopedRenderTarget::QtScopedRenderTarget(App& app, QPaintDevice& device) : app_(app) {
    target_ = app_.renderTargetStorage.install(std::make_unique<QtRenderTarget>(device));
    app_.renderTarget = target_;
}

QtScopedRenderTarget::~QtScopedRenderTarget() {
    app_.renderTarget = nullptr;
    // This is what ends the QPainter: ~QtRenderTarget calls QPainter::end(), and it
    // runs here because the storage releases the wrapper it is holding.
    app_.renderTargetStorage.install(nullptr);
}
