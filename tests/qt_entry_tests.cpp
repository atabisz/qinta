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
//   9. THE DASHED STROKE STYLE (run 18), and it is the one run here that exists for a
//      DEFECT rather than for a seam. Commit fc9ceb0 left App::dashedStrokeStyle null
//      forever on Windows -- the create wrote one member and every reader read another --
//      so every dashed mermaid connector drew solid for several commits and the create-once
//      latch turned it into a per-frame COM leak. Nothing in the tree could see it. This run
//      calls the real ensureDashedStrokeStyle out of src/paint_document.cpp and asserts the
//      member afterwards, and THIS TARGET IS THE ONLY ONE THAT CAN: every other target
//      compiling that file inherits TINTA_D2D_BACKEND from `tinta`, which puts the
//      CreateStrokeStyle arm in the build and dereferences a null app.d2dFactory.
//      No pixels -- the style has no on-screen effect without a mermaid fixture, and a
//      fixture would make this a rendering assertion instead of an ownership one.
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
#include <QContextMenuEvent>
#include <QCursor>
#include <QEventLoop>
#include <QFontDatabase>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QResizeEvent>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
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
// For ensureDashedStrokeStyle, which run 18 calls directly. This target already LINKED
// src/paint_document.cpp before this include existed -- the file is in QINTA_APP_SOURCES
// (CMakeLists.txt:674 -- and :410 is the OTHER list, `SOURCES`, which is the one that
// carries TINTA_D2D_BACKEND) because the document runs above paint through it, so the only
// thing missing was a declaration, not a build change. That mattered for choosing where
// run 18 lives: every OTHER target that compiles paint_document.cpp inherits
// TINTA_D2D_BACKEND from `tinta`, which puts the CreateStrokeStyle arm in the build and
// makes the call dereference a null app.d2dFactory. This target is the only one that
// compiles that function with its portable arm live.
#include "input.h"
#include "keymap.h"
#include "paint_document.h"
#include "qt_init.h"
#include "qt_key_map.h"
// Run 19's whole point: the four functions src/main_qt.cpp calls to place and colour its
// window, reached from here so the test drives the SAME code rather than a copy of it. This
// header also pulls launch_options.h, settings_data.h and window_placement.h, which is why
// run 19 adds one include and not four.
#include "qt_launch.h"
#include "qt_window.h"
#include "repaint_sink.h"
#include "settings.h"
#include "startpage.h"
#include "text_api.h"
// The chrome formats' own header, and the reason this file needs it now is the port: the
// bodies run 11 asserts on moved out of src/qt_init.cpp into src/text_formats_app.cpp, so
// installQtBackends' call resolves through here. qt_init.h still declares the Qt-named
// wrappers, which is why every existing assertion below compiles unchanged.
#include "text_formats.h"
#include "theme_apply.h"
#include "theme_data.h"
// ISC-17's Win32 half, in its own TU so <windows.h>'s macro set never reaches this file. Compiled
// into this target only (CMakeLists.txt), and it compiles to two stubs off Windows so run 20's call
// sites need no guard of their own beyond the one assertion that IS platform-specific.
#include "win32_monitor_dump.h"
#include "window_sink.h"

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

