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
#include <QPixmap>

#include <cstdio>
#include <filesystem>
#include <fstream>

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

    // THE CACHED LAYOUT HAS TO GO OR THE PIXEL EQUALITY BELOW PROVES NOTHING.
    // QtAppWindow::paintDocument relayouts only when the run list is empty or the widget
    // resized, and neither is true here -- run 4's runs are still in App and the window is
    // the same size. So without this the grab would redraw run 4's layout, the counts would
    // match, and the assertion would be comparing the file's parse against nothing at all.
    app.layoutTextRuns.clear();
    app.layoutRects.clear();

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

    std::error_code rmEc;
    std::filesystem::remove(tmp, rmEc);
    std::filesystem::remove(emptyPath, rmEc);

    // Idempotent by construction -- install(nullptr) on an empty storage -- and called
    // here anyway so the file ends in the state every earlier run ended in.
    releaseQtBackends(app);

    std::printf("checks=%d failures=%d\n", g_checks, g_failures);
    if (g_failures == 0)
        std::printf("PASS: a Qt window lays out and draws a parsed document through "
                    "render.cpp and paint_document.cpp, off the Windows path.\n");
    return g_failures == 0 ? 0 : 1;
}
