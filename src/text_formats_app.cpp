#include "text_formats.h"

#include <cstdint>
#include <memory>

#include "app.h"
#include "text_api.h"
#include "theme_data.h"
#include "utils.h"

// The document formats' one definition. Moved out of src/qt_init.cpp rather than written:
// the body below is installQtTextFormats with its Seated<>::install swapped for a
// unique_ptr assignment and its name made backend-neutral, and every number in it came
// from src/d2d_init.cpp's updateTextFormats before that. So the numbers have not been
// chosen twice, and this file is where they stop being chosen at all.
//
// WHY A NEW TRANSLATION UNIT and not src/text_utils_app.cpp, which is the neighbouring
// portable App-aware file: that file's own header comment states it holds the MEASUREMENT
// helper, and it says in as many words that adding an App&-taking function to
// text_utils.cpp "would have made its own header comment false". Adding CONSTRUCTION to
// text_utils_app.cpp would do the same thing one level up. src/i18n_app.cpp set the
// precedent for splitting rather than relaxing; this follows it.
//
// WHY THE STORAGE IS A PLAIN unique_ptr AND NOT Seated<>. Seated<Seam, Native> exists to
// wrap an ALREADY-CREATED native handle in place and to keep that wrapping idempotent --
// its whole second member is a comparison key for "is this the same native I wrapped last
// time". Once creation returns an owning portable pointer there is no native to compare
// and nothing to seat, and keeping the template would have preserved the coupling in the
// TYPE while removing it from the call. That is the shape this slice is here to delete:
// last time the coupling was on a parameter (ID2D1DeviceContext* dc), here it is on a
// field, and a census keyed on either would report the other as done.

