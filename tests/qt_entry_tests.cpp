// The first test on this tree that paints a tinta WINDOW and reads the pixels back.
//
// Everything else that asserts on drawing paints into a bitmap: tests/render_api_shared.cpp
// hands the seam a QImage or an ID2D1Bitmap and never involves a window, a paint event
// or App's storage. That is the right shape for the seam's own assertions and it leaves
// a gap exactly where the port is riskiest -- between a widget's paintEvent and the
// abstract render target the tree draws through.
//
// WHAT IS UNDER TEST, in the order the assertions run:
//
//   1. Seated<>::install() -- the non-Windows entry point in include/seated.h, which
//      had zero callers until src/qt_init.cpp existed. Two DISTINCT instantiations
//      (RenderTarget and TextFactory), so one working says nothing about the other.
//   2. QtScopedRenderTarget's lifetime contract: App::renderTarget is non-null during
//      the paint and null after it, because the QPainter QtRenderTarget begins must
//      end before paintEvent returns.
//   3. Pixels out of QWidget::grab(). A window that paints nothing is
//      indistinguishable from a window that painted correctly unless the pixels are
//      read, and the colours here come from the real theme table rather than from
//      constants chosen to be easy.
//   4. A PARSED DOCUMENT DRAWN THROUGH src/render.cpp AND src/paint_document.cpp.
//      This is the reason the file grew past run 3, and it is the first execution of
//      either file on EITHER platform outside the tinta binary itself. Until it
//      existed, render.cpp -- 3,294 lines, the largest file in the tree -- was on
//      tests/portable-sources.txt as compile-only, and the paint pass lived inside
//      main_d2d.cpp's WM_PAINT where no test could reach it.
//
// THE CONTROL IS THE POINT OF THE THIRD RUN. "Some pixels in the text band differ
// from the background" is only evidence that text was drawn if the same measurement
// can come back ZERO. So the factory is released and the window grabbed again: with
// no text factory the window paints background only, and the band must be flat. Then
// it is re-installed and grabbed a third time, which also asserts installation is
// re-callable rather than a one-shot.
//
// THE DOCUMENT RUNS NEED THEIR OWN CONTROLS, and a different kind, because both
// branches of paintEvent draw dark text on the theme background. A pixel count alone
// cannot tell a rendered document from the placeholder line, so three things
// discriminate rather than one:
//
//   * QtAppWindow::documentPainted() -- which branch the window took, reported by the
//     window rather than inferred from pixels. It exists for this test and the header
//     says so.
//   * TWO DISJOINT BANDS. placeholderRect() is centred; the document band is the top
//     quarter. The disjointness is ASSERTED, not trusted to the arithmetic, and each
//     run reads both bands. A document must light the top band and the placeholder
//     must not.
//   * A whole-image inequality against run 1. If the document run produced the
//     placeholder image, every count below could still pass.
//
// Run 5 clears App::root and requires the placeholder back at run 1's exact count.
// Run 6 sets the document and releases the factory, which must leave BOTH bands flat
// -- weaker than it looks, and stated as such: paintEvent's factory guard sits before
// the branch, so run 6 proves the document's pixels are text through the seam and not
// that the document branch itself is factory-dependent.
#include <QApplication>
#include <QFontDatabase>
#include <QImage>
#include <QKeyEvent>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "app.h"
#include "document.h"
#include "file_utils.h"
#include "geometry.h"
#include "qt_init.h"
#include "qt_window.h"

