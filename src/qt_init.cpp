#include "qt_init.h"

#include <memory>

#include "app.h"
#include "render_qt.h"

#include <QImage>
#include "text_api.h"
#include "text_formats.h"
#include "text_qt.h"
#include "theme_apply.h"
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

// THE SECOND makeFormat IS GONE FROM THIS FILE, and it went on the condition its own
// comment set. That comment read: "This one takes a Seated<> because the chrome formats are
// still created NATIVELY on Windows -- updateOverlayFormats has not been ported... Two
// shapes for the same job is the honest state of a half-finished port rather than a design,
// and the second one goes when updateOverlayFormats follows." updateOverlayFormats has now
// followed -- it does not exist -- so the helper moved to src/text_formats_app.cpp where
// BOTH overloads now sit side by side, still two shapes but in one file where the difference
// between them is one line and visible.
//
// A prediction in that comment turned out to be worth more than the port it described: the
// two shapes were expected to collapse into one here. They did not. The Seated<> template
// argument survives because App's chrome members still declare it, and collapsing THAT is a
// separate mechanical slice. What actually left is every native creation, every seat and the
// coupling in the CODE -- which is the part that could hide a divergence.

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

// THE CHROME FORMATS AND THEIR TRIMMED-ROW HELPER HAVE LEFT THIS FILE TOO, to
// installChromeFormats and configureTrimmedRow in src/text_formats_app.cpp, called by BOTH
// initialisers. installQtBackends below calls the shared name directly -- there is no Qt
// wrapper for these two, unlike installQtTextFormats above, because neither had a caller
// outside this file to keep compiling.
//
// THIS FILE WAS THE SOURCE AND d2d_init.cpp WAS THE DELETION, which is the reverse of how a
// port usually reads and is worth one sentence. The body that moved is the one written HERE:
// every number in it was copied out of updateOverlayFormats when this file was written, so
// the shared function is the Qt body promoted rather than the Windows body ported. What the
// move cost the Windows side is nothing -- a chrome dump taken before and after says so --
// and what it cost this side is one real behaviour change, in configureTrimmedRow's null-sign
// guard, described at the function.
//
// ONE PROPERTY HAD TO BE ADDED THAT NEITHER BODY NEEDED IN ITS OLD HOME. The paragraph at
// installQtBackends' call site used to explain that this function did not need to be
// idempotent -- "there is one caller and one lifetime, so the release is releaseQtBackends'
// job and this stays a plain install" -- while the Windows function opened with
// releaseOverlayFormats() precisely because it runs on every theme change, zoom step and DPI
// change. Merging them merges the CALLERS, so the survivor takes the stricter contract:
// installChromeFormats releases first. Without that, this body's three trimmingSigns
// push_backs would have started accumulating on the Windows path the moment it was shared,
// and nothing in either file would have said so.

// THE PER-THEME PREVIEWS AND THE CHROME RELEASE WENT WITH THEM, for the reason the pair
// exists at all: a release that lives beside its creation cannot fall out of step with it.
// releaseQtChromeFormats is now releaseChromeFormats and ensureQtThemePreviewFormats is a
// forwarding call further down -- that one keeps its Qt name because
// tests/qt_entry_tests.cpp calls it by name and include/qt_init.h declares it.

// Moved out with its install half, for the reason the pair exists at all: a release that
// lives beside its creation cannot fall out of step with it. Both are now in
// src/text_formats_app.cpp and both are called from both backends -- which is also what
// gave the Windows path a release that actually releases, since the block it replaces
// nulled eleven raw IDWriteTextFormat* members without calling Release on any of them.
void releaseQtTextFormats(App& app) { releaseDocumentTextFormats(app); }

}  // namespace

tinta::RenderTarget* qtFactoryTarget() {
    static QtRenderTarget* const target = [] {
        auto* device = new QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
        device->fill(Qt::transparent);
        return new QtRenderTarget(*device);
    }();
    return target;
}