// A mouse event through the event system, for the same reason sendWheel gives. `held` is
// the button state AFTER the event, which is what Qt reports: a press holds its own button,
// a release holds nothing, a hover move holds nothing.
void sendMouse(QtAppWindow& window, QEvent::Type type, QPointF local,
               Qt::MouseButton button = Qt::NoButton, Qt::MouseButtons held = Qt::NoButton,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent ev(type, local, QPointF(window.mapToGlobal(local.toPoint())), button, held,
                   modifiers);
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

    // THE KEYMAP, WHICH src/main_qt.cpp APPLIES AND WHICH THIS FILE HAS TO APPLY TOO. The key
    // router sends every key through handleKeyDown, and handleKeyDown sends it through
    // translateActionKey before its switch. Found by the J check in run 8 failing on the
    // router's first run: App::keymap is zero-initialised, and an unbound map translates every
    // default action key to 0 -- J, K, T, Q and F11 all fall through, while Down, which names
    // no action, still works. K "passed" in the same run only because the scroll was already 0.
    // So the control is asserted here, where the reason is one line away, and a default
    // Settings is applied -- the "windows" profile, which is what a first launch gets.
    check(translateActionKey(app.keymap, 'J', false) == 0,
          "CONTROL: an unbound keymap translates J to 0, which is why the next line exists");
    applyKeymap(app, Settings{});
    check(translateActionKey(app.keymap, 'J', false) == 'J' &&
              translateActionKey(app.keymap, VK_F11, false) == VK_F11,
          "applyKeymap bound the default profile: J and F11 now reach handleKeyDown's switch");

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

    // ISC-3, FIRST ENDPOINT. Constructing the window must have installed the repaint
    // sink, because off Windows that is the only thing standing between the 277
    // requestRepaint sites under src/ and QWidget::update(). This half is a property of
    // the constructor and is asserted here, where a window has just been built; the
    // SECOND endpoint -- that a request actually arrives as a paint -- is a different
    // claim and is measured at the end of this file, deliberately not by re-reading
    // this one. A window that installed a sink pointing at nothing would pass here.
    check(tinta::repaintSinkInstalled(),
          "constructing QtAppWindow installed the repaint sink, so requestRepaint has somewhere to go");
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
          "src/input.cpp handleMouseWheel's number reached through Qt's eighths-of-a-degree "
          "units");
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
          "wheel's 60, from handleKeyDown's VK_DOWN case");
    sendKey(window, Qt::Key_Up);
    check(sameScroll(app.scrollY, 0.0f), "and the Up arrow steps back by the same amount");
    sendKey(window, Qt::Key_J);
    check(sameScroll(app.scrollY, lineStep),
          "J scrolls down like the Down arrow -- the vi bindings share handleKeyDown's "
          "arrow cases, not "
          "an addition made in the port");
    sendKey(window, Qt::Key_K);
    check(sameScroll(app.scrollY, 0.0f), "and K scrolls up");
    sendKey(window, Qt::Key_PageDown);
    std::printf("scroll_after_page_down=%.2f expected=%.2f\n", app.scrollY, pageStep);
    check(sameScroll(app.scrollY, pageStep),
          "Page Down moves 80% of the WINDOW height, so a page is a screenful and not a step");
    sendKey(window, Qt::Key_Space);
    check(sameScroll(app.scrollY, std::min(2.0f * pageStep, maxScrollY)),
          "Space is a second Page Down, which is handleKeyDown's VK_SPACE binding");
    sendKey(window, Qt::Key_PageUp);
    check(sameScroll(app.scrollY, std::min(2.0f * pageStep, maxScrollY) - pageStep),
          "and Page Up moves one page back");
    sendKey(window, Qt::Key_End);
    check(sameScroll(app.scrollY, maxScrollY),
          "End jumps to the bottom, through ensureLayoutComplete -- the only scroll target that "
          "cannot be computed from the layout already in hand");
    sendKey(window, Qt::Key_Home);
    check(sameScroll(app.scrollY, 0.0f), "and Home comes back to the top");

    // THE HORIZONTAL AXIS, AND THE DIVERGENCE THE KEY ROUTER RETIRED. This block used to
    // assert the opposite: the Qt window's own key switch clamped the Right arrow through
    // src/scroll.cpp, and the check was written to fail on Windows' arithmetic on purpose.
    // The router hands Right to handleKeyDown, whose VK_RIGHT does `targetScrollX += dpi(60)`
    // with no upper clamp and leaves src/paint_document.cpp's prepareDocumentViewport to clamp
    // the visible offset -- so both stacks now do the same thing, and the check says what that
    // is. Changing input.cpp to clamp would have kept the old assertion green, and it would
    // have changed tinta, which this slice does not. The tall fixture wraps to the window, so
    // the maximum is ZERO and the two halves separate cleanly: the arithmetic moves by 60, the
    // paint brings it back.
    const float maxScrollX = std::max(0.0f, app.contentWidth - documentViewportWidth(app));
    sendKey(window, Qt::Key_Right);
    std::printf("content_width=%.1f viewport_width=%.1f max_scroll_x=%.2f scroll_x=%.2f\n",
                app.contentWidth, documentViewportWidth(app), maxScrollX, app.scrollX);
    check(sameScroll(app.targetScrollX, 60.0f * scale),
          "the Right arrow takes handleKeyDown's arithmetic -- targetScrollX grows by 60 with no "
          "upper clamp, which is src/input.cpp's VK_RIGHT and now this stack's too");
    grabWindow(window);
    check(sameScroll(app.scrollX, maxScrollX),
          "and the PAINT clamps the visible offset against the content width, which is the half "
          "of the Windows behaviour that keeps an over-long target from showing");
    sendKey(window, Qt::Key_Left);
    check(sameScroll(app.scrollX, 0.0f), "and Left clamps at zero");

    // Ctrl+J, RESTORED. Slice 2 dropped this control on the belief that a sent event reached
    // handleKeyDown with Ctrl reported up. Half true: Qt's cache DOES follow a sent key event
    // (include/keyboard.h has the probe), so on Linux this passed even before the modifier
    // scope existed and is no reading of it there. On Windows handleKeyDown asks GetKeyState,
    // which no sent event reaches, so there the scope is what lets Ctrl arrive. Either way
    // handleKeyDown takes its `if (ctrl)` switch, which has no 'J': plain J scrolled 50 above,
    // this one must not.
    // Ctrl+O is still NOT sent from here: promptOpenDocument blocks in QFileDialog::exec until
    // a human clicks, so sending it would hang the suite rather than fail it.
    sendKey(window, Qt::Key_J, Qt::ControlModifier);
    check(sameScroll(app.scrollY, 0.0f),
          "CONTROL: Ctrl+J does not scroll -- Ctrl reached handleKeyDown through the event's "
          "modifiers, which a sent event could not do before the modifier scope");

    // Ctrl+wheel ZOOMS, as of slice 5 -- this check asserted the opposite until the timer
    // sink existed. applyZoomDelta applies text formats on the leading notch, sets
    // zoomApplyPending and arms TIMER_ZOOM_APPLY; the tick after that finds nothing new and
    // clears the flag. So the claim has three readings in order: armed, then a real event
    // loop running past two 80 ms intervals, then disarmed with the flag down. The first
    // reading is what makes the last one mean anything -- a flag that was never set, or a
    // timer that was never armed, would pass "the flag is clear" without a single tick.
    const float zoomBefore = app.zoomFactor;
    sendWheel(window, -1.0f, Qt::ControlModifier);
    std::printf("ctrl_wheel zoom=%.3f->%.3f pending=%d timer_active=%d\n", zoomBefore,
                app.zoomFactor, app.zoomApplyPending ? 1 : 0,
                window.timerActive(TIMER_ZOOM_APPLY) ? 1 : 0);
    check(sameScroll(app.scrollY, 0.0f) && app.zoomFactor < zoomBefore &&
              app.appliedZoomFactor == app.zoomFactor,
          "Ctrl+wheel down zooms OUT through handleMouseWheel -> applyZoomDelta, and the leading "
          "notch bakes the new factor into the text formats; it does not scroll");
    check(app.zoomApplyPending && window.timerActive(TIMER_ZOOM_APPLY),
          "and it leaves zoomApplyPending set with TIMER_ZOOM_APPLY armed in the window's timer "
          "sink -- armTimer reached a QTimer instead of a no-op");
    {
        QEventLoop loop;
        QTimer::singleShot(400, &loop, &QEventLoop::quit);
        loop.exec();
    }
    std::printf("ctrl_wheel after_loop pending=%d timer_active=%d\n",
                app.zoomApplyPending ? 1 : 0, window.timerActive(TIMER_ZOOM_APPLY) ? 1 : 0);
    check(!app.zoomApplyPending && !window.timerActive(TIMER_ZOOM_APPLY),
          "after the event loop runs 400 ms the tick has cleared zoomApplyPending and disarmed "
          "the timer -- input.cpp's handleTimer ran on this stack, which is WM_TIMER's dispatch");
    // Back to the fixture's zoom, so every check below measures the layout it was written
    // against. updateTextFormats is what the timer would have called.
    app.zoomFactor = zoomBefore;
    updateTextFormats(app);
    app.layoutDirty = true;

    // The zoom half of this check is what makes it a reading of the modifier scope. Without
    // the scope, the wheel reads Qt's cache, which still holds the Ctrl the Ctrl+J above left
    // there -- a sent wheel event does not refresh it -- so handleMouseWheel takes its ZOOM
    // branch. Zooming at scrollY 0 leaves scrollY at 0, so the scrollY half alone passed
    // that plant; the zoomFactor half is the one it fails.
    sendWheel(window, -1.0f, Qt::ShiftModifier);
    check(sameScroll(app.scrollY, 0.0f) && app.zoomFactor == zoomBefore,
          "Shift+wheel neither scrolls VERTICALLY nor zooms: it is the horizontal gesture, "
          "from src/input.cpp handleMouseWheel's Shift branch");
    check(sameScroll(app.scrollX, std::min(60.0f * scale, maxScrollX)),
          "it moves the horizontal offset instead -- zero here, because the content wraps to "
          "the window, which is the same clamp the Right arrow hit above");
    sendKey(window, Qt::Key_Home);

    // The wheel is routed, so an open overlay takes it before the document does: help is
    // handleMouseWheel's `helpScroll -= delta * dpi(60)` branch, which a local scroll in the
    // window could never reach.
    app.showHelp = true;
    app.helpScroll = 0.0f;
    sendWheel(window, -1.0f);
    std::printf("help_wheel helpScroll=%.2f scrollY=%.2f\n", app.helpScroll, app.scrollY);
    check(sameScroll(app.helpScroll, wheelStep) && sameScroll(app.scrollY, 0.0f),
          "a wheel notch over the help overlay scrolls the overlay by 60 and leaves the document "
          "where it was");
    app.showHelp = false;
    app.helpScroll = 0.0f;

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
    // ALL TEN, not just folderBrowserFormat -- see the discriminator note below.
    tinta::TextFormat* const beforeSecond[10] = {
        app.searchTextFormat, app.signalSmallFormat, app.themeTitleFormat,
        app.themeHeaderFormat, app.statsFormat, app.folderBrowserFormat,
        app.tocFormatBold, app.tocFormat, app.editorTextFormat, app.editorGutterFormat};
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
    //
    // ACROSS ALL TEN, not one. A single chosen pointer (folderBrowserFormat) is not a
    // reliable discriminator on its own: measured on macOS, comparing that one pointer
    // alone flips between pass and fail across otherwise-identical runs (2 passes, then
    // 3 fails, across 5 consecutive `ctest -R qt_entry` invocations, no code changed
    // between them) -- the allocator sometimes hands the just-freed same-size block
    // straight back to the very next same-size request, which a single-object identity
    // check cannot tell apart from "never freed at all". Requiring instead that AT LEAST
    // ONE of the ten independently-allocated objects moved keeps the property this check
    // exists for (a total no-op, the lazy-door case, changes NONE of them) while making a
    // false failure need all ten allocations to collide with their prior addresses at
    // once -- not observed once in the runs above, where every one of the ten moved.
    int unchanged = 0;
    tinta::TextFormat* const afterSecond[10] = {
        app.searchTextFormat, app.signalSmallFormat, app.themeTitleFormat,
        app.themeHeaderFormat, app.statsFormat, app.folderBrowserFormat,
        app.tocFormatBold, app.tocFormat, app.editorTextFormat, app.editorGutterFormat};
    for (int i = 0; i < 10; i++) {
        if (afterSecond[i] == beforeSecond[i]) unchanged++;
    }
    std::printf("idem_unchanged=%d/10\n", unchanged);
    check(unchanged < 10,
          "and at least one of the ten formats is a NEW object -- idempotent in STATE, not "
          "by early return, which is what a caller on the theme-change path needs");

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

    // ---- RUN 18: THE DASHED STROKE STYLE, AND THE fc9ceb0 FAILURE MODE ----------------
    //
    // WHAT THIS IS FOR. Commit fc9ceb0 left App::dashedStrokeStyle null forever on
    // Windows: ensureDashedStrokeStyle's Direct2D arm wrote the native handle into one
    // member and every reader read a DIFFERENT one. Both spellings compiled. Every dashed
    // mermaid connector drew solid for several commits, and because the create-once latch
    // is `if (app.dashedStrokeStyle) return;`, the create re-ran every frame and leaked the
    // previous COM pointer. Nothing in the tree caught it, and the reason nothing did is
    // that the only caller was a mermaid connector inside a Win32 WM_PAINT -- so the fix
    // that retired the member trio is worth exactly as much as the instrument that can see
    // it come back.
    //
    // THIS CALLS THE REAL FUNCTION. Not a copy of the property block, not a local
    // MarkerStrokeStyle: src/paint_document.cpp's own ensureDashedStrokeStyle, linked out
    // of this target's QINTA_APP_SOURCES. A test holding its own copy of the creation
    // cannot fail on a defect planted in the real one.
    //
    // WHAT IT CAN AND CANNOT SEE. This target compiles the #else arm, so the assertion
    // below is "the portable arm installs a handle", not "CreateStrokeStyle's HRESULT is
    // checked". The two arms now end in the SAME statement -- assign to
    // app.dashedStrokeStyle -- which is why an arm-specific test still constrains the
    // shared shape: the failure fc9ceb0 was is a missing assignment, and a missing
    // assignment is what this measures. The Direct2D arm's own failure path needs a
    // d2dFactory and stays uncovered; said here rather than left implied.
    //
    // The document runs above never touched it: no fixture in this file contains a mermaid
    // diagram, so paintDocumentLayout's call at src/paint_document.cpp:216 never ran. That
    // is asserted rather than assumed, because if it HAD run the "starts null" precondition
    // below would be doing no work and the run would still read green.
    check(app.dashedStrokeStyle == nullptr,
          "PRECONDITION: nothing in this file's fixtures has a dashed connector, so the style is "
          "still null after seventeen runs of painting");

    ensureDashedStrokeStyle(app);
    check(app.dashedStrokeStyle != nullptr,
          "THE fc9ceb0 ASSERTION: ensureDashedStrokeStyle INSTALLED a handle. This is the exact "
          "state that was wrong for several commits -- the function ran, reported nothing, and "
          "left the member null, so every dashed line drew solid");

    // THE LATCH, which is the other half of what fc9ceb0 cost. A second call must not
    // build a second object: on the Direct2D arm that is a per-frame COM leak, and the
    // pointer identity is the only thing that can tell "returned early" from "created
    // again and got a coincidentally equal address" -- on the portable arm
    // MarkerStrokeStyle has static storage duration, so a re-create WOULD return the same
    // address. So the identity check is backed by the reset control below rather than
    // standing alone.
    const tinta::StrokeStyle* firstStyle = app.dashedStrokeStyle.get();
    ensureDashedStrokeStyle(app);
    check(app.dashedStrokeStyle.get() == firstStyle,
          "and a second call returned early rather than creating a second one -- the create-once "
          "latch, which is what turned the missing install into a per-frame leak");

    // THE CONTROL, and without it the assertion above is not discriminating: a member that
    // were non-null for any reason other than this call would satisfy it. Clear it, prove
    // the same expression reads null, then prove the function fills it again. A green that
    // cannot go red is not a reading.
    app.dashedStrokeStyle.reset();
    check(app.dashedStrokeStyle == nullptr,
          "CONTROL: the member reads null once reset, so the assertion above can come back red");
    ensureDashedStrokeStyle(app);
    check(app.dashedStrokeStyle != nullptr,
          "CONTROL: and the function fills it again after a reset, which is the shape App::shutdown "
          "leaves behind on a device loss");

    // Left empty for the same reason every other run cleans up after itself: the next
    // assertion in this file should not depend on a state this one created.
    app.dashedStrokeStyle.reset();

    // ---- RUN 19: WHERE THE WINDOW GOES, AND WHAT COLOUR IT IS -------------------------
    //
    // The launch options reach a real QtAppWindow, or they do not. Everything the slice
    // measured before this point was the PARSE -- 37 assertions in
    // tests/launch_options_tests.cpp that `--pos 1920 -300` yields hasPos/posX/posY -- and a
    // parse that is right about its fields says nothing about whether a window moved. Those
    // are two claims and only the second one is the feature.
    //
    // THE CHAIN UNDER TEST IS THE REAL ONE, every link:
    //   tinta::parseLaunchOptions -> tinta::placementRequestFrom -> tinta::choosePlacement
    //   -> tinta::applyPlacement -> QWidget's own geometry
    // Not one of those is re-implemented here. src/main_qt.cpp calls the same four in the same
    // order, and the only thing left uncovered in main() is which arguments it passes them --
    // which is why applyPlacement and applyLightMode were pulled out of it at all. A test
    // holding its own `resize(); if (!sentinel) move();` could not fail on a defect planted in
    // main()'s copy, and this tree has paid for that shape before.
    //
    // WHY NO ORDINARY show(). On Windows this target runs with QT_QPA_PLATFORM=windows -- the
    // REAL platform, not offscreen (CMakeLists.txt sets it that way deliberately) -- so a bare
    // show() would flash three windows onto the desktop of whoever ran ctest.
    // Qt::WA_DontShowOnScreen creates and lays out the widget through the same code path and
    // never maps it. That has a LIMIT and it is stated rather than implied: what is asserted
    // below is that applyPlacement reached the widget's own geometry, NOT that a window manager
    // honoured the coordinate. The second claim needs a visible window and a screen capture,
    // and it is not made here.
    {
        const std::vector<tinta::MonitorArea> screens = tinta::monitorsFromQt();
        std::printf("qt_screens=%d\n", static_cast<int>(screens.size()));
        check(!screens.empty(),
              "PRECONDITION: Qt reported at least one screen, so choosePlacement below is "
              "clamping against a real work area rather than taking its no-monitors branch");

        // ---- ISC-10: --pos X Y moves the window -----------------------------------
        //
        // --pos is the arm that CANNOT be clamped -- src/window_placement.cpp returns it
        // outright, deliberately, because a dropped tab's coordinate came from a live cursor.
        // So the expected geometry here is the literal argument, and that is the one case in
        // this run where a hardcoded number is the right assertion rather than a fragile one.
        const tinta::LaunchOptions posOptions =
            tinta::parseLaunchOptions({"--pos", "260", "140", "doc.md"});
        Settings posSaved;  // savedX/savedY keep the sentinel, so ONLY --pos can produce a
                            // position here -- if the plumbing dropped the flag the window
                            // would not be moved at all rather than moved to the wrong place.
        posSaved.windowWidth = 500;
        posSaved.windowHeight = 320;

        const tinta::PlacementRequest posRequest = tinta::placementRequestFrom(posOptions, posSaved);
        // The field-copy assertion, and it is the one a swapped posX/posY dies on. 260 and 140
        // are deliberately UNEQUAL and deliberately not a round pair: with 100/100 a transposed
        // copy is invisible.
        check(posRequest.hasPos && posRequest.posX == 260 && posRequest.posY == 140,
              "placementRequestFrom carried --pos across as hasPos/posX=260/posY=140 -- the nine "
              "field copies, on the axis where a transposition still compiles and still runs");
        check(posRequest.savedX == kWindowPositionUseDefault &&
                  posRequest.savedWidth == 500 && posRequest.savedHeight == 320,
              "and it carried Settings' four geometry fields, sentinel included and unresolved");

        const tinta::WindowRect posRect = choosePlacement(posRequest, screens);
        std::printf("pos_rect=%d,%d %dx%d\n", posRect.x, posRect.y, posRect.width, posRect.height);

        QtAppWindow posWindow(app);
        posWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        tinta::applyPlacement(posWindow, posRect);
        posWindow.show();
        // pos() and not geometry().topLeft(): move() sets the FRAME position and pos() is the
        // frame's, so on a platform that decorates windows the two differ by the frame margin
        // and only one of them is the counterpart of the call under test. Both are printed,
        // because a divergence between them is worth seeing rather than hiding behind a pass.
        std::printf("pos_window pos=%d,%d geom=%d,%d %dx%d\n", posWindow.pos().x(),
                    posWindow.pos().y(), posWindow.geometry().x(), posWindow.geometry().y(),
                    posWindow.geometry().width(), posWindow.geometry().height());
        check(posWindow.pos().x() == 260 && posWindow.pos().y() == 140,
              "ISC-10: `--pos 260 140` put the WINDOW at 260,140 -- read off the widget after "
              "show(), not off the parse");
        check(posWindow.width() == 500 && posWindow.height() == 320,
              "and the size came from Settings rather than from a hardcoded default, so the "
              "1000x700 that used to live in main() is really gone");

        // CONTROL: the same window, the same Settings, no --pos. Without this the assertion
        // above cannot distinguish "the flag moved it" from "it happened to land there" -- and
        // with savedX at the sentinel the honest answer is that applyPlacement must not call
        // move() AT ALL, which is the sentinel branch nothing else in the tree exercises.
        const tinta::WindowRect plainRect = choosePlacement(
            tinta::placementRequestFrom(tinta::parseLaunchOptions({"doc.md"}), posSaved), screens);
        check(plainRect.x == kWindowPositionUseDefault && plainRect.y == kWindowPositionUseDefault,
              "CONTROL: with no --pos and nothing saved, choosePlacement returns the sentinel "
              "rather than inventing a coordinate");
        QtAppWindow plainWindow(app);
        plainWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        tinta::applyPlacement(plainWindow, plainRect);
        plainWindow.show();
        std::printf("plain_window pos=%d,%d %dx%d\n", plainWindow.pos().x(), plainWindow.pos().y(),
                    plainWindow.width(), plainWindow.height());
        check(plainWindow.pos().x() != 260 || plainWindow.pos().y() != 140,
              "CONTROL: and that window is NOT at 260,140, so the reading above measures the "
              "flag and not the default");
        check(plainWindow.width() == 500 && plainWindow.height() == 320,
              "CONTROL: the sentinel suppressed move() and NOT resize() -- the size is still "
              "applied, which is the half of applyPlacement that runs unconditionally");

        // ---- ISC-11: --cascade steps by exactly kCascadeStep ------------------------
        //
        // Two runs differing by one flag, and the difference must be kCascadeStep in BOTH axes.
        // Asserted against the constant rather than against 40: a literal here would keep
        // passing if src/window_placement.cpp changed the step, which is the one change this
        // assertion exists to notice.
        //
        // The saved position is derived from Qt's OWN first work area rather than hardcoded,
        // because a saved rectangle off every display takes choosePlacement's nearest-monitor
        // clamp instead of the cascade path, and this host's displays start at negative Y. The
        // margin precondition is asserted, not assumed: near the right or bottom edge the clamp
        // would EAT the 40 and the failure would read as a broken step.
        const tinta::WindowRect& work = screens[0].work;
        std::printf("work0=%d,%d %dx%d cascade_step=%d\n", work.x, work.y, work.width, work.height,
                    tinta::kCascadeStep);
        Settings cascadeSaved;
        cascadeSaved.windowWidth = 400;
        cascadeSaved.windowHeight = 300;
        cascadeSaved.windowX = work.x + 10;
        cascadeSaved.windowY = work.y + 10;
        check(work.width >= 400 + 10 + tinta::kCascadeStep &&
                  work.height >= 300 + 10 + tinta::kCascadeStep,
              "PRECONDITION: the first work area has room for the stepped window, so a clamp "
              "cannot eat the step and be misread as a missing one");

        const tinta::WindowRect noCascade = choosePlacement(
            tinta::placementRequestFrom(tinta::parseLaunchOptions({"doc.md"}), cascadeSaved),
            screens);
        const tinta::WindowRect cascaded = choosePlacement(
            tinta::placementRequestFrom(tinta::parseLaunchOptions({"--cascade", "doc.md"}),
                                        cascadeSaved),
            screens);
        // THE CONTROL COMES FIRST here, because it is a precondition for the subtraction below
        // meaning anything: if the no-flag run were already offset from the saved position, a
        // difference of 40 could be two clamps rather than one step.
        check(noCascade.x == cascadeSaved.windowX && noCascade.y == cascadeSaved.windowY,
              "CONTROL: the no-flag run sits exactly on the saved position and is not already "
              "offset, so a difference below is the cascade and not a clamp");

        QtAppWindow noCascadeWindow(app);
        noCascadeWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        tinta::applyPlacement(noCascadeWindow, noCascade);
        noCascadeWindow.show();
        QtAppWindow cascadeWindow(app);
        cascadeWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        tinta::applyPlacement(cascadeWindow, cascaded);
        cascadeWindow.show();
        const int dx = cascadeWindow.pos().x() - noCascadeWindow.pos().x();
        const int dy = cascadeWindow.pos().y() - noCascadeWindow.pos().y();
        std::printf("cascade_windows no=%d,%d yes=%d,%d delta=%d,%d\n", noCascadeWindow.pos().x(),
                    noCascadeWindow.pos().y(), cascadeWindow.pos().x(), cascadeWindow.pos().y(), dx,
                    dy);
        check(dx == tinta::kCascadeStep && dy == tinta::kCascadeStep,
              "ISC-11: two WINDOWS differing only by --cascade sit exactly kCascadeStep apart in "
              "both axes -- so a spawned window cannot land on top of the one that spawned it");
        check(dx == dy,
              "and the step is the same in both axes, which is what a one-axis edit to the "
              "cascade block would break while still passing a single-axis assertion");

        // ---- ISC-12: --light changes the pixels ------------------------------------
        //
        // The claim is not "currentThemeIndex became 0" -- that is a field, and a field nothing
        // paints with is not a feature. The claim is that the window DRAWS differently, so this
        // grabs the same widget twice across the one call and compares a corner pixel.
        const int savedThemeIndex = app.currentThemeIndex;
        const D2DTheme savedThemeValue = app.theme;
        const bool savedDarkMode = app.darkMode;

        // A DARK BASELINE, found by asking the theme table rather than by naming an index. With
        // app.theme already light the comparison below would be vacuous and would still read
        // green, and which slot is dark is exactly the sort of thing a reordered table changes.
        int darkIndex = -1;
        for (int i = 0; i < THEME_COUNT; i++) {
            if (themeAt(i).isDark) {
                darkIndex = i;
                break;
            }
        }
        std::printf("dark_index=%d light_index=%d theme_count=%d\n", darkIndex,
                    kLightModeThemeIndex, THEME_COUNT);
        check(darkIndex >= 0 && darkIndex != kLightModeThemeIndex,
              "PRECONDITION: the theme table holds a dark theme that is NOT the --light slot, so "
              "the two grabs below can actually differ");

        app.currentThemeIndex = darkIndex;
        app.theme = themeAt(darkIndex);
        app.darkMode = app.theme.isDark;
        const tinta::Color darkBackground = app.theme.background;

        QtAppWindow themeWindow(app);
        themeWindow.resize(200, 150);
        const QImage darkGrab = grabWindow(themeWindow);

        // CONTROL FIRST, again deliberately: two grabs at the SAME setting must be identical.
        // Without it, "the images differ" could be grab noise -- and this tree has a window
        // capture that hashes differently on an identical build, so the worry is measured
        // rather than theoretical.
        const QImage darkGrabAgain = grabWindow(themeWindow);
        check(darkGrab == darkGrabAgain,
              "CONTROL: two grabs of the same window at the same theme are pixel-identical, so a "
              "difference below is the theme and not capture noise");

        tinta::applyLightMode(app);
        std::printf("after_light index=%d darkMode=%d bg=%d,%d,%d (was %d,%d,%d)\n",
                    app.currentThemeIndex, app.darkMode ? 1 : 0, expect8(app.theme.background.r),
                    expect8(app.theme.background.g), expect8(app.theme.background.b),
                    expect8(darkBackground.r), expect8(darkBackground.g),
                    expect8(darkBackground.b));
        check(app.currentThemeIndex == kLightModeThemeIndex && !app.darkMode,
              "applyLightMode moved the index AND darkMode together -- the pair that has to "
              "agree, because darkMode is read off the theme rather than assumed");

        const QImage lightGrab = grabWindow(themeWindow);
        check(lightGrab != darkGrab,
              "ISC-12: the same window PAINTS DIFFERENTLY after --light, which is the claim -- a "
              "changed field that reached no pixel would pass the assertion above and fail here");
        check(pixelIs(lightGrab, 2, 2, app.theme.background, 2),
              "and the corner pixel carries the LIGHT theme's background specifically, so the "
              "difference is the theme this flag names rather than any repaint");
        check(!pixelIs(lightGrab, 2, 2, darkBackground, 2),
              "and it no longer carries the dark one, which is what separates 'switched' from "
              "'drew both' in a window that clears before it draws");

        // Restored because the file's convention is that every run ends in the state it found,
        // and because the cleanup below is read against the theme the earlier runs used.
        app.currentThemeIndex = savedThemeIndex;
        app.theme = savedThemeValue;
        app.darkMode = savedDarkMode;

        // ---- ISC-17: DO THE TWO ENUMERATIONS DESCRIBE THE SAME DISPLAYS? -------------
        //
        // Everything the placement suite proves, it proves about a monitor list handed to it as
        // DATA. That is the right design and it is also a blind spot the size of the whole
        // feature: swap the list for a different description of the same desktop and every
        // assertion stays green while every window lands somewhere else. src/main_d2d.cpp places
        // windows over EnumDisplayMonitors/GetMonitorInfoW; the Qt arm places them over
        // QScreen::geometry()/availableGeometry(); nothing in the tree had ever compared the two.
        //
        // THE ANSWER IS CONDITIONAL AND THE CONDITION IS PRINTED. Win32 reports PHYSICAL pixels
        // to a per-monitor-aware process; Qt 6 reports QScreen::geometry() in LOGICAL pixels,
        // physical over devicePixelRatio. So equality is only available where every ratio is 1,
        // and an equality read without that is an equality that expires when somebody scales a
        // display. Every ratio is therefore printed, and the equality assertion below is GATED on
        // them -- where a ratio is not 1 the run states the scaling instead of asserting equality,
        // which is the criterion's own "or the disagreement is stated" branch.
        const std::vector<tinta::MonitorArea> win32Screens = tinta::monitorsFromWin32();
        bool everyRatioIsOne = true;
        const QList<QScreen*> qtScreens = QGuiApplication::screens();
        for (const QScreen* s : qtScreens) {
            if (s == nullptr) continue;
            std::printf("  qt  dpr=%.4f geom=%d,%d %dx%d work=%d,%d %dx%d\n", s->devicePixelRatio(),
                        s->geometry().x(), s->geometry().y(), s->geometry().width(),
                        s->geometry().height(), s->availableGeometry().x(),
                        s->availableGeometry().y(), s->availableGeometry().width(),
                        s->availableGeometry().height());
            if (s->devicePixelRatio() != 1.0) everyRatioIsOne = false;
        }
        for (const tinta::MonitorArea& m : win32Screens) {
            std::printf("  w32 bounds=%d,%d %dx%d work=%d,%d %dx%d\n", m.bounds.x, m.bounds.y,
                        m.bounds.width, m.bounds.height, m.work.x, m.work.y, m.work.width,
                        m.work.height);
        }
        // Printed AFTER the per-screen lines so the ratios it summarises are on screen above it,
        // and printed unconditionally so the DPI condition is in the log of every run rather than
        // only of the runs where it bites.
        std::printf("dpi_awareness=%s win32_monitors=%d qt_monitors=%d every_dpr_is_1=%d\n",
                    tinta::processDpiAwareness(), static_cast<int>(win32Screens.size()),
                    static_cast<int>(screens.size()), everyRatioIsOne ? 1 : 0);

#ifdef _WIN32
        // THE COUNT IS COMPARABLE WHATEVER THE SCALING IS -- a display is a display at any ratio,
        // and this is the half of the criterion no DPI caveat touches. It is also the half that
        // would catch the enumeration this tree could plausibly get wrong: QGuiApplication::screens()
        // omitting a display Win32 lists, or listing a virtual one Win32 does not.
        check(win32Screens.size() == screens.size(),
              "ISC-17: Win32 and Qt agree on HOW MANY displays this host has -- the one comparison "
              "that is valid at any scaling factor");

        if (everyRatioIsOne && win32Screens.size() == screens.size()) {
            // Order is NOT assumed to match: EnumDisplayMonitors' order and
            // QGuiApplication::screens()' order are both unspecified, so a positional comparison
            // would report a disagreement that is really a permutation. Each Win32 rectangle is
            // required to have SOME Qt counterpart, which is the claim that actually matters --
            // that the two describe the same set of rectangles.
            int matchedBounds = 0;
            int matchedWork = 0;
            for (const tinta::MonitorArea& w : win32Screens) {
                for (const tinta::MonitorArea& q : screens) {
                    if (w.bounds.x == q.bounds.x && w.bounds.y == q.bounds.y &&
                        w.bounds.width == q.bounds.width && w.bounds.height == q.bounds.height) {
                        matchedBounds++;
                        if (w.work.x == q.work.x && w.work.y == q.work.y &&
                            w.work.width == q.work.width && w.work.height == q.work.height) {
                            matchedWork++;
                        }
                        break;
                    }
                }
            }
            // THE ORDER DIFFERS ON THIS HOST, AND THAT IS REPORTED RATHER THAN ASSERTED.
            // Measured 2026-09-21: Qt leads with the primary at 0,0 and Win32 leads with the side
            // monitor at 3441,-498, so a positional comparison would have reported two
            // disagreements that are really one permutation -- which is why the loop above matches
            // as a SET. Neither order is documented, so asserting "they differ" would over-fit
            // this desktop and asserting "they agree" would be false here; the honest instrument
            // prints the number and leaves it.
            //
            // IT IS NOT COSMETIC. choosePlacement's tie-break for a window equidistant from two
            // displays IS enumeration order (ISC-4's 18 residual ties, all at 1x1), so where a tie
            // occurs the two arms can resolve it to different monitors. That is a stated limit of
            // the port, not a defect either arm has.
            int positionalMatches = 0;
            for (size_t i = 0; i < win32Screens.size() && i < screens.size(); i++) {
                if (win32Screens[i].bounds.x == screens[i].bounds.x &&
                    win32Screens[i].bounds.y == screens[i].bounds.y) {
                    positionalMatches++;
                }
            }
            std::printf("matched_bounds=%d matched_work=%d positional=%d of %d\n", matchedBounds,
                        matchedWork, positionalMatches, static_cast<int>(win32Screens.size()));
            check(matchedBounds == static_cast<int>(win32Screens.size()),
                  "ISC-17: every Win32 monitor RECTANGLE has a Qt counterpart with identical "
                  "bounds -- so the port did not quietly change which rectangles a window is "
                  "hit-tested against");
            check(matchedWork == static_cast<int>(win32Screens.size()),
                  "ISC-17: and identical WORK areas too, which is the rectangle a saved window is "
                  "CLAMPED into -- the two-rectangles-per-monitor pair, both halves");
        } else {
            // Not a skip dressed as a pass: the criterion's other branch, and it prints the
            // reason. A host with a scaled display cannot make this comparison as integers, and
            // saying so is the honest reading -- what it means for the port is that Settings'
            // saved geometry was written by a PHYSICAL-pixel process and would be read by a
            // LOGICAL-pixel one, which is a real defect this test cannot see on this host.
            std::printf("ISC-17 STATED RATHER THAN ASSERTED: everyRatioIsOne=%d -- Win32 physical "
                        "pixels and Qt logical pixels are not integer-comparable here\n",
                        everyRatioIsOne ? 1 : 0);
        }
#else
        check(win32Screens.empty(),
              "ISC-17 is a WINDOWS-ONLY differential: off Windows there is no second enumeration, "
              "and monitorsFromWin32() correctly reports none rather than inventing one");
#endif
    }

    // ---- THE KEY ROUTER: Qt KEY CODES TO VIRTUAL KEYS, ONE KEY AT A TIME ---------------
    //
    // One check per key rather than a loop over a table, because a table in the test would
    // be a second copy of src/qt_key_map.cpp's and would agree with it by construction. F11
    // is the case that matters: Qt::Key_F11 is 0x0100003A and VK_F11 is 0x7A, so a cast in
    // place of the table passes every letter and fails here.
    check(tinta::vkFromQtKey(Qt::Key_A) == 'A' && tinta::vkFromQtKey(Qt::Key_Z) == 'Z',
          "vkFromQtKey: letters pass through -- the one range where the two numberings agree");
    check(tinta::vkFromQtKey(Qt::Key_0) == '0' && tinta::vkFromQtKey(Qt::Key_9) == '9',
          "vkFromQtKey: and so do digits");
    check(static_cast<unsigned>(Qt::Key_F11) != VK_F11 && tinta::vkFromQtKey(Qt::Key_F11) == VK_F11,
          "vkFromQtKey: F11 maps 0x0100003A to 0x7A, the case a cast would get wrong");
    check(tinta::vkFromQtKey(Qt::Key_F1) == VK_F1 && tinta::vkFromQtKey(Qt::Key_F12) == VK_F12,
          "vkFromQtKey: and both ends of the F1..F12 offset range land on their VK");
    check(tinta::vkFromQtKey(Qt::Key_Escape) == VK_ESCAPE, "vkFromQtKey: Escape");
    check(tinta::vkFromQtKey(Qt::Key_PageUp) == VK_PRIOR &&
              tinta::vkFromQtKey(Qt::Key_PageDown) == VK_NEXT,
          "vkFromQtKey: Page Up and Page Down are VK_PRIOR and VK_NEXT -- Windows' names");
    check(tinta::vkFromQtKey(Qt::Key_Backtab) == VK_TAB,
          "vkFromQtKey: Backtab, which is how Qt reports Shift+Tab, is VK_TAB as Windows reports it");
    check(tinta::vkFromQtKey(Qt::Key_Enter) == VK_RETURN &&
              tinta::vkFromQtKey(Qt::Key_Return) == VK_RETURN,
          "vkFromQtKey: the keypad Enter and Return are both VK_RETURN");
    check(tinta::vkFromQtKey(Qt::Key_Comma) == VK_OEM_COMMA &&
              tinta::vkFromQtKey(Qt::Key_Slash) == VK_OEM_2,
          "vkFromQtKey: the two OEM punctuation keys keymap.h names");
    check(tinta::vkFromQtKey(Qt::Key_Question) == 0 && tinta::vkFromQtKey(Qt::Key_F13) == 0,
          "CONTROL: a key no handler names maps to 0 -- '?' because shifted punctuation is "
          "matched on its character, F13 because nothing binds it");

    // ---- requestQuit AND ZEN MODE REACH THE WINDOW THROUGH ITS SINK -------------------
    //
    // What stood here asserted requestQuit() read FALSE on this target, which was the proof
    // TINTA_QT_UI had selected the off-window arm. It is replaced, not deleted: the arm now
    // delivers to include/window_sink.h, so the same proof is "true with a sink installed,
    // false with none" -- and on Windows, without TINTA_QT_UI, it would read true from
    // PostQuitMessage in BOTH halves, so the false half is the one that still pins the macro.
    //
    // A RECORDING SINK FOR THE DELIVERY, THE WINDOW'S OWN FOR THE REGISTRATION. The window's
    // quit closure calls QCoreApplication::quit(), whose effect this suite cannot observe
    // without an exec() it does not run; and its zen closures show the window fullscreen,
    // which on the Windows platform would cover the desktop of whoever ran ctest. So the
    // registration is read off the slot, the key path is driven into a recorder on both
    // hosts, and the real fullscreen is measured below under offscreen only.
    {
        tinta::clearWindowSink();
        check(!requestQuit(),
              "TINTA_QT_UI: with no window sink, requestQuit() returns false -- the off-window "
              "arm, not PostQuitMessage, which would have returned true");

        QtAppWindow sinkProbe(app);
        sinkProbe.resize(600, 400);
        check(static_cast<bool>(tinta::windowSinkSlot().quit) &&
                  static_cast<bool>(tinta::windowSinkSlot().zenEnter) &&
                  static_cast<bool>(tinta::windowSinkSlot().zenLeave),
              "constructing a QtAppWindow installs all three window-sink members");

        int quits = 0, enters = 0, leaves = 0;
        tinta::WindowSink recorder;
        recorder.quit = [&quits]() { ++quits; };
        recorder.zenEnter = [&enters]() {
            ++enters;
            tinta::ZenRestore restore;
            restore.valid = true;
            return restore;
        };
        recorder.zenLeave = [&leaves](const tinta::ZenRestore& restore) {
            ++leaves;
            return restore.valid;
        };
        tinta::setWindowSink(std::move(recorder));

        check(requestQuit() && quits == 1,
              "requestQuit() delivers to the installed sink and returns true");

        const bool zenBefore = app.zenMode;
        sendKey(sinkProbe, Qt::Key_F11);
        std::printf("zen_after_f11=%d enters=%d leaves=%d\n", app.zenMode ? 1 : 0, enters, leaves);
        check(!zenBefore && app.zenMode && enters == 1,
              "F11 through the key router turns zen mode ON and asks the window sink to enter "
              "-- vkFromQtKey, handleKeyDown, toggleZenMode, deliverZenEnter");
        sendKey(sinkProbe, Qt::Key_F11);
        check(!app.zenMode && leaves == 1,
              "and F11 again turns it OFF through the sink's leave");

        // THE MUTATION, run rather than argued: no sink, and the same key. toggleZenMode
        // flips zenMode, gets an invalid ZenRestore back and flips it off again -- which is
        // exactly the silent no-op the sink replaced, and the reason the check above can fail.
        tinta::clearWindowSink();
        sendKey(sinkProbe, Qt::Key_F11);
        check(!app.zenMode,
              "CONTROL: with the sink cleared, F11 leaves zen mode OFF -- an enter nobody can "
              "deliver is read as a failed enter");
        check(!requestQuit(), "CONTROL: and requestQuit() is false again with the sink cleared");
    }
    check(!static_cast<bool>(tinta::windowSinkSlot().quit),
          "and a destroyed window leaves no sink behind -- every member captured it");

    // THE REAL FULLSCREEN, offscreen only, for the reason the repaint block below gives:
    // showFullScreen() on the Windows platform is a real frame over the real desktop.
    if (QApplication::platformName() == QLatin1String("offscreen")) {
        QtAppWindow zenProbe(app);
        zenProbe.resize(600, 400);
        sendKey(zenProbe, Qt::Key_F11);
        QApplication::processEvents();
        std::printf("zen_real: zenMode=%d fullscreen=%d\n", app.zenMode ? 1 : 0,
                    zenProbe.isFullScreen() ? 1 : 0);
        check(app.zenMode && zenProbe.isFullScreen(),
              "F11 with the window's OWN sink sets zen mode AND puts the widget in full-screen "
              "state -- the key reaches the window, not only App");
        sendKey(zenProbe, Qt::Key_F11);
        QApplication::processEvents();
        check(!app.zenMode && !zenProbe.isFullScreen(),
              "and F11 again clears both");
    } else {
        std::printf("zen_real: SKIPPED on platform %s -- showFullScreen would cover the desktop; "
                    "the recording-sink half above ran here\n",
                    QApplication::platformName().toUtf8().constData());
    }

    // ---- THE MOUSE ROUTER (slice 3) ------------------------------------------------
    //
    // Every event goes through sendEvent into QtAppWindow's overrides and on into
    // src/input.cpp, so each check below fails if the override is missing, calls the wrong
    // handler, or passes the wrong coordinates. Targets are read off the laid-out text runs
    // rather than guessed, converted doc -> widget the way handleMouseDown converts back
    // (docX = x - documentViewportX + scrollX, docY = y + scrollY).
    {
        QtAppWindow mouseProbe(app);
        mouseProbe.resize(600, 400);
        check(mouseProbe.openDocument(tmpUtf8), "mouse router: the fixture opens");
        (void)grabWindow(mouseProbe);  // the paint lays the document out
        check(mouseProbe.hasMouseTracking(),
              "mouse router: the window tracks the mouse with no button held, as WM_MOUSEMOVE does");
        check(static_cast<bool>(tinta::windowSinkSlot().cursor),
              "constructing a QtAppWindow installs the window sink's cursor member");

        // THE PRECONDITION EVERY CLICK BELOW DEPENDS ON, asserted rather than assumed: until
        // this slice loadDocumentFromPath left currentFile empty, startPageActive() stayed
        // true over an open document, and handleMouseDown gave every press to the start page.
        check(app.currentFile == tmpUtf8 && !startPageActive(app),
              "opening a document records it as the current file, so clicks reach the document "
              "rather than the start page");

        // Aimed at a TextRect, the list findTextRectAt and selectionOffsetAtPoint hit-test --
        // LayoutTextRun::selectable is not what they read, and on this fixture it is never set.
        const App::TextRect* widest = nullptr;
        for (const App::TextRect& tr : app.textRects) {
            if (!widest || tr.rect.right - tr.rect.left > widest->rect.right - widest->rect.left) {
                widest = &tr;
            }
        }
        check(widest != nullptr && widest->rect.right - widest->rect.left > 40.0f,
              "mouse router: the fixture has a text rect wide enough to drag across");
        if (widest) {
            const float offX = documentViewportX(app) - app.scrollX;
            const float midY = (widest->rect.top + widest->rect.bottom) * 0.5f - app.scrollY;
            const QPointF start(widest->rect.left + offX + 2.0f, midY);
            const QPointF end(widest->rect.right + offX - 2.0f, midY);
            std::printf("mouse_target rect=[%.1f..%.1f] y=%.1f start=(%.1f,%.1f) end=(%.1f,%.1f)\n",
                        widest->rect.left, widest->rect.right, midY, start.x(), start.y(),
                        end.x(), end.y());
            auto resetClicks = [&app]() {
                app.clickCount = 0;
                app.lastClickTime = std::chrono::steady_clock::time_point{};
                app.hasSelection = false;
                app.selecting = false;
            };

            // Hover: the move reaches handleMouseMove, and its cursor comes back to the widget.
            mouseProbe.setCursor(Qt::ArrowCursor);
            sendMouse(mouseProbe, QEvent::MouseMove, start);
            std::printf("hover mouseX=%d mouseY=%d overText=%d cursor=%d\n", app.mouseX, app.mouseY,
                        app.overText ? 1 : 0, static_cast<int>(mouseProbe.cursor().shape()));
            check(app.mouseX == start.toPoint().x() && app.mouseY == start.toPoint().y(),
                  "a hover move reaches handleMouseMove with the widget position");
            check(mouseProbe.cursor().shape() == Qt::IBeamCursor,
                  "hovering document text sets the I-beam through setCursorShape and the sink");
            {
                // CONTROL: the same move with the cursor member unset leaves the arrow.
                tinta::WindowSink saved = tinta::windowSinkSlot();
                tinta::windowSinkSlot().cursor = nullptr;
                mouseProbe.setCursor(Qt::ArrowCursor);
                sendMouse(mouseProbe, QEvent::MouseMove, end);
                sendMouse(mouseProbe, QEvent::MouseMove, start);
                check(mouseProbe.cursor().shape() == Qt::ArrowCursor,
                      "CONTROL: with the cursor member unset the same hover leaves the arrow -- "
                      "the I-beam above came through the sink");
                tinta::windowSinkSlot() = std::move(saved);
            }

            // Drag-select: press, held move, release.
            resetClicks();
            sendMouse(mouseProbe, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
            sendMouse(mouseProbe, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
            sendMouse(mouseProbe, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
            std::printf("drag hasSelection=%d anchor=%zu focus=%zu selecting=%d\n",
                        app.hasSelection ? 1 : 0, app.selAnchor, app.selFocus,
                        app.selecting ? 1 : 0);
            check(app.hasSelection && app.selAnchor != app.selFocus,
                  "press, drag and release across a text run select text through input.cpp");
            check(!app.selecting, "and the release ends the selection drag");

            // Shift+click EXTENDS that selection: handleMouseDown's isShiftDown branch keeps the
            // anchor and moves the focus. Untestable in slice 3, because the answer came from
            // Qt's cached modifiers and a sent MOUSE event does not update them; the modifier
            // scope (include/keyboard.h) is what makes this reach input.cpp as Shift.
            {
                const size_t anchorBefore = app.selAnchor;
                const size_t focusBefore = app.selFocus;
                const QPointF mid((start.x() + end.x()) * 0.5, midY);
                app.clickCount = 0;
                app.lastClickTime = std::chrono::steady_clock::time_point{};
                sendMouse(mouseProbe, QEvent::MouseButtonPress, mid, Qt::LeftButton,
                          Qt::LeftButton, Qt::ShiftModifier);
                sendMouse(mouseProbe, QEvent::MouseButtonRelease, mid, Qt::LeftButton,
                          Qt::NoButton, Qt::ShiftModifier);
                std::printf("shiftclick hasSelection=%d anchor=%zu->%zu focus=%zu->%zu\n",
                            app.hasSelection ? 1 : 0, anchorBefore, app.selAnchor, focusBefore,
                            app.selFocus);
                check(app.hasSelection && app.selAnchor == anchorBefore &&
                          app.selFocus != focusBefore && app.selFocus != anchorBefore,
                      "Shift+click keeps the anchor and moves the focus -- Shift reached "
                      "input.cpp through the event's own modifiers");

                // CONTROL: the same click with no Shift collapses the selection instead.
                app.clickCount = 0;
                app.lastClickTime = std::chrono::steady_clock::time_point{};
                sendMouse(mouseProbe, QEvent::MouseButtonPress, mid, Qt::LeftButton,
                          Qt::LeftButton);
                sendMouse(mouseProbe, QEvent::MouseButtonRelease, mid, Qt::LeftButton,
                          Qt::NoButton);
                check(!app.hasSelection,
                      "CONTROL: the same click without Shift collapses the selection, so the "
                      "extension above was the Shift branch and not the click itself");
            }

            // Double-click: Qt turns the second press into a DblClick event.
            resetClicks();
            sendMouse(mouseProbe, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
            sendMouse(mouseProbe, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
            sendMouse(mouseProbe, QEvent::MouseButtonDblClick, start, Qt::LeftButton, Qt::LeftButton);
            sendMouse(mouseProbe, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
            std::printf("dblclick clickCount=%d mode=%d hasSelection=%d\n", app.clickCount,
                        static_cast<int>(app.selectionMode), app.hasSelection ? 1 : 0);
            check(app.clickCount == 2 && app.selectionMode == App::SelectionMode::Word &&
                      app.hasSelection,
                  "a double-click reaches the press path as a second press and selects a word");
            // CONTROL: the same two events with the click clock reset between them -- the Word
            // mode above has to have come from input.cpp's own click counting.
            resetClicks();
            sendMouse(mouseProbe, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
            sendMouse(mouseProbe, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
            app.lastClickTime = std::chrono::steady_clock::time_point{};
            sendMouse(mouseProbe, QEvent::MouseButtonDblClick, start, Qt::LeftButton, Qt::LeftButton);
            sendMouse(mouseProbe, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
            check(app.clickCount == 1 && app.selectionMode == App::SelectionMode::Normal,
                  "CONTROL: with the click clock reset between them the same pair is two single "
                  "clicks");

            // Context menu: a mouse-reason event over the document opens the document menu.
            app.showContextMenu = false;
            {
                QContextMenuEvent ev(QContextMenuEvent::Mouse, start.toPoint(),
                                     mouseProbe.mapToGlobal(start.toPoint()));
                QApplication::sendEvent(&mouseProbe, &ev);
            }
            check(app.showContextMenu, "a right-click context event opens the document menu");
            // A left press outside the menu dismisses it, and the release is swallowed.
            sendMouse(mouseProbe, QEvent::MouseButtonPress, QPointF(5.0, 395.0), Qt::LeftButton,
                      Qt::LeftButton);
            sendMouse(mouseProbe, QEvent::MouseButtonRelease, QPointF(5.0, 395.0), Qt::LeftButton,
                      Qt::NoButton);
            check(!app.showContextMenu && !app.swallowNextMouseUp,
                  "a click away closes the menu and the release is consumed");

            // Side buttons: WM_XBUTTONUP's shape, on release only.
            //
            // THE ENTRY NAMES THE DOCUMENT ALREADY OPEN, deliberately. A different file would
            // go through openDocumentInViewer, which persists recents and reading positions to
            // settings.ini -- and this target is not run under run_portable_test.cmake, so that
            // would be the real user's file. The same-file entry takes navOpenEntry's other
            // arm, which only restores the scroll, and still proves both stacks' routing.
            app.navBack.clear();
            app.navForward.clear();
            app.pendingScrollRestore = -1.0f;
            app.navBack.push_back({app.currentFile, 123.0f});
            sendMouse(mouseProbe, QEvent::MouseButtonPress, start, Qt::BackButton, Qt::BackButton);
            check(app.navBack.size() == 1 && app.pendingScrollRestore == -1.0f,
                  "CONTROL: a Back PRESS navigates nowhere -- Windows navigates on release");
            sendMouse(mouseProbe, QEvent::MouseButtonRelease, start, Qt::BackButton, Qt::NoButton);
            std::printf("back navBack=%zu navForward=%zu restore=%.1f\n", app.navBack.size(),
                        app.navForward.size(), app.pendingScrollRestore);
            check(app.navBack.empty() && app.navForward.size() == 1 &&
                      app.pendingScrollRestore == 123.0f,
                  "a Back release walks back through navigateBack");
            sendMouse(mouseProbe, QEvent::MouseButtonRelease, start, Qt::ForwardButton,
                      Qt::NoButton);
            check(app.navForward.empty() && app.navBack.size() == 1,
                  "and a Forward release walks forward again");
            app.pendingScrollRestore = -1.0f;  // not left for a later paint to apply
        }
    }

    // ---- ISC-3, SECOND ENDPOINT: DOES A REPAINT REQUEST ARRIVE AS A PAINT? ---------
    //
    // The registration check earlier in this file is a property of the constructor. This
    // is the other endpoint, and it is measured through paintCount() rather than by
    // re-reading the registration, because the two claims fail independently: a sink
    // holding a closure that calls the wrong thing, or nothing, registers perfectly.
    //
    // WHY NOT grab(). Every other paint assertion in this file is driven by grabWindow(),
    // and grab() renders the widget whether or not anything asked it to -- so a grab
    // after a requestRepaint would rise identically with the seam still wired to
    // (void)hwnd. That is this tree's an-assertion-over-one-capture-cannot-fail shape, and
    // it is why this block shows the window and pumps the event loop instead.
    //
    // WHY THE OFFSCREEN GUARD, and why it is not a gap. update() is documented to do
    // nothing on a hidden widget, so the receipt cannot be observed without show() --
    // and on Windows this target runs the REAL platform (CMakeLists.txt sets that
    // deliberately), where show() would flash a window onto the desktop of whoever ran
    // ctest. It costs nothing to skip: the sink is the OFF-WINDOWS arm of requestRepaint.
    // On Windows the same call reaches InvalidateRect and the sink is never consulted, so
    // running this block there would be pointing the instrument away from its subject.
    if (QApplication::platformName() == QLatin1String("offscreen")) {
        QtAppWindow repaintProbe(app);
        repaintProbe.resize(320, 240);
        repaintProbe.show();
        // Twice: the first flushes the show and its initial paint, the second settles
        // anything the first posted, so the baseline below is a quiet window.
        QApplication::processEvents();
        QApplication::processEvents();

        const int paintsBeforeRequest = repaintProbe.paintCount();
        std::printf("repaint_probe paints_before_request=%d\n", paintsBeforeRequest);

        // SUBJECT FIRST, then the control -- a control read against a red subject is not
        // a reading, which this tree has paid for once.
        requestRepaint(nullptr);
        QApplication::processEvents();
        const int paintsAfterRequest = repaintProbe.paintCount();
        std::printf("repaint_probe paints_after_request=%d\n", paintsAfterRequest);
        check(paintsAfterRequest > paintsBeforeRequest,
              "requestRepaint(nullptr) off Windows reached QWidget::update() and the widget really "
              "painted -- the receipt, not the registration");

        // THE CONTROL THAT MAKES THE ASSERTION ABOVE MEAN ANYTHING. processEvents() is
        // free to deliver paints for reasons of its own, and if it did, the rise above
        // would be an artefact of pumping the loop rather than of the request. So pump it
        // again, ask for nothing, and require the count to hold still.
        QApplication::processEvents();
        QApplication::processEvents();
        std::printf("repaint_probe paints_after_control=%d\n", repaintProbe.paintCount());
        check(repaintProbe.paintCount() == paintsAfterRequest,
              "CONTROL: pumping the event loop without a request paints nothing, so the rise above "
              "was the request and not the pump");

        repaintProbe.hide();
        // The destructor's unregistration, which is the one thing in this class that
        // would be a use-after-free rather than a missing feature: the sink captures
        // `this`, and the 277 sites that call it know nothing about this window's
        // lifetime. Scoped so the destructor runs while the process can still assert.
        {
            QtAppWindow shortLived(app);
            check(tinta::repaintSinkInstalled(), "the short-lived window installed its own sink");
        }
        check(!tinta::repaintSinkInstalled(),
              "~QtAppWindow cleared the sink, so a later requestRepaint cannot call update() on a "
              "destroyed widget");
        // Deliberately not re-installed: the run ends below, and the single-window
        // contract in include/qt_window.h means the slot holds one sink -- the outer
        // window's was displaced by shortLived's the moment it was constructed. That is
        // the contract being visible in a test rather than only in a comment.
    } else {
        std::printf("ISC-3 RECEIPT SKIPPED ON THIS PLATFORM: platform=%s -- update() cannot be "
                    "observed on a hidden widget and show() here would flash a real window; the "
                    "sink is the off-Windows arm and is not consulted on this platform at all\n",
                    QApplication::platformName().toUtf8().constData());
    }

    // THE THEME HOOKS (include/theme_apply.h). applyTheme and updateTextFormats left
    // src/d2d_init.cpp on 2026-09-24, and this is their first caller off Windows. Run here,
    // with the Qt text factory still installed, because updateTextFormats rebuilds the
    // formats through it.
    {
        // Font enumeration through src/theme_qt.cpp. Sorted is the contract the theme
        // editor's picker relies on; the second call must not append a second copy.
        app.systemFontFamilies.clear();
        enumerateSystemFontFamilies(app);
        const size_t fontCount = app.systemFontFamilies.size();
        std::printf("theme_hooks font_families=%zu\n", fontCount);
        check(fontCount > 0, "enumerateSystemFontFamilies found at least one family");
        check(std::is_sorted(app.systemFontFamilies.begin(), app.systemFontFamilies.end()),
              "enumerateSystemFontFamilies returned the families sorted");
        enumerateSystemFontFamilies(app);
        check(app.systemFontFamilies.size() == fontCount,
              "a second enumerateSystemFontFamilies is a no-op, not a second copy");

        // appliedZoomFactor is written ONLY by updateTextFormats, so a sentinel in it says
        // which branch applyTheme took. Same fonts, forced by copying the target's two
        // families into App first so the branch does not depend on the theme table:
        // relayout only, formats kept.
        const int themeBefore = app.currentThemeIndex;
        const int target = (themeBefore + 1) % themeCount();
        app.theme.fontFamily = themeAt(target).fontFamily;
        app.theme.codeFontFamily = themeAt(target).codeFontFamily;
        app.appliedZoomFactor = -1.0f;
        app.layoutDirty = false;
        applyTheme(app, target);
        check(app.currentThemeIndex == target && app.darkMode == themeAt(target).isDark,
              "applyTheme switched the theme index and the dark flag");
        check(app.layoutDirty, "applyTheme marked the layout dirty");
        check(app.appliedZoomFactor == -1.0f,
              "unchanged fonts made applyTheme skip updateTextFormats (appliedZoomFactor untouched)");

        // Different fonts: forced by planting a family no theme uses, so updateTextFormats
        // MUST run -- and it runs through the portable skeleton and the Qt no-op fallback.
        app.theme.fontFamily = L"tinta-theme-hooks-plant";
        app.appliedZoomFactor = -1.0f;
        applyTheme(app, themeBefore);
        check(app.appliedZoomFactor == app.zoomFactor,
              "a font change made applyTheme call updateTextFormats (appliedZoomFactor rewritten)");
        check(app.textFormat != nullptr && app.headingFormats[0] != nullptr,
              "updateTextFormats rebuilt the document formats through the Qt factory");
        check(app.currentThemeIndex == themeBefore, "applyTheme restored the original theme");

        // Out of range is refused without touching anything.
        applyTheme(app, themeCount());
        check(app.currentThemeIndex == themeBefore, "applyTheme ignores an out-of-range index");
    }

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
                    "render.cpp and paint_document.cpp, off the Windows path, TYPESETS an "
                    "equation through math_render.cpp rather than falling back to its source, and "
                    "ensureDashedStrokeStyle INSTALLS a stroke style handle -- the state commit "
                    "fc9ceb0 got wrong with nothing watching.\n");
    return g_failures == 0 ? 0 : 1;
}
