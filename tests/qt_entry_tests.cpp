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
//   5. THE CHROME FORMATS AND THE TWO SCALES (runs 11-13). Ten formats whose only
//      callers are still Windows-only files, so no pixel in here can see them and the
//      by-name and by-SIZE assertions are the entire guard. The size half is not
//      pedantry: chrome is scaled by contentScale alone and editor text by
//      contentScale * zoomFactor, both arrangements produce ten non-null formats, and
//      only the numbers tell them apart.
//   6. UNIFORM LINE SPACING (run 14) -- the one assertion in this file that would have
//      FAILED before the slice that added it. QtTextFormat stored SetLineSpacing's
//      three arguments and QtTextRun read none of them, so the editor format could be
//      non-null, correctly sized, and lay text out on the wrong grid.
//   7. THE SCROLLBAR (runs 15-16), which is src/scrollbar.cpp lifted out of
//      main_d2d.cpp's paint handler rather than a QScrollBar, and resizeEvent's scroll
//      re-clamp. Both are read in PIXELS, with a short document as the control.
//   8. MATHS (run 17). src/math_render.cpp measures through app.textFactory and draws
//      through the render seam as of this slice, and src/math_stub.cpp -- which returned
//      null from mathParse so that every equation fell back to its own LaTeX source -- is
//      deleted. This is the only target that can tell those two states apart off Windows:
//      a successful LINK proves the symbols exist, and the stub also linked. What the stub
//      could never produce is a laid-out box, so run 17 is a pair of documents whose TeX
//      differs by one token -- valid or not -- read in App's own primitive lists and in
//      pixels. Its first version measured an H1 underline and said so; see the fixtures.
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
#include <QResizeEvent>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdio>
// std::abs(int) is declared here and not in <cmath>. libstdc++ happens to pull it in
// anyway, which is exactly the sort of thing MSVC does not.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <memory>

#include "app.h"
#include "document.h"
#include "file_utils.h"
#include "geometry.h"
#include "math_render.h"
#include "qt_init.h"
#include "qt_window.h"
#include "text_api.h"
// The chrome formats' own header, and the reason this file needs it now is the port: the
// bodies run 11 asserts on moved out of src/qt_init.cpp into src/text_formats_app.cpp, so
// installQtBackends' call resolves through here. qt_init.h still declares the Qt-named
// wrappers, which is why every existing assertion below compiles unchanged.
#include "text_formats.h"
#include "theme_data.h"