void installQtBackends(App& app) {
    app.textFactory = app.textFactoryStorage.install(std::make_unique<QtTextFactory>());

    // THE CJK/EMOJI FALLBACK FAMILIES, AND THIS CALL IS THE REASON #155 WORKS AT A DEFAULT
    // LAUNCH RATHER THAN ONLY AFTER A THEME SWITCH.
    //
    // installFontFallback's one portable caller is updateTextFormats, and on THIS stack
    // updateTextFormats is not part of startup: this function installs the document and
    // chrome formats directly, and src/qt_launch.cpp calls updateTextFormats only when the
    // saved zoom differs from the one the formats were built at. So at default zoom with no
    // theme change nothing would ever seat the list, and every ideograph in the session
    // would resolve in whatever order the platform configuration gave -- which is exactly
    // the state #155 describes. On Windows the equivalent call sat inside updateTextFormats
    // and WinMain ran it at startup, so the hole was this stack's alone.
    //
    // BEFORE installQtTextFormats, though it need not be: the families are attached to
    // layouts, never to formats, so no format built below can be missing them. Ordered this
    // way so the factory is fully configured before anything is created through it.
    installFontFallback(app);

    // The factory target, so App has a render target outside paint (see qt_init.h). Only when
    // nothing else is seated: installing during a paint must not replace the paint target.
    if (!app.renderTarget) app.renderTarget = qtFactoryTarget();

    // THE BRUSH IS PROCESS-LIFETIME HERE, AND WAS DEVICE-LIFETIME ON DIRECT2D, and the
    // asymmetry was real rather than an oversight. Direct2D's brush was created by the
    // render target (the retired src/d2d_init.cpp called CreateSolidColorBrush on it) and
    // died with it, so d2d_init reseated one per device. QtBrush holds a tinta::Color and nothing
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

    // AND THE CHROME FORMATS, through the shared function now.
    //
    // THE PARAGRAPH THAT STOOD HERE IS QUOTED BECAUSE IT IS WHAT THE PORT INVALIDATED, and
    // it was correct when written: "Windows splits them across two entry points... There,
    // updateTextFormats runs at startup and updateOverlayFormats runs from a device-lost
    // rebuild and a DPI change as well, so it begins by calling app.releaseOverlayFormats()
    // to make itself idempotent. Here there is one caller and one lifetime, so the release
    // is releaseQtBackends' job and this stays a plain install."
    //
    // Both halves of that were true and the second is now false, because sharing the body
    // shared the CALLERS: the merged function inherits updateTextFormats' frequency -- every
    // theme change, every zoom step, every DPI change -- so it releases first, and this call
    // site is no longer the one that gets to decide. The reason this mattered rather than
    // being a tidiness point: the shared body pushes three ellipsis trimming signs into
    // App::trimmingSigns per call and clears none, which was safe under exactly the
    // one-caller assumption this paragraph recorded.
    //
    // THE THEME PREVIEWS ARE STILL NOT HERE, deliberately: ~30 formats across every theme,
    // wanted only when the chooser opens, and ensureQtThemePreviewFormats below is the lazy
    // door -- the division of labour the retired Windows stack had, and now literally the same function.
    installChromeFormats(app);

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

// THE PER-THEME PREVIEW TRIPLE IS A FORWARDING CALL NOW, like installQtTextFormats above:
// the body is ensureThemePreviewFormats in src/text_formats_app.cpp, and the Windows
// function of the same name that it was written to mirror no longer exists -- there is one
// function with one name, and this wrapper survives only because two callers name the Qt one.
//
// THE PARAGRAPH THAT JUSTIFIED THE WRAPPER BEING PUBLIC HAS BEEN OVERTAKEN BY ITS OWN
// PREDICTION, so it is corrected rather than dropped. It said: "This one's future caller is
// src/overlays.cpp:1467, which is 4,108 lines of unported Windows code, so left static it
// would be a function with no caller and no reading." That future arrived from the other
// direction: overlays.cpp never learned the Qt name, it calls ensureThemePreviewFormats and
// always did, and the port made that call resolve to the portable definition without
// overlays.cpp changing a character. So the shared function has a real Windows caller and
// this wrapper has two test callers, which is why it stays declared in include/qt_init.h
// rather than being deleted with its body.
void ensureQtThemePreviewFormats(App& app) { ensureThemePreviewFormats(app); }

void releaseQtBackends(App& app) {
    // Formats before the factory that made them. QtTextFormat does not hold a pointer
    // back into the factory today -- QFont is a value -- so this ordering is not load
    // bearing yet, and it is written this way because the reverse reads as "release the
    // owner first" to anyone who checks later.
    releaseQtTextFormats(app);
    // Which also clears the theme previews, so a release-then-reinstall rebuilds them
    // lazily rather than finding a non-empty vector of seated nulls.
    releaseChromeFormats(app);

    // The raw pointer goes first. If install() ran the destructor before the
    // pointer was cleared, App would hold a dangling tinta::TextFactory* for the
    // length of one statement -- harmless today and exactly the kind of ordering
    // that stops being harmless when a destructor grows.
    app.textFactory = nullptr;
    app.textFactoryStorage.install(nullptr);
    app.brush = nullptr;
    app.brushStorage.install(nullptr);
    // Borrowed, never owned, so releasing it is forgetting it.
    if (app.renderTarget == qtFactoryTarget()) app.renderTarget = nullptr;
}

QtScopedRenderTarget::QtScopedRenderTarget(App& app, QPaintDevice& device)
    : app_(app), previous_(app.renderTarget) {
    target_ = app_.renderTargetStorage.install(std::make_unique<QtRenderTarget>(device));
    app_.renderTarget = target_;
}

QtScopedRenderTarget::~QtScopedRenderTarget() {
    // Back to what was seated before the paint: the factory target once the backends are
    // installed, nullptr before that. It used to be nullptr unconditionally.
    app_.renderTarget = previous_;
    // This is what ends the QPainter: ~QtRenderTarget calls QPainter::end(), and it
    // runs here because the storage releases the wrapper it is holding.
    app_.renderTargetStorage.install(nullptr);
}