namespace {

// A one-line-per-format helper, because the alternative is a three-line block per format that
// differs only in the arguments and hides a copy-paste error beautifully.
//
// "SIXTEEN" WAS THE WORD HERE, TWICE, and both were true when only the document formats came
// through this helper. It is the ONLY overload now and takes all 24 call sites below -- the
// sixteen document formats plus the thirteen chrome storages, which overlap because two of
// those sites sit inside loops. The count is written as call sites rather than formats on
// purpose: `ensureThemePreviewFormats` creates three per theme, so the number of formats this
// helper produces at runtime depends on how many themes are installed and is not a fact about
// this file.
tinta::TextFormat* makeFormat(App& app, std::unique_ptr<tinta::TextFormat>& storage,
                              const wchar_t* family, tinta::FontWeight weight,
                              tinta::FontStyle style, float size) {
    storage = app.textFactory->CreateTextFormat(family, weight, style,
                                                tinta::FontStretch::Normal, size, L"en-us");
    if (!storage) return nullptr;
    // Every one of the 24 gets this. On Windows it was a run of ten
    // `if (native...) native...->SetParagraphAlignment(...)` lines plus a loop for the
    // heading array -- eleven statements that a twelfth format could be added without.
    // Setting it inside the helper makes that impossible rather than merely unlikely.
    storage->SetParagraphAlignment(tinta::ParagraphAlignment::Near);
    return storage.get();
}

// THE SECOND OVERLOAD IS GONE, and it took the last DirectWrite name in this file with it. It
// took a Seated<tinta::TextFormat, IDWriteTextFormat>& for the chrome storages, and its own
// comment said it was spelled out rather than aliased so that the slice collapsing it to
// unique_ptr would have an obvious grep target. That worked: the grep found it, the thirteen
// chrome storages in include/app.h are plain unique_ptrs now, and all twenty-four call sites
// below bind to the one helper above WITHOUT A SINGLE EDIT -- which is the whole reason
// Seated<> kept reset(), get() and operator bool at those exact spellings.
//
// THE CLAIM I INHERITED FROM IT WAS WRONG, and it is worth more than the deletion. It said
// "Nothing else differs -- same factory call, same locale, same ParagraphAlignment::Near on
// every format", with the difference narrowed to the last line. The two bodies also differed
// on the FAILURE path, and in opposite directions:
//
//   deleted:  build into a LOCAL, test it, install on success -- a failed CreateTextFormat
//             returned nullptr and left the storage holding its PREVIOUS wrapper
//   kept:     assign over the storage, THEN test -- a failed CreateTextFormat has already
//             destroyed the previous format by the time nullptr is returned
//
// So chrome creation failure used to leave `storage` non-null while the raw pointer beside it
// went null, and that pair is exactly the inconsistency tests/theme_tests.cpp asserts against
// on the RELEASE path. Collapsing the type makes the two paths agree, which is a BEHAVIOUR
// CHANGE and not a mechanical one. It is unreachable in practice -- CreateTextFormat does not
// fail for a family this app hardcodes -- and it is written down because "mechanical" was the
// word that nearly moved thirteen declarations without anyone reading the two bodies together.

// A single-line row that must not wrap and must trim with an ellipsis. Three formats want
// exactly this -- folder browser, TOC, TOC bold -- and on Windows it was written TWICE:
// once inline for the folder browser and once as a lambda for the two TOC formats. That
// duplication is what let the two copies differ, and they did: the folder browser's ignored
// the HRESULT from CreateEllipsisTrimmingSign and the TOC lambda checked it. One helper.
void configureTrimmedRow(App& app, tinta::TextFormat* format) {
    if (!format) return;

    // NoWrap FIRST, and the order is load-bearing on one backend: QtTextRun's trimming
    // branch is gated on `noWrap` (src/text_qt.cpp:165) and skips the elide entirely if the
    // wrapping is still Wrap when the layout runs. Windows does not care. Ordering for the
    // stricter backend is why one shared function is safe for both.
    format->SetWordWrapping(tinta::WordWrapping::NoWrap);

    std::unique_ptr<tinta::InlineObject> ellipsis =
        app.textFactory->CreateEllipsisTrimmingSign(format);

    // TRIMMING IS SET ONLY WITH A LIVE SIGN, and this is a BEHAVIOUR CHANGE on the Qt side
    // plus the correction of a comment that asserted the opposite. The Qt body said the
    // trimming was set even for a null sign, "which is d2d_init's behaviour and not an
    // accident". It was not d2d_init's behaviour: BOTH Windows sites guarded SetTrimming on
    // a non-null sign, so a failed CreateEllipsisTrimmingSign left granularity None on
    // Windows and Character on Qt. Latent -- that call does not fail in practice -- and
    // exactly the class this port keeps turning up: prose asserting parity that no
    // instrument checked. The guard follows Windows because Windows is the backend whose
    // rendering is measured, and now there is one behaviour to be right or wrong about
    // instead of two.
    if (ellipsis) {
        const tinta::Trimming trimming{tinta::TrimmingGranularity::Character, 0, 0};
        format->SetTrimming(trimming, ellipsis.get());

        // AFTER SetTrimming, and this vector is why the function takes an App at all.
        // DirectWrite retains the sign and Qt does not, so the only caller correct against
        // both backends is one that keeps it alive itself. releaseChromeFormats clears it,
        // and installChromeFormats releases before it installs -- without that pairing this
        // push would run three times per theme change forever.
        app.trimmingSigns.push_back(std::move(ellipsis));
    }
}

}  // namespace

// MOVED FROM src/d2d_init.cpp, NOT REWRITTEN, and it moved because its own comment there
// said what was stopping it: "PORTABLE AS WRITTEN, AND DELIBERATELY LEFT HERE... It stays in
// d2d_init.cpp because there is no portable init file yet to move it INTO." This file is
// that init file, so the stated blocker expired the moment it landed. The body is two
// assignments now rather than six add() calls, because the sets moved into
// include/text_formats.h where a header-only test can reach them.
//
// A function that names no Windows type can still be Windows-only, and this is what that
// looks like: it sat in a file the port DELETES, behind an include no portable target
// carries, with one caller in WinMain. The census that keeps this port honest greps for
// DirectWrite identifiers and this function has never had one -- so nothing measured it as
// debt, and it was the third of three format functions all the same.
void installTypography(App& app) {
    app.bodyTypography = tinta::bodyTypographySet();
    app.codeTypography = tinta::codeTypographySet();
}