namespace {

// The document fixture. A heading, a paragraph and a fenced code block exercise three
// different layout paths and three different text formats, which is what runs 4-6 are
// for.
//
// STILL WITHOUT MATHS, BUT NO LONGER FOR THE OLD REASON, and the old reason is recorded
// rather than deleted because it named a limit this file was the instrument for. It read:
// "DELIBERATELY WITHOUT MATHS: src/math_render.cpp does not compile off Windows and
// src/math_stub.cpp stands in for it, so an equation here would be asserting the stub's
// box arithmetic and reading as a rendered formula." Both halves are now false --
// math_render.cpp measures through app.textFactory and the stub is deleted -- so maths
// gets its OWN fixtures and its own run (17) instead of being smuggled in here. Keeping
// this one equation-free is what lets run 17's counts be read as maths: every earlier run
// is a control for it by construction.
const char kMarkdown[] =
    "# Heading one\n"
    "\n"
    "A paragraph of ordinary text, long enough to be more than one glyph wide.\n"
    "\n"
    "```cpp\n"
    "int x = 1;\n"
    "```\n";

// RUN 17'S PAIR, and the pair is the instrument -- neither string is evidence alone.
//
// The two documents are IDENTICAL except for one token: the matrix is closed by
// \end{bmatrix} in one and \end{matrix} in the other, which is the malformed input
// tests/math_layout_tests.cpp:210 already uses to assert the fallback exists. So mathParse
// returns a box for the first and null for the second, and src/render.cpp's
// layoutMathBlock takes its typeset path for one and its raw-source-in-code-style path for
// the other with every other byte of input held constant.
//
// WHY A PAIR RATHER THAN A DOCUMENT WITH AND WITHOUT AN EQUATION. "The window drew more
// pixels with an equation in it" is satisfied by a code block full of LaTeX source, which
// is exactly what the fallback draws and exactly what qinta did until this slice. A pixel
// count against a maths-free control would therefore have gone green on the stub. What
// separates the two paths is which PRIMITIVE LISTS fill: a laid-out matrix pushes its
// bracket strokes into App::layoutLines (src/math_render.cpp:799) and no code-block
// background, and the fallback does the exact opposite.
//
// THE FENCED $$ FORM ON ITS OWN LINES is the spelling tests/math_layout_tests.cpp:177
// already proves the Markdown parser turns into a MathDisplay element, so run 17 measures
// the maths backend rather than re-litigating a parser question that has its own test.
//
// AND THERE IS NO HEADING IN EITHER FIXTURE, WHICH IS A CORRECTION AND NOT A SIMPLIFICATION.
// Both fixtures opened with "# Equation" and the run asserted that App::layoutLines was
// non-empty for the equation and EMPTY for the fallback, on the stated grounds that
// mathBoxRetain was the only producer of that list in this fixture. The assertion failed on
// both platforms with identical numbers -- math_lines=1, fallback_lines=1, and the same
// rule at (40.0,136.4)-(560.0,136.4) -- which is 520 pixels wide in a 600-pixel window and
// therefore not a fraction bar. It was the H1 UNDERLINE (src/render.cpp:400-405). Two
// things came out of that: the heading is gone, so the only primitives in either fixture
// come from the $$ block; and \frac was replaced by a matrix, because a fraction bar is a
// RULE and lands in App::layoutRects, where a code block's background also lands, whereas
// bracket strokes are LINES and a code block draws none. Recorded here rather than
// silently fixed because a control that measured the heading would have gone green on the
// stub, which is the exact failure this file's header warns about.
const char kMathMarkdown[] =
    "$$\n"
    "\\begin{bmatrix}1&2\\\\3&4\\\\5&6\\\\7&8\\\\9&0\\end{bmatrix}\n"
    "$$\n";

const char kMathFallbackMarkdown[] =
    "$$\n"
    "\\begin{bmatrix}1&2\\\\3&4\\\\5&6\\\\7&8\\\\9&0\\end{matrix}\n"
    "$$\n";

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

// Resize the widget AND deliver the resize event, because on a widget that has never been
// shown the first half does not imply the second.
//
// MEASURED, not assumed, and it is the reason this helper exists. The resize run below was
// written with QApplication::processEvents() after the resize and read app.height still at
// 400 with layoutDirty clear: Qt defers the resize event of a hidden widget to its show,
// so nothing was ever posted for processEvents to deliver. resize() alone therefore leaves
// width()/height() correct and QtAppWindow::resizeEvent never entered -- which would have
// been a test that reported the override missing when the override was fine.
//
// WHAT THIS DOES AND DOES NOT PROVE, on the same footing as sendWheel's note about focus:
// it exercises the real dispatch, QWidget::event() to the virtual, so the override is
// under test. It does NOT prove Qt delivers a resize event to a SHOWN window, which is
// Qt's contract rather than tinta's -- and showing a window from a test would put a real
// frame on the screen of whoever runs the suite.
void sendResize(QtAppWindow& window, int w, int h) {
    const QSize before(window.width(), window.height());
    window.resize(w, h);
    QResizeEvent ev(QSize(w, h), before);
    QApplication::sendEvent(&window, &ev);
}

// float equality, at the tolerance the arithmetic deserves. Every value asserted below is
// a sum of at most a few multiples of 50 or 60, so anything further out than this is a
// different number and not a rounding difference. Written as a helper because an
// exact == on a float would be the wrong claim and a `> 0` would be the wrong test.
bool sameScroll(float actual, float expected) {
    return std::abs(actual - expected) < 0.01f;
}

// The same tolerance under a name that says what is being compared. Deliberately not a
// call to sameScroll: a font size is not a scroll offset, and a reader who found a format
// size compared with a scroll helper would be right to wonder which one had been reused
// by accident.
bool sameSize(float actual, float expected) {
    return std::abs(actual - expected) < 0.01f;
}

// A long single-line string for the two format runs below. Long enough that any format
// will overflow a narrow layout box, which is what makes the elide measurable.
const wchar_t kLongRow[] =
    L"a-very-long-file-name-that-cannot-possibly-fit-inside-a-narrow-column.md";
constexpr uint32_t kLongRowLength =
    static_cast<uint32_t>(sizeof(kLongRow) / sizeof(kLongRow[0]) - 1);

// Measured width of `kLongRow` laid out through the seam at `maxWidth`, or -1 if the
// layout could not be created. A float rather than a bool so the elided and unelided
// numbers can be printed side by side -- the assertion is that they differ by hundreds
// of pixels, and a boolean pair would hide a three-pixel difference passing for an elide.
float rowWidthAt(App& app, tinta::TextFormat* format, float maxWidth,
                 uint32_t* lineCountOut) {
    if (!app.textFactory || !format) return -1.0f;
    std::unique_ptr<tinta::TextLayout> layout =
        app.textFactory->CreateTextLayout(kLongRow, kLongRowLength, format, maxWidth, 100.0f);
    if (!layout) return -1.0f;
    tinta::TextMetrics metrics{};
    layout->GetMetrics(&metrics);
    if (lineCountOut) *lineCountOut = metrics.lineCount;
    return metrics.width;
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
    // THE TYPOGRAPHY SETS, AND THESE TWO LINES FAILED WHEN THEY WERE FIRST RUN, reading
    // count 0 against the 3 and 2 below -- which is what they are here to record.
    // createTypography had exactly ONE caller in the tree, src/main_d2d.cpp, so this stack
    // had never had an OpenType feature list at all: QtTextLayout::SetTypography returns at
    // its own first line on an empty set, for all 13 createLayout sites in render.cpp.
    //
    // Nothing rendered differently, and that is the reason it survived: of the three tags
    // Qt can honour, liga and calt are HarfBuzz defaults already on and kern maps to
    // QTextCharFormat::setFontKerning, whose Qt default is already true. So the two paths
    // agreed in OUTPUT while disagreeing in DATA, and no pixel count could ever have
    // separated them. A default that happens to agree is not agreement: the day a set
    // carries parameter = 0 to DISABLE a feature, or qtCanHonourFeature widens past Qt
    // 6.4, the empty list starts drawing differently with nothing watching.
    check(app.bodyTypography.count == 3 && app.bodyTypography.has("liga") &&
              app.bodyTypography.has("kern") && app.bodyTypography.has("calt"),
          "the BODY typography set is installed on this stack -- liga + kern + calt");
    check(app.codeTypography.count == 2 && app.codeTypography.has("liga") &&
              app.codeTypography.has("dlig"),
          "the CODE typography set is installed on this stack -- liga + dlig");
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
        std::filesystem::temp_directory_path() / "qinta_entry_fixture.md";
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
        std::filesystem::temp_directory_path() / "qinta_entry_empty.md";
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
        std::filesystem::temp_directory_path() / "qinta_entry_tall.md";
    {
        std::ofstream out(tallPath, std::ios::binary);
        out << tallMarkdown();
    }
    const std::string tallUtf8 = tallPath.u8string();

    check(window.openDocument(tallUtf8),
          "QtAppWindow::openDocument loaded the tall fixture -- the SAME entry point main() "
          "takes for argv[1] and Ctrl+O takes after the dialog, so neither can drift");
    check(window.windowTitle().contains(QStringLiteral("qinta_entry_tall")),
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

    // --- Run 11: THE CHROME FORMATS -----------------------------------------------
    //
    // Ten formats that were null off Windows until src/qt_init.cpp's
    // installQtChromeFormats existed, and the reason they are asserted BY NAME and BY
    // SIZE rather than through a pixel count is that no pixel in this file can see them:
    // their only callers are src/overlays.cpp, src/editor.cpp and src/tabs.cpp, none of
    // which compiles off Windows yet. So this run is the whole guard, and a null here is
    // the segfault src/render.cpp:378 already produced once for the document formats.
    const bool chromeAllPresent =
        app.searchTextFormat != nullptr && app.signalSmallFormat != nullptr &&
        app.themeTitleFormat != nullptr && app.themeHeaderFormat != nullptr &&
        app.statsFormat != nullptr && app.folderBrowserFormat != nullptr &&
        app.tocFormat != nullptr && app.tocFormatBold != nullptr &&
        app.editorTextFormat != nullptr && app.editorGutterFormat != nullptr;
    check(chromeAllPresent,
          "all TEN chrome formats exist after installQtBackends -- search, signals, theme "
          "title, theme header, stats, folder browser, both TOC, editor text and gutter");

    // THE SIZES, and they are not decoration. Every number here is copied from
    // src/d2d_init.cpp, and a format that exists at the wrong size draws chrome that
    // looks deliberate: a stats box at 16 instead of 13 overflows its fixed rectangle,
    // which is the exact failure the two-scale rule in run 12 exists to prevent.
    //
    // GUARDED ON chromeAllPresent, AND THE GUARD IS NOT DEFENSIVE PADDING. Written without
    // it, the printf below dereferences ten pointers, so the planted control that removes
    // installQtChromeFormats would SEGFAULT here instead of failing -- and a run that
    // crashes prints no FAIL line and no tally, so the harness reports zero failures for
    // the plant it exists to catch. That exact defect is already recorded forty lines up,
    // against the heading formats; this is the same lesson applied before it bit twice.
    if (!chromeAllPresent) {
        std::printf("chrome_sizes=UNREADABLE: at least one format is null, so every size "
                    "assertion below is skipped rather than crashed through\n");
    } else {
    std::printf("chrome_sizes=%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                app.searchTextFormat->GetFontSize(), app.signalSmallFormat->GetFontSize(),
                app.themeTitleFormat->GetFontSize(), app.themeHeaderFormat->GetFontSize(),
                app.statsFormat->GetFontSize(), app.folderBrowserFormat->GetFontSize(),
                app.tocFormatBold->GetFontSize(), app.tocFormat->GetFontSize(),
                app.editorTextFormat->GetFontSize(), app.editorGutterFormat->GetFontSize());
    check(sameSize(app.searchTextFormat->GetFontSize(), 16.0f * scale) &&
              sameSize(app.signalSmallFormat->GetFontSize(), 10.0f * scale) &&
              sameSize(app.themeTitleFormat->GetFontSize(), 28.0f * scale) &&
              sameSize(app.themeHeaderFormat->GetFontSize(), 11.0f * scale) &&
              sameSize(app.statsFormat->GetFontSize(), 13.0f * scale),
          "the five overlay formats carry d2d_init's sizes: 16, 10, 28, 11, 13");
    check(sameSize(app.folderBrowserFormat->GetFontSize(), 13.0f * scale) &&
              sameSize(app.tocFormatBold->GetFontSize(), 13.0f * scale) &&
              sameSize(app.tocFormat->GetFontSize(), 12.0f * scale),
          "and the three trimmed rows carry 13, 13 and 12 -- the TOC pair DIFFERS in size "
          "as well as weight, which is easy to miss where the two calls are adjacent");
    check(sameSize(app.editorTextFormat->GetFontSize(), 14.0f * scale * app.zoomFactor) &&
              sameSize(app.editorGutterFormat->GetFontSize(), 9.5f * scale * app.zoomFactor),
          "and the two editor formats carry 14 and 9.5");
    }  // chromeAllPresent

    // A MEASUREMENT AND NOT A NULL CHECK, for the same reason the space widths are one: a
    // zero character width makes every editor caret land on column zero, and the editor
    // still draws. src/editor.cpp divides by this number.
    std::printf("editor_char_width=%.3f\n", app.editorCharWidth);
    check(app.editorCharWidth > 0.0f,
          "editorCharWidth was measured through the seam -- src/editor.cpp divides by it, so "
          "a zero here is a division by zero and not a cosmetic default");

    // THE TRIMMING SIGNS, counted rather than checked for non-emptiness. Three formats go
    // through configureTrimmedRow and QtTextFactory::CreateEllipsisTrimmingSign never
    // returns null, so the number is exactly three -- and the vector exists at all only
    // because DirectWrite retains the sign where Qt does not (include/app.h's
    // App::trimmingSigns). A count of 0 would mean the ellipsis went out of scope and the
    // Windows build would be reading freed memory.
    std::printf("trimming_signs=%zu\n", app.trimmingSigns.size());
    check(app.trimmingSigns.size() == 3,
          "App::trimmingSigns holds exactly three signs -- folder browser and both TOC rows "
          "-- which is what keeps the DirectWrite backend's retained pointer alive");

    // AND THE TRIMMING IS HONOURED, which is a different claim from "SetTrimming was
    // called". THE DISCRIMINATING PAIR: editorTextFormat and folderBrowserFormat are BOTH
    // NoWrap, so neither can wrap out of the box; only one of them elides. A long row
    // measured at a narrow width therefore overflows on one and fits on the other, and the
    // gap is hundreds of pixels rather than a rounding difference.
    const float narrow = 60.0f * scale;
    uint32_t elidedLines = 0;
    uint32_t overflowLines = 0;
    const float elidedWidth = rowWidthAt(app, app.folderBrowserFormat, narrow, &elidedLines);
    const float overflowWidth = rowWidthAt(app, app.editorTextFormat, narrow, &overflowLines);
    std::printf("row_width_elided=%.2f row_width_nowrap=%.2f narrow=%.2f lines=%u,%u\n",
                elidedWidth, overflowWidth, narrow, elidedLines, overflowLines);
    check(elidedLines == 1 && overflowLines == 1,
          "both formats produce ONE line at a narrow width, so neither wrapped and the width "
          "comparison below is about the elide alone");
    check(elidedWidth >= 0.0f && elidedWidth <= narrow,
          "the folder browser row FITS its 60-pixel box, so the ellipsis trimming recorded by "
          "configureTrimmedRow reached QtTextRun's elide");
    check(overflowWidth > narrow * 2.0f,
          "CONTROL: the editor format, NoWrap and untrimmed, overflows the same box by more "
          "than double -- so the fit above is an elide and not a narrow font");

    // --- Run 11b: THE SHARED INSTALL IS IDEMPOTENT ---------------------------------
    //
    // A DEFECT THIS PORT WOULD HAVE INTRODUCED, not one it inherited, which is why the
    // assertion is new rather than moved. installQtChromeFormats pushed three ellipsis
    // signs into App::trimmingSigns per call and cleared none, and that was SAFE because
    // it had exactly one caller at startup -- src/qt_init.cpp said so in as many words.
    // Sharing the body with Windows shared its CALLERS, and updateOverlayFormats ran on
    // every theme change, every zoom step and every DPI change. Three signs a theme change,
    // for the life of the process, and nothing in either file would have flagged it.
    //
    // CALLED DIRECTLY rather than through installQtBackends, because the claim is about
    // THIS function's contract: its first act is releaseChromeFormats(app).
    const size_t signsAfterFirst = app.trimmingSigns.size();
    tinta::TextFormat* const beforeSecond = app.folderBrowserFormat;
    installChromeFormats(app);
    const bool stillAllPresent =
        app.searchTextFormat != nullptr && app.signalSmallFormat != nullptr &&
        app.themeTitleFormat != nullptr && app.themeHeaderFormat != nullptr &&
        app.statsFormat != nullptr && app.folderBrowserFormat != nullptr &&
        app.tocFormat != nullptr && app.tocFormatBold != nullptr &&
        app.editorTextFormat != nullptr && app.editorGutterFormat != nullptr;
    std::printf("idem_signs=%zu,%zu idem_nonnull=%d idem_charwidth=%.3f\n", signsAfterFirst,
                app.trimmingSigns.size(), stillAllPresent ? 10 : 0, app.editorCharWidth);
    check(app.trimmingSigns.size() == 3,
          "a SECOND installChromeFormats with no release between leaves THREE trimming signs "
          "and not six -- the release-first is what makes the shared body safe on Windows, "
          "where its caller runs on every theme change and every zoom step");
    // THE OTHER HALF, and without it the assertion above passes on a function that releases
    // and then returns: an empty vector is also not six.
    check(stillAllPresent && app.editorCharWidth > 0.0f,
          "and the ten formats are still non-null with editorCharWidth re-measured, so the "
          "count above is a REBUILD and not a release that forgot to install");
    // POINTER IDENTITY, ASSERTED THE OPPOSITE WAY ROUND FROM THE PREVIEWS BELOW, and the
    // difference is the whole design: ensureThemePreviewFormats is a lazy door that early-
    // returns, so the same pointer proves the no-op; installChromeFormats releases and
    // rebuilds, so a SURVIVING pointer would mean the release-first never ran and the
    // three-sign count above came from an early return instead.
    check(app.folderBrowserFormat != beforeSecond,
          "and the format is a NEW object -- idempotent in STATE, not by early return, which "
          "is what a caller on the theme-change path needs");

    // --- Run 12: THE TWO-SCALE RULE ------------------------------------------------
    //
    // The single subtlest line in installQtChromeFormats. Chrome uses app.contentScale
    // ALONE; the two editor formats use contentScale * zoomFactor, because editor text IS
    // document text. src/d2d_init.cpp:378 gives the reason -- "fixed UI size so zooming the
    // document cannot overflow the fixed stats box" -- and getting it backwards makes the
    // search box and the stats readout grow with Ctrl+= and look like a feature rather
    // than a defect.
    //
    // NO PIXEL IN THIS FILE CAN SEE THIS EITHER, and no null check can: both arrangements
    // produce ten non-null formats. Only the sizes tell them apart.
    releaseQtBackends(app);
    app.zoomFactor = 2.0f;
    installQtBackends(app);
    std::printf("zoom2_chrome=%.2f,%.2f zoom2_editor=%.2f,%.2f\n",
                app.searchTextFormat ? app.searchTextFormat->GetFontSize() : -1.0f,
                app.statsFormat ? app.statsFormat->GetFontSize() : -1.0f,
                app.editorTextFormat ? app.editorTextFormat->GetFontSize() : -1.0f,
                app.editorGutterFormat ? app.editorGutterFormat->GetFontSize() : -1.0f);
    check(app.searchTextFormat && app.statsFormat &&
              sameSize(app.searchTextFormat->GetFontSize(), 16.0f * scale) &&
              sameSize(app.statsFormat->GetFontSize(), 13.0f * scale),
          "AT DOUBLE ZOOM THE CHROME DOES NOT MOVE: search is still 16 and stats still 13, so "
          "zooming a document cannot overflow a fixed overlay box");
    check(app.editorTextFormat && app.editorGutterFormat &&
              sameSize(app.editorTextFormat->GetFontSize(), 28.0f * scale) &&
              sameSize(app.editorGutterFormat->GetFontSize(), 19.0f * scale),
          "and the two EDITOR formats DID double, to 28 and 19 -- editor text is document "
          "text, so it is the one thing on the other scale");

    releaseQtBackends(app);
    app.zoomFactor = 1.0f;
    installQtBackends(app);
    check(app.editorTextFormat && sameSize(app.editorTextFormat->GetFontSize(), 14.0f * scale),
          "and back at zoom 1 the editor format is 14 again, so the install reads zoomFactor "
          "live rather than baking it once");

    // --- Run 13: THE THEME PREVIEWS, and the lazy door -----------------------------
    //
    // Roughly thirty formats across every theme, wanted only while the theme chooser is
    // open, so installQtBackends does NOT create them -- that is asserted first, because
    // an eager install would be a silent cost on every launch and would look identical
    // from every other assertion in this file.
    check(app.themePreviewFormats.empty(),
          "installQtBackends did NOT build the per-theme previews: they are the lazy door, "
          "the same division of labour d2d_init uses");

    ensureQtThemePreviewFormats(app);
    std::printf("theme_count=%d preview_slots=%zu\n", themeCount(),
                app.themePreviewFormats.size());
    check(app.themePreviewFormats.size() == static_cast<size_t>(themeCount()),
          "ensureQtThemePreviewFormats filled one slot per theme");

    int previewTriples = 0;
    bool previewSizesOrdered = true;
    for (const App::ThemePreviewFormats& slot : app.themePreviewFormats) {
        if (slot.name && slot.preview && slot.code) previewTriples++;
        // 14 / 11 / 10. Asserted as an ORDER rather than three equalities per theme
        // because the sizes are shared and the families are not -- a per-theme size
        // check would be thirty copies of one claim.
        if (!slot.name || !slot.preview || !slot.code ||
            !(slot.name->GetFontSize() > slot.preview->GetFontSize() &&
              slot.preview->GetFontSize() > slot.code->GetFontSize())) {
            previewSizesOrdered = false;
        }
    }
    std::printf("preview_triples=%d\n", previewTriples);
    check(previewTriples == themeCount(),
          "every theme got all THREE formats -- name, preview and code -- so a chooser row "
          "cannot draw two of three and leave a null for the third");
    check(previewSizesOrdered,
          "and each triple is ordered 14 > 11 > 10, so the three carry their d2d_init sizes "
          "rather than one shared default");

    // IDEMPOTENCE BY POINTER IDENTITY, not by size. The vector being non-empty afterwards
    // would pass on a function that rebuilt all thirty formats every time the chooser
    // opened; the same pointer proves the early return fired. It also proves the RESIZE
    // ordering held -- a push_back per theme would have reallocated and invalidated every
    // pointer already handed to a caller.
    tinta::TextFormat* const firstName = app.themePreviewFormats[0].name;
    ensureQtThemePreviewFormats(app);
    check(app.themePreviewFormats.size() == static_cast<size_t>(themeCount()) &&
              app.themePreviewFormats[0].name == firstName,
          "a second call is a no-op down to the POINTER, so the chooser can call it on every "
          "open without rebuilding thirty formats");

    // AND THE RELEASE CLEARS THE VECTOR RATHER THAN NULLING THE WRAPPERS INSIDE IT, which
    // is the half that is easy to get wrong: the early return above tests EMPTINESS, so a
    // release that left thirty seated nulls behind would turn the lazy door into a
    // permanent no-op and the chooser would draw nothing after the first theme change.
    releaseQtBackends(app);
    check(app.themePreviewFormats.empty(),
          "releaseQtBackends CLEARED the preview vector, so the lazy door still opens after a "
          "release -- nulling the wrappers in place would have wedged it shut");
    check(app.trimmingSigns.empty() && app.editorCharWidth == 0.0f,
          "and it dropped the trimming signs and zeroed editorCharWidth rather than leaving "
          "them as stale values pointing at a released factory");
    check(app.searchTextFormat == nullptr && app.editorGutterFormat == nullptr,
          "and the chrome formats went with the factory that made them");

    installQtBackends(app);

    // --- Run 14: UNIFORM LINE SPACING ----------------------------------------------
    //
    // THE ONE ASSERTION IN THIS FILE THAT WOULD HAVE FAILED BEFORE THIS SLICE, and the
    // defect it catches is why the run exists. QtTextFormat::SetLineSpacing stored all
    // three of its arguments and NOTHING READ THEM: QtTextRun positioned lines on Qt's
    // natural metrics regardless. So installQtChromeFormats could hand back a non-null
    // editorTextFormat that laid text out on the wrong grid -- issue #181's fontSize*1.5
    // row pitch silently ignored -- and every null check, every size check and every
    // pixel count above would still have passed.
    //
    // MEASURED THROUGH THE FACTORY RATHER THAN THROUGH editorTextFormat, because that
    // format is NoWrap and this is a claim about MULTIPLE LINES. Two formats identical
    // except for the SetLineSpacing call is the only pair that isolates it.
    const float spacingFontSize = 12.0f;
    const float uniformHeight = 40.0f;
    const float uniformBaseline = 30.0f;
    const float spacingWidth = 120.0f;
    std::unique_ptr<tinta::TextFormat> naturalFormat = app.textFactory->CreateTextFormat(
        app.theme.fontFamily, tinta::FontWeight::Normal, tinta::FontStyle::Normal,
        tinta::FontStretch::Normal, spacingFontSize, L"en-us");
    std::unique_ptr<tinta::TextFormat> uniformFormat = app.textFactory->CreateTextFormat(
        app.theme.fontFamily, tinta::FontWeight::Normal, tinta::FontStyle::Normal,
        tinta::FontStretch::Normal, spacingFontSize, L"en-us");
    check(naturalFormat != nullptr && uniformFormat != nullptr,
          "two identical formats were created for the line-spacing pair");
    if (naturalFormat && uniformFormat) {
        naturalFormat->SetWordWrapping(tinta::WordWrapping::Wrap);
        uniformFormat->SetWordWrapping(tinta::WordWrapping::Wrap);
        uniformFormat->SetLineSpacing(tinta::LineSpacingMethod::Uniform, uniformHeight,
                                      uniformBaseline);

        std::unique_ptr<tinta::TextLayout> naturalLayout = app.textFactory->CreateTextLayout(
            kLongRow, kLongRowLength, naturalFormat.get(), spacingWidth, 10000.0f);
        std::unique_ptr<tinta::TextLayout> uniformLayout = app.textFactory->CreateTextLayout(
            kLongRow, kLongRowLength, uniformFormat.get(), spacingWidth, 10000.0f);
        check(naturalLayout != nullptr && uniformLayout != nullptr,
              "and both laid out at a width narrow enough to wrap");

        if (naturalLayout && uniformLayout) {
            tinta::TextMetrics naturalMetrics{};
            tinta::TextMetrics uniformMetrics{};
            naturalLayout->GetMetrics(&naturalMetrics);
            uniformLayout->GetMetrics(&uniformMetrics);
            std::printf("spacing_lines=%u,%u spacing_height=%.2f,%.2f expected_uniform=%.2f\n",
                        naturalMetrics.lineCount, uniformMetrics.lineCount,
                        naturalMetrics.height, uniformMetrics.height,
                        static_cast<float>(uniformMetrics.lineCount) * uniformHeight);

            // PRECONDITION. On one line the two arrangements agree at the first line's
            // top and the height comparison would be reading a single line box, which is
            // the false green available here.
            check(uniformMetrics.lineCount >= 3,
                  "PRECONDITION: the row wrapped to at least three lines, so a per-line pitch "
                  "is measurable at all");
            check(naturalMetrics.lineCount == uniformMetrics.lineCount,
                  "the two layouts wrapped IDENTICALLY, so line spacing changed the pitch and "
                  "not the break positions");
            check(sameSize(uniformMetrics.height,
                           static_cast<float>(uniformMetrics.lineCount) * uniformHeight),
                  "AND THE UNIFORM LAYOUT IS EXACTLY lineCount * 40 TALL. This is the check "
                  "that fails before QtTextRun honours SetLineSpacing -- the format stored the "
                  "value and the run ignored it, so the height came back on Qt's natural "
                  "metrics and issue #181's row grid was silently dropped");
            check(uniformMetrics.height > naturalMetrics.height,
                  "and it is TALLER than the naturally-spaced twin, so the 40 is doing work "
                  "rather than happening to equal the font's own line height");

            // A BUFFER FOR EIGHT AND NOT FOR ONE, and the first draft of this run asked for
            // one and got `false` out of both layouts. That is the CONTRACT and not a
            // defect: GetLineMetrics returns lineCount <= maxLineCount and writes nothing
            // when the buffer is too small, which is DirectWrite's E_NOT_SUFFICIENT_BUFFER
            // rendered as a bool. src/qt_init.cpp gets away with `&lm, 1` because it
            // measures a one-character string. Recorded here because a caller that ignores
            // the return would read an untouched LineMetrics full of zeroes and compute a
            // row pitch of nothing.
            tinta::LineMetrics uniformLines[8]{};
            tinta::LineMetrics naturalLines[8]{};
            uint32_t uniformLineCount = 0;
            uint32_t naturalLineCount = 0;
            const bool gotUniform =
                uniformLayout->GetLineMetrics(uniformLines, 8, &uniformLineCount);
            const bool gotNatural =
                naturalLayout->GetLineMetrics(naturalLines, 8, &naturalLineCount);
            const tinta::LineMetrics& uniformLine = uniformLines[0];
            const tinta::LineMetrics& naturalLine = naturalLines[0];
            std::printf("line_metrics_uniform=%.2f/%.2f natural=%.2f/%.2f counts=%u,%u\n",
                        uniformLine.height, uniformLine.baseline, naturalLine.height,
                        naturalLine.baseline, uniformLineCount, naturalLineCount);
            check(gotUniform && gotNatural,
                  "GetLineMetrics answered for both layouts");
            check(uniformLineCount == uniformMetrics.lineCount &&
                      naturalLineCount == naturalMetrics.lineCount,
                  "and reported the same line count GetMetrics did, so the two readings are of "
                  "one layout and not of two different passes");
            // CONTROL for the buffer contract itself, since getting it wrong is what made
            // the first version of this run report zeroes: a buffer of one must REFUSE a
            // five-line layout rather than fill in the first line and call it success.
            tinta::LineMetrics tooSmall[1]{};
            uint32_t tooSmallCount = 0;
            check(!uniformLayout->GetLineMetrics(tooSmall, 1, &tooSmallCount) &&
                      tooSmallCount == uniformMetrics.lineCount && tooSmall[0].height == 0.0f,
                  "CONTROL: a one-entry buffer is REFUSED for a five-line layout, and the count "
                  "still comes back so the caller can size a second call -- which is what "
                  "DirectWrite's E_NOT_SUFFICIENT_BUFFER contract says and what a caller that "
                  "ignored the return would silently read as a zero-height row");
            check(sameSize(uniformLine.height, uniformHeight) &&
                      sameSize(uniformLine.baseline, uniformBaseline),
                  "GetLineMetrics reports THE CELL and not the glyph box: 40 tall with the "
                  "baseline at 30, which is what src/d2d_init.cpp's editor row arithmetic reads "
                  "back and would otherwise compute a row pitch from the font instead");
            check(!sameSize(naturalLine.height, uniformHeight),
                  "CONTROL: the naturally-spaced layout reports a DIFFERENT line height, so the "
                  "40 above was read from the format rather than being a constant this test "
                  "would have found either way");
        }
    }

    // --- Run 15: resizeEvent -------------------------------------------------------
    //
    // WHAT THIS FIXES IS THE SCROLL OFFSET, NOT THE LAYOUT, and the direction is the part
    // that had to be measured rather than reasoned about. Every maximum is
    // contentHeight - height, so a TALLER window has a SMALLER maximum -- growing the
    // window is what leaves app.scrollY past the end, and shrinking it never can. The
    // comment in src/qt_window.cpp said the opposite until this run was written.
    //
    // THE ABSENCE OF A GRAB AFTER THE RESIZE IS THE INSTRUMENT, exactly as in run 10.
    // prepareDocumentViewport clamps on every paint, so with a grab in between the
    // assertion would pass whether resizeEvent existed or not.
    //
    // Driven through sendResize rather than resize(), and that helper's comment is the
    // finding: a widget that has never been shown gets no resize event out of resize() at
    // all, not even a posted one.
    check(window.openDocument(tallUtf8), "the tall document is back for the resize run");
    // Plain resize() for the baseline, deliberately: the grab that follows syncs App's
    // viewport itself, so no event is needed to get to 400 and using one here would make
    // the flag read below ambiguous about which resize set it.
    window.resize(600, 400);
    grabWindow(window);
    sendKey(window, Qt::Key_End);
    const float bottomAt400 = app.scrollY;
    const float contentAt400 = app.contentHeight;
    std::printf("resize_baseline: h=%d content=%.1f scroll=%.2f dirty=%d\n", app.height,
                contentAt400, bottomAt400, app.layoutDirty ? 1 : 0);
    check(!app.layoutDirty,
          "BASELINE: the paint cleared layoutDirty, so the flag below can only have been set "
          "by resizeEvent");
    check(sameScroll(bottomAt400, std::max(0.0f, contentAt400 - 400.0f)),
          "BASELINE: End put the document at the 400-tall window's maximum");
    check(bottomAt400 > 0.0f,
          "PRECONDITION: that maximum is not zero, so there is a clamp to observe");

    sendResize(window, 600, 700);
    std::printf("after_grow: h=%d app_h=%d content=%.1f scroll=%.2f dirty=%d\n", window.height(),
                app.height, app.contentHeight, app.scrollY, app.layoutDirty ? 1 : 0);
    check(app.layoutDirty,
          "resizeEvent MARKED THE LAYOUT DIRTY rather than relayouting: Qt delivers one of "
          "these per frame of a window drag, so the work belongs in the paint -- which is "
          "WM_SIZE's shape, so the two stacks agree");
    check(app.height == 700,
          "and it synced App's viewport, with no paint and no key event in between");
    check(sameScroll(app.scrollY, std::max(0.0f, contentAt400 - 700.0f)),
          "AND IT RE-CLAMPED THE SCROLL OFFSET against the new height, BEFORE any paint. "
          "prepareDocumentViewport would have clamped on the next frame, so what this removes "
          "is one frame of a document showing past its own end");
    check(app.scrollY < bottomAt400,
          "which is a smaller offset than the 400-tall window's maximum -- GROWING the window "
          "shrinks the maximum, and this is the direction the original comment had backwards");

    // --- Run 16: THE SCROLLBAR, in pixels ------------------------------------------
    //
    // src/scrollbar.cpp, drawn by the Qt window for the first time. NOT a QScrollBar: the
    // approved slice asked for one and tinta turned out to already have a scrollbar --
    // ninety-five lines of custom-drawn rounded thumb stranded inside main_d2d.cpp's paint
    // handler. A platform control down one stack's right edge and this thumb down the
    // other's would have made the two builds disagree about the most visible piece of
    // chrome in the product. include/scrollbar.h carries the full argument.
    //
    // TWO PRECONDITIONS AND A CONTROL, because a strip of right-edge pixels is exactly the
    // kind of band that can light up for a reason that has nothing to do with a scrollbar.
    const float sbStripLeft = 600.0f - dpi(app, 12.0f);
    const float sbStripRight = 600.0f - dpi(app, 2.0f);
    const float trackTop = chromeTopHeight(app);
    const float trackMid = trackTop + (700.0f - trackTop) * 0.5f;
    const tinta::RectF sbTopHalf{sbStripLeft, trackTop, sbStripRight, trackMid};
    const tinta::RectF sbBottomHalf{sbStripLeft, trackMid, sbStripRight, 700.0f};

    check(sameSize(documentViewportWidth(app), 600.0f),
          "PRECONDITION: no side panel is open, so the document viewport is the whole window "
          "and the thumb is inside the strip sampled below");
    check(app.contentWidth <= documentViewportWidth(app),
          "PRECONDITION: the document does not scroll horizontally, so no horizontal bar is "
          "drawn across the bottom of the strip to be counted as the vertical thumb");

    sendKey(window, Qt::Key_Home);
    const QImage sbTopImage = grabWindow(window);
    check(app.verticalScrollbarVisible,
          "updateScrollbarExtent latched the vertical bar visible for a document taller than "
          "the window");
    // The thumb is trackHeight^2 / scrollExtent tall, so it fits inside half a track only
    // while the content is at least twice the track. ASSERTED rather than assumed: on a
    // document just barely taller than the window the thumb fills the track, both halves
    // below light up, and the position assertions would pass while measuring nothing.
    check(app.scrollbarContentHeight >= 2.0f * (700.0f - trackTop),
          "PRECONDITION: the content is at least twice the track, so the thumb occupies less "
          "than half of it and a top/bottom comparison can discriminate");

    const int sbTopAtTop = nonBackgroundPixels(sbTopImage, sbTopHalf, bg);
    const int sbBottomAtTop = nonBackgroundPixels(sbTopImage, sbBottomHalf, bg);
    std::printf("scrollbar_at_top: top_half=%d bottom_half=%d\n", sbTopAtTop, sbBottomAtTop);
    check(sbTopAtTop > 0,
          "AT THE TOP OF THE DOCUMENT THERE ARE PIXELS IN THE UPPER HALF OF THE TRACK. This is "
          "the first time anything on this stack has drawn the scrollbar, and it is the answer "
          "to 'nothing on screen says how far down you are'");
    check(sbBottomAtTop == 0,
          "and NONE in the lower half, so the count above is a thumb at a POSITION and not a "
          "band that lights up wherever it is sampled");

    sendKey(window, Qt::Key_End);
    const QImage sbBottomImage = grabWindow(window);
    const int sbTopAtBottom = nonBackgroundPixels(sbBottomImage, sbTopHalf, bg);
    const int sbBottomAtBottom = nonBackgroundPixels(sbBottomImage, sbBottomHalf, bg);
    std::printf("scrollbar_at_bottom: top_half=%d bottom_half=%d\n", sbTopAtBottom,
                sbBottomAtBottom);
    check(sbBottomAtBottom > 0 && sbTopAtBottom == 0,
          "AT THE BOTTOM THE THUMB HAS MOVED to the lower half and left the upper one empty, so "
          "it tracks app.scrollY rather than being drawn at a fixed place");
    // WITHIN A TENTH RATHER THAN EQUAL, and the tolerance is reasoned rather than tuned:
    // sbY is fractional at both ends, so a rounded rectangle antialiases differently
    // against the two subpixel offsets and the edge rows are worth a few pixels either
    // way. A thumb whose height were derived from scrollY rather than from the extent
    // would differ by the whole track, not by an edge row.
    check(sbTopAtTop > 0 && std::abs(sbBottomAtBottom - sbTopAtTop) <= sbTopAtTop / 10,
          "and it is the same SIZE at both ends to within a tenth, so the thumb kept its "
          "height while moving -- its height comes from the content extent and not from the "
          "scroll position");

    // CONTROL: a document that FITS. verticalScrollbarVisible goes false, nothing is
    // drawn, and the strip must go flat -- which is what proves the counts above measured
    // the scrollbar and not a right-hand margin, a glyph or a rounding artefact.
    check(window.openDocument(tmpUtf8), "CONTROL: the short document opens over the tall one");
    const QImage sbNoneImage = grabWindow(window);
    std::printf("control_content_height=%.1f app_height=%d visible=%d\n", app.contentHeight,
                app.height, app.verticalScrollbarVisible ? 1 : 0);
    check(app.contentHeight <= static_cast<float>(app.height),
          "PRECONDITION: it fits the window, so there is nothing to scroll");
    check(!app.verticalScrollbarVisible,
          "so updateScrollbarExtent un-latched the bar -- the latch only ever GROWS during an "
          "incremental layout and must not survive a smaller document");
    check(nonBackgroundPixels(sbNoneImage, sbTopHalf, bg) == 0 &&
              nonBackgroundPixels(sbNoneImage, sbBottomHalf, bg) == 0,
          "CONTROL: BOTH HALVES OF THE STRIP ARE FLAT BACKGROUND. Every scrollbar count above "
          "measured the scrollbar");

    // --- Run 17: MATHS, off Windows -------------------------------------------------
    //
    // WHY THIS RUN EXISTS AND WHY IT IS HERE. src/math_render.cpp measured through
    // DirectWrite and drew through Direct2D until this slice; qinta linked
    // src/math_stub.cpp in its place, whose mathParse returned null so that every
    // equation fell back to its own LaTeX source in code style. The stub is deleted and
    // the real backend is on qinta's link line -- and a successful link is NOT evidence
    // for that, because the stub linked too. Only a reading that separates "typeset" from
    // "fell back" can tell the two states apart, and this is the only target that paints
    // a Qt window and reads the pixels back.
    //
    // THE SEAM'S OWN NEW QUESTION FIRST, both directions, because mathFontFamily is the
    // one place the maths path asks the text factory something DirectWrite has no single
    // method for. A predicate that always answers true and one that always answers false
    // each pass a single-direction check. The positive direction is taken from the Qt font
    // database rather than named, because no family name is installed on every host --
    // the guard at the top of main() has already refused to run without a database.
    const QStringList families = QFontDatabase::families();
    check(!families.isEmpty(), "PRECONDITION: the Qt font database still has families in it");
    const std::wstring firstFamily = families.isEmpty() ? std::wstring()
                                                        : families.front().toStdWString();
    check(!firstFamily.empty() && app.textFactory->FontFamilyExists(firstFamily.c_str()),
          "QtTextFactory::FontFamilyExists finds a family the Qt font database lists");
    check(!app.textFactory->FontFamilyExists(L"NoSuchFamily QINTA 12345"),
          "CONTROL: and rejects one it does not, so it is not a predicate that always says yes");
    check(!app.textFactory->FontFamilyExists(nullptr) && !app.textFactory->FontFamilyExists(L""),
          "and treats no family name at all as absent rather than crashing on it");

    // The box, straight off mathParse, which is the whole ported measurement path:
    // CreateTextFormat, CreateTextLayout, ApplyFontFallback and GetMetrics through
    // app.textFactory -- a Qt one here.
    const MathBoxPtr mathBox = mathParse(app, L"\\frac{1}{2}", 24, true);
    check(mathBox != nullptr,
          "mathParse laid out an equation with a Qt text factory seated, which src/math_stub.cpp "
          "could not do by construction: its mathParse returned null");
    check(mathBox && std::isfinite(mathBoxWidth(mathBox)) && mathBoxWidth(mathBox) > 0 &&
              mathBoxHeight(mathBox) > 0,
          "and the box has finite positive extents, so the Qt measurement produced NUMBERS and not "
          "a zero-sized box that would draw nothing and still be non-null");
    check(mathBox && mathBoxBaseline(mathBox) > 0 &&
              mathBoxBaseline(mathBox) < mathBoxHeight(mathBox),
          "with its baseline strictly inside it, which is what inline alignment is derived from");
    check(mathParse(app, L"\\begin{bmatrix}1&2\\end{matrix}", 24, true) == nullptr,
          "CONTROL: malformed TeX still returns null, so the check above is a layout result and not "
          "a function that returns a box for anything");

    // Now the same two through the WINDOW, which is what a reader means by "qinta typesets
    // maths". Through openDocument rather than by assigning App::root, because that is the
    // path both main() and Ctrl+O take AND because it is what invalidates the cached
    // layout -- run 4's comment has the measurement behind that.
    const std::filesystem::path mathPath =
        std::filesystem::temp_directory_path() / "qinta_entry_math.md";
    const std::filesystem::path mathFallbackPath =
        std::filesystem::temp_directory_path() / "qinta_entry_math_fallback.md";
    { std::ofstream out(mathPath, std::ios::binary); out << kMathMarkdown; }
    { std::ofstream out(mathFallbackPath, std::ios::binary); out << kMathFallbackMarkdown; }
    const std::string mathUtf8 = mathPath.u8string();
    const std::string mathFallbackUtf8 = mathFallbackPath.u8string();

    // The whole widget, not the top quarter. The fixture is one heading and one equation,
    // so nothing else can put ink anywhere in it, and a band would add the question of
    // whether the equation happened to land inside it.
    const tinta::RectF wholeWindow{0.0f, 0.0f, static_cast<float>(window.width()),
                                   static_cast<float>(window.height())};

    check(window.openDocument(mathUtf8), "the equation document opens");
    const QImage mathImage = grabWindow(window);
    const size_t mathLines = app.layoutLines.size();
    const size_t mathRects = app.layoutRects.size();
    const size_t mathRuns = app.layoutTextRuns.size();
    const int mathInk = nonBackgroundPixels(mathImage, wholeWindow, bg);
    std::printf("math_lines=%zu math_rects=%zu math_runs=%zu math_ink=%d\n", mathLines, mathRects,
                mathRuns, mathInk);
    check(window.documentPainted(), "and the window took the document branch for it");
    check(mathRuns > 0, "the layout produced text runs -- the matrix cells");
    check(mathInk > 0, "and the window has ink in it");
    check(mathLines > 0,
          "AND IT DREW THE BRACKET STROKES: mathBoxRetain put lines in App::layoutLines, and the "
          "$$ block is the ONLY thing in this fixture, so those strokes are a laid-out matrix and "
          "not LaTeX source in a code block");
    if (mathLines > 0) {
        const App::LayoutLine& stroke = app.layoutLines.front();
        std::printf("stroke_p1=(%.1f,%.1f) stroke_p2=(%.1f,%.1f)\n", stroke.p1.x, stroke.p1.y,
                    stroke.p2.x, stroke.p2.y);
        check(std::abs(stroke.p2.x - stroke.p1.x) < static_cast<float>(window.width()) / 4.0f,
              "and the first stroke is NARROW rather than window-wide, which is what caught the "
              "first version of this run measuring an H1 underline");
    }

    // THE CONTROL, and it is the one that makes the run mean anything: the same document
    // with the same delimiters and one token changed, so mathParse returns null.
    // src/render.cpp's layoutMathBlock takes its fallback path and draws the source in code
    // style -- which draws a background RECT and no lines at all. If the count above were
    // coming from anything but the equation, it would come back non-zero here too.
    check(window.openDocument(mathFallbackUtf8), "CONTROL: the malformed-TeX document opens");
    const QImage fallbackImage = grabWindow(window);
    const size_t fallbackLines = app.layoutLines.size();
    const size_t fallbackRects = app.layoutRects.size();
    const int fallbackInk = nonBackgroundPixels(fallbackImage, wholeWindow, bg);
    std::printf("fallback_lines=%zu fallback_rects=%zu fallback_runs=%zu fallback_ink=%d\n",
                fallbackLines, fallbackRects, app.layoutTextRuns.size(), fallbackInk);
    check(window.documentPainted(), "CONTROL: it is still a painted document, not a placeholder");
    check(!app.layoutTextRuns.empty() && fallbackInk > 0,
          "CONTROL: and it still draws text, so the difference below is about the maths path and "
          "not about one of the two documents failing to render at all");
    check(fallbackLines == 0,
          "CONTROL: BUT IT DREW NO STROKES. The fallback is a code block, so every line in the run "
          "above came from the equation");
    check(fallbackRects > mathRects,
          "CONTROL: and it drew MORE rects than the equation did -- the code-block background the "
          "fallback path builds, which is the primitive the typeset path does not produce");
    check(mathImage != fallbackImage,
          "and the two windows are not the same image, pixel for pixel -- which is the difference "
          "between typesetting an equation and printing its source");

    // The cache holds Qt text layouts, and releaseQtBackends below takes the factory that
    // made them out of App. Cleared here for the same reason
    // tests/math_layout_tests.cpp:265 clears it before CoUninitialize.
    mathClearCache();
    app.clearLayoutCache();

    std::error_code rmEc;
    std::filesystem::remove(tmp, rmEc);
    std::filesystem::remove(emptyPath, rmEc);
    std::filesystem::remove(tallPath, rmEc);
    std::filesystem::remove(mathPath, rmEc);
    std::filesystem::remove(mathFallbackPath, rmEc);

    // Idempotent by construction -- install(nullptr) on an empty storage -- and called
    // here anyway so the file ends in the state every earlier run ended in.
    releaseQtBackends(app);

    std::printf("checks=%d failures=%d\n", g_checks, g_failures);
    if (g_failures == 0)
        std::printf("PASS: a Qt window lays out and draws a parsed document through "
                    "render.cpp and paint_document.cpp, off the Windows path, and TYPESETS an "
                    "equation through math_render.cpp rather than falling back to its source.\n");
    return g_failures == 0 ? 0 : 1;
}
