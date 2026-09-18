#include "text_formats.h"

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

// A one-line-per-format helper, because the alternative is sixteen three-line blocks that
// differ only in the arguments and hide a copy-paste error beautifully.
tinta::TextFormat* makeFormat(App& app, std::unique_ptr<tinta::TextFormat>& storage,
                              const wchar_t* family, tinta::FontWeight weight,
                              tinta::FontStyle style, float size) {
    storage = app.textFactory->CreateTextFormat(family, weight, style,
                                                tinta::FontStretch::Normal, size, L"en-us");
    if (!storage) return nullptr;
    // Every one of the sixteen gets this. On Windows it was a run of ten
    // `if (native...) native...->SetParagraphAlignment(...)` lines plus a loop for the
    // heading array -- eleven statements that a twelfth format could be added without.
    // Setting it inside the helper makes that impossible rather than merely unlikely.
    storage->SetParagraphAlignment(tinta::ParagraphAlignment::Near);
    return storage.get();
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
