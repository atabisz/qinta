#include "qt_init.h"

#include <memory>

#include "app.h"
#include "render_qt.h"
#include "text_api.h"
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

// A one-line-per-format helper, because the alternative is sixteen three-line blocks
// that differ only in the arguments and hide a copy-paste error beautifully.
tinta::TextFormat* makeFormat(App& app, Seated<tinta::TextFormat, IDWriteTextFormat>& storage,
                              const wchar_t* family, tinta::FontWeight weight,
                              tinta::FontStyle style, float size) {
    std::unique_ptr<tinta::TextFormat> format = app.textFactory->CreateTextFormat(
        family, weight, style, tinta::FontStretch::Normal, size, L"en-us");
    if (!format) return nullptr;
    // Every format in updateTextFormats gets this, in a run of ten `if (native...)`
    // lines at src/d2d_init.cpp:205-217. It is set here instead so a new format cannot
    // be added without it.
    format->SetParagraphAlignment(tinta::ParagraphAlignment::Near);
    return storage.install(std::move(format));
}

// THE DOCUMENT FORMATS, AND ONLY THOSE. This is the Qt counterpart of
// updateTextFormats (src/d2d_init.cpp:119-225), and the numbers below are copied from
// there rather than chosen: 16.0f body, 14.0f code, 28.0f the standalone heading format,
// {32,26,22,18,16,14} the per-level headings, 0.68f the sup/sub ratio, all times
// contentScale * zoomFactor, all bold/italic combinations because inline spans nest.
// A number that disagrees with d2d_init is a divergence, not a Qt decision.
//
// WHY THIS FUNCTION HAD TO EXIST AT ALL, and it is the finding of this slice rather than
// a planned step: the Qt window segfaulted on the first document it was given, at
// src/render.cpp:378, dereferencing a null baseFormat that came from layoutHeading's
// app.headingFormats[levelIndex] ? ... : app.textFormat -- both null. Every one of these
// formats is created by DirectWrite in d2d_init.cpp, so off Windows App had a text
// FACTORY and no text FORMATS, and nothing in the tree said so: the placeholder line
// built its own format on the stack, and no test had ever laid out a document off
// Windows. A backtrace found it in one run; no amount of reading would have.
//
// WHAT IS DELIBERATELY NOT HERE: the ~25 CHROME formats -- search, theme picker, folder
// browser, TOC, signals, stats, editor text and gutter, and the per-theme preview
// triples. They are created in the same Windows file and they are still null off Windows.
// Nothing on the Qt stack draws chrome yet, so creating them would be sixteen more
// unasserted lines; when something does draw chrome, THIS is the function it is missing.
//
// THE SPACE WIDTHS ARE MEASURED, not defaulted, through the seam overload of measureText
// (src/text_utils_app.cpp) -- the one function that got its own translation unit for
// exactly this call. render.cpp's getSpaceWidth reads them and a zero there collapses
// every inter-word gap in the document, which draws and looks almost right, so it is the
// kind of default that would have survived a pixel count.
void installQtTextFormats(App& app) {
    if (!app.textFactory) return;

    const float scale = app.contentScale * app.zoomFactor;
    const float fontSize = 16.0f * scale;
    const float codeSize = 14.0f * scale;
    const wchar_t* fontFamily = app.theme.fontFamily;
    const wchar_t* codeFont = app.theme.codeFontFamily;
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
    app.boldItalicFormat = makeFormat(app, app.boldItalicFormatStorage, fontFamily, W::Bold,
                                      S::Italic, fontSize);
    app.codeBoldFormat = makeFormat(app, app.codeBoldFormatStorage, codeFont, W::Bold,
                                    S::Normal, codeSize);
    app.codeItalicFormat = makeFormat(app, app.codeItalicFormatStorage, codeFont, W::Normal,
                                      S::Italic, codeSize);
    app.codeBoldItalicFormat = makeFormat(app, app.codeBoldItalicFormatStorage, codeFont,
                                          W::Bold, S::Italic, codeSize);
    app.supSubFormat = makeFormat(app, app.supSubFormatStorage, fontFamily, W::Normal,
                                  S::Normal, fontSize * 0.68f);

    const float headingSizes[] = {32.0f, 26.0f, 22.0f, 18.0f, 16.0f, 14.0f};
    for (int i = 0; i < 6; i++) {
        app.headingFormats[i] = makeFormat(app, app.headingFormatsStorage[i], headingFont,
                                           W::Bold, S::Normal, headingSizes[i] * scale);
    }

    app.spaceWidthText = measureText(app, L" ", app.textFormat);
    app.spaceWidthBold = measureText(app, L" ", app.boldFormat);
    app.spaceWidthItalic = measureText(app, L" ", app.italicFormat);
    app.spaceWidthCode = measureText(app, L" ", app.codeFormat);
}

void releaseQtTextFormats(App& app) {
    // The raw pointer before the storage, for the same reason releaseQtBackends does it
    // in that order.
    auto drop = [](tinta::TextFormat*& pointer,
                   Seated<tinta::TextFormat, IDWriteTextFormat>& storage) {
        pointer = nullptr;
        storage.install(nullptr);
    };
    drop(app.textFormat, app.textFormatStorage);
    drop(app.headingFormat, app.headingFormatStorage);
    drop(app.codeFormat, app.codeFormatStorage);
    drop(app.boldFormat, app.boldFormatStorage);
    drop(app.italicFormat, app.italicFormatStorage);
    drop(app.boldItalicFormat, app.boldItalicFormatStorage);
    drop(app.codeBoldFormat, app.codeBoldFormatStorage);
    drop(app.codeItalicFormat, app.codeItalicFormatStorage);
    drop(app.codeBoldItalicFormat, app.codeBoldItalicFormatStorage);
    drop(app.supSubFormat, app.supSubFormatStorage);
    for (int i = 0; i < 6; i++) drop(app.headingFormats[i], app.headingFormatsStorage[i]);

    // The measured widths go too. A stale width against a released format is the same
    // class of bug as a stale wrapper over a released native, and it would survive a
    // reinstall silently because the new formats measure the same.
    app.spaceWidthText = 0.0f;
    app.spaceWidthBold = 0.0f;
    app.spaceWidthItalic = 0.0f;
    app.spaceWidthCode = 0.0f;
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
}

void releaseQtBackends(App& app) {
    // Formats before the factory that made them. QtTextFormat does not hold a pointer
    // back into the factory today -- QFont is a value -- so this ordering is not load
    // bearing yet, and it is written this way because the reverse reads as "release the
    // owner first" to anyone who checks later.
    releaseQtTextFormats(app);

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