namespace {

// The document fixture. DELIBERATELY WITHOUT MATHS: src/math_render.cpp does not
// compile off Windows and src/math_stub.cpp stands in for it, so an equation here would
// be asserting the stub's box arithmetic and reading as a rendered formula. A heading, a
// paragraph and a fenced code block exercise three different layout paths and three
// different text formats, which is what this run is for.
const char kMarkdown[] =
    "# Heading one\n"
    "\n"
    "A paragraph of ordinary text, long enough to be more than one glyph wide.\n"
    "\n"
    "```cpp\n"
    "int x = 1;\n"
    "```\n";

// A document TALLER THAN THE WINDOW, which is the precondition every scroll assertion
// in runs 8-10 depends on. kMarkdown above is three short blocks: it lays out to less
// than 400 pixels, so app.contentHeight - app.height is negative, every maximum clamps
// to zero, and a scroll test on it would pass by measuring nothing at all. That is the
// single most likely way this file could go green while testing nothing, so the
// precondition is ASSERTED (app.contentHeight > app.height) before anything is scrolled.
//
// Built from a repeated block rather than written out, because the only property that
// matters is height, and 40 headed sections is unambiguously past 400 pixels at any
// plausible font size. The heading numbers differ per section so the pixels differ per
// scroll position -- a document of forty identical paragraphs would look the same at
// every offset and defeat the image comparisons.
std::string tallMarkdown() {
    std::string out;
    for (int i = 1; i <= 40; i++) {
        out += "## Section " + std::to_string(i) + "\n\n";
        out += "Paragraph " + std::to_string(i) +
               " of the tall fixture, with enough words in it to wrap and to differ from "
               "its neighbours when the view scrolls.\n\n";
    }
    return out;
}

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    g_checks++;
    std::printf("%s : %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failures++;
}

// The same rounding QColor::fromRgbF does, so an expected value here is the value
// the backend would have produced and not a second, independently wrong conversion.
int expect8(float v) { return static_cast<int>(v * 255.0f + 0.5f); }

bool near8(int actual, int expected, int tol) {
    return actual >= expected - tol && actual <= expected + tol;
}

bool pixelIs(const QImage& img, int x, int y, const tinta::Color& color, int tol) {
    const QRgb p = img.pixel(x, y);
    return near8(qRed(p), expect8(color.r), tol) && near8(qGreen(p), expect8(color.g), tol) &&
           near8(qBlue(p), expect8(color.b), tol);
}

// How many pixels inside the band are NOT the background colour. This is the whole
// text measurement, and it is a count rather than a boolean so the control's zero and
// the real run's number can be printed side by side -- a boolean pair would hide the
// case where "text" is three stray antialiased pixels.
int nonBackgroundPixels(const QImage& img, const tinta::RectF& band,
                        const tinta::Color& background) {
    const int x0 = qMax(0, static_cast<int>(band.left));
    const int y0 = qMax(0, static_cast<int>(band.top));
    const int x1 = qMin(img.width(), static_cast<int>(band.right));
    const int y1 = qMin(img.height(), static_cast<int>(band.bottom));
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (!pixelIs(img, x, y, background, 2)) n++;
    return n;
}

// grab() renders the widget into a pixmap this process can read. The backing store
// cannot be read under the offscreen platform, and a probe that cannot read its own
// output is the failure mode this tree keeps catching itself in.
QImage grabWindow(QtAppWindow& window) {
    return window.grab().toImage().convertToFormat(QImage::Format_RGB32);
}

// Where a document's first lines land: the top quarter of the window. Chosen to be
// DISJOINT from placeholderRect(), which is centred, so the two measurements cannot
// both be reading the same glyphs. main() asserts the disjointness rather than
// trusting this comment -- placeholderRect's band half-height is a constant in
// src/qt_window.cpp and a reword there would silently overlap these two.
tinta::RectF documentBand(const QtAppWindow& window) {
    return tinta::RectF{0.0f, 0.0f, static_cast<float>(window.width()),
                        static_cast<float>(window.height()) * 0.25f};
}

// A wheel event delivered the way Qt delivers one, through the event system rather than
// by calling wheelEvent directly.
//
// THROUGH sendEvent ON PURPOSE. Calling window.wheelEvent(&ev) would not compile from
// here -- it is protected -- and if it were public it would skip the dispatch that makes
// this a test of the wiring rather than of the handler body. What it does NOT exercise is
// focus: a posted event reaches the widget whether or not it can hold focus, which is why
// the focus policy is asserted separately and explicitly.
//
// notches is in the same unit src/scroll.h takes: POSITIVE IS WHEEL UP. Qt wants eighths
// of a degree, so 120 per notch, and the sign is passed straight through.
void sendWheel(QtAppWindow& window, float notches, Qt::KeyboardModifiers mods = Qt::NoModifier,
               bool horizontalAxis = false) {
    const int delta = static_cast<int>(notches * 120.0f);
    const QPoint angle = horizontalAxis ? QPoint(delta, 0) : QPoint(0, delta);
    const QPointF local(static_cast<qreal>(window.width()) / 2.0,
                        static_cast<qreal>(window.height()) / 2.0);
    QWheelEvent ev(local, QPointF(window.mapToGlobal(local.toPoint())), QPoint(0, 0), angle,
                   Qt::NoButton, mods, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&window, &ev);
}

void sendKey(QtAppWindow& window, int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QKeyEvent ev(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(&window, &ev);
}

// float equality, at the tolerance the arithmetic deserves. Every value asserted below is
// a sum of at most a few multiples of 50 or 60, so anything further out than this is a
// different number and not a rounding difference. Written as a helper because an
// exact == on a float would be the wrong claim and a `> 0` would be the wrong test.
bool sameScroll(float actual, float expected) {
    return std::abs(actual - expected) < 0.01f;
}

}  // namespace

int main(int argc, char** argv) {
    // QApplication, not QGuiApplication: QtAppWindow is a QWidget.
    QApplication qapp(argc, argv);

    const QString platform = QApplication::platformName();
    std::printf("platform=%s\n", platform.toUtf8().constData());

    // The same guard tests/qt_text_tests.cpp carries, and for the same measured
    // reason: offscreen has a real font database OFF Windows and none ON it, so the
    // platform is pinned per OS in CMakeLists.txt. Without a font database the text
    // assertions below would be measuring a substitution to an empty family, which
    // draws nothing and would read as a broken seam.
    if (QFontDatabase::families().isEmpty()) {
        std::printf("FAIL: no font database under platform=%s. The text assertions\n"
                    "      below would fail for an environment reason and read as a\n"
                    "      broken render seam. Refusing to run. Use offscreen OFF\n"
                    "      Windows and windows ON it, never minimal.\n",
                    platform.toUtf8().constData());
        return 2;
    }
    check(true, "a font database is present, so a blank text band would be a real finding");

    App app;
    check(app.renderTarget == nullptr, "a fresh App has no render target");
    check(app.textFactory == nullptr, "a fresh App has no text factory");

    installQtBackends(app);
    check(app.textFactory != nullptr,
          "installQtBackends put a Qt text factory in App's storage -- Seated<TextFactory>::install()");
    check(app.renderTarget == nullptr,
          "installQtBackends did NOT install a render target: that is one paint event's business");
    check(app.brush != nullptr,
          "installQtBackends put a brush in App's storage -- paintDocumentLayout returns early without one");

    // THE FORMATS, ASSERTED BY NAME rather than left to the document run to discover.
    // These were all null off Windows until src/qt_init.cpp created them, and the way
    // that was found was a segfault at src/render.cpp:378 -- so the assertion goes here,
    // where the reason a null is fatal is one line away.
    check(app.textFormat != nullptr, "the body text format exists");
    // AND THE PIXEL ASSERTIONS BELOW DO NOT COVER THIS, measured: the plant that removed
    // all six heading formats failed exactly these two checks and nothing else, because
    // layoutHeading falls back to app.textFormat and the document still draws. So a
    // by-name check is not redundant with a pixel count here -- it is the only guard.
    check(app.headingFormats[0] != nullptr && app.headingFormats[5] != nullptr,
          "all six heading levels have a format -- layoutHeading indexes this array directly");
    check(app.codeFormat != nullptr && app.boldFormat != nullptr && app.italicFormat != nullptr &&
              app.boldItalicFormat != nullptr && app.codeBoldFormat != nullptr &&
              app.codeItalicFormat != nullptr && app.codeBoldItalicFormat != nullptr &&
              app.supSubFormat != nullptr,
          "the code, bold, italic and nested-span formats exist too");
    // GUARDED, and the guard was put here by a planted control rather than by foresight.
    // The plant removed the six heading formats and this line SEGFAULTED on the null
    // instead of failing -- and a run that crashes prints no FAIL line at all, so the
    // harness reported zero failures for a plant it was supposed to catch. An assertion
    // that cannot survive the absence of the thing it asserts is not an assertion.
    check(app.headingFormats[0] && app.textFormat &&
              app.headingFormats[0]->GetFontSize() > app.textFormat->GetFontSize(),
          "an h1 is larger than body text, so the formats carry their d2d_init sizes and not one default");

    // A MEASUREMENT, not a null check, and the distinction is the point. A zero space
    // width collapses every inter-word gap in the document; it still draws, still counts
    // pixels, and looks almost right, so no pixel assertion below would have caught it.
    std::printf("space_widths=%.3f,%.3f,%.3f,%.3f\n", app.spaceWidthText, app.spaceWidthBold,
                app.spaceWidthItalic, app.spaceWidthCode);
    check(app.spaceWidthText > 0.0f && app.spaceWidthBold > 0.0f &&
              app.spaceWidthItalic > 0.0f && app.spaceWidthCode > 0.0f,
          "the four cached space widths were measured through the seam and are non-zero");

    QtAppWindow window(app);
    window.resize(600, 400);

    const tinta::RectF band = window.placeholderRect();
    std::printf("band=%.1f,%.1f..%.1f,%.1f\n", band.left, band.top, band.right, band.bottom);
    check(band.left >= 0.0f && band.top >= 0.0f && band.right <= 600.0f && band.bottom <= 400.0f,
          "the placeholder band lies inside the window");

    const tinta::RectF docBand = documentBand(window);
    std::printf("doc_band=%.1f,%.1f..%.1f,%.1f\n", docBand.left, docBand.top, docBand.right,
                docBand.bottom);
    // The one assertion that makes every document/placeholder comparison below mean
    // anything. If these bands overlapped, "the document band lit up" and "the
    // placeholder band lit up" would be partly the same reading.
    check(docBand.bottom <= band.top,
          "the document band and the placeholder band are disjoint, so the two counts measure different pixels");

    // --- Run 1: the real thing --------------------------------------------------
    const QImage painted = grabWindow(window);
    std::printf("grab=%dx%d isnull=%d paints=%d\n", painted.width(), painted.height(),
                painted.isNull() ? 1 : 0, window.paintCount());
    if (painted.isNull() || painted.width() < 600 || painted.height() < 400) {
        check(false, "the grab produced an image the size of the window (nothing below is decidable)");
        std::printf("checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    check(true, "the grab produced an image the size of the window");
    check(window.paintCount() >= 1, "the window's paintEvent ran");
    check(window.renderTargetInstalled(),
          "the paint got a non-null render target out of App's storage -- Seated<RenderTarget>::install()");
    check(app.renderTarget == nullptr,
          "App::renderTarget is null again after the paint, so the QPainter ended with the scope");

    const tinta::Color& bg = app.theme.background;
    std::printf("theme=%ls background=%d,%d,%d\n", app.theme.name, expect8(bg.r), expect8(bg.g),
                expect8(bg.b));
    check(pixelIs(painted, 2, 2, bg, 2),
          "a corner pixel carries the theme background, so Clear() reached the window");
    check(pixelIs(painted, 598, 398, bg, 2),
          "the opposite corner does too, so Clear() covered the whole device and not a sub-rect");

    const int paintedDiff = nonBackgroundPixels(painted, band, bg);
    std::printf("band_non_background_painted=%d\n", paintedDiff);
    check(paintedDiff > 0,
          "pixels in the band differ from the background, so DrawText through the seam drew glyphs");

    // App holds no document yet, so this is the placeholder branch -- asserted rather
    // than assumed, because it is the baseline every document assertion below is read
    // against.
    check(!window.documentPainted(),
          "with no document in App the window reports the placeholder branch, not a document");
    const int paintedDocBand = nonBackgroundPixels(painted, docBand, bg);
    std::printf("doc_band_non_background_placeholder=%d\n", paintedDocBand);
    check(paintedDocBand == 0,
          "the placeholder leaves the document band flat, so a count there later can only be a document");

    // --- Run 2: the control, with no text factory -------------------------------
    releaseQtBackends(app);
    check(app.textFactory == nullptr, "releaseQtBackends cleared the text factory");
    check(app.textFormat == nullptr && app.headingFormats[0] == nullptr,
          "releaseQtBackends dropped the document formats too, so nothing outlives the factory that made it");
    check(app.spaceWidthText == 0.0f,
          "and the measured space widths went with them rather than staying as stale numbers");

    const int paintsBeforeControl = window.paintCount();
    const QImage control = grabWindow(window);
    check(window.paintCount() > paintsBeforeControl, "the control grab painted again");
    check(pixelIs(control, 2, 2, bg, 2), "the control still cleared to the theme background");

    const int controlDiff = nonBackgroundPixels(control, band, bg);
    std::printf("band_non_background_control=%d\n", controlDiff);
    check(controlDiff == 0,
          "CONTROL: with no text factory the band is flat background, so the count above measures TEXT");
    check(paintedDiff > controlDiff,
          "CONTROL: the two runs disagree, so the text measurement could have come back negative");

    // --- Run 3: install again -------------------------------------------------
    installQtBackends(app);
    check(app.textFactory != nullptr, "installQtBackends is re-callable after a release");
    const QImage repainted = grabWindow(window);
    const int repaintedDiff = nonBackgroundPixels(repainted, band, bg);
    std::printf("band_non_background_reinstalled=%d\n", repaintedDiff);
    check(repaintedDiff == paintedDiff,
          "the re-installed factory draws the same glyph pixels as the first, so install/release is symmetric");

    // --- Run 4: a parsed document, drawn through render.cpp and paint_document.cpp --
    //
    // The window does the layout itself (QtAppWindow::paintDocument calls
    // layoutDocument when the run list is empty or the size changed), so this run
    // exercises the path src/main_qt.cpp will take rather than a layout the test
    // arranged beforehand. That is why nothing here calls layoutDocument directly.
    const qmd::ParseResult doc = app.parser.parse(kMarkdown);
    check(doc.success, "the fixture markdown parses");
    check(doc.root != nullptr, "the parse produced a root element");
    app.root = doc.root;

    // A DELTA, not an absolute. App::drawCalls is zeroed once per frame by
    // src/main_d2d.cpp:102, which is Windows-only chrome this stack does not run, so the
    // counter accumulates across grabs here. Reading it as a total would have passed for
    // the wrong reason after run 1.
    const size_t drawCallsBefore = app.drawCalls;
    const QImage documentImage = grabWindow(window);
    std::printf("layout_text_runs=%zu layout_rects=%zu draw_calls_delta=%zu\n",
                app.layoutTextRuns.size(), app.layoutRects.size(),
                app.drawCalls - drawCallsBefore);

    check(window.documentPainted(),
          "the window took the DOCUMENT branch: layoutDocument and paintDocumentLayout both ran");
    check(!app.layoutTextRuns.empty(),
          "layoutDocument produced text runs, so render.cpp executed off the Windows path");
    check(app.drawCalls > drawCallsBefore,
          "paintDocumentLayout reached its drawing sites, so the pass drew rather than returning early");

    const int documentDocBand = nonBackgroundPixels(documentImage, docBand, bg);
    std::printf("doc_band_non_background_document=%d\n", documentDocBand);
    check(documentDocBand > 0,
          "the document band carries non-background pixels, which only a document draws there");
    check(documentDocBand > paintedDocBand,
          "and it carries MORE than the placeholder run did, which was zero");

    // The strongest single discriminator in the file: if the document branch had somehow
    // produced the placeholder, every count above could still pass.
    check(documentImage != painted,
          "the document image is not the placeholder image, pixel for pixel");

    // --- Run 5: the control, document cleared -----------------------------------
    app.root = nullptr;
    const QImage clearedImage = grabWindow(window);
    check(!window.documentPainted(),
          "CONTROL: with App::root cleared the window falls back to the placeholder branch");
    const int clearedDocBand = nonBackgroundPixels(clearedImage, docBand, bg);
    std::printf("doc_band_non_background_cleared=%d\n", clearedDocBand);
    check(clearedDocBand == 0,
          "CONTROL: the document band is flat again, so the count in run 4 measured the DOCUMENT");
    check(nonBackgroundPixels(clearedImage, band, bg) == paintedDiff,
          "CONTROL: the placeholder returns at run 1's exact count, so drawing a document left no residue");

    // --- Run 6: the control, document present and no text factory ----------------
    //
    // WEAKER THAN IT LOOKS, and worth saying so where the assertion is rather than only
    // in the header: paintEvent's `if (!app_.textFactory) return;` sits BEFORE the branch
    // choice, so this proves the document band's pixels are text drawn through the seam.
    // It does not prove the document branch is itself factory-dependent.
    app.root = doc.root;
    releaseQtBackends(app);
    const QImage documentNoFactory = grabWindow(window);
    check(!window.documentPainted(),
          "CONTROL: no text factory stops the paint before the branch, document or not");
    check(nonBackgroundPixels(documentNoFactory, docBand, bg) == 0,
          "CONTROL: with a document set and no factory the document band is flat");
    check(nonBackgroundPixels(documentNoFactory, band, bg) == 0,
          "CONTROL: and so is the placeholder band");
    check(pixelIs(documentNoFactory, 2, 2, bg, 2),
          "CONTROL: the window still cleared, so the flat bands are a drawing result and not a dead grab");

    // --- Run 7: the path main() actually takes -- a file on disk ------------------
    //
    // Runs 4-6 set App::root from a parse of a string literal. main() does not have a
    // string literal; it has argv[1]. This run exercises loadDocumentFromPath, which is
    // the ONLY line of main() that is not a constructor call or a seam install, so it is
    // the only line of main() a test can reach at all.
    //
    // A REAL FILE RATHER THAN A FAKE ONE. The whole point of the function is the file
    // read -- parseDocument already had four years of Windows use behind it -- so a test
    // that injected the content would assert everything except the part that is new.
    installQtBackends(app);
    app.root = nullptr;
    app.sourceText.clear();

    // THE CACHED LAYOUT HAS TO GO OR THE PIXEL EQUALITY BELOW PROVES NOTHING, and this
    // test used to do it itself with two clear() calls right here. It does not any more,
    // because loadDocumentFromPath now clears the cache as part of replacing App::root --
    // and the difference between those two arrangements is the whole point of the change.
    //
    // QtAppWindow::paintDocument relayouts only when the run list is empty or the widget
    // resized, and neither is true at this line: run 4's runs are still in App and the
    // window is the same size. A test that cleared the cache itself would therefore pass
    // whether the loader invalidated anything or not, and the application -- which has no
    // such line -- would draw the OLD document after Ctrl+O. So the compensation is gone
    // and the behaviour is asserted instead, below.
    check(!app.layoutTextRuns.empty(),
          "run 4's layout is still cached at this point, so the assertion after the load is "
          "about the loader and not about an already-empty vector");

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "tinta_qt_entry_fixture.md";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << kMarkdown;
    }
    check(std::filesystem::exists(tmp), "the fixture file was written");

    // u8string() and not string(): loadDocumentFromPath takes UTF-8, because every
    // std::string path in this tree is UTF-8 (toWide is what widens them). path::string()
    // hands back the NATIVE narrow encoding, which on Windows is the ACP -- identical here,
    // since the temp directory is ASCII on both hosts, and that is exactly why it would be
    // the wrong call to make: it would work by luck and break on the first accented
    // username. In C++17 u8string() returns std::string, so no conversion is needed.
    const std::string tmpUtf8 = tmp.u8string();
    check(loadDocumentFromPath(app, tmpUtf8),
          "loadDocumentFromPath opened the file and parsed it");
    check(app.root != nullptr, "and put a root element in App, which is what main() needs");
    check(app.sourceText == std::string(kMarkdown),
          "and app.sourceText holds the file's bytes, which is annotationsParseSource' input "
          "for when annotations.cpp ports");

    const QImage fromFileImage = grabWindow(window);
    check(window.documentPainted(),
          "the window took the DOCUMENT branch for a document that came off DISK");
    const int fromFileDocBand = nonBackgroundPixels(fromFileImage, docBand, bg);
    std::printf("doc_band_non_background_from_file=%d\n", fromFileDocBand);
    check(fromFileDocBand == documentDocBand,
          "and it draws the SAME pixels as the in-memory parse of the same bytes, so the file "
          "read is byte-exact rather than merely successful");

    // CONTROL: a path that is not there. This is the branch main() reports to stderr, and
    // the assertion that matters is not the false -- it is that App is UNTOUCHED, because a
    // loader that cleared root on failure would blank a document the user was reading.
    // `auto` and not `const auto*`: App::root is a qmd::ElementPtr, which markdown.h:54
    // makes a std::shared_ptr<Element>. The pointer spelling was a guess off main_d2d.cpp's
    // usage -- `app.root = result.root` reads exactly the same either way -- and g++ refused
    // to deduce it, which is the compiler doing the reading I did not. The copy is also what
    // makes the assertion below meaningful: it holds the document alive independently, so a
    // loader that reset root would be caught rather than compared against a dangling value.
    const auto rootBefore = app.root;
    const std::string missing = tmpUtf8 + ".does-not-exist";
    check(!loadDocumentFromPath(app, missing),
          "CONTROL: a missing path returns false rather than throwing or succeeding");
    check(app.root == rootBefore,
          "CONTROL: and leaves App::root exactly as it was, so a failed open loses nothing");

    // CONTROL: a real file that is not markdown-parseable is NOT a thing that exists --
    // md4c accepts any byte sequence -- so the empty file is the nearest case, and it is
    // asserted to SUCCEED. That is the designed behaviour (an empty document is the start
    // page on the Windows side), and stating it here stops a later reader from "fixing" it.
    const std::filesystem::path emptyPath =
        std::filesystem::temp_directory_path() / "tinta_qt_entry_empty.md";
    { std::ofstream out(emptyPath, std::ios::binary); }
    const std::string emptyUtf8 = emptyPath.u8string();
    check(loadDocumentFromPath(app, emptyUtf8),
          "an EMPTY file loads successfully, which is the same empty document the Windows "
          "side shows as the start page");
    check(app.sourceText.empty(), "and app.sourceText is empty, not stale from the last load");

    // --- Run 8: SCROLLING -- the thing basis F is scored on ----------------------
    //
    // Every run above draws the first screenful of a document and stops. This one moves
    // it, which is what makes the difference between a render and a viewer, and it is
    // driven through QApplication::sendEvent rather than by calling the scroll functions
    // -- so what is under test is the WIRING (a QWheelEvent's units and sign, the key
    // table, the modifier branches) and not arithmetic a unit test could reach.
    //
    // THE PRECONDITION IS ASSERTED BEFORE ANYTHING IS SCROLLED. On a document shorter
    // than the window every maximum is zero, every scroll clamps to the top, and every
    // assertion in this run would pass while measuring nothing whatever. That is the most
    // likely false green available here, so the fixture is deliberately tall and the
    // check below is the one that makes the rest decidable.
    const std::filesystem::path tallPath =
        std::filesystem::temp_directory_path() / "tinta_qt_entry_tall.md";
    {
        std::ofstream out(tallPath, std::ios::binary);
        out << tallMarkdown();
    }
    const std::string tallUtf8 = tallPath.u8string();

    check(window.openDocument(tallUtf8),
          "QtAppWindow::openDocument loaded the tall fixture -- the SAME entry point main() "
          "takes for argv[1] and Ctrl+O takes after the dialog, so neither can drift");
    check(window.windowTitle().contains(QStringLiteral("tinta_qt_entry_tall")),
          "and it retitled the window from the path, which is how the running app says which "
          "file is open");
    check(sameScroll(app.scrollY, 0.0f), "a freshly opened document starts at the top");
    check(app.layoutTextRuns.empty(),
          "and it invalidated the cached layout, which is the assertion that replaced this "
          "test's own two clear() calls in run 7 -- without it a second document draws the "
          "first document's runs");

    const QImage topImage = grabWindow(window);
    check(window.documentPainted(), "and the window painted it as a document");

    const float scale = app.contentScale;
    std::printf("content_height=%.1f app_height=%d content_scale=%.2f\n", app.contentHeight,
                app.height, scale);
    check(app.contentHeight > static_cast<float>(app.height),
          "PRECONDITION: the tall fixture is taller than the window, so there is somewhere to "
          "scroll. Every check below measures nothing at all if this one fails");

    const float maxScrollY = std::max(0.0f, app.contentHeight - static_cast<float>(app.height));
    const float wheelStep = 60.0f * scale;
    const float lineStep = 50.0f * scale;
    const float pageStep = static_cast<float>(app.height) * 0.8f;

    sendWheel(window, -1.0f);
    std::printf("scroll_after_one_notch=%.2f expected=%.2f\n", app.scrollY, wheelStep);
    check(sameScroll(app.scrollY, wheelStep),
          "one wheel notch DOWN moves the document by 60 device-independent pixels, which is "
          "src/input.cpp:884's number reached through Qt's eighths-of-a-degree units");
    check(sameScroll(app.targetScrollY, app.scrollY),
          "and the target and the live offset agree, because the smooth-scroll animation the "
          "two members imply is not switched on in either stack");

    const QImage scrolledImage = grabWindow(window);
    check(scrolledImage != topImage,
          "THE PIXELS MOVED. A scroll that changes app.scrollY and leaves the image alone is a "
          "scroll the paint pass ignored, and no assertion on app.scrollY alone can tell those "
          "two apart");
    check(nonBackgroundPixels(scrolledImage, docBand, bg) > 0,
          "and the top band still carries document pixels, so the view scrolled rather than "
          "running off the end of the content");

    sendKey(window, Qt::Key_Home);
    check(sameScroll(app.scrollY, 0.0f), "Home returns to the top");
    check(grabWindow(window) == topImage,
          "AND THE WINDOW IS PIXEL-FOR-PIXEL WHAT IT WAS BEFORE THE SCROLL. A round trip is the "
          "one shape that cannot pass on a paint pass that draws something plausible at every "
          "offset");

    sendWheel(window, 1.0f);
    check(sameScroll(app.scrollY, 0.0f),
          "CONTROL: a wheel notch UP at the top clamps at zero instead of going negative");
    check(grabWindow(window) == topImage,
          "CONTROL: and the image is unchanged, so the clamp holds in the paint and not only in "
          "the arithmetic");

    sendWheel(window, -1000.0f);
    std::printf("scroll_after_1000_notches=%.2f max_scroll=%.2f\n", app.scrollY, maxScrollY);
    check(sameScroll(app.scrollY, maxScrollY),
          "CONTROL: a thousand notches land exactly on contentHeight - height and not one pixel "
          "further, so the clamp is a maximum rather than a slowdown");
    const QImage bottomImage = grabWindow(window);
    check(bottomImage != topImage, "the end of the document is a different picture from its top");
    check(nonBackgroundPixels(bottomImage, docBand, bg) > 0,
          "and there are still document pixels in the top band at the very bottom of the "
          "document, which is what distinguishes a clamp from scrolling into empty space");

    // The key table. Each of these is a number COPIED from src/input.cpp rather than
    // chosen, and the pair below is the reason they are asserted one at a time: the wheel
    // moves 60 and the arrow keys move 50. Harmonising them would look like a cleanup.
    sendKey(window, Qt::Key_Home);
    sendKey(window, Qt::Key_Down);
    check(sameScroll(app.scrollY, lineStep),
          "the Down arrow steps 50 device-independent pixels -- a DIFFERENT number from the "
          "wheel's 60, from src/input.cpp:3966");
    sendKey(window, Qt::Key_Up);
    check(sameScroll(app.scrollY, 0.0f), "and the Up arrow steps back by the same amount");
    sendKey(window, Qt::Key_J);
    check(sameScroll(app.scrollY, lineStep),
          "J scrolls down like the Down arrow -- the vi bindings are src/input.cpp:3943's, not "
          "an addition made in the port");
    sendKey(window, Qt::Key_K);
    check(sameScroll(app.scrollY, 0.0f), "and K scrolls up");
    sendKey(window, Qt::Key_PageDown);
    std::printf("scroll_after_page_down=%.2f expected=%.2f\n", app.scrollY, pageStep);
    check(sameScroll(app.scrollY, pageStep),
          "Page Down moves 80% of the WINDOW height, so a page is a screenful and not a step");
    sendKey(window, Qt::Key_Space);
    check(sameScroll(app.scrollY, std::min(2.0f * pageStep, maxScrollY)),
          "Space is a second Page Down, which is src/input.cpp:3973's binding");
    sendKey(window, Qt::Key_PageUp);
    check(sameScroll(app.scrollY, std::min(2.0f * pageStep, maxScrollY) - pageStep),
          "and Page Up moves one page back");
    sendKey(window, Qt::Key_End);
    check(sameScroll(app.scrollY, maxScrollY),
          "End jumps to the bottom, through ensureLayoutComplete -- the only scroll target that "
          "cannot be computed from the layout already in hand");
    sendKey(window, Qt::Key_Home);
    check(sameScroll(app.scrollY, 0.0f), "and Home comes back to the top");

    // THE HORIZONTAL AXIS, AND THE ONE PLACE THIS PORT DELIBERATELY DOES NOT COPY WINDOWS.
    // src/input.cpp:3959 does `targetScrollX += dpi(60)` with no upper clamp and leaves
    // src/paint_document.cpp to clamp the visible offset, so on Windows targetScrollX grows
    // past the end of the content and several Lefts do nothing afterwards. src/scroll.cpp
    // clamps both axes at every site. The tall fixture wraps to the window, so the maximum
    // here is ZERO and this assertion reads the divergence directly: it is satisfied by the
    // clamped behaviour and fails on Windows' arithmetic.
    const float maxScrollX = std::max(0.0f, app.contentWidth - documentViewportWidth(app));
    sendKey(window, Qt::Key_Right);
    std::printf("content_width=%.1f viewport_width=%.1f max_scroll_x=%.2f scroll_x=%.2f\n",
                app.contentWidth, documentViewportWidth(app), maxScrollX, app.scrollX);
    check(sameScroll(app.scrollX, std::min(60.0f * scale, maxScrollX)),
          "the Right arrow clamps the HORIZONTAL offset against the content width. THE ONE "
          "DELIBERATE DIVERGENCE from src/input.cpp, which clamps this axis at zero only -- so "
          "this check fails on the behaviour it was ported from, on purpose");
    check(sameScroll(app.targetScrollX, app.scrollX),
          "and targetScrollX is clamped too, which is the half Windows leaves growing");
    sendKey(window, Qt::Key_Left);
    check(sameScroll(app.scrollX, 0.0f), "and Left clamps at zero");

    // CONTROL: the two modifier branches. Ctrl+O is NOT sent from here and must not be:
    // promptOpenDocument calls QFileDialog::exec, which blocks until a human clicks, so a
    // test that sent it would hang the suite rather than fail it. What is testable is that
    // the modifier branch is taken at all, and Ctrl+J is the discriminating key -- if the
    // switch ran before the modifier check, Ctrl+J would scroll.
    sendKey(window, Qt::Key_J, Qt::ControlModifier);
    check(sameScroll(app.scrollY, 0.0f),
          "CONTROL: Ctrl+J does not scroll, so the modifier branch is checked BEFORE the key "
          "table and Ctrl+O's neighbours cannot move the document");
    sendWheel(window, -1.0f, Qt::ControlModifier);
    check(sameScroll(app.scrollY, 0.0f),
          "CONTROL: Ctrl+wheel does not scroll either. It is ZOOM on Windows "
          "(src/input.cpp:874) and unimplemented here, and doing nothing is the honest "
          "behaviour -- scrolling would be the wrong gesture rather than a missing one");

    sendWheel(window, -1.0f, Qt::ShiftModifier);
    check(sameScroll(app.scrollY, 0.0f),
          "Shift+wheel does not scroll VERTICALLY: it is the horizontal gesture, from "
          "src/input.cpp:875");
    check(sameScroll(app.scrollX, std::min(60.0f * scale, maxScrollX)),
          "it moves the horizontal offset instead -- zero here, because the content wraps to "
          "the window, which is the same clamp the Right arrow hit above");
    sendKey(window, Qt::Key_Home);

    // NOTHING ABOVE TESTS THIS, and that is the point of asserting it separately.
    // QApplication::sendEvent delivers to a widget whether or not it can hold focus, so
    // every key check in this run would pass on a window that never receives a single key
    // in the running application -- and QWidget's default IS Qt::NoFocus. There is no
    // pixel, no probe and no assertion above that can see the difference.
    check(window.focusPolicy() != Qt::NoFocus,
          "the window can take keyboard focus, so the key table above is reachable in the "
          "application and not only through sendEvent");

    // --- Run 9: A SECOND DOCUMENT, which is basis F's other half ------------------
    //
    // Ctrl+O's dialog cannot be driven from a test; everything after it can, and this is
    // it. TWO DEFECTS SHARE ONE ASSERTION at the end, and neither is visible without it.
    check(window.openDocument(tmpUtf8), "the short document opens again over the tall one");
    const QImage shortFresh = grabWindow(window);
    check(window.documentPainted(), "and paints as a document");
    std::printf("content_height_short=%.1f\n", app.contentHeight);

    check(window.openDocument(tallUtf8), "the tall document opens again");
    // THIS PAIR FOUND A REAL DEFECT AND IS THE REASON THE LOAD SETS app.layoutDirty.
    // contentHeight is read BEFORE the End key and asserted to be zero rather than the
    // short document's 316.8: a load that clears the run vectors and leaves the previous
    // document's geometry behind makes every clamp arithmetic about the wrong document,
    // and the failure is silent because the number is plausible. Then End -- which is the
    // ONLY scroll target that cannot be computed from the layout already in hand, so it
    // is the only one that has to force a layout -- lands on this document's maximum.
    check(sameScroll(app.contentHeight, 0.0f),
          "the load left no geometry from the document before it, so a scroll arriving before "
          "the first paint cannot clamp against the wrong document's height");
    sendKey(window, Qt::Key_End);
    std::printf("end_before_any_paint: scroll_y=%.2f content_height=%.1f runs=%zu expected=%.2f\n",
                app.scrollY, app.contentHeight, app.layoutTextRuns.size(), maxScrollY);
    check(sameScroll(app.scrollY, maxScrollY),
          "and End scrolled to the bottom WITH NO PAINT IN BETWEEN, so ensureLayoutComplete "
          "actually laid the document out. Every other End in this file runs after a paint has "
          "already built the layout, which is why they all passed while this one failed");
    check(!app.layoutTextRuns.empty(),
          "and the layout it forced is a real one, not an empty pass that merely cleared the "
          "dirty flag");
    grabWindow(window);

    check(window.openDocument(tmpUtf8), "and now the short document opens over a SCROLLED tall one");
    // BEFORE ANY PAINT. src/paint_document.cpp clamps app.scrollY against the new
    // document's height on the next paint, which would hide a missing reset entirely --
    // the image comparison below cannot see this defect, only this line can.
    check(sameScroll(app.scrollY, 0.0f),
          "the load reset the scroll offset itself, BEFORE a paint had a chance to clamp it "
          "against the new document's height and hide the omission");
    check(app.layoutTextRuns.empty(), "and dropped the tall document's cached layout");

    const QImage shortAfterTall = grabWindow(window);
    check(shortAfterTall == shortFresh,
          "A SHORT DOCUMENT OPENED AFTER A SCROLLED TALL ONE IS PIXEL-FOR-PIXEL THE SAME AS "
          "OPENING IT FRESH. This is the whole of Ctrl+O in one comparison: it fails if the "
          "loader leaves the previous layout cached, because QtAppWindow::paintDocument "
          "relayouts only on a resize or an empty run list");

    // --- Run 10: the viewport sync in the INPUT path, not the paint path ----------
    //
    // app.width and app.height used to be assigned in exactly one place -- inside
    // QtAppWindow::paintDocument -- and every scroll maximum is derived from app.height. So
    // a key or wheel event arriving after a resize and before the next paint would clamp
    // against the OLD height. THE ABSENCE OF A GRAB IN THIS RUN IS THE INSTRUMENT: with one,
    // the paint would sync the viewport and the assertion would pass either way.
    check(window.openDocument(tallUtf8), "the tall document is back for the resize check");
    grabWindow(window);
    window.resize(600, 700);
    check(window.height() == 700, "the widget is 700 tall now");
    sendKey(window, Qt::Key_End);  // deliberately no grab between the resize and this line
    std::printf("app_height_after_resize=%d content_height=%.1f scroll_y=%.2f\n", app.height,
                app.contentHeight, app.scrollY);
    check(app.height == 700,
          "the KEY HANDLER synced App's viewport from the widget with no paint in between");
    check(sameScroll(app.scrollY, std::max(0.0f, app.contentHeight - 700.0f)),
          "and End landed on a maximum computed from the new height -- 300 pixels away from "
          "where the stale one would have put it");

    std::error_code rmEc;
    std::filesystem::remove(tmp, rmEc);
    std::filesystem::remove(emptyPath, rmEc);
    std::filesystem::remove(tallPath, rmEc);

    // Idempotent by construction -- install(nullptr) on an empty storage -- and called
    // here anyway so the file ends in the state every earlier run ended in.
    releaseQtBackends(app);

    std::printf("checks=%d failures=%d\n", g_checks, g_failures);
    if (g_failures == 0)
        std::printf("PASS: a Qt window lays out and draws a parsed document through "
                    "render.cpp and paint_document.cpp, off the Windows path.\n");
    return g_failures == 0 ? 0 : 1;
}