void releaseDocumentTextFormats(App& app) {
    // The eleven pairs themselves are App::releaseDocumentFormats, defined in app.h. That
    // is not where they belong on any reading but one, and the one is a link measurement:
    // App::shutdown() is an inline member that also has to drop them, and calling THIS
    // function from there put its symbol in the link closure of every target that includes
    // app.h -- twenty-odd test executables came back LNK2019 for a text format none of
    // them uses. See the comment at the member.
    //
    // THIS IS WHERE THE WINDOWS CODE LEAKED, and it is worth a sentence because it is the
    // one behaviour the slice changes. Two of the four release blocks this replaces set
    // eleven raw IDWriteTextFormat* members to nullptr without calling Release on any of
    // them, and one of the two runs on every theme change, zoom step and DPI change.
    // reset() on an owning unique_ptr destroys the wrapper and D2DTextFormat's destructor
    // Releases. No pixel moves; the process just stops shedding eleven formats per
    // rebuild of the format cache.
    app.releaseDocumentFormats();

    app.spaceWidthText = 0.0f;
    app.spaceWidthBold = 0.0f;
    app.spaceWidthItalic = 0.0f;
    app.spaceWidthCode = 0.0f;
}

void installDocumentTextFormats(App& app) {
    // Release FIRST, then bail. Bailing first would leave the previous theme's formats
    // live behind a factory the caller has told us it no longer has.
    releaseDocumentTextFormats(app);
    if (!app.textFactory) return;

    const float scale = app.contentScale * app.zoomFactor;
    const float fontSize = 16.0f * scale;
    const float codeSize = 14.0f * scale;
    const wchar_t* fontFamily = app.theme.fontFamily;
    const wchar_t* codeFont = app.theme.codeFontFamily;
    // The theme's heading face, which inherits the main font unless the theme sets
    // headingfont= (#155). Abyss pins its headings to Segoe UI in data -- bolding its
    // Light body family would render faux-bold.
    const wchar_t* headingFont = themeHeadingFont(app.theme);

    using W = tinta::FontWeight;
    using S = tinta::FontStyle;

    app.textFormat = makeFormat(app, app.textFormatStorage, fontFamily, W::Normal, S::Normal,
                                fontSize);
    app.headingFormat = makeFormat(app, app.headingFormatStorage, headingFont, W::Bold,
                                   S::Normal, 28.0f * scale);
    app.codeFormat = makeFormat(app, app.codeFormatStorage, codeFont, W::Normal, S::Normal,
                                codeSize);
    app.boldFormat = makeFormat(app, app.boldFormatStorage, fontFamily, W::Bold, S::Normal,
                                fontSize);
    app.italicFormat = makeFormat(app, app.italicFormatStorage, fontFamily, W::Normal,
                                  S::Italic, fontSize);
    // Inline spans nest, so every weight/style combination needs a format:
    // ***both***, **`code`**, *`code`*
    app.boldItalicFormat = makeFormat(app, app.boldItalicFormatStorage, fontFamily, W::Bold,
                                      S::Italic, fontSize);
    app.codeBoldFormat = makeFormat(app, app.codeBoldFormatStorage, codeFont, W::Bold,
                                    S::Normal, codeSize);
    app.codeItalicFormat = makeFormat(app, app.codeItalicFormatStorage, codeFont, W::Normal,
                                      S::Italic, codeSize);
    app.codeBoldItalicFormat = makeFormat(app, app.codeBoldItalicFormatStorage, codeFont,
                                          W::Bold, S::Italic, codeSize);
    // Small format for ^superscript^ / ~subscript~ spans. IT SURVIVES TO THE DRAW ON BOTH
    // BACKENDS NOW -- that was not true when this file landed, and the correction is left
    // visible because the sentence it replaces was confidently wrong about this file's own
    // output. It read "IT IS NULLED AGAIN ON WINDOWS BEFORE ANYTHING DRAWS", and it was:
    // updateTextFormats called updateOverlayFormats, whose first statement dropped this
    // format, so ^superscript^ rendered at body size on Windows while the Qt path rendered
    // it small. The fix was one deleted line in App::releaseOverlayFormats, not a change
    // here, which is why the bug could be described accurately from this file and still not
    // be findable from it. Held by tests/theme_tests.cpp's classification check rather than
    // by this comment.
    app.supSubFormat = makeFormat(app, app.supSubFormatStorage, fontFamily, W::Normal,
                                  S::Normal, fontSize * 0.68f);

    const float headingSizes[] = {32.0f, 26.0f, 22.0f, 18.0f, 16.0f, 14.0f};
    for (int i = 0; i < 6; i++) {
        app.headingFormats[i] = makeFormat(app, app.headingFormatsStorage[i], headingFont,
                                           W::Bold, S::Normal, headingSizes[i] * scale);
    }

    // THE SPACE WIDTHS ARE MEASURED, not defaulted, through the seam overload of
    // measureText (src/text_utils_app.cpp) -- the one function that got its own translation
    // unit for exactly this call. render.cpp's getSpaceWidth reads them and a zero there
    // collapses every inter-word gap in the document, which draws and looks almost right,
    // so it is the kind of default that would have survived a pixel count.
    //
    // The Windows path measured these through the IDWriteTextFormat* overload instead. The
    // two agree by derivation rather than by assertion: tests/text_factory_tests.cpp
    // asserts app.textFactory->CreateTextLayout and dwriteFactory->CreateTextLayout return
    // identical TextMetrics across eleven fields for the same string and format, and each
    // overload is a thin measure over one of those two calls. The derivation breaks
    // silently if either body stops being thin, which is why the printed-page hashes are
    // the claim that actually guards this line.
    app.spaceWidthText = measureText(app, L" ", app.textFormat);
    app.spaceWidthBold = measureText(app, L" ", app.boldFormat);
    app.spaceWidthItalic = measureText(app, L" ", app.italicFormat);
    app.spaceWidthCode = measureText(app, L" ", app.codeFormat);
}

void releaseChromeFormats(App& app) {
    // The ten drops plus the preview clear are App::releaseOverlayFormats, an inline member,
    // for the same measured reason releaseDocumentTextFormats delegates to
    // App::releaseDocumentFormats: App::shutdown() also has to drop them, and calling a free
    // function from an inline member of app.h puts that symbol in the link closure of every
    // target that includes the header -- twenty-odd test executables came back LNK2019 for a
    // text format none of them uses. The member does the dropping; this function owns the
    // two extras that are not formats.
    app.releaseOverlayFormats();

    // AFTER the formats that reference them. Nothing in either backend reads a sign back --
    // QtTextFormat kept a bool and DirectWrite holds its own reference -- so this is not a
    // dangling-pointer fix today. It is ordered this way because the reverse reads as "free
    // the thing the format points at, then the format", and the next person to add a backend
    // that DOES retain the raw pointer should find the rule rather than discover it.
    app.trimmingSigns.clear();
    app.editorCharWidth = 0.0f;
}

void installChromeFormats(App& app) {
    // Release FIRST, then bail -- identical to installDocumentTextFormats and for the second
    // of the two reasons stated at the declaration. Bailing first would leave the previous
    // theme's formats live behind a factory the caller has told us it no longer has, and
    // releasing only on the success path would let trimmingSigns grow by three on every
    // theme change, zoom step and DPI change.
    releaseChromeFormats(app);
    if (!app.textFactory) return;

    // contentScale ALONE, no zoomFactor, for the eight overlay formats. The reason is in
    // d2d_init.cpp's own words at the site this body replaces: "fixed UI size so zooming the
    // document cannot overflow the fixed stats box". The two editor formats are on the other
    // scale further down, because editor text IS document text. Getting the two the wrong way
    // round would make chrome grow with Ctrl+= and look like a feature.
    const float scale = app.contentScale;
    const wchar_t* segoe = L"Segoe UI";

    using W = tinta::FontWeight;
    using S = tinta::FontStyle;

    // THE FAMILY LITERALS ARE A KNOWN DIVERGENCE OFF WINDOWS and they are copied verbatim
    // anyway: neither Segoe UI nor Consolas exists on a stock Linux box, so Qt substitutes.
    // A different family here would make the two stacks disagree on purpose, and the
    // substitution is Qt's job -- it already happens for the document formats, whose theme
    // families are equally Windows-flavoured.
    app.searchTextFormat = makeFormat(app, app.searchTextFormatStorage,
                                      app.theme.fontFamily, W::Normal, S::Normal,
                                      16.0f * scale);
    app.signalSmallFormat = makeFormat(app, app.signalSmallFormatStorage, segoe,
                                       W::SemiBold, S::Normal, 10.0f * scale);
    app.themeTitleFormat = makeFormat(app, app.themeTitleFormatStorage,
                                      L"Segoe UI Light", W::Light, S::Normal,
                                      28.0f * scale);
    app.themeHeaderFormat = makeFormat(app, app.themeHeaderFormatStorage, segoe,
                                       W::SemiBold, S::Normal, 11.0f * scale);

    // The theme's CODE face, not its body face -- the stats line shows counts in the same
    // font the document's code spans use. Worth naming because it is the format whose family
    // column caught a published claim: on the default theme this resolves to Cascadia Code,
    // and four sites said the default code face was Consolas. See tests/typography_tests.cpp.
    app.statsFormat = makeFormat(app, app.statsFormatStorage, app.theme.codeFontFamily,
                                 W::Normal, S::Normal, 13.0f * scale);

    app.folderBrowserFormat = makeFormat(app, app.folderBrowserFormatStorage, segoe,
                                         W::Normal, S::Normal, 13.0f * scale);
    configureTrimmedRow(app, app.folderBrowserFormat);

    // Bold at 13, plain at 12 -- the SIZES differ as well as the weights, which was easy to
    // miss in the Windows original where the two calls were adjacent.
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

        // THE MEASUREMENT GOES THROUGH THE SEAM, WHERE THE WINDOWS ORIGINAL STAYED NATIVE,
        // and the difference is the port's doing rather than a choice made here. That code
        // explained itself at the site: at that point in that file app.editorTextFormat was
        // still null, because seatTextFormats() ran forty lines earlier, so a seam call
        // there would have read null and measured nothing. Here the wrapper is what
        // makeFormat just returned, so there is no null to read and no ordering to get
        // wrong. Same rule, opposite consequence -- and the native overload of measureText
        // that the old site used no longer exists in the tree.
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

        // Issue #181's arithmetic: rows sit on a fontSize*1.5 grid and the baseline centres
        // the natural line box inside the cell, with fontSize*1.2 as the fallback when the
        // metrics came back zero.
        //
        // THE ORDER MATTERS AGAINST THE MEASUREMENT ABOVE: the natural metrics have to be
        // read BEFORE uniform spacing overrides them. Both predecessors measured first and
        // both said so; keeping the order is the whole reason those two blocks are adjacent
        // rather than tidied apart.
        const float editorLineHeight = editorFontSize * 1.5f;
        const float editorBaseline =
            (naturalHeight > 0.0f && naturalBaseline > 0.0f)
                ? naturalBaseline + (editorLineHeight - naturalHeight) * 0.5f
                : editorFontSize * 1.2f;
        app.editorTextFormat->SetLineSpacing(tinta::LineSpacingMethod::Uniform,
                                             editorLineHeight, editorBaseline);
    }

    // 9.5 DIP Consolas, right-aligned against the gutter's inner edge, vertically centred in
    // its row. The gutter is the one chrome format that pins a family rather than taking one
    // from the theme, and it is on the EDITOR scale because it has to line up with the rows
    // it numbers.
    app.editorGutterFormat = makeFormat(app, app.editorGutterFormatStorage, L"Consolas",
                                        W::Normal, S::Normal, 9.5f * editorScale);
    if (app.editorGutterFormat) {
        app.editorGutterFormat->SetTextAlignment(tinta::TextAlignment::Trailing);
        app.editorGutterFormat->SetParagraphAlignment(tinta::ParagraphAlignment::Center);
        app.editorGutterFormat->SetWordWrapping(tinta::WordWrapping::NoWrap);
    }
}

void ensureThemePreviewFormats(App& app) {
    if (!app.textFactory) return;
    if (!app.themePreviewFormats.empty()) return;

    // contentScale alone, like every other overlay format. The chooser is chrome.
    const float scale = app.contentScale;

    using W = tinta::FontWeight;
    using S = tinta::FontStyle;

    // THE RESIZE-THEN-SEAT ORDERING WAS THE WHOLE HAZARD HERE and the port retires it rather
    // than preserving it. The wrappers live INSIDE the vector's elements, so the Windows
    // version had to create all ~30 natives, resize once, and only THEN seat -- seating
    // before the resize left every wrapper pointing into a buffer resize() had reallocated,
    // and App::seatTextFormats still carried a comment warning about exactly that. There is
    // no seating pass left to order wrongly: install() returns the live pointer, so each slot
    // is filled after the buffer it lives in is final. The single up-front resize stays,
    // because a push_back per theme would still reallocate under the pointers already handed
    // out inside the loop.
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
