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
//      callers were Windows-only files when these runs were written, so no pixel in here
//      could see them and the by-name and by-SIZE assertions were the entire guard; since
//      2026-10-06 src/render_frame.cpp's renderFrame draws them in this window (run 11's
//      own comment says what that changed). The size half is not
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
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileOpenEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QResizeEvent>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QStyleHints>
#include <QHostAddress>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <QTimer>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstdio>
// std::abs(int) is declared here and not in <cmath>. libstdc++ happens to pull it in
// anyway, which is exactly the sort of thing MSVC does not.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <memory>

#include "app.h"
#include "document.h"
#include "file_utils.h"
#include "geometry.h"
#include "i18n.h"  // tr(), so the chip assertions read the translation table and not a copy
#include "signals.h"
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
#include "editor.h"
#include "export.h"  // exportPdfFile, defined in src/print.cpp
#include "input.h"
#include "keymap.h"
#include "paint_document.h"
#include "platform_paths.h"
#include "print.h"
#include "instance_channel_qt.h"
#include "menu_bar_qt.h"  // populateMenuBar, keyStaysWithWindow
#include "window_launch_qt.h"  // the relaunch test hook
#include "utils.h"            // launchQuickNoteWindow, launchDocumentWindow  // the single-instance hand-off
#include "print_qt.h"  // printDocumentTo, the testable half of the Qt print arm
#include "image_api.h"  // encodePng, for the factory probe
#include "overlays.h"   // folderSearchToggleAt, the search probe's hit test
#include "render.h"     // layoutDocument, run outside paint by the factory probe
#include "qt_init.h"
#include "qt_key_map.h"
// Run 19's whole point: the four functions src/main_qt.cpp calls to place and colour its
// window, reached from here so the test drives the SAME code rather than a copy of it. This
// header also pulls launch_options.h, settings_data.h and window_placement.h, which is why
// run 19 adds one include and not four.
#include "qt_launch.h"
#include "qt_window.h"
#include "repaint_sink.h"
#include "search.h"
#include "settings.h"
#include "startpage.h"
#include "tableedit.h"
#include "text_api.h"
// The chrome formats' own header, and the reason this file needs it now is the port: the
// bodies run 11 asserts on moved out of src/qt_init.cpp into src/text_formats_app.cpp, so
// installQtBackends' call resolves through here. qt_init.h still declares the Qt-named
// wrappers, which is why every existing assertion below compiles unchanged.
#include "text_formats.h"
// The Qt text backend's own types, for the #155 block: QtTextFactory's fallback-family slot
// and QtTextLayout::fontFamilies are the only way to witness the chain arriving on a layout
// without assuming which CJK faces the host has installed.
#include "text_qt.h"
#include "theme_apply.h"
#include "theme_data.h"
// ISC-17's Win32 half, in its own TU so <windows.h>'s macro set never reaches this file. Compiled
// into this target only (CMakeLists.txt), and it compiles to two stubs off Windows so run 20's call
// sites need no guard of their own beyond the one assertion that IS platform-specific.
#include "tabs.h"  // tabStripBackground, what the caption must match
#include "win32_chrome_readback.h"
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
//
// AND IT LETS THE LAYOUT FINISH, since 2026-10-06. The window draws tinta's frame now
// (src/render_frame.cpp), which lays a document out viewport-first and continues in
// WM_APP_LAYOUT_CHUNK messages through the post sink. A single grab therefore sees a partial
// layout and a partial contentHeight, exactly as tinta's first frame does. Every reading in this
// file was taken against a COMPLETE layout, so the helper pumps the queued chunks until App says
// the layout is done, then grabs again: the frame a user sees once the document has settled.
// The pumping is also the evidence that the chunks continue on this stack at all -- before the
// slice the Qt post sink refused WM_APP_LAYOUT_CHUNK, so this loop would have run to its bound.
App* g_settleApp = nullptr;
int g_settlePumps = 0;
QImage grabWindow(QtAppWindow& window) {
    QImage img = window.grab().toImage().convertToFormat(QImage::Format_RGB32);
    if (g_settleApp && g_settleApp->root && !g_settleApp->layoutComplete) {
        for (int i = 0; i < 2000 && !g_settleApp->layoutComplete; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            ++g_settlePumps;
        }
        img = window.grab().toImage().convertToFormat(QImage::Format_RGB32);
    }
    return img;
}

// A launch command line, through the parse src/main_qt.cpp runs.
tinta::LaunchOptions parse_qt_launch(const std::vector<std::string>& args) {
    return tinta::parseLaunchOptions(args);
}

// What src/file_utils.cpp's loadDocumentFromPath does to App when it replaces the document,
// for the runs that parse a string literal instead of reading a file. tinta's frame draws the
// START PAGE while currentFile is empty and lays a document out only when layoutDirty says so,
// so a root assigned on its own is a state no loader produces -- the frame would correctly
// show the start page and never lay the document out. Until 2026-10-06 this window relaid out
// whenever its run list was empty, which is why a bare assignment used to be enough.
void seatDocument(App& app, const qmd::ElementPtr& root, const char* name) {
    app.root = root;
    app.currentFile = root ? name : "";
    app.clearLayoutCache();
    app.layoutDirty = true;
    app.layoutComplete = false;
    app.contentHeight = 0.0f;
    app.contentWidth = 0.0f;
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
// notches is in the unit handleMouseWheel takes: POSITIVE IS WHEEL UP. Qt wants eighths
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

    // THE CONFIGURATION HOME IS PINNED FIRST, before anything can resolve it. A clean close
    // saves settings (slice 11) and the draft timer writes drafts, and unpinned both land in
    // the real per-user directory -- on Windows the settings.ini tinta itself reads. Not an
    // APPDATA redirect: the Win32 path backend asks SHGetFolderPathW, which ignores the
    // variable. A pin that did not take refuses the run rather than writing the real file.
    QTemporaryDir configHome;
    const std::wstring realConfigDir = platform::userConfigDirectory(L"Tinta");
    if (configHome.isValid()) {
        pinTintaConfigDir(QDir::toNativeSeparators(configHome.path()).toStdWString());
    }
    const std::wstring settingsPath = getSettingsPath();
    const std::wstring pinnedDir = QDir::toNativeSeparators(configHome.path()).toStdWString();
    if (!configHome.isValid() || settingsPath.rfind(pinnedDir, 0) != 0 ||
        (!realConfigDir.empty() && settingsPath.rfind(realConfigDir, 0) == 0)) {
        std::printf("FAIL: the configuration home is not pinned to a temporary directory, so a\n"
                    "      close would write the real settings.ini. Refusing to run.\n");
        return 2;
    }
    check(true, "the configuration home is pinned to a temporary directory, away from the real one");

    const QString platform = QApplication::platformName();
    std::printf("platform=%s\n", platform.toUtf8().constData());

    // THE LIST main() PARSES (qt_launch.h). ctest starts this binary with no arguments, so it
    // must be empty: the program name is skipped there rather than in parseLaunchOptions, and a
    // list that kept it would hand the exe's own path over as the launch document.
    {
        const std::vector<std::string> args = tinta::launchArguments();
        std::printf("launch_arguments n=%zu first=%s\n", args.size(),
                    args.empty() ? "-" : args[0].c_str());
        check(args.empty(), "launchArguments leaves out the program name");
    }

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
    g_settleApp = &app;
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
    check(app.renderTarget != nullptr && app.renderTarget == qtFactoryTarget(),
          "installQtBackends seated the FACTORY render target, the one App holds outside paint");
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
    check(app.renderTarget == qtFactoryTarget() && app.renderTargetStorage.get() == nullptr,
          "after the paint App holds the factory target again and the paint target is released, "
          "so the QPainter ended with the scope");

    const tinta::Color& bg = app.theme.background;
    std::printf("theme=%ls background=%d,%d,%d\n", app.theme.name, expect8(bg.r), expect8(bg.g),
                expect8(bg.b));
    // BOTTOM-left, not top-left, since the window draws tinta's frame: the top rows carry the
    // tab strip tinta draws in its client area, so (2,2) is chrome and not background.
    check(pixelIs(painted, 2, 397, bg, 2),
          "a corner pixel carries the theme background, so Clear() reached the window");
    check(pixelIs(painted, 598, 398, bg, 2),
          "the opposite corner does too, so Clear() covered the whole device and not a sub-rect");

    const int paintedDiff = nonBackgroundPixels(painted, band, bg);
    std::printf("band_non_background_painted=%d\n", paintedDiff);
    check(paintedDiff > 0,
          "pixels in the band differ from the background, so DrawText through the seam drew glyphs");

    // App holds no document yet, so this is the START PAGE, tinta's no-document frame -- it
    // replaced this window's placeholder line on 2026-10-06. Asserted rather than assumed,
    // because it is the baseline every document assertion below is read against.
    check(!window.documentPainted(),
          "with no document in App the window reports the start page, not a document");
    const int paintedDocBand = nonBackgroundPixels(painted, docBand, bg);
    std::printf("doc_band_non_background_start_page=%d\n", paintedDocBand);
    // NOT `== 0` ANY MORE, AND THE REASON IS THE CHANGE: the placeholder left the top band
    // flat, the start page and its tab strip do not. What a later document count needs is
    // a baseline to differ FROM, and run 5 asserts the frame returns to exactly this count.
    check(paintedDocBand >= 0,
          "the start page's document-band count is recorded as the no-document baseline");

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
    seatDocument(app, doc.root, "qinta_entry_inline.md");

    // A COMPARISON, not an absolute. This was written as a delta because App::drawCalls
    // was zeroed only by the retired src/main_d2d.cpp, which this stack did not run, so the
    // counter accumulated across grabs. It is zeroed at the top of every frame now, by
    // src/render_frame.cpp's renderFrame, which this window runs -- so drawCallsBefore is the
    // previous grab's frame (run 3's, with no document), the check below reads that the
    // document frame drew more than that one, and draw_calls_delta is the difference between
    // the two frames rather than this grab's own total.
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
    check(documentDocBand != paintedDocBand,
          "and it differs from the start page's count, so the band measured the document");

    // The strongest single discriminator in the file: if the document branch had somehow
    // produced the placeholder, every count above could still pass.
    check(documentImage != painted,
          "the document image is not the start-page image, pixel for pixel");

    // --- Run 5: the control, document cleared -----------------------------------
    seatDocument(app, nullptr, "");
    const QImage clearedImage = grabWindow(window);
    check(!window.documentPainted(),
          "CONTROL: with App::root cleared the window falls back to the start page");
    const int clearedDocBand = nonBackgroundPixels(clearedImage, docBand, bg);
    std::printf("doc_band_non_background_cleared=%d\n", clearedDocBand);
    check(clearedDocBand == paintedDocBand,
          "CONTROL: the document band returns to run 1's exact start-page count, so the count in "
          "run 4 measured the DOCUMENT");
    check(nonBackgroundPixels(clearedImage, band, bg) == paintedDiff,
          "CONTROL: the start page returns at run 1's exact count, so drawing a document left no residue");

    // --- Run 6: the control, document present and no text factory ----------------
    //
    // WEAKER THAN IT LOOKS, and worth saying so where the assertion is rather than only
    // in the header: paintEvent's `if (!app_.textFactory) return;` sits BEFORE the branch
    // choice, so this proves the document band's pixels are text drawn through the seam.
    // It does not prove the document branch is itself factory-dependent.
    seatDocument(app, doc.root, "qinta_entry_inline.md");
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
    // A LAYOUT TO INVALIDATE, built here rather than inherited from run 4. Runs 5 and 6 seat
    // and clear documents the way a loader does (seatDocument), which clears the cache, so the
    // precondition below would otherwise be false for a reason that has nothing to do with
    // the loader. The in-memory document is laid out by one settled grab and then dropped from
    // App WITHOUT clearing its runs -- the stale state the loader has to clean up.
    seatDocument(app, doc.root, "qinta_entry_inline.md");
    grabWindow(window);
    app.root = nullptr;
    app.currentFile.clear();
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
    // src/scroll.cpp's since-deleted step function, and the check was written to fail on Windows' arithmetic on purpose.
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

    // THE TWO POLICY TIMERS THAT WERE GUARDED OUT (slice 7). app.h's updateBlinkTimer,
    // resetCursorBlink and startNotificationTimer said `if (app.hwnd)`, which is null for the
    // whole of a qinta run, so the arm never happened and the sink above never saw it. They
    // ask canArmTimer now. Each check reads the WINDOW's timer, not only the App flag the
    // handler writes, because a flag left at its default would pass "cleared" on its own.
    {
        const bool searchBefore = app.showSearch, activeBefore = app.searchActive;
        app.showSearch = true;
        app.searchActive = true;
        updateBlinkTimer(app);
        const bool blinkArmed = window.timerActive(TIMER_CURSOR_BLINK);
        const bool blinkStart = app.cursorBlinkOn;
        {
            // 650 ms: one 500 ms tick and not two, so the phase has a single right answer.
            QEventLoop loop;
            QTimer::singleShot(650, &loop, &QEventLoop::quit);
            loop.exec();
        }
        const bool blinkAfter = app.cursorBlinkOn;
        app.showSearch = searchBefore;
        app.searchActive = activeBefore;
        updateBlinkTimer(app);
        std::printf("cursor_blink armed=%d start=%d after_tick=%d disarmed=%d on=%d\n",
                    blinkArmed ? 1 : 0, blinkStart ? 1 : 0, blinkAfter ? 1 : 0,
                    window.timerActive(TIMER_CURSOR_BLINK) ? 0 : 1, app.cursorBlinkOn ? 1 : 0);
        check(blinkArmed && blinkStart,
              "an active search arms TIMER_CURSOR_BLINK on the window with the cursor shown");
        check(!blinkAfter,
              "one 500 ms tick later the cursor is hidden -- handleTimer's blink branch ran");
        check(!window.timerActive(TIMER_CURSOR_BLINK) && app.cursorBlinkOn,
              "leaving search disarms the blink and leaves the cursor shown");

        signalPush(app, SIG_INFO, SIGI_INFO, L"timer probe");
        const bool chipArmed = window.timerActive(TIMER_NOTIFICATION);
        // Pinned, so signalsNeedTicks is false and the handler's first tick has nothing to
        // animate: the branch that disarms is the one under test.
        app.signalChips.back().pinned = true;
        {
            QEventLoop loop;
            QTimer::singleShot(150, &loop, &QEventLoop::quit);
            loop.exec();
        }
        std::printf("notification armed=%d after_tick_active=%d\n", chipArmed ? 1 : 0,
                    window.timerActive(TIMER_NOTIFICATION) ? 1 : 0);
        check(chipArmed, "signalPush arms TIMER_NOTIFICATION on the window");
        check(!window.timerActive(TIMER_NOTIFICATION),
              "with nothing left to animate, the first tick disarms it -- the handler's own exit");
        app.signalChips.clear();
    }

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
    //
    // CORRECTED 2026-09-29, TWICE OVER. Those three files compile AND link off Windows now:
    // all three have been in QINTA_APP_SOURCES since the 2026-09-25 key router. And "only
    // callers" was never the whole list: src/signals.cpp, src/annotations.cpp and
    // src/editrail.cpp read these formats too, and they link as well. What survived was the
    // pixel half, read for two of the drawers only: renderEditor's one caller and fifteen of
    // overlays.cpp's sixteen render* functions' callers were src/main_d2d.cpp. Since
    // 2026-10-06 that caller is src/render_frame.cpp's renderFrame, which this window runs,
    // so the pixel half has gone too and "the whole guard" is not re-claimed here.
    const bool chromeAllPresent =
        app.searchTextFormat != nullptr && app.signalSmallFormat != nullptr &&
        app.themeTitleFormat != nullptr && app.themeHeaderFormat != nullptr &&
        app.statsFormat != nullptr && app.folderBrowserFormat != nullptr &&
        app.tocFormat != nullptr && app.tocFormatBold != nullptr &&
        app.editorTextFormat != nullptr && app.editorGutterFormat != nullptr;
    check(chromeAllPresent,
          "all TEN chrome formats exist after installQtBackends -- search, signals, theme "
          "title, theme header, stats, folder browser, both TOC, editor text and gutter");

    // THE SIZES, and they are not decoration. Every number here was copied from
    // the retired src/d2d_init.cpp, and a format that exists at the wrong size draws chrome that
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
    // document text. installChromeFormats quotes the retired src/d2d_init.cpp's reason --
    // "fixed UI size so zooming the document cannot overflow the fixed stats box" -- and
    // getting it backwards makes the search box and the stats readout grow with Ctrl+= and
    // look like a feature rather than a defect.
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
                  "baseline at 30, which is what the Direct2D stack's editor row arithmetic read "
                  "back and would otherwise have computed a row pitch from the font instead");
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
        // BOTTOM-left of the 200x150 window, since the window draws tinta's frame and the top rows
        // are its tab strip -- (2,2) is chrome there, not the theme's background.
        check(pixelIs(lightGrab, 2, 147, app.theme.background, 2),
              "and the corner pixel carries the LIGHT theme's background specifically, so the "
              "difference is the theme this flag names rather than any repaint");
        check(!pixelIs(lightGrab, 2, 147, darkBackground, 2),
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
        // assertion stays green while every window lands somewhere else. The retired
        // src/main_d2d.cpp placed windows over EnumDisplayMonitors/GetMonitorInfoW; the Qt arm places them over
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
    // delivers to include/window_sink.h, so the proof is "true with a sink installed, false
    // with none". The macro and the PostQuitMessage arm it guarded are both gone (2026-10-11),
    // so the false half now pins the sink's behaviour rather than a compile definition.
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
              "with no window sink, requestQuit() returns false -- nothing to deliver to, and no "
              "PostQuitMessage arm left to answer true instead");

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
        requestRepaint();
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
        // MUST run -- and it runs through the portable skeleton and, since 2026-10-12, a
        // real Qt installFontFallback rather than the empty body that stood here.
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

    // --- #155: CJK IDEOGRAPH FAMILIES ORDERED BY UI LANGUAGE --------------------
    //
    // Han unification shares code points across zh/ja/ko while the glyph SHAPES differ, so
    // whichever family resolves first decides the variant every ideograph is drawn with.
    // `src/theme_qt.cpp`'s installFontFallback was an empty body until 2026-10-12 and the
    // comment above it said so: the Windows chain ordered the ideograph families by the UI
    // language and the Qt arm had no counterpart.
    //
    // WHAT THIS BLOCK DOES NOT ASSERT, and the reason is the whole design: nothing here
    // reads which CJK face the host actually resolved. It cannot -- no CI runner is
    // guaranteed Yu Gothic UI or PingFang SC, and a check that needs them is a check that
    // cannot grade the thing on two of three platforms. So the ORDER is read off a pure
    // function of the language tag, which is gradable everywhere, and the chain's ARRIVAL
    // is read off the layout's own families list.
    {
        const auto indexOf = [](const std::vector<std::wstring>& list,
                                const wchar_t* family) -> int {
            for (size_t i = 0; i < list.size(); i++) {
                if (list[i] == family) return static_cast<int>(i);
            }
            return -1;
        };
        // The LAST position any of these names takes, and the FIRST -- the pair is what
        // "this language's whole group leads" means, as against "one of its faces does".
        const auto lastOf = [&](const std::vector<std::wstring>& list,
                                std::initializer_list<const wchar_t*> names) {
            int worst = -1;
            for (const wchar_t* name : names) worst = std::max(worst, indexOf(list, name));
            return worst;
        };
        const auto firstOf = [&](const std::vector<std::wstring>& list,
                                 std::initializer_list<const wchar_t*> names) {
            int best = -1;
            for (const wchar_t* name : names) {
                const int at = indexOf(list, name);
                if (at >= 0 && (best < 0 || at < best)) best = at;
            }
            return best;
        };
        // One Windows, one macOS and one Linux face per language. Naming all three is what
        // makes ISC-3 a claim about parity rather than about Windows: porting the deleted
        // arm's four Windows families alone would have left macOS and Linux unchanged.
        const std::initializer_list<const wchar_t*> jaFaces = {
            L"Yu Gothic UI", L"Hiragino Sans", L"Noto Sans CJK JP"};
        const std::initializer_list<const wchar_t*> zhFaces = {
            L"Microsoft YaHei UI", L"PingFang SC", L"Noto Sans CJK SC"};
        const std::initializer_list<const wchar_t*> koFaces = {
            L"Malgun Gothic", L"Apple SD Gothic Neo", L"Noto Sans CJK KR"};

        const std::vector<std::wstring> ja = cjkFallbackFamilies(L"ja-JP");
        const std::vector<std::wstring> ko = cjkFallbackFamilies(L"ko-KR");
        const std::vector<std::wstring> zh = cjkFallbackFamilies(L"zh-CN");
        const std::vector<std::wstring> en = cjkFallbackFamilies(L"en-US");
        const std::vector<std::wstring> unknown = cjkFallbackFamilies(L"xx-YY");
        const std::vector<std::wstring> empty = cjkFallbackFamilies(L"");

        std::printf("cjk_order ja=%d/%d ko=%d/%d zh=%d/%d n=%zu\n",
                    lastOf(ja, jaFaces), firstOf(ja, zhFaces),
                    lastOf(ko, koFaces), firstOf(ko, zhFaces),
                    lastOf(zh, zhFaces), firstOf(zh, jaFaces), ja.size());

        // ISC-1 / ISC-3. The three platform faces of the leading language ALL sit ahead of
        // every other language's faces. Asserted as a span rather than as "first element
        // is X", because a list that led with Yu Gothic UI and then put Microsoft YaHei UI
        // second would satisfy the weaker reading while still rendering macOS and Linux
        // kanji in a Chinese face.
        check(lastOf(ja, jaFaces) >= 0 && firstOf(ja, zhFaces) >= 0 &&
                  lastOf(ja, jaFaces) < firstOf(ja, zhFaces) &&
                  lastOf(ja, jaFaces) < firstOf(ja, koFaces),
              "a Japanese UI language puts every Japanese face -- Windows, macOS and Linux -- "
              "ahead of the Chinese and Korean ones (#155)");
        check(lastOf(ko, koFaces) >= 0 && lastOf(ko, koFaces) < firstOf(ko, zhFaces) &&
                  lastOf(ko, koFaces) < firstOf(ko, jaFaces),
              "a Korean UI language puts every Korean face ahead of the Chinese and Japanese ones");
        check(lastOf(zh, zhFaces) >= 0 && lastOf(zh, zhFaces) < firstOf(zh, jaFaces) &&
                  lastOf(zh, zhFaces) < firstOf(zh, koFaces),
              "a Chinese UI language puts every Chinese face ahead of the Japanese and Korean ones");

        // The default arm, which is the deleted code's own `: cjkZh` fallthrough. An
        // unknown and an empty tag are the two readings a fresh container can produce.
        check(en == zh && unknown == zh && empty == zh,
              "en-US, an unknown tag and an empty tag all take the Simplified-Chinese-first "
              "default, which is the Direct2D arm's own fallthrough");
        check(cjkFallbackFamilies(L"ja") == ja && cjkFallbackFamilies(L"JA_jp.UTF-8") == ja,
              "the primary subtag is what is read, so a bare \"ja\" and a POSIX-spelled "
              "\"JA_jp.UTF-8\" order the same as \"ja-JP\"");

        // ISC-2, the counter-case: a function that ignored its argument and returned one
        // list would pass every ordering check above by accident.
        check(ja != zh && ko != zh && ja != ko,
              "CONTROL: the three orderings are three different lists, so the checks above are "
              "not reading one constant");

        // The emoji tail is last in all three, and the deleted arm's full-range mapping is
        // what it replaces.
        check(!ja.empty() && ja.back() == L"Segoe UI Symbol" && zh.back() == L"Segoe UI Symbol",
              "the emoji and symbol faces come after every CJK face, as the Direct2D arm's "
              "full-range mapping sat behind its three range mappings");

        // THE INSTALLED-ONLY FILTER (theme_apply.h: on macOS an absent NanumGothic blocks Qt on
        // a download).
        // A FAKE predicate, so these grade on every host including one with none of the 26
        // families. Three names, one per group, so a filter that sorted, deduplicated or
        // dropped the order would come back in a different sequence for ja than for zh.
        const auto fakeHas = +[](const std::wstring& family) {
            return family == L"PingFang SC" || family == L"Noto Sans CJK JP" ||
                   family == L"Segoe UI Symbol";
        };
        const std::vector<std::wstring> zhKept = installedFamiliesOnly(zh, fakeHas);
        const std::vector<std::wstring> jaKept = installedFamiliesOnly(ja, fakeHas);
        check(zhKept == std::vector<std::wstring>{L"PingFang SC", L"Noto Sans CJK JP",
                                                  L"Segoe UI Symbol"},
              "installedFamiliesOnly keeps exactly the installed families, in the zh order");
        check(jaKept == std::vector<std::wstring>{L"Noto Sans CJK JP", L"PingFang SC",
                                                  L"Segoe UI Symbol"},
              "CONTROL: and in the ja order for the ja list -- the filter keeps the input's "
              "order rather than imposing one");
        check(installedFamiliesOnly(zh, +[](const std::wstring&) { return false; }).empty() &&
                  installedFamiliesOnly(zh, +[](const std::wstring&) { return true; }) == zh,
              "nothing installed keeps nothing, everything installed keeps the whole list");

        // ISC-4 / ISC-5. THE LIST IS SEATED AT STARTUP, which is the half the parity audit
        // did not name: installFontFallback's one portable caller is updateTextFormats, and
        // src/qt_launch.cpp calls that only when the saved zoom differs from the built one.
        // So a default launch would never have seated it.
        //
        // RELEASE-AND-REINSTALL ON THIS App, which is the pattern runs 2 and 3 above already
        // use and which is therefore proven on all three platforms. A second App on the heap
        // read the same claim more directly and is deliberately NOT used: this file has never
        // constructed two, it was in the change that hung macOS, and a construct with no
        // precedent on a host I cannot run is not worth a stronger pre-reading.
        //
        // The PRE-READING is what keeps this from being a tautology: after the release there
        // is no factory at all, so the list below cannot be the one updateTextFormats seated
        // a few lines up.
        releaseQtBackends(app);
        check(app.textFactory == nullptr,
              "PRE-READING: releaseQtBackends left no text factory, so a seated list below is "
              "installQtBackends' own work and not a leftover");
        installQtBackends(app);
        auto* startupFactory = dynamic_cast<QtTextFactory*>(app.textFactory);

        // What the installer should seat: this host's UI language order, filtered to what
        // this host has. Computed through the same two functions, so the check below grades
        // the INSTALLER's wiring; the filter's own behaviour is graded by the fake above.
        const std::vector<std::wstring> uiTags = platform::preferredUiLanguages();
        const auto hostHas = +[](const std::wstring& family) {
            return QFontDatabase::hasFamily(QString::fromStdWString(family));
        };
        QStringList expectedSeat;
        for (const std::wstring& family : installedFamiliesOnly(
                 cjkFallbackFamilies(uiTags.empty() ? std::wstring() : uiTags.front()),
                 hostHas)) {
            expectedSeat << QString::fromStdWString(family);
        }
        std::printf("cjk_startup seated=%d expected=%d of %zu\n",
                    startupFactory ? static_cast<int>(startupFactory->fallbackFamilies().size())
                                   : -1,
                    static_cast<int>(expectedSeat.size()), ja.size());
        check(startupFactory != nullptr && startupFactory->fallbackFamilies() == expectedSeat,
              "installQtBackends ALONE seated the installed fallback families, in order -- no "
              "updateTextFormats, no zoom change, no theme switch, which is what a default "
              "launch is");
        // On a host with NONE of the 26 (the Linux job's DejaVu-only image) the seat is empty,
        // and an empty seat is also what a missing installer call leaves. Said, not passed
        // off: the startup caller is witnessed on macOS and Windows, where the list is not
        // empty, and an unfiltered seat is red there because it is longer than this one.
        if (expectedSeat.isEmpty()) {
            std::printf("cjk_startup UNWITNESSED: this host has none of the fallback families, "
                        "so an empty seat cannot be told from no seat\n");
        }

        auto* factory = dynamic_cast<QtTextFactory*>(app.textFactory);
        check(factory != nullptr, "PRECONDITION: this App's text factory is the Qt one");
        // updateTextFormats is installFontFallback's OTHER caller, and it has to reach it too
        // -- that is the portable half, the one Windows always had. Without this, only the
        // startup caller would be witnessed.
        //
        // A SENTINEL rather than an empty list, so this grades on every host: the expected
        // seat can be empty (above), and "cleared, then still empty" passes whether or not
        // updateTextFormats reached the installer. No host has this family, so a seat that
        // replaced it can only be the installer's.
        const QStringList sentinel{QStringLiteral("QINTA sentinel family 155")};
        factory->setFallbackFamilies(sentinel);
        check(factory->fallbackFamilies() == sentinel,
              "PRE-READING: the factory holds a sentinel list, so a different list after "
              "updateTextFormats is that call's own work");
        updateTextFormats(app);
        check(factory->fallbackFamilies() != sentinel &&
                  factory->fallbackFamilies() == expectedSeat,
              "updateTextFormats reaches installFontFallback too, seating the identical list -- "
              "so BOTH of its callers are witnessed, not just the startup one");
        // What the INSTALLER seated, read off the factory and not recomputed: the Latin and
        // walk checks below run against this, so a broken installer reaches them. (They first
        // ran against expectedSeat, and an unfiltered installer then left the walk green.)
        const QStringList installerSeat = factory->fallbackFamilies();

        // ISC-6 / ISC-7 / ISC-8 / ISC-9: the layout half.
        //
        // The family is taken from the font DATABASE rather than named, for the reason the
        // FontFamilyExists block above gives: no family name is installed on every host.
        //
        // A LATIN family, and not a dot-prefixed one. This was `families().front()`, which on
        // macOS is ".Apple Symbols Fallback" -- a private face with no Latin glyphs -- so the
        // Latin-width check below measured Qt's substitute before the apply and the first
        // appended family after it, and went red (143.06 -> 145.92) on the host's choice of
        // font, not on the chain. A real body font does not move: Helvetica, Menlo and Times
        // New Roman read identical widths with and without the macOS families appended.
        QStringList hostFamilies;
        for (const QString& family : QFontDatabase::families(QFontDatabase::Latin)) {
            if (!family.startsWith(QLatin1Char('.'))) hostFamilies << family;
        }
        check(hostFamilies.size() >= 2,
              "PRECONDITION: the font database has at least two Latin families -- one to lay "
              "out in, at least one to chain behind it");
        const QString hostFamily = hostFamilies.isEmpty() ? QString() : hostFamilies.front();
        const std::wstring hostFamilyW = hostFamily.toStdWString();

        // THE CHAIN UNDER TEST IS SEATED HERE, from families this host HAS, rather than read
        // off the installer. The installer's seat is the CJK families the host has, which is
        // none on the Linux job, and every append check below would then compare empty lists.
        // These checks are about what ApplyFontFallback does with a list, so the list is the
        // test's to choose; the installer's own list is graded above and again below.
        const QStringList seatedHere = hostFamilies.mid(1, 3);
        factory->setFallbackFamilies(seatedHere);

        auto makeFormat = [&](const wchar_t* family) {
            return app.textFactory->CreateTextFormat(
                family, tinta::FontWeight::Normal, tinta::FontStyle::Normal,
                tinta::FontStretch::Normal, 16.0f, L"en-us");
        };
        const auto widthOf = [](tinta::TextLayout* layout) {
            tinta::TextMetrics m{};
            layout->GetMetrics(&m);
            return m.width;
        };

        auto realFormat = makeFormat(hostFamilyW.c_str());
        check(realFormat != nullptr, "PRECONDITION: a format on a family the host has");

        // MIXED TEXT AGAIN. A previous push replaced this with Latin-only text on the theory
        // that shaping ideographs through the list was what hung macOS. It hung anyway, in
        // blockquote_layout and math_layout rather than here, and the stack sample named the
        // real cause: the NanumGothic download (theme_apply.h). So the ideographs come back.
        auto mixed = app.textFactory->CreateTextLayout(L"Latin 漢字 あ", 10,
                                                       realFormat.get(), 2000.0f, 2000.0f);
        auto* qtMixed = dynamic_cast<QtTextLayout*>(mixed.get());
        check(qtMixed != nullptr, "PRECONDITION: the layout is a QtTextLayout");

        // ISC-8, and it is the claim src/render.cpp:59-80 measured and refused to give up:
        // 15 sites create layouts through the factory and have never had a fallback, several
        // of them MEASURE text. A factory that attached the chain itself would move their
        // widths. So a layout nobody asked carries ONLY its own family.
        const QStringList beforeApply = qtMixed ? qtMixed->fontFamilies() : QStringList();
        check(beforeApply.size() == 1 && beforeApply.first() == hostFamily,
              "a layout nobody asked carries its own family and nothing else -- CreateTextLayout "
              "does not attach the chain (src/render.cpp's measured refusal)");

        mixed->ApplyFontFallback();
        const QStringList afterApply = qtMixed ? qtMixed->fontFamilies() : QStringList();
        std::printf("cjk_layout before=%d after=%d seated=%d\n",
                    static_cast<int>(beforeApply.size()),
                    static_cast<int>(afterApply.size()),
                    static_cast<int>(seatedHere.size()));

        // ISC-6. The primary STAYS AT INDEX 0: a chain that displaced it would change the
        // face of every Latin letter in the document, which is a bigger regression than the
        // bug being fixed.
        // `afterApply.size() > beforeApply.size()` is NOT redundant with the sum, and it was
        // added because arm A proved it: with the factory's list empty the sum reads
        // 1 == 1 + 0 and `mid(1) == seatedHere` compares two empty lists, so both halves
        // passed while nothing whatever had been appended. A check that cannot fail when the
        // feature is absent is not evidence for it, however well it reads.
        check(afterApply.size() > beforeApply.size() &&
                  afterApply.size() == beforeApply.size() + seatedHere.size() &&
                  afterApply.first() == hostFamily,
              "ApplyFontFallback appended the whole ordered list AFTER the layout's own family, "
              "which stays at index 0");
        check(!seatedHere.isEmpty() && afterApply.mid(1) == seatedHere,
              "and appended it in the factory's order, not sorted or reversed -- the order IS the "
              "content of #155");

        mixed->ApplyFontFallback();
        check(qtMixed && qtMixed->fontFamilies() == afterApply,
              "a second ApplyFontFallback is idempotent rather than growing the list, which the "
              "four call sites' repeat-per-paint frequency needs");

        // ISC-7. The guard that keeps Latin text safe off Windows: Qt walks the families
        // list for a missing FAMILY and not only a missing glyph, so an unguarded append
        // behind a theme font the host lacks would resolve the whole document to the first
        // CJK family in the list.
        auto absentFormat = makeFormat(L"NoSuchFamily QINTA 12345");
        auto absent = app.textFactory->CreateTextLayout(L"Latin only", 10, absentFormat.get(),
                                                        2000.0f, 2000.0f);
        auto* qtAbsent = dynamic_cast<QtTextLayout*>(absent.get());
        const QStringList absentBefore = qtAbsent ? qtAbsent->fontFamilies() : QStringList();
        absent->ApplyFontFallback();
        check(qtAbsent && absentBefore.size() == 1 && qtAbsent->fontFamilies() == absentBefore,
              "CONTROL: behind a family the host does NOT have, ApplyFontFallback leaves the font "
              "alone, so Qt's own substitution still picks the body face rather than a CJK one");

        // ISC-9, the parity claim (claim 16): a Latin string's measured width must not move.
        // Read from the layout after the apply, which reshapes -- run_ is invalidated now,
        // where the empty body deliberately did not invalidate it.
        //
        // Against the INSTALLER's seat where this host has one -- the CJK faces are what the
        // app appends, so they are the families a width could move behind -- and against the
        // test's own list on a host with none, so the apply is never a no-op here.
        const QStringList latinChain = installerSeat.isEmpty() ? seatedHere : installerSeat;
        factory->setFallbackFamilies(latinChain);
        auto latin = app.textFactory->CreateTextLayout(L"The quick brown fox", 19,
                                                       realFormat.get(), 2000.0f, 2000.0f);
        const float latinBefore = widthOf(latin.get());
        latin->ApplyFontFallback();
        const float latinAfter = widthOf(latin.get());
        auto* qtLatin = dynamic_cast<QtTextLayout*>(latin.get());
        std::printf("cjk_latin_width host=[%s] chain=%d before=%.4f after=%.4f\n",
                    hostFamily.toUtf8().constData(), static_cast<int>(latinChain.size()),
                    latinBefore, latinAfter);
        check(latinBefore > 0.0f, "PRECONDITION: the Latin string measured non-zero");
        check(qtLatin && qtLatin->fontFamilies().size() == 1 + latinChain.size(),
              "PRECONDITION: the chain really was attached to the Latin layout, so the width "
              "below is read with it and not without it");
        check(std::fabs(latinAfter - latinBefore) < 0.01f,
              "attaching the chain did not move a Latin string's measured width, which is what "
              "the 15 measuring sites would have paid for a format-level chain");

        // THE macOS HANG, as a regression check: EVERY seated family, one at a time, directly
        // behind the primary, shaping U+E000. Private-use, so the primary lacks it and Qt has
        // to load the next family's engine -- the call that, for NanumGothic, blocked in
        // CoreText's downloadable-font manager until the asset arrived (44 minutes on the Mac
        // that measured it; never within the CI job).
        //
        // One family at a time rather than the whole seat in one layout: Qt stops at the
        // first face that covers the glyph, so one walk through the whole list passes or fails
        // on what the host's earlier faces happen to cover.
        //
        // WHERE THIS CAN GO RED, stated because it is narrow: only on a macOS host that has
        // NOT downloaded Nanum, which is every fresh CI runner. The Mac this was written on
        // downloaded it during the measurement, and an unfiltered installer then walked all
        // 26 here and returned. On that host the unfiltered installer is caught by the
        // startup check above instead (26 seated against the installed subset). Here, ctest's
        // TIMEOUT is what turns a hang into a red in two minutes rather than a 55-minute stall.
        int walked = 0;
        for (const QString& family : installerSeat) {
            factory->setFallbackFamilies(QStringList{family});
            auto walk = app.textFactory->CreateTextLayout(L"\uE000x", 2, realFormat.get(),
                                                          2000.0f, 2000.0f);
            walk->ApplyFontFallback();
            if (widthOf(walk.get()) > 0.0f) ++walked;
        }
        std::printf("cjk_walk walked=%d of %d\n", walked, static_cast<int>(installerSeat.size()));
        check(walked == installerSeat.size(),
              "every family the installer seats loads its engine and RETURNS for a glyph the "
              "primary lacks -- the absent-family download that hung macOS is not reachable");
        factory->setFallbackFamilies(installerSeat);
    }

    // THE TWO POSTED MESSAGES (slice 8). An async image download and a folder scan each run
    // on a worker thread and hand their result back through postWindowMessage, which off the
    // Win32 stack returned false until include/post_sink.h existed -- so both workers deleted
    // what they had produced, a remote image stayed `pending` for the life of the document,
    // and folder search never armed at all. Each half below reads the state BEFORE the post
    // (pending; the debounce armed) as well as after, because "not pending" and "one result"
    // are also what a check reads if nothing was ever started.
    //
    // A WINDOW OF ITS OWN, and not `window`: every probe window above clears ALL the sinks
    // in its destructor (qt_window.cpp's single-window note), so by this point `window` has
    // none and each check below read false on the first run for that reason alone.
    {
        QtAppWindow postWindow(app);
        postWindow.resize(800, 600);
        // Runs the event loop until `done` holds or `budgetMs` passes, returning `done`.
        // Polling rather than one long exec(), because the worker's timing is the network
        // stack's: a refused connection is immediate on Linux and can take seconds on
        // Windows.
        auto pumpUntil = [](auto done, int budgetMs) {
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
            while (!done() && std::chrono::steady_clock::now() < deadline) {
                QEventLoop loop;
                QTimer::singleShot(10, &loop, &QEventLoop::quit);
                loop.exec();
            }
            return static_cast<bool>(done());
        };

        const std::filesystem::path postDir =
            std::filesystem::temp_directory_path() / "qinta_post_probe";
        std::error_code dirEc;
        std::filesystem::remove_all(postDir, dirEc);
        std::filesystem::create_directories(postDir, dirEc);
        // Port 9 (discard) on loopback: refused, never answered, and never leaves the host.
        const std::string remote = "http://127.0.0.1:9/qinta-post-probe.png";
        {
            std::ofstream a(postDir / "a.md", std::ios::binary);
            a << "# post probe\n\n![remote](" << remote << ")\n\n![local svg](local.svg)\n";
            // A LOCAL SVG beside the remote image. loadSvgBitmap refused until 2026-10-07
            // unless App's Direct2D and WIC factories were set, and on qinta they never
            // were, so every SVG image drew as its alt-text placeholder.
            std::ofstream svg(postDir / "local.svg", std::ios::binary);
            svg << "<svg xmlns='http://www.w3.org/2000/svg' width='200' height='120'>"
                   "<rect x='20' y='20' width='160' height='60' fill='#E040FB'/></svg>";
            std::ofstream b(postDir / "b.md", std::ios::binary);
            b << "# sibling\n\nThe word zyxneedle is here.\n";
            // Holds the query too, and must NOT be returned: the scan reads .md and
            // .markdown only, so this is what makes "exactly one result" a reading of the
            // filter and not of a directory that only had one file in it.
            std::ofstream c(postDir / "c.txt", std::ios::binary);
            c << "zyxneedle\n";
        }
        check(loadDocumentFromPath(app, (postDir / "a.md").u8string()),
              "the post probe document loaded");
        // Layout happens inside paint, which is the only time app.renderTarget is set, and
        // getOrLoadImage starts the download only with a render target.
        grabWindow(postWindow);
        {
            auto svgIt = app.imageCache.find("local.svg");
            const bool svgLoaded = svgIt != app.imageCache.end() && !svgIt->second.failed &&
                                   svgIt->second.bitmap != nullptr;
            std::printf("local_svg loaded=%d width=%d height=%d\n", svgLoaded ? 1 : 0,
                        svgLoaded ? svgIt->second.width : -1,
                        svgLoaded ? svgIt->second.height : -1);
            check(svgLoaded && svgIt->second.width == 200 && svgIt->second.height == 120,
                  "a local SVG image loads at its intrinsic 200x120 (it was always an alt-text "
                  "placeholder on qinta until 2026-10-07)");
        }
        auto entryIt = app.imageCache.find(remote);
        const bool startedPending = entryIt != app.imageCache.end() && entryIt->second.pending;
        // The arm is RECORDED as it happens, not sampled after the pump. The reflow timer is
        // 60 ms and the pump runs the loop in 10 ms slices, so a slow slice lets the timer fire
        // and disarm before a sample can see it. On macOS CI that read reflow_armed=0 on one
        // attempt of 37568545171 and both attempts of 37571709269 (2026-10-07), while
        // "the reflow tick disarmed it" passed beside it. The wrapper forwards every call, so
        // the window's QTimers behave exactly as before.
        bool reflowArmSeen = false;
        const auto originalArm = tinta::timerSinkSlot().arm;
        tinta::timerSinkSlot().arm = [&reflowArmSeen, originalArm](unsigned id, unsigned ms) {
            if (id == TIMER_IMAGE_REFLOW) reflowArmSeen = true;
            if (originalArm) originalArm(id, ms);
        };
        const bool completed = pumpUntil(
            [&app, &remote]() {
                auto it = app.imageCache.find(remote);
                return it != app.imageCache.end() && !it->second.pending;
            },
            15000);
        const bool reflowArmed = reflowArmSeen;
        tinta::timerSinkSlot().arm = originalArm;
        {
            QEventLoop loop;
            QTimer::singleShot(200, &loop, &QEventLoop::quit);
            loop.exec();
        }
        entryIt = app.imageCache.find(remote);
        const bool failed = entryIt != app.imageCache.end() && entryIt->second.failed;
        std::printf("post_image pending=%d completed=%d reflow_armed=%d reflow_after=%d "
                    "failed=%d\n",
                    startedPending ? 1 : 0, completed ? 1 : 0, reflowArmed ? 1 : 0,
                    postWindow.timerActive(TIMER_IMAGE_REFLOW) ? 1 : 0, failed ? 1 : 0);
        check(startedPending,
              "laying out a remote image leaves its cache entry pending with a worker started");
        check(completed && failed,
              "the worker's WM_APP_IMAGE_READY reached completeAsyncImage through the post "
              "sink: the entry is no longer pending, and failed, since port 9 refuses");
        check(reflowArmed, "and completeAsyncImage armed TIMER_IMAGE_REFLOW on the window");
        check(!postWindow.timerActive(TIMER_IMAGE_REFLOW),
              "and the reflow tick disarmed it -- handleTimer's reflow branch ran");

        const bool folderBefore = app.folderSearchEnabled, showBefore = app.showSearch,
                   editBefore = app.editMode;
        const std::wstring queryBefore = app.searchQuery;
        app.folderSearchEnabled = true;
        app.showSearch = true;
        app.editMode = false;
        app.searchQuery = L"zyxneedle";
        performSearch(app);
        const bool debounceArmed = postWindow.timerActive(TIMER_FOLDER_SEARCH);
        const bool found = pumpUntil([&app]() { return !app.folderResults.empty(); }, 5000);
        std::printf("post_folder armed=%d found=%d results=%d first=%ls\n",
                    debounceArmed ? 1 : 0, found ? 1 : 0, (int)app.folderResults.size(),
                    app.folderResults.empty() ? L"-" : app.folderResults[0].fileName.c_str());
        check(debounceArmed, "a folder query arms TIMER_FOLDER_SEARCH on the window");
        check(app.folderResults.size() == 1 && app.folderResults[0].fileName == L"b.md" &&
                  app.folderResults[0].totalMatches == 1,
              "and the scan's WM_APP_FOLDER_SEARCH delivered exactly the one sibling .md "
              "holding the query -- not the document itself, not the .txt");
        clearFolderSearch(app);
        app.folderSearchEnabled = folderBefore;
        app.showSearch = showBefore;
        app.editMode = editBefore;
        app.searchQuery = queryBefore;
        app.searchMatches.clear();

        // THE THIRD POSTED MESSAGE (2026-10-12): WM_APP_PANDOC_DONE, and this one is here for a
        // DEFECT rather than for a seam. src/pandoc.cpp's worker ends with
        // postWindowMessage(hwnd, WM_APP_PANDOC_DONE, ok ? 1 : 0, 0), and handlesPostedMessage
        // did not name that message, so the Qt sink refused it and the completion was dropped.
        // app.pandocRunning has two writers -- true when the worker starts, false in the catch
        // for a worker that FAILED to start -- so a conversion that ran left it true forever and
        // the guard in pandocExportFlow refused every later export in the session, silently.
        //
        // POSTED FROM A WORKER THREAD, because that is the only thread the real sender runs on:
        // the post sink's closure is the one piece of the Qt window a non-GUI thread touches, and
        // a post from this thread would exercise a path pandoc never takes.
        //
        // WHAT THIS CANNOT SEE, said here rather than implied: nothing below calls
        // pandocExportFlow or runs pandoc. The dialog is a real application-modal QFileDialog
        // with no test seam (include/file_dialog.h says a test cannot click one), so the
        // conversion half of the flow is unwitnessed and the claim here is about the completion
        // message's path -- from the post inward. The flag these checks read IS the one the
        // guard reads; the guard's own early return is not executed.
        {
            const bool pandocRunningBefore = app.pandocRunning;
            app.signalChips.clear();

            // Round one: a success. The flag is read BEFORE the post as well, because `false`
            // after is also what a flag nobody ever set reads.
            app.pandocRunning = true;
            const bool armedOnce = app.pandocRunning;
            bool postedOk = false;
            std::thread okWorker(
                [&postedOk]() { postedOk = postWindowMessage(tinta::PostedMessage::PandocDone, 1); });
            okWorker.join();
            const bool clearedOnce =
                pumpUntil([&app]() { return !app.pandocRunning; }, 2000);
            const size_t chipsAfterOk = app.signalChips.size();
            const std::wstring okText =
                app.signalChips.empty() ? std::wstring() : app.signalChips.back().text;
            const int okSeverity = app.signalChips.empty() ? -1 : app.signalChips.back().severity;
            const int okIcon = app.signalChips.empty() ? -1 : app.signalChips.back().icon;

            // Round two: a SECOND export in the same session, which is the whole regression. The
            // flag arms again and clears again, so the state pandocExportFlow's guard reads
            // admits it. wParam = 0 this time, which must produce the other chip -- one value
            // cannot yield both.
            app.signalChips.clear();
            app.pandocRunning = true;
            const bool armedTwice = app.pandocRunning;
            bool postedFail = false;
            std::thread failWorker([&postedFail]() {
                postedFail = postWindowMessage(tinta::PostedMessage::PandocDone, 0);
            });
            failWorker.join();
            const bool clearedTwice =
                pumpUntil([&app]() { return !app.pandocRunning; }, 2000);
            const std::wstring failText =
                app.signalChips.empty() ? std::wstring() : app.signalChips.back().text;
            const int failSeverity =
                app.signalChips.empty() ? -1 : app.signalChips.back().severity;
            const int failIcon = app.signalChips.empty() ? -1 : app.signalChips.back().icon;

            std::printf("pandoc_done posted=%d,%d armed=%d,%d "
                        "cleared=%d,%d chips=%d ok=[%ls] sev=%d icon=%d fail=[%ls] sev=%d "
                        "icon=%d\n",
                        postedOk ? 1 : 0,
                        postedFail ? 1 : 0, armedOnce ? 1 : 0, armedTwice ? 1 : 0,
                        clearedOnce ? 1 : 0, clearedTwice ? 1 : 0, (int)chipsAfterOk,
                        okText.c_str(), okSeverity, okIcon, failText.c_str(), failSeverity,
                        failIcon);

            check(postedOk && postedFail,
                  "postWindowMessage ACCEPTS both completions from a worker thread -- it returned "
                  "false for the whole of this defect, which is how the worker learned nothing");
            check(armedOnce && clearedOnce,
                  "a PandocDone post clears app.pandocRunning, the flag that was true for "
                  "the life of the process");
            check(chipsAfterOk == 1 && okText == tr(app, "toast.pandoc_ok") &&
                      okSeverity == SIG_SUCCESS && okIcon == SIGI_CHECK,
                  "wParam=1 pushes exactly one chip, the translated success toast, at "
                  "SIG_SUCCESS/SIGI_CHECK");
            check(armedTwice && clearedTwice,
                  "and a SECOND export in the same session clears it again -- the regression was "
                  "that every export after the first was refused by a flag nothing reset");
            check(failText == tr(app, "toast.pandoc_fail") && failSeverity == SIG_ERROR &&
                      failIcon == SIGI_WARNING,
                  "wParam=0 pushes the failure toast instead, at SIG_ERROR/SIGI_WARNING");

            app.signalChips.clear();
            app.pandocRunning = pandocRunningBefore;
        }

        std::filesystem::remove_all(postDir, dirEc);
    }

    // THE REPAINT GUARDS (slice 9). Nine callers asked `if (app.hwnd)` before requestRepaint,
    // which is null for the whole of a qinta run, so each changed App and drew nothing until
    // something else happened to repaint. They ask canRepaint now. Read through a COUNTING
    // sink rather than the window's update(), because update() leaves nothing a test can
    // count -- and exactly one per call, since each of these functions has one repaint and
    // an extra would be a second site nobody meant. Own window for the timer sink, for the
    // reason the post block gives.
    {
        QtAppWindow repaintWindow(app);
        repaintWindow.resize(800, 600);
        int repaints = 0;
        tinta::setRepaintSink([&repaints]() { ++repaints; });

        signalPush(app, SIG_INFO, SIGI_INFO, L"repaint probe");
        const int afterPush = repaints;

        // Hover: the chip's rect is written by paint, so it is set here instead. The timer is
        // disarmed first so the arm read below belongs to the hover and not to the push.
        App::SignalChip& chip = app.signalChips.back();
        chip.rect = tinta::RectF{10.0f, 10.0f, 210.0f, 50.0f};
        disarmTimer(TIMER_NOTIFICATION);
        const int beforeHover = repaints;
        signalMouseMove(app, 50.0f, 20.0f);
        const int hoverRepaints = repaints - beforeHover;
        const bool hoverArmed = repaintWindow.timerActive(TIMER_NOTIFICATION);
        app.signalHoverChip = 0;
        app.signalChips.clear();

        const bool tableBefore = app.tableEditActive;
        app.tableEditActive = true;
        app.tableEditText.clear();
        app.tableEditCaret = 0;
        const int beforeChar = repaints;
        tableEditChar(app, L'x');
        const int charRepaints = repaints - beforeChar;
        const bool charInserted = app.tableEditText == L"x";
        app.tableEditActive = tableBefore;
        app.tableEditText.clear();
        app.tableEditCaret = 0;

        const int themeBefore = app.currentThemeIndex;
        const int beforeTheme = repaints;
        applyTheme(app, (themeBefore + 1) % themeCount());
        const int themeRepaints = repaints - beforeTheme;
        applyTheme(app, themeBefore);

        std::printf("repaint_guards push=%d hover=%d hover_armed=%d char=%d theme=%d\n",
                    afterPush, hoverRepaints, hoverArmed ? 1 : 0, charRepaints, themeRepaints);
        check(afterPush == 1, "signalPush delivers exactly one repaint on the Qt stack");
        check(hoverRepaints == 1,
              "a signalMouseMove that changes the hovered chip delivers exactly one repaint");
        check(hoverArmed,
              "and it arms TIMER_NOTIFICATION -- the same guard held the hover animation out");
        check(charInserted && charRepaints == 1,
              "tableEditChar inserts the character and delivers exactly one repaint");
        check(themeRepaints == 1, "applyTheme delivers exactly one repaint");
        tinta::clearRepaintSink();
    }

    // DROPPED FILES (2026-10-12). src/input.cpp's handleDropFiles was written, declared, and
    // left with NO CALLER when 0b39ae6 deleted main_d2d.cpp's WM_DROPFILES case -- and nothing
    // in the tree called setAcceptDrops either, so a QWidget received no drag or drop event at
    // all. README advertises the feature twice and it worked on no platform in between.
    //
    // THE acceptDrops() CHECK IS ASSERTED ALONE, AND NOT FOR THE REASON THIS COMMENT FIRST
    // GAVE. It said every other check here would still pass on a build with no
    // setAcceptDrops, because QApplication::sendEvent reaches a handler whether the widget
    // accepts drops or not -- the setFocusPolicy argument, carried over by analogy. The
    // mutation refuted it: with setAcceptDrops(true) removed, ALL SIX checks in this block go
    // red and drops=0,0,0,0, because Qt gates drag and drop delivery on acceptDrops() even
    // for a synthetic event, which it does not do for keys.
    //
    // So the flag is covered either way and the separate assertion earns its place as a
    // DIAGNOSTIC instead: six reds have two possible causes -- the flag, or the handlers --
    // and this is the one check that says which. Recorded rather than quietly reworded,
    // because the conclusion survived while its reason did not, and the wrong reason is what
    // would have been reused on the next widget-attribute slice.
    //
    // Own window, for the reason the post block above gives: every probe window's destructor
    // clears all the sinks, and tabOpenPath repaints through one.
    {
        QtAppWindow dropWindow(app);
        dropWindow.resize(800, 600);

        const std::filesystem::path dropDir =
            std::filesystem::temp_directory_path() / "qinta_drop_probe";
        std::error_code dropEc;
        std::filesystem::remove_all(dropDir, dropEc);
        std::filesystem::create_directories(dropDir, dropEc);
        {
            std::ofstream one(dropDir / "dropped-one.md", std::ios::binary);
            one << "# dropped one\n\nThe first dropped document.\n";
            std::ofstream two(dropDir / "dropped-two.md", std::ios::binary);
            two << "# dropped two\n\nThe second, which must open behind the first.\n";
            std::ofstream three(dropDir / "dropped-three.md", std::ios::binary);
            three << "# dropped three\n\nThe multi-file round's second opener.\n";
            // UNSUPPORTED, and the point of it: isSupportedDropPath takes the document
            // extensions plus .txt, so this must be thrown away rather than opened. Dropped
            // BESIDE a supported file, so the exclusion is read as "acted on one of two" and
            // not as "the whole drop failed".
            std::ofstream exe(dropDir / "not-a-document.exe", std::ios::binary);
            exe << "MZ";
        }
        auto fileUrl = [&dropDir](const char* name) {
            return QUrl::fromLocalFile(
                QString::fromStdString((dropDir / name).string()));
        };
        // A drag or drop event the way Qt delivers one, for the reason sendWheel gives.
        //
        // A DROP IS PRECEDED BY ITS OWN DRAG ENTER, and that is fidelity rather than
        // convenience: the platform never delivers a Drop that no DragEnter opened, and Qt
        // holds drag state between the two. Measured the hard way -- without it, the FIRST
        // synthetic Drop was delivered and every later one was silently discarded, with
        // isAccepted() false and nothing opened, while a DragEnter over the identical payload
        // was still being accepted. That reads exactly like a broken dropEvent and it was a
        // broken test sequence (claim 18: a first-run red from a probe written this run is a
        // suspect instrument, not a verdict).
        auto sendDrag = [&dropWindow](QEvent::Type type, const QMimeData& mime) {
            const QPoint at(dropWindow.width() / 2, dropWindow.height() / 2);
            QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&dropWindow, &enter);
            if (type == QEvent::DragEnter) return enter.isAccepted();
            QDropEvent ev(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&dropWindow, &ev);
            return ev.isAccepted();
        };

        const bool accepts = dropWindow.acceptDrops();

        QMimeData oneFile;
        oneFile.setUrls({fileUrl("dropped-one.md")});
        const bool enterLocal = sendDrag(QEvent::DragEnter, oneFile);

        // THE TWO COUNTER-CASES. A handler that accepts everything passes the check above and
        // fails both of these, which is the only thing separating "it accepts a file drop"
        // from "it accepts".
        QMimeData remoteUrl;
        remoteUrl.setUrls({QUrl("http://127.0.0.1:9/not-a-local-file.md")});
        const bool enterRemote = sendDrag(QEvent::DragEnter, remoteUrl);
        QMimeData plainText;
        plainText.setText("just some dragged text");
        const bool enterText = sendDrag(QEvent::DragEnter, plainText);

        // Round one: a single supported file.
        const size_t tabsBefore = app.tabs.size();
        const bool droppedOne = sendDrag(QEvent::Drop, oneFile);
        const std::string currentAfterOne = app.currentFile;
        const size_t tabsAfterOne = app.tabs.size();

        // Round two: two supported files and one unsupported, in one drop. handleDropFiles
        // opens each supported path with the FIRST activating, and skips the rest -- so the
        // tab count moving by two rather than three is the exclusion reading.
        //
        // BOTH SUPPORTED PATHS ARE FRESH, and that is load-bearing rather than tidy. The first
        // version of this round re-dropped dropped-one.md from round one and asserted +2; it
        // read +1, because tabOpenPath switches to an already-open path instead of
        // duplicating it (src/tabs.cpp:197-205). The code was right and the assertion's
        // arithmetic was wrong -- so the exclusion check now measures only the skip, and the
        // already-open behaviour is round four's claim instead of a confound in this one.
        QMimeData mixedFiles;
        mixedFiles.setUrls({fileUrl("dropped-two.md"), fileUrl("not-a-document.exe"),
                            fileUrl("dropped-three.md")});
        const size_t tabsBeforeMixed = app.tabs.size();
        const bool droppedMixed = sendDrag(QEvent::Drop, mixedFiles);
        const size_t tabsAfterMixed = app.tabs.size();
        const std::string currentAfterMixed = app.currentFile;

        // Round three: ONLY unsupported paths. Nothing opens, and the event is still accepted
        // -- it carried a local file, which is all dragEnterEvent promised.
        QMimeData onlyExe;
        onlyExe.setUrls({fileUrl("not-a-document.exe")});
        const size_t tabsBeforeExe = app.tabs.size();
        const bool droppedExe = sendDrag(QEvent::Drop, onlyExe);
        const size_t tabsAfterExe = app.tabs.size();
        const std::string currentAfterExe = app.currentFile;

        // Round four: a file ALREADY OPEN. Found by round two's wrong assertion rather than
        // designed, and kept because it is the behaviour a user hits most -- dropping the
        // document you are already reading must not give you a second tab on it. The
        // discriminator is the pair: the tab count does not move AND the current document
        // becomes that file, which together separate "switched to the existing tab" from
        // "ignored the drop".
        app.activeTab = 0;
        const std::string currentBeforeReopen = app.currentFile;
        const size_t tabsBeforeReopen = app.tabs.size();
        const bool droppedReopen = sendDrag(QEvent::Drop, oneFile);
        const size_t tabsAfterReopen = app.tabs.size();
        const std::string currentAfterReopen = app.currentFile;

        // DIAGNOSTIC, kept: it is what separates "the payload is wrong" from "the delivery was
        // refused". A DragEnter over each payload runs droppedLocalPaths on exactly the data
        // the Drop carried, so if enter accepts and drop does not, the conversion is fine and
        // the event never reached the handler.
        std::printf("drop payloads urls=%d,%d,%d enter_again=%d,%d\n",
                    (int)oneFile.urls().size(), (int)mixedFiles.urls().size(),
                    (int)onlyExe.urls().size(),
                    sendDrag(QEvent::DragEnter, mixedFiles) ? 1 : 0,
                    sendDrag(QEvent::DragEnter, onlyExe) ? 1 : 0);

        auto leaf = [](const std::string& p) {
            return std::filesystem::path(p).filename().string();
        };
        std::printf("drop accepts=%d enter=local:%d,remote:%d,text:%d drops=%d,%d,%d,%d "
                    "tabs=%d->%d->%d->%d->%d current=one:%s,mixed:%s,exe:%s,"
                    "reopen:%s(from %s)\n",
                    accepts ? 1 : 0, enterLocal ? 1 : 0, enterRemote ? 1 : 0, enterText ? 1 : 0,
                    droppedOne ? 1 : 0, droppedMixed ? 1 : 0, droppedExe ? 1 : 0,
                    droppedReopen ? 1 : 0, (int)tabsBefore, (int)tabsAfterOne,
                    (int)tabsAfterMixed, (int)tabsAfterExe, (int)tabsAfterReopen,
                    leaf(currentAfterOne).c_str(), leaf(currentAfterMixed).c_str(),
                    leaf(currentAfterExe).c_str(), leaf(currentAfterReopen).c_str(),
                    leaf(currentBeforeReopen).c_str());

        check(accepts,
              "the window accepts drops -- without setAcceptDrops(true) Qt delivers neither "
              "handler, and measured: every check in this block goes red with it removed, so "
              "this one is the diagnostic that says the flag is the cause");
        check(enterLocal, "a drag carrying a local file is accepted");
        check(!enterRemote && !enterText,
              "and one carrying only a remote URL, or no URL at all, is NOT -- the "
              "counter-case for the check above");
        check(droppedOne && tabsAfterOne == tabsBefore + 1 &&
                  std::filesystem::path(currentAfterOne).filename() == "dropped-one.md",
              "dropping one supported file opens it as the current document, in a new tab");
        check(droppedMixed && tabsAfterMixed == tabsBeforeMixed + 2 &&
                  std::filesystem::path(currentAfterMixed).filename() == "dropped-two.md",
              "a drop of two supported files and one unsupported opens TWO tabs -- not three "
              "-- and the first supported one activates");
        // VACUOUS ON ITS OWN, and measured so: with the handleDropFiles call removed this
        // check stays GREEN, because "nothing opened" is exactly what a dead caller produces
        // too. It is load-bearing only beside the two checks above, which prove the handler is
        // reached at all -- and that pairing is the whole content of the claim.
        check(droppedExe && tabsAfterExe == tabsBeforeExe &&
                  currentAfterExe == currentAfterMixed,
              "a drop of only unsupported paths opens nothing and changes nothing, while the "
              "event is still accepted");
        check(droppedReopen && tabsAfterReopen == tabsBeforeReopen &&
                  std::filesystem::path(currentAfterReopen).filename() == "dropped-one.md" &&
                  currentAfterReopen != currentBeforeReopen,
              "dropping an already-open file switches to its tab instead of duplicating it -- "
              "the count holds AND the current document changes, which is what tells that "
              "apart from the drop being ignored");

        while (app.tabs.size() > tabsBefore) {
            tabCloseIndex(app, (int)app.tabs.size() - 1);
        }
        std::filesystem::remove_all(dropDir, dropEc);
    }

    // THE WINDOW CAPTION (2026-10-12). win32_api.h's setWindowTitle had a `(void)title;` body
    // -- on EVERY platform, since there is one window stack -- so all 13 updateWindowTitle
    // call sites computed a caption and dropped it. Two Qt files had worked around that with
    // QWidget::setWindowTitle calls of their own, in the opposite word order and with a full
    // path where updateWindowTitle puts a file name, and src/editor.cpp had a third copy for
    // the dirty marker. One computer now, reached through the window sink.
    //
    // THE FORMAT IS ASSERTED AS AN EXACT STRING, NOT A `contains`. The pre-existing check at
    // the tall-window run uses contains() and passed under BOTH formats, which is exactly why
    // it could not see this: `<path> - Qinta` and `Qinta - <name>` both contain the file name.
    // So each check below reads the whole caption, and the path case is caught by its own
    // exclusion -- the caption must not hold a separator from the fixture's parent directory.
    //
    // Own window, for the reason the post block gives.
    {
        QtAppWindow titleWindow(app);
        titleWindow.resize(800, 600);

        const std::filesystem::path titleDir =
            std::filesystem::temp_directory_path() / "qinta_title_probe";
        std::error_code titleEc;
        std::filesystem::remove_all(titleDir, titleEc);
        std::filesystem::create_directories(titleDir, titleEc);
        {
            std::ofstream a(titleDir / "caption-one.md", std::ios::binary);
            a << "# caption one\n";
            std::ofstream b(titleDir / "caption-two.md", std::ios::binary);
            b << "# caption two\n";
        }

        const bool fileBefore = !app.currentFile.empty();
        const std::string currentBefore = app.currentFile;
        const bool editBefore = app.editMode;
        const bool dirtyBefore = app.editorDirty;
        const size_t tabsAtStart = app.tabs.size();

        // ISC-1/2: the seam reaches the window at all. The counter is read beside the caption
        // because an unchanged caption and an uninstalled sink are the same string.
        const int countBefore = titleWindow.titleCount();
        check(loadDocumentFromPath(app, (titleDir / "caption-one.md").u8string()),
              "the caption probe's first document loaded");
        app.editMode = false;
        app.editorDirty = false;
        updateWindowTitle(app);
        const int countAfterOne = titleWindow.titleCount();
        const QString captionOne = titleWindow.windowTitle();

        // ISC-3: a tab switch retitles, and BOTH directions are read -- a caption set once and
        // never again passes a single-direction check.
        tabOpenPath(app, (titleDir / "caption-two.md").u8string(), true);
        const QString captionTwo = titleWindow.windowTitle();
        int backTo = -1;
        for (size_t i = 0; i < app.tabs.size(); i++) {
            if (app.tabs[i].path.find("caption-one.md") != std::string::npos ||
                ((int)i == app.activeTab &&
                 app.currentFile.find("caption-one.md") != std::string::npos)) {
                backTo = (int)i;
            }
        }
        if (backTo >= 0) tabActivate(app, backTo);
        const QString captionBack = titleWindow.windowTitle();

        // ISC-13: the dirty marker, which used to be written once by editor.cpp at the
        // transition and then wiped by the next retitle. The clean reading above is its
        // counter-case: a function that always prefixed `* ` would have failed captionOne.
        app.editMode = true;
        app.editorDirty = true;
        updateWindowTitle(app);
        const QString captionDirty = titleWindow.windowTitle();
        app.editorDirty = false;
        updateWindowTitle(app);
        const QString captionClean = titleWindow.windowTitle();

        // ISC-4: the quick note, whose name comes from the i18n table and not a literal.
        enterQuickNoteMode(app);
        const QString captionNote = titleWindow.windowTitle();
        const QString untitledExpected =
            QStringLiteral("Qinta - ") + QString::fromWCharArray(tr(app, "title.untitled"));

        std::printf("caption count=%d->%d one=[%s] two=[%s] back=[%s] dirty=[%s] clean=[%s] "
                    "note=[%s] expect_note=[%s]\n",
                    countBefore, countAfterOne, captionOne.toUtf8().constData(),
                    captionTwo.toUtf8().constData(), captionBack.toUtf8().constData(),
                    captionDirty.toUtf8().constData(), captionClean.toUtf8().constData(),
                    captionNote.toUtf8().constData(), untitledExpected.toUtf8().constData());

        check(countAfterOne == countBefore + 1,
              "updateWindowTitle delivers exactly one caption through the window sink -- the "
              "seam discarded its argument on every platform until this slice");
        check(captionOne == QStringLiteral("Qinta - caption-one.md"),
              "and the caption is updateWindowTitle's format exactly: 'Qinta - ' then the FILE "
              "NAME");
        check(!captionOne.contains(QStringLiteral("qinta_title_probe")) &&
                  !captionOne.contains(QLatin1Char('\\')) &&
                  !captionOne.contains(QLatin1Char('/')),
              "and it holds no directory component -- the exclusion that catches the full-path "
              "format the two deleted Qt sites used");
        check(captionTwo == QStringLiteral("Qinta - caption-two.md") &&
                  captionBack == QStringLiteral("Qinta - caption-one.md") &&
                  captionTwo != captionBack,
              "a tab switch retitles in BOTH directions -- one direction alone cannot tell a "
              "live caption from one set once at launch");
        check(captionDirty == QStringLiteral("Qinta - * caption-one.md"),
              "a dirty buffer prefixes the marker, which editor.cpp used to write once at the "
              "transition and the next retitle silently wiped");
        check(captionClean == QStringLiteral("Qinta - caption-one.md"),
              "and a clean buffer does NOT -- the counter-case for the marker");
        check(captionNote == untitledExpected,
              "a quick note titles from the i18n table's title.untitled, with no backing file");

        // The caption's words come from title.plain / title.dirty, so an override reaches it.
        // They were literals beside two keys nothing read; English output is unchanged above,
        // and only an override can tell a caption built from the keys from one that matches
        // them. languages.ini in the pinned config home, then reloaded away again.
        {
            const std::wstring iniPath =
                tintaConfigDir() + platform::pathSeparator() + L"languages.ini";
            {
                std::ofstream ini{std::filesystem::path(iniPath)};
                ini << "[en]\ntitle.plain=Probe :: %1$s\ntitle.dirty=Probe :: * %1$s\n";
            }
            const int languageBefore = app.currentLanguageIndex;
            app.currentLanguageIndex = LANG_INDEX_EN;
            loadLanguageOverrides();
            app.editMode = false;
            app.currentFile = (titleDir / "caption-one.md").string();
            updateWindowTitle(app);
            const QString overridden = titleWindow.windowTitle();
            app.editMode = true;
            app.editorDirty = true;
            updateWindowTitle(app);
            const QString overriddenDirty = titleWindow.windowTitle();
            app.editorDirty = false;
            std::filesystem::remove(std::filesystem::path(iniPath));
            loadLanguageOverrides();
            app.currentLanguageIndex = languageBefore;
            std::printf("caption override=[%s] dirty=[%s]\n", overridden.toUtf8().constData(),
                        overriddenDirty.toUtf8().constData());
            check(overridden == QStringLiteral("Probe :: caption-one.md") &&
                      overriddenDirty == QStringLiteral("Probe :: * caption-one.md"),
                  "a languages.ini override of title.plain and title.dirty reaches the caption");
        }

        // Restore. The quick note cleared currentFile and entered edit mode.
        app.editMode = editBefore;
        app.editorDirty = dirtyBefore;
        app.currentFile = fileBefore ? currentBefore : std::string();
        while (app.tabs.size() > tabsAtStart) {
            tabCloseIndex(app, (int)app.tabs.size() - 1);
        }
        std::filesystem::remove_all(titleDir, titleEc);
    }

    // MIDDLE-CLICK AND THE MOUSE GRAB (2026-10-12), the parity list's items 4 and 5. Taken
    // together because they are one region of the mouse path.
    //
    // tabStripMouseDown has had a `bool middle` since the strip was written, closing the tab
    // under the press, and its ONE caller passed a hardcoded false -- while mousePressEvent
    // handed anything that was not LeftButton to QWidget. So the README's middle-click-to-
    // close worked on no platform. And releaseMouseIfCaptured was `(void)hwnd;`, which is
    // correct at five of its six sites (inside a mouse-up, where Qt's implicit grab is ending
    // anyway) and wrong at the sixth: tabs.cpp's tabDragCancel, reached from the Esc branch of
    // handleKeyDown. A keystroke does not end an implicit grab, so Esc aborted the drag's
    // state and left the widget holding the mouse.
    {
        QtAppWindow mouseWindow(app);
        mouseWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        mouseWindow.resize(900, 600);

        const std::filesystem::path mdir =
            std::filesystem::temp_directory_path() / "qinta_middle_probe";
        std::error_code mec;
        std::filesystem::remove_all(mdir, mec);
        std::filesystem::create_directories(mdir, mec);
        for (const char* n : {"mid-one.md", "mid-two.md", "mid-three.md"}) {
            std::ofstream f(mdir / n, std::ios::binary);
            f << "# " << n << "\n";
        }
        const size_t tabsAtStart = app.tabs.size();
        for (const char* n : {"mid-one.md", "mid-two.md", "mid-three.md"}) {
            tabOpenPath(app, (mdir / n).u8string(), true);
        }
        // The strip's hit rects come from renderTabStrip, so a paint has to happen before a
        // press can be aimed at a tab. Aiming by arithmetic instead would assert this test's
        // own layout maths rather than the strip's.
        grabWindow(mouseWindow);

        // A press whose accepted state is readable -- sendMouse above returns nothing, and
        // "consumed only where it acted" is half of this claim.
        // PRESS AND RELEASE, because a click is both and the difference is load-bearing here:
        // tabStripMouseDown sets App::swallowNextMouseUp, so a press delivered without its
        // release leaves that flag set and the NEXT release anywhere in this block is
        // swallowed. Measured -- it cost one confident red further down.
        auto pressButton = [&mouseWindow](Qt::MouseButton button, QPointF local) {
            const QPointF global(mouseWindow.mapToGlobal(local.toPoint()));
            QMouseEvent press(QEvent::MouseButtonPress, local, global, button, button,
                              Qt::NoModifier);
            QApplication::sendEvent(&mouseWindow, &press);
            const bool accepted = press.isAccepted();
            QMouseEvent release(QEvent::MouseButtonRelease, local, global, button, Qt::NoButton,
                                Qt::NoModifier);
            QApplication::sendEvent(&mouseWindow, &release);
            return accepted;
        };
        auto leafOf = [](const std::string& p) {
            return std::filesystem::path(p).filename().string();
        };

        const float stripMid = chromeTopHeight(app) * 0.5f;
        // The middle tab of the three, read from the strip's own hit list.
        float targetX = -1.0f;
        std::string targetLeaf;
        for (const auto& hit : app.tabHits) {
            if (hit.index < 0) continue;  // -2 plus, -3 chevron
            const std::string leaf =
                (hit.index == app.activeTab) ? leafOf(app.currentFile)
                                             : leafOf(app.tabs[(size_t)hit.index].path);
            if (leaf == "mid-two.md") {
                // Left of any close button, so the press is on the TAB and not its X -- the
                // close rect would close the same tab and make the reading ambiguous.
                targetX = hit.rect.left + 8.0f;
                targetLeaf = leaf;
            }
        }

        // --- item 4: middle-click closes, and only on the strip -------------------------
        const size_t tabsBeforeMid = app.tabs.size();
        const bool midAccepted =
            targetX >= 0.0f && pressButton(Qt::MiddleButton, QPointF(targetX, stripMid));
        const size_t tabsAfterMid = app.tabs.size();
        bool twoStillOpen = false;
        for (size_t i = 0; i < app.tabs.size(); i++) {
            const std::string leaf = ((int)i == app.activeTab) ? leafOf(app.currentFile)
                                                               : leafOf(app.tabs[i].path);
            if (leaf == "mid-two.md") twoStillOpen = true;
        }

        // THE COUNTER-CASE: below the strip. A handler that closed on any middle press passes
        // the check above and fails this one, and it is also the `return 0` / `break`
        // distinction -- an unconsumed press must be left to the widget.
        const size_t tabsBeforeBelow = app.tabs.size();
        const bool belowAccepted =
            pressButton(Qt::MiddleButton, QPointF(200.0f, chromeTopHeight(app) + 80.0f));
        const size_t tabsAfterBelow = app.tabs.size();

        // And the LEFT button on the strip must still activate rather than close.
        float leftTargetX = -1.0f;
        int leftTargetIndex = -1;
        for (const auto& hit : app.tabHits) {
            if (hit.index >= 0) {
                leftTargetX = hit.rect.left + 8.0f;
                leftTargetIndex = hit.index;
                break;
            }
        }
        const size_t tabsBeforeLeft = app.tabs.size();
        if (leftTargetX >= 0.0f) pressButton(Qt::LeftButton, QPointF(leftTargetX, stripMid));
        const size_t tabsAfterLeft = app.tabs.size();
        const int activeAfterLeft = app.activeTab;

        // --- item 5: Esc during a tab drag gives the mouse back -------------------------
        //
        // The grab is taken explicitly here because a SENT press does not create Qt's
        // implicit grab -- only a real platform press does. That is the honest shape: the
        // claim is about what tabDragCancel does to a grab that exists, and the pre-reading
        // below is what stops "no grabber after" from being a grab that never happened.
        app.tabDragIndex = 0;
        app.tabDragging = true;
        mouseWindow.grabMouse();
        const bool grabbedBefore = QWidget::mouseGrabber() == &mouseWindow;
        sendKey(mouseWindow, Qt::Key_Escape);
        const bool grabbedAfter = QWidget::mouseGrabber() == &mouseWindow;
        const int dragAfterEsc = app.tabDragIndex;

        // THE GUARD, which is what makes routing all six release sites through one seam safe
        // rather than a risk: with ANOTHER widget holding the grab, a delivery must not steal
        // it. Without this clause a release from a bracketed site could cut short a drag that
        // belongs to a sibling widget.
        QWidget other;
        other.setAttribute(Qt::WA_DontShowOnScreen, true);
        other.resize(10, 10);
        other.grabMouse();
        const bool otherHasIt = QWidget::mouseGrabber() == &other;
        const bool stolen = tinta::deliverReleaseMouse();
        const bool otherStillHasIt = QWidget::mouseGrabber() == &other;
        other.releaseMouse();

        std::printf("midclick target=%s accepted=%d tabs=%d->%d two_open=%d | below "
                    "accepted=%d tabs=%d->%d | left tabs=%d->%d active=%d | esc grabbed=%d->%d "
                    "drag=%d | guard other=%d stolen=%d other_kept=%d\n",
                    targetLeaf.empty() ? "-" : targetLeaf.c_str(), midAccepted ? 1 : 0,
                    (int)tabsBeforeMid, (int)tabsAfterMid, twoStillOpen ? 1 : 0,
                    belowAccepted ? 1 : 0, (int)tabsBeforeBelow, (int)tabsAfterBelow,
                    (int)tabsBeforeLeft, (int)tabsAfterLeft, activeAfterLeft,
                    grabbedBefore ? 1 : 0, grabbedAfter ? 1 : 0, dragAfterEsc,
                    otherHasIt ? 1 : 0, stolen ? 1 : 0, otherStillHasIt ? 1 : 0);

        check(!targetLeaf.empty(),
              "the strip's hit list named the middle tab, so the press below is aimed at it "
              "and not at this test's arithmetic");
        check(midAccepted && tabsAfterMid == tabsBeforeMid - 1 && !twoStillOpen,
              "a MIDDLE press on a tab closes that tab and consumes the event -- "
              "tabStripMouseDown's `middle` had one caller passing a hardcoded false");
        check(!belowAccepted && tabsAfterBelow == tabsBeforeBelow,
              "and a middle press BELOW the strip closes nothing and is left to the widget, "
              "which is WinMain's `break` against its `return 0`");
        check(tabsAfterLeft == tabsBeforeLeft && activeAfterLeft == leftTargetIndex,
              "a LEFT press on a tab still activates it and closes nothing");
        check(grabbedBefore && !grabbedAfter && dragAfterEsc == -1,
              "Esc during a tab drag releases the mouse grab AND clears the drag -- the grab "
              "is read before as well as after, so this cannot be a grab that never happened");
        check(otherHasIt && !stolen && otherStillHasIt,
              "and a release delivered while ANOTHER widget holds the grab does not steal it "
              "-- the GetCapture()==hwnd guard, which is what makes one seam safe for all six "
              "call sites");

        // THE RISK THIS SLICE TAKES, PROBED DIRECTLY. Routing releaseMouseIfCaptured through
        // a live seam makes the FIVE bracketed sites -- the ones inside handleMouseUp -- fire
        // a real QWidget::releaseMouse for the first time. The drag-select run earlier in this
        // file covers the behaviour, but it cannot cover THIS: a sent press creates no implicit
        // grab, so the guard refuses and the release never executes there. A grab taken by hand
        // is what puts a real one in front of the mouse-up.
        //
        // Asserted as a PAIR, because either half alone is satisfied by the wrong thing: the
        // selection must still form (so the release did not cut the drag short) AND the grab
        // must be gone afterwards (so the release actually ran rather than being refused).
        {
            const bool selBefore = app.hasSelection;
            app.hasSelection = false;
            // MEASURED, not assumed. The first version of this block read selecting=1 after
            // the release and the release had provably run (the grab was gone), which looks
            // exactly like the new seam breaking the drag-end path. It was the test: the
            // tab-strip presses above are presses with no matching release, and
            // tabStripMouseDown sets App::swallowNextMouseUp, so handleMouseUp returned early
            // on the first release it saw and never cleared `selecting`. A real click is a
            // press AND a release; the presses above now send both, and this reading is kept
            // as the discriminator that named the cause.
            const bool swallowPending = app.swallowNextMouseUp;
            const QPointF dragFrom(60.0, chromeTopHeight(app) + 120.0f);
            const QPointF dragTo(360.0, chromeTopHeight(app) + 120.0f);
            mouseWindow.grabMouse();
            const bool grabHeldForDrag = QWidget::mouseGrabber() == &mouseWindow;
            sendMouse(mouseWindow, QEvent::MouseButtonPress, dragFrom, Qt::LeftButton,
                      Qt::LeftButton);
            sendMouse(mouseWindow, QEvent::MouseMove, dragTo, Qt::NoButton, Qt::LeftButton);
            sendMouse(mouseWindow, QEvent::MouseButtonRelease, dragTo, Qt::LeftButton,
                      Qt::NoButton);
            const bool grabGoneAfterUp = QWidget::mouseGrabber() != &mouseWindow;
            const bool selecting = app.selecting;
            std::printf("bracketed grab_held=%d grab_gone=%d selecting=%d swallow_pending=%d\n",
                        grabHeldForDrag ? 1 : 0, grabGoneAfterUp ? 1 : 0, selecting ? 1 : 0,
                        swallowPending ? 1 : 0);
            check(grabHeldForDrag && grabGoneAfterUp && !selecting,
                  "a mouse-up WITH a real grab held releases it and still ends the drag "
                  "cleanly -- the five bracketed release sites, which the sent-event drag "
                  "run cannot reach because sendEvent creates no implicit grab");
            if (QWidget::mouseGrabber() == &mouseWindow) mouseWindow.releaseMouse();
            app.hasSelection = selBefore;
        }

        while (app.tabs.size() > tabsAtStart) {
            tabCloseIndex(app, (int)app.tabs.size() - 1);
        }
        std::filesystem::remove_all(mdir, mec);
        // THE RESIDUE READING, AND WHY IT IS ASSERTED RATHER THAN JUST PRINTED. This block
        // left ONE DRAINING signal chip, and a draining chip animates: it turned the overlay
        // frame run further down this file red on LINUX only, where two grabs of an unchanged
        // window then differed over a 277x66 region instead of the 2 px that run's comment
        // records. Its `reverts` check reads maskArea < 2500 and the mask was 18282. Windows
        // read area 0 through the same change, so this was invisible on the host it was
        // written on and CI is what caught it -- the frame run's own revert diffs were a
        // perfect 0 and 0, so nothing about it looked like a drift problem.
        //
        // A test that leaves animating state is a test that breaks a LATER one, so the
        // cleanup is a claim here rather than a tidy-up: assert the chips are gone, so this
        // cannot regress silently into somebody else's run.
        std::printf("midclick_residue chips=%d needs_ticks=%d chip=[%ls] selecting=%d "
                    "has_sel=%d edit=%d over_text=%d mouse=%d,%d active=%d tabs=%d\n",
                    (int)app.signalChips.size(), signalsNeedTicks(app) ? 1 : 0,
                    app.signalChips.empty() ? L"-" : app.signalChips.back().text.c_str(),
                    app.selecting ? 1 : 0, app.hasSelection ? 1 : 0, app.editMode ? 1 : 0,
                    app.overText ? 1 : 0, app.mouseX, app.mouseY, app.activeTab,
                    (int)app.tabs.size());
        // THE ACTIVE CHIPS ONLY. The tray is parked and static, so it does not animate and is
        // not what broke the frame run; a later block clears it for its own reasons. Narrow on
        // purpose -- a cleanup that resets more than it dirtied is the next test's surprise.
        //
        // The chip's text named the cause: "Press Esc to exit edit mode", pushed by THIS
        // block's own Esc keystroke -- the one testing the grab release.
        app.signalChips.clear();
        app.signalHoverChip = 0;
        disarmTimer(TIMER_NOTIFICATION);
        check(app.signalChips.empty() && !signalsNeedTicks(app),
              "this block leaves no draining signal chip -- one did, and it turned the overlay "
              "frame run red on Linux by animating between two grabs of an unchanged window");
    }

    // WM_CLOSE (slice 10). Five portable senders post it -- File > Exit, closing the start
    // page, dragging off the last tab, the unsaved-changes dialog resuming a close -- and
    // the post sink used to refuse it, so each did nothing on qinta. And the window had no
    // closeEvent, so its title bar discarded a dirty buffer without asking. Both now reach
    // windowCloseMayProceed, WinMain's WM_CLOSE body. Own window, for the post block's
    // reason; quit-on-last-window off, so a close that works does not queue an app quit.
    {
        const bool quitBefore = QApplication::quitOnLastWindowClosed();
        QApplication::setQuitOnLastWindowClosed(false);
        const bool addedTab = app.tabs.empty();
        if (addedTab) app.tabs.emplace_back();
        const int activeBefore = app.activeTab;
        app.activeTab = 0;
        const bool editBefore = app.editMode;
        const bool dirtyBefore = app.editorDirty;

        QtAppWindow closeWindow(app);
        closeWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        closeWindow.resize(800, 600);

        // Clean: a posted Close is accepted, waits for the queue, then closes.
        closeWindow.show();
        const bool cleanPosted = postWindowMessage(tinta::PostedMessage::Close);
        const bool visibleBeforeQueue = closeWindow.isVisible();
        QApplication::processEvents();
        const bool cleanClosed = !closeWindow.isVisible();

        // Dirty: close() is refused and opens the dialog on the dirty tab.
        closeWindow.show();
        app.editMode = true;
        app.editorDirty = true;
        const bool dirtyClose = closeWindow.close();
        const bool dialogOpened = app.confirmExitPending && app.pendingWindowClose &&
                                  closeWindow.isVisible();
        // With the dialog up, a posted close only re-marks the pending close.
        app.pendingWindowClose = false;
        postWindowMessage(tinta::PostedMessage::Close);
        QApplication::processEvents();
        const bool behindDialog = closeWindow.isVisible() && app.pendingWindowClose;
        // Keep editing cancels the whole close.
        confirmExitAction(app, 3);
        const bool cancelled = !app.confirmExitPending && !app.pendingWindowClose &&
                               closeWindow.isVisible();
        // Resolved, the next close goes through.
        app.editMode = false;
        app.editorDirty = false;
        const bool resolvedClose = closeWindow.close() && !closeWindow.isVisible();

        std::printf("window_close clean_posted=%d queued=%d clean_closed=%d dirty_close=%d "
                    "dialog=%d behind_dialog=%d cancelled=%d resolved=%d\n",
                    cleanPosted, visibleBeforeQueue, cleanClosed, dirtyClose, dialogOpened,
                    behindDialog, cancelled, resolvedClose);
        check(cleanPosted && visibleBeforeQueue,
              "the post sink takes WM_CLOSE and queues it rather than closing mid-sender");
        check(cleanClosed, "and a clean window closes once the queue runs");
        check(!dirtyClose && dialogOpened,
              "a dirty buffer refuses close() and opens the unsaved-changes dialog");
        check(behindDialog, "with the dialog open, a posted WM_CLOSE only marks the close pending");
        check(cancelled, "Keep editing cancels the pending close and the window stays");
        check(resolvedClose, "once resolved, close() goes through");

        app.editMode = editBefore;
        app.editorDirty = dirtyBefore;
        app.escPressedOnce = false;
        app.activeTab = activeBefore;
        if (addedTab) app.tabs.clear();
        QApplication::setQuitOnLastWindowClosed(quitBefore);
    }

    // LOAD BEFORE WRITE (slice 11). qinta wrote the four theme fields -- the theme picker's
    // persistThemeChoice -- without ever loading them, so on Windows a theme pick put App's
    // defaults over tinta's saved light/dark pair. It loads them now, and a clean close saves
    // them plus the geometry. Every read below goes through loadSettings() on the pinned home.
    {
        // applySavedTheme: the four fields, and the start theme they select. Index chosen at
        // runtime as the first LIGHT theme past slot 0, so darkMode has to move off the
        // default Midnight's true and the fallback slot cannot pass for a mapped index.
        int lightIndex = -1;
        for (int i = 1; i < themeCount() && lightIndex < 0; ++i) {
            if (!themeAt(i).isDark) lightIndex = i;
        }
        // Follow-system stays OFF here, so the saved index is what starts; the follow-on start
        // is slice 12's block below.
        App themeApp;
        Settings themed;
        themed.themeIndex = lightIndex;
        themed.followSystemTheme = false;
        themed.lightThemeIndex = themeApp.lightThemeIndex + 1;
        themed.darkThemeIndex = themeApp.darkThemeIndex + 2;
        const bool darkBefore = themeApp.darkMode;
        tinta::applySavedTheme(themeApp, themed, true);
        const bool mapped = lightIndex > 0 && themeApp.currentThemeIndex == lightIndex &&
                            themeApp.theme.name == themeAt(lightIndex).name &&
                            !themeApp.darkMode && darkBefore &&
                            themeApp.followSystemTheme == themed.followSystemTheme &&
                            themeApp.lightThemeIndex == themed.lightThemeIndex &&
                            themeApp.darkThemeIndex == themed.darkThemeIndex;
        themed.themeIndex = themeCount();
        tinta::applySavedTheme(themeApp, themed, true);
        const bool clampHigh = themeApp.currentThemeIndex == 0;
        themeApp.currentThemeIndex = lightIndex;
        themed.themeIndex = -1;
        tinta::applySavedTheme(themeApp, themed, true);
        const bool clampLow = themeApp.currentThemeIndex == 0 &&
                              themeApp.theme.name == themeAt(0).name;

        // The close save. A settings.ini is seeded first with a zoomFactor App does not hold.
        // Until 2026-10-11 this asserted the file's 1.75 SURVIVED, because the launch never loaded
        // zoom and a full save would have put App's default over it. The launch loads every field
        // now (applySavedPreferences), so the close writes App's zoom, as WinMain's did, and the
        // seeded 1.75 is what proves the value came from App rather than from the file.
        const bool quitBefore = QApplication::quitOnLastWindowClosed();
        QApplication::setQuitOnLastWindowClosed(false);
        const bool addedTab = app.tabs.empty();
        if (addedTab) app.tabs.emplace_back();
        const int activeBefore = app.activeTab;
        app.activeTab = 0;
        const int themeBefore = app.currentThemeIndex;
        const bool followBefore = app.followSystemTheme;
        const int lightBefore = app.lightThemeIndex;
        const int darkIndexBefore = app.darkThemeIndex;

        Settings seed;
        seed.zoomFactor = 1.75f;
        seed.themeIndex = 1;
        saveSettings(seed);
        app.currentThemeIndex = 2;
        app.followSystemTheme = !seed.followSystemTheme;
        app.lightThemeIndex = seed.lightThemeIndex + 3;
        app.darkThemeIndex = seed.darkThemeIndex + 4;

        QtAppWindow saveWindow(app);
        saveWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        tinta::applyPlacement(saveWindow, tinta::WindowRect{140, 160, 820, 610});
        tinta::showPlaced(saveWindow, false);
        const bool shownPlain = saveWindow.isVisible() && !saveWindow.isMaximized();
        const QPoint savePos = saveWindow.pos();
        const bool cleanClose = saveWindow.close();
        const Settings afterClean = loadSettings();
        const bool themeSaved = afterClean.themeIndex == 2 &&
                                afterClean.followSystemTheme == app.followSystemTheme &&
                                afterClean.lightThemeIndex == app.lightThemeIndex &&
                                afterClean.darkThemeIndex == app.darkThemeIndex;
        const bool geometrySaved = afterClean.windowX == savePos.x() &&
                                   afterClean.windowY == savePos.y() && savePos.x() == 140 &&
                                   savePos.y() == 160 && afterClean.windowWidth == 820 &&
                                   afterClean.windowHeight == 610 && !afterClean.windowMaximized;
        const bool zoomKept = app.zoomFactor != 1.75f && afterClean.zoomFactor == app.zoomFactor;

        // A refused close writes nothing: the file's bytes are compared, not its fields.
        auto readSettingsFile = []() {
            std::ifstream in(std::filesystem::path(getSettingsPath()), std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>());
        };
        const std::string bytesBefore = readSettingsFile();
        tinta::showPlaced(saveWindow, false);
        app.currentThemeIndex = 3;
        app.editMode = true;
        app.editorDirty = true;
        const bool dirtyClose = saveWindow.close();
        const bool dirtyUnwritten = !dirtyClose && !bytesBefore.empty() &&
                                    readSettingsFile() == bytesBefore;
        confirmExitAction(app, 3);
        app.editMode = false;
        app.editorDirty = false;

        // Maximized: the flag is saved and the restore rectangle on disk is left alone,
        // because the maximized widget's rectangle is not one to restore to.
        tinta::showPlaced(saveWindow, true);
        const bool shownMaximized = saveWindow.isMaximized();
        saveWindow.close();
        const Settings afterMax = loadSettings();
        const bool maxSaved = afterMax.windowMaximized && afterMax.windowX == 140 &&
                              afterMax.windowY == 160 && afterMax.windowWidth == 820 &&
                              afterMax.windowHeight == 610 && afterMax.themeIndex == 3;

        std::printf("settings_lbw light_index=%d mapped=%d clamp_high=%d clamp_low=%d "
                    "shown=%d clean_close=%d theme=%d geometry=%d,%d %dx%d max=%d zoom=%.2f "
                    "dirty_close=%d dirty_unwritten=%d shown_max=%d max_saved=%d\n",
                    lightIndex, mapped, clampHigh, clampLow, shownPlain, cleanClose, themeSaved,
                    afterClean.windowX, afterClean.windowY, afterClean.windowWidth,
                    afterClean.windowHeight, afterClean.windowMaximized, afterClean.zoomFactor,
                    dirtyClose, dirtyUnwritten, shownMaximized, maxSaved);
        check(mapped, "applySavedTheme maps all four fields and draws the saved theme, "
                      "darkMode read off it");
        check(clampHigh, "a saved index past the theme registry starts on slot 0, as in WinMain");
        check(clampLow, "and so does a negative one, with the theme drawn from the same slot");
        check(shownPlain, "showPlaced(false) shows the window unmaximized");
        check(cleanClose && themeSaved, "a clean close saves the four theme fields");
        check(geometrySaved, "and the geometry, as applyPlacement's inverse: frame pos, client size");
        check(zoomKept, "and writes the zoom App holds over the file's, now that the launch loads it");
        check(dirtyUnwritten, "a close refused for a dirty buffer leaves settings.ini byte-identical");
        check(shownMaximized && maxSaved,
              "a maximized close saves the flag and leaves the restore rectangle as it was");

        app.currentThemeIndex = themeBefore;
        app.followSystemTheme = followBefore;
        app.lightThemeIndex = lightBefore;
        app.darkThemeIndex = darkIndexBefore;
        app.escPressedOnce = false;
        app.activeTab = activeBefore;
        if (addedTab) app.tabs.clear();
        QApplication::setQuitOnLastWindowClosed(quitBefore);
    }

    // FOLLOW-SYSTEM (slice 12). qinta loaded followSystemTheme and never acted on it: a
    // follow-on start drew the saved index, and the two in-app toggles post WM_SETTINGCHANGE
    // "ImmersiveColorSet", which the post sink refused. The switch is theme_apply.h's one body,
    // shared with WinMain until 0b39ae6; the reading is qt_launch's systemPrefersLight.
    {
        // A light and a dark theme past slot 0, and slot 0 (Paper, light) as a third index
        // neither of them is, so a switch cannot be mistaken for staying put.
        int lightIdx = -1;
        int darkIdx = -1;
        for (int i = 1; i < themeCount(); ++i) {
            if (!themeAt(i).isDark && lightIdx < 0) lightIdx = i;
            if (themeAt(i).isDark && darkIdx < 0) darkIdx = i;
        }
        const int otherIdx = 0;

        // The start: with follow on, the reading picks, not the saved index.
        App startApp;
        Settings follow;
        follow.followSystemTheme = true;
        follow.themeIndex = otherIdx;
        follow.lightThemeIndex = lightIdx;
        follow.darkThemeIndex = darkIdx;
        tinta::applySavedTheme(startApp, follow, true);
        const bool startLight = startApp.followSystemTheme &&
                                startApp.currentThemeIndex == lightIdx && !startApp.darkMode;
        tinta::applySavedTheme(startApp, follow, false);
        const bool startDark = startApp.currentThemeIndex == darkIdx && startApp.darkMode;
        follow.darkThemeIndex = themeCount();
        tinta::applySavedTheme(startApp, follow, false);
        const bool startClamped = startApp.currentThemeIndex == 0;

        const int themeBefore = app.currentThemeIndex;
        const bool followBefore = app.followSystemTheme;
        const int lightBefore = app.lightThemeIndex;
        const int darkIndexBefore = app.darkThemeIndex;
        app.lightThemeIndex = lightIdx;
        app.darkThemeIndex = darkIdx;
        const bool picks = followThemeIndex(app, true) == lightIdx &&
                           followThemeIndex(app, false) == darkIdx;

        // Follow off: nothing moves and nothing is written.
        Settings seed;
        seed.themeIndex = otherIdx;
        saveSettings(seed);
        app.followSystemTheme = false;
        app.currentThemeIndex = otherIdx;
        const bool offSwitched = followSystemAppearance(app, false);
        const bool offInert = !offSwitched && app.currentThemeIndex == otherIdx &&
                              loadSettings().themeIndex == otherIdx;

        // Follow on, already showing the wanted theme: no switch.
        app.followSystemTheme = true;
        applyTheme(app, darkIdx);
        const bool sameSwitched = followSystemAppearance(app, false);
        const bool sameInert = !sameSwitched && app.currentThemeIndex == darkIdx &&
                               loadSettings().themeIndex == otherIdx;

        // Follow on, a different reading: switch, and persist through the pinned file.
        const bool switched = followSystemAppearance(app, true);
        const Settings afterSwitch = loadSettings();
        const bool persisted = switched && app.currentThemeIndex == lightIdx && !app.darkMode &&
                               afterSwitch.themeIndex == lightIdx &&
                               afterSwitch.followSystemTheme &&
                               afterSwitch.lightThemeIndex == lightIdx &&
                               afterSwitch.darkThemeIndex == darkIdx;

        // The in-app toggle's post, through a real window's post sink: accepted, queued, then
        // answered with this arm's own reading. A different settings area is left alone.
        bool posted = false;
        bool queued = false;
        bool applied = false;
        bool otherAreaIgnored = false;
        const bool reading = tinta::systemPrefersLight();
        {
            QtAppWindow followWindow(app);
            followWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
            followWindow.resize(800, 600);
            applyTheme(app, otherIdx);
            posted = postWindowMessage(tinta::PostedMessage::SettingChange, 0,
                                       reinterpret_cast<std::intptr_t>(L"ImmersiveColorSet"));
            queued = app.currentThemeIndex == otherIdx;
            QApplication::processEvents();
            applied = app.currentThemeIndex == (reading ? lightIdx : darkIdx) &&
                      loadSettings().themeIndex == app.currentThemeIndex;
            applyTheme(app, otherIdx);
            postWindowMessage(tinta::PostedMessage::SettingChange, 0,
                              reinterpret_cast<std::intptr_t>(L"Policy"));
            QApplication::processEvents();
            otherAreaIgnored = app.currentThemeIndex == otherIdx;
        }

        std::printf("follow_system light=%d dark=%d start_light=%d start_dark=%d "
                    "start_clamped=%d picks=%d off_inert=%d same_inert=%d persisted=%d "
                    "posted=%d queued=%d reading=%d applied=%d other_area=%d\n",
                    lightIdx, darkIdx, startLight, startDark, startClamped, picks, offInert,
                    sameInert, persisted, posted, queued, reading, applied, otherAreaIgnored);
        check(lightIdx > 0 && darkIdx > 0, "the registry has a light and a dark theme past slot 0");
        check(startLight && startDark,
              "a follow-on start draws the theme the reading picks, not the saved index");
        check(startClamped, "and an out-of-range picked index still starts on slot 0");
        check(picks, "followThemeIndex picks the saved light or dark preference");
        check(offInert, "follow off: the system reading switches nothing and writes nothing");
        check(sameInert, "follow on, wanted theme already showing: no switch, no write");
        check(persisted, "follow on, a new reading: switch, and persist the choice");
        check(posted && queued,
              "the post sink takes WM_SETTINGCHANGE and queues it rather than switching mid-sender");
        check(applied, "and applies ImmersiveColorSet with this arm's own reading");
        check(otherAreaIgnored, "a WM_SETTINGCHANGE for another area switches nothing");

        // THE READING ITSELF. Below 6.5 it is the palette, which a test can set. On 6.5+ it is
        // colorScheme, which a test can only REQUEST (setColorScheme, 6.8); a platform that does
        // not honour the request is printed as a skip, never counted as a pass.
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
        {
            const QPalette paletteBefore = QGuiApplication::palette();
            QPalette darkPalette = paletteBefore;
            darkPalette.setColor(QPalette::Window, QColor(30, 30, 30));
            QApplication::setPalette(darkPalette);
            const bool readsDark = !tinta::systemPrefersLight();
            QPalette lightPalette = paletteBefore;
            lightPalette.setColor(QPalette::Window, QColor(240, 240, 240));
            QApplication::setPalette(lightPalette);
            const bool readsLight = tinta::systemPrefersLight();
            QApplication::setPalette(paletteBefore);
            std::printf("follow_reading palette dark=%d light=%d\n", readsDark, readsLight);
            check(readsDark && readsLight,
                  "below Qt 6.5 the reading follows the palette's window lightness");
        }
#elif QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        {
            QStyleHints* hints = QGuiApplication::styleHints();
            QtAppWindow schemeWindow(app);
            schemeWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
            // Light first, so the Dark request below is a CHANGE whatever the system shows --
            // colorSchemeChanged does not fire for a request that matches the current scheme.
            hints->setColorScheme(Qt::ColorScheme::Light);
            QApplication::processEvents();
            applyTheme(app, otherIdx);
            hints->setColorScheme(Qt::ColorScheme::Dark);
            QApplication::processEvents();
            const bool honoured = hints->colorScheme() == Qt::ColorScheme::Dark;
            const bool readsDark = !tinta::systemPrefersLight();
            const bool signalled = app.currentThemeIndex == darkIdx;
            hints->setColorScheme(Qt::ColorScheme::Light);
            QApplication::processEvents();
            const bool honouredLight = hints->colorScheme() == Qt::ColorScheme::Light;
            const bool readsLight = tinta::systemPrefersLight();
            const bool signalledLight = app.currentThemeIndex == lightIdx;
            hints->unsetColorScheme();
            QApplication::processEvents();
            if (honoured && honouredLight) {
                std::printf("follow_reading scheme dark=%d light=%d signal_dark=%d "
                            "signal_light=%d\n",
                            readsDark, readsLight, signalled, signalledLight);
                check(readsDark && readsLight, "on Qt 6.5+ the reading is colorScheme");
                check(signalled && signalledLight,
                      "and colorSchemeChanged switches a follow-on window");
            } else {
                std::printf("follow_reading SKIP: this platform did not honour setColorScheme "
                            "(dark=%d light=%d)\n",
                            honoured, honouredLight);
            }
        }
#else
        std::printf("follow_reading SKIP: Qt 6.5-6.7 has colorScheme but no setColorScheme\n");
#endif

        app.followSystemTheme = followBefore;
        app.lightThemeIndex = lightBefore;
        app.darkThemeIndex = darkIndexBefore;
        applyTheme(app, themeBefore);
    }

    // THE TITLE BAR (slice 13). theme_qt.cpp's applyWindowChrome was `{}`, so the caption kept
    // the platform's colours under every theme. It now delivers windowChromeFor through the
    // window sink, and on Windows the window paints it with the DWM calls the Direct2D stack
    // made, now src/window_chrome_win32.cpp's applyDwmWindowChrome.
    {
        int lightIdx = -1;
        int darkIdx = -1;
        for (int i = 1; i < themeCount(); ++i) {
            if (!themeAt(i).isDark && lightIdx < 0) lightIdx = i;
            if (themeAt(i).isDark && darkIdx < 0) darkIdx = i;
        }
        const int themeBefore = app.currentThemeIndex;
        auto sameColor = [](const tinta::Color& a, const tinta::Color& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b;
        };
        auto sameChrome = [&](const tinta::WindowChrome& a, const tinta::WindowChrome& b) {
            return a.dark == b.dark && sameColor(a.caption, b.caption) && sameColor(a.text, b.text);
        };
        // The expected COLORREF, 0x00BBGGRR, rounded the way the DWM helper rounds.
        auto colorref = [](const tinta::Color& c) {
            auto byte = [](float v) { return static_cast<std::uint32_t>(v * 255.0f + 0.5f); };
            return byte(c.r) | (byte(c.g) << 8) | (byte(c.b) << 16);
        };

        applyTheme(app, lightIdx);  // before any window: delivered to nobody, and harmless
        bool constructed = false;
        bool darkDelivered = false;
        bool formula = false;
        bool distinct = false;
        bool dwmDarkRead = false;
        bool dwmCaptionRead = false;
        bool dwmDark = false;
        bool dwmLight = false;
        bool dwmCaption = false;
        {
            QtAppWindow chromeWindow(app);
            chromeWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
            const tinta::WindowChrome wantLight = windowChromeFor(app);
            constructed = chromeWindow.chromeCount() == 1 &&
                          sameChrome(chromeWindow.lastChrome(), wantLight) && !wantLight.dark;
            applyTheme(app, darkIdx);
            const tinta::WindowChrome wantDark = windowChromeFor(app);
            darkDelivered = chromeWindow.chromeCount() == 2 &&
                            sameChrome(chromeWindow.lastChrome(), wantDark) && wantDark.dark;
            // What the colours ARE: the drawn tab strip and the theme's text, as WinMain painted.
            formula = sameColor(wantDark.caption, tabStripBackground(app)) &&
                      sameColor(wantDark.text, app.theme.text);
            distinct = !sameColor(wantLight.caption, wantDark.caption);

            // What DWM now holds. Only Windows has a reading, and a refused read is a skip.
            const tinta::DwmChromeReading darkRead = tinta::readDwmChrome(chromeWindow.winId());
            applyTheme(app, lightIdx);
            const tinta::DwmChromeReading lightRead = tinta::readDwmChrome(chromeWindow.winId());
            dwmDarkRead = darkRead.darkRead && lightRead.darkRead;
            if (dwmDarkRead) {
                dwmDark = darkRead.dark;
                dwmLight = !lightRead.dark;
            }
            dwmCaptionRead = darkRead.captionRead && lightRead.captionRead;
            if (dwmCaptionRead) {
                dwmCaption = darkRead.caption == colorref(wantDark.caption) &&
                             lightRead.caption == colorref(wantLight.caption);
            }
        }
        const bool cleared = !tinta::windowSinkSlot().chrome;

        std::printf("window_chrome constructed=%d dark_delivered=%d formula=%d distinct=%d "
                    "cleared=%d dwm_dark_read=%d dwm_dark=%d dwm_light=%d dwm_caption_read=%d "
                    "dwm_caption=%d\n",
                    constructed, darkDelivered, formula, distinct, cleared, dwmDarkRead, dwmDark,
                    dwmLight, dwmCaptionRead, dwmCaption);
        check(constructed,
              "a new window applies the current theme's chrome itself, since the first "
              "applyTheme ran before it existed");
        check(darkDelivered, "applyTheme delivers the new theme's chrome to the window");
        check(formula, "the caption is the tab strip's colour and the caption text the theme's");
        check(distinct, "a light and a dark theme give different captions");
        check(cleared, "the window's destruction clears the chrome sink");
#ifdef _WIN32
        // Each read on its own: Windows 11 answers the dark flag and refuses the caption colour
        // (measured 2026-09-29, build 26200), so the caption is a SKIP here, and the colour DWM
        // got is covered only by the delivery checks above plus the helper tinta.exe shares.
        if (dwmDarkRead) {
            check(dwmDark && dwmLight, "DWM holds the dark caption variant the theme asks for");
        } else {
            std::printf("window_chrome SKIP: DwmGetWindowAttribute refused the dark flag\n");
        }
        if (dwmCaptionRead) {
            check(dwmCaption, "and the caption colour the theme asks for");
        } else {
            std::printf("window_chrome SKIP: DwmGetWindowAttribute refused the caption colour\n");
        }
#endif
        applyTheme(app, themeBefore);
    }

    // ---- PRINTING ON THE QT STACK (slice 15) -----------------------------------------
    //
    // src/print.cpp compiles off Windows and qinta links it, where src/print_qt.cpp used to
    // define all seven print symbols as refusals. Four claims, each through the real entry
    // point the key and mouse routers call, from OUTSIDE a paint event, because that is where
    // the routers call them:
    //   the preview rasterises a page, flips and re-paginates, and closing restores the
    //   screen; a PDF export writes as many pages as the preview paginated; the link peek
    //   returns a bitmap; and (since slice 20) printDocument with no document opens nothing.
    //
    // OUTSIDE A PAINT EVENT IS THE POINT. On this stack app.renderTarget exists only inside
    // paintEvent, so every one of these calls finds it null. src/print_pages.h's offscreen
    // hook is what makes them work at all, and the first check below is the precondition
    // that keeps that hook under test: a run that happened to sit inside a paint would pass
    // with the hook deleted.
    {
        std::error_code rmEcPrint;
        QtAppWindow printWindow(app);
        printWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        printWindow.resize(900, 700);
        check(printWindow.openDocument(tallUtf8), "print: the tall fixture opens");
        const QImage screenImage = grabWindow(printWindow);
        const int screenWidth = app.width;
        const float screenContentH = app.contentHeight;
        const qmd::ElementPtr printRootBefore = app.root;
        const std::string fileBefore = app.currentFile;
        check(app.renderTarget == qtFactoryTarget(),
              "print: outside a paint event App holds the factory target, not a paint target "
              "-- the case every call below runs in");

        // A4 portrait, chosen rather than detected, so the page count below does not depend
        // on the locale of whoever runs the test.
        app.printPreviewPaper = 0;
        app.printPreviewLandscape = false;

        // Counts the pixels that are not page-white. A blank page reads 0, which is what
        // separates "a page was rasterised" from "a white buffer of the right size".
        auto inkPixels = [](const std::vector<uint8_t>& px) {
            size_t ink = 0;
            for (size_t i = 0; i + 3 < px.size(); i += 4) {
                if (px[i] < 250 || px[i + 1] < 250 || px[i + 2] < 250) ++ink;
            }
            return ink;
        };

        const auto t0 = std::chrono::steady_clock::now();
        openPrintPreview(app);
        const auto t1 = std::chrono::steady_clock::now();
        const int pagesA4 = static_cast<int>(app.printPreviewBounds.size()) - 1;
        const unsigned pxW = app.printPreviewPxW;
        const unsigned pxH = app.printPreviewPxH;
        const size_t ink0 = inkPixels(app.printPreviewPixels);
        const bool sized = app.printPreviewPixels.size() == size_t(pxW) * pxH * 4;
        const bool cornerWhite = sized && pxW > 4 && app.printPreviewPixels[0] == 255 &&
                                 app.printPreviewPixels[1] == 255 &&
                                 app.printPreviewPixels[2] == 255;
        const std::vector<uint8_t> page0 = app.printPreviewPixels;

        // One page flip, timed on its own: this is the live-UI cost render_api.h priced.
        const auto t2 = std::chrono::steady_clock::now();
        printPreviewSetPage(app, 1);
        const auto t3 = std::chrono::steady_clock::now();
        const bool flipped = app.printPreviewPage == 1 && app.printPreviewPixels.size() ==
                             page0.size() && app.printPreviewPixels != page0;
        const size_t ink1 = inkPixels(app.printPreviewPixels);

        const QImage previewImage = grabWindow(printWindow);
        const bool previewFrame = !printWindow.documentPainted() && previewImage != screenImage;

        printPreviewSetFormat(app, 1, true);  // Letter, landscape
        const bool reformatted = app.printPreviewPageW == PRINT_PAPERS[1].h &&
                                 app.printPreviewPageH == PRINT_PAPERS[1].w &&
                                 app.printPreviewPxW > app.printPreviewPxH &&
                                 inkPixels(app.printPreviewPixels) > 0;
        const int pagesLetterLandscape = static_cast<int>(app.printPreviewBounds.size()) - 1;
        printPreviewSetFormat(app, 0, false);  // back to A4 portrait for the PDF below
        const bool backToA4 = static_cast<int>(app.printPreviewBounds.size()) - 1 == pagesA4;

        closePrintPreview(app);
        const bool closed = !app.showPrintPreview && app.printPreviewPixels.empty();
        const bool restored = app.width == screenWidth && app.contentHeight == screenContentH;
        const bool targetReturned = app.renderTarget == qtFactoryTarget();

        const double openMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double flipMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
        std::printf("print_preview pages_a4=%d pages_letter_landscape=%d px=%ux%u ink0=%zu "
                    "ink1=%zu sized=%d corner_white=%d flipped=%d preview_frame=%d "
                    "reformatted=%d back_to_a4=%d closed=%d restored=%d open_ms=%.2f "
                    "flip_ms=%.2f\n",
                    pagesA4, pagesLetterLandscape, pxW, pxH, ink0, ink1, sized, cornerWhite,
                    flipped, previewFrame, reformatted, backToA4, closed, restored, openMs,
                    flipMs);
        check(pagesA4 >= 3, "print: the tall fixture paginates into several A4 pages");
        check(sized && ink0 > 0 && cornerWhite,
              "print: the preview rasterised page 1 -- the buffer is the page's size, the "
              "margin is white, and there is ink inside it");
        check(flipped && ink1 > 0, "print: flipping to page 2 rasterises a different page");
        check(previewFrame,
              "print: with the preview open the window paints the preview frame, not the "
              "document");
        check(reformatted, "print: Letter landscape re-paginates at the new page size");
        check(backToA4, "print: and A4 portrait gives the A4 page count back");
        check(closed && restored,
              "print: closing the preview restores the screen layout (width and content "
              "height as before it opened)");
        check(targetReturned,
              "print: App still holds the factory target after the print calls, as before them");

        // PDF EXPORT, through QPdfWriter. The page count is read out of the FILE, so a
        // writer that produced a valid header and no pages cannot pass.
        const std::filesystem::path pdfPath =
            std::filesystem::temp_directory_path() / "qinta_entry_print.pdf";
        std::filesystem::remove(pdfPath, rmEcPrint);
        const bool exported = exportPdfFile(app, pdfPath.wstring());
        std::string pdfBytes;
        {
            std::ifstream in(pdfPath, std::ios::binary);
            pdfBytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        int pdfPages = 0;
        for (size_t at = pdfBytes.find("/Type /Page"); at != std::string::npos;
             at = pdfBytes.find("/Type /Page", at + 1)) {
            if (pdfBytes.compare(at, 12, "/Type /Pages") != 0) ++pdfPages;
        }
        const bool pdfHeader = pdfBytes.compare(0, 5, "%PDF-") == 0;
        std::printf("print_pdf exported=%d bytes=%zu header=%d pages=%d\n", exported,
                    pdfBytes.size(), pdfHeader, pdfPages);
        check(exported && pdfHeader, "print: exportPdfFile wrote a PDF on the Qt stack");
        check(pdfPages == pagesA4,
              "print: and it has exactly as many pages as the A4 preview paginated");
        std::filesystem::remove(pdfPath, rmEcPrint);

        // THE LINK PEEK. A bitmap for a real file, and App's document handed back after it.
        float peekH = 300.0f;
        std::shared_ptr<tinta::Bitmap> peek =
            renderPeekBitmap(app, tmp.wstring(), 400.0f, peekH);
        const bool peekBack = app.root == printRootBefore && app.currentFile == fileBefore &&
                              app.width == screenWidth && app.renderTarget == qtFactoryTarget();
        std::printf("print_peek bitmap=%d height=%.1f restored=%d\n", peek != nullptr, peekH,
                    peekBack);
        check(peek != nullptr && peekH > 0.0f,
              "print: renderPeekBitmap returns a bitmap for a real file on the Qt stack");
        check(peekBack, "print: and the screen document, file and width are restored after it");

        // PRINT NO LONGER REFUSES (slice 20), so this check changed, and it could not stay as
        // it was: printDocument now opens a MODAL QPrintDialog, and calling it here ran the
        // dialog under the offscreen platform -- the run ended mid-line. The dialog half is the
        // part no test reaches; printDocumentTo, its other half, is asserted in the QPrinter
        // block below. What stays testable here is the guard: with no document, printDocument
        // returns false before any dialog exists.
        const qmd::ElementPtr rootForPrint = app.root;
        app.root = nullptr;
        const bool printedNothing = printDocument(app);
        app.root = rootForPrint;
        check(!printedNothing,
              "print: with no document printDocument returns false and opens no dialog");
    }

    // ---- THE FACTORY TARGET OUTSIDE PAINT (slice 16) -------------------------------------
    //
    // Three portable paths that need a render target as a FACTORY and run outside a paint
    // event on this stack: layout creating a local image's bitmap, a remote download
    // completing through the post sink, and a .docx export rasterising a diagram. Before
    // qtFactoryTarget() all three refused silently on qinta: the image was cached as failed,
    // and the diagram went into the .docx as its Mermaid source.
    //
    // THE REMOTE IMAGE IS SERVED FOR REAL, from a QTcpServer on loopback, because the success
    // path is the one that was broken. The post-sink run above uses a refused port, which
    // reaches completeAsyncImage on its failure branch only, and that branch never touched
    // the render target.
    {
        QtAppWindow factoryWindow(app);  // its post sink, which the download completes through
        factoryWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        factoryWindow.resize(600, 400);
        check(app.renderTarget == qtFactoryTarget(),
              "factory: outside paint App holds the factory target");

        tinta::DecodedImage pixel;
        pixel.width = 4;
        pixel.height = 3;
        pixel.pixels.assign(4 * 3 * 4, 0);
        for (size_t i = 0; i < pixel.pixels.size(); i += 4) {
            pixel.pixels[i + 2] = 255;  // premultiplied BGRA: opaque red
            pixel.pixels[i + 3] = 255;
        }
        pixel.ok = true;
        const std::string png = tinta::encodePng(pixel);

        std::error_code facEc;
        const std::filesystem::path facDir =
            std::filesystem::temp_directory_path() / "qinta_factory_probe";
        std::filesystem::remove_all(facDir, facEc);
        std::filesystem::create_directories(facDir, facEc);
        {
            std::ofstream img(facDir / "pixel.png", std::ios::binary);
            img.write(png.data(), static_cast<std::streamsize>(png.size()));
        }

        QTcpServer server;
        const bool listening = server.listen(QHostAddress::LocalHost, 0);
        int served = 0;
        QObject::connect(&server, &QTcpServer::newConnection, [&server, &png, &served]() {
            while (QTcpSocket* sock = server.nextPendingConnection()) {
                QObject::connect(sock, &QTcpSocket::readyRead, sock, [sock, &png, &served]() {
                    sock->readAll();
                    const QByteArray head =
                        QByteArray("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\n"
                                   "Connection: close\r\nContent-Length: ") +
                        QByteArray::number(static_cast<qint64>(png.size())) + "\r\n\r\n";
                    sock->write(head);
                    sock->write(png.data(), static_cast<qint64>(png.size()));
                    sock->disconnectFromHost();
                    ++served;
                });
                QObject::connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });
        const std::string remote = "http://127.0.0.1:" + std::to_string(server.serverPort()) +
                                   "/pixel.png";
        {
            std::ofstream md(facDir / "doc.md", std::ios::binary);
            md << "# factory probe\n\n![local](pixel.png)\n\n![remote](" << remote << ")\n";
        }
        check(loadDocumentFromPath(app, (facDir / "doc.md").u8string()),
              "factory: the probe document loaded");

        // OUTSIDE PAINT, on purpose: this is the layout an input handler or a timer runs.
        layoutDocument(app);
        const bool stillFactory = app.renderTarget == qtFactoryTarget();
        const App::ImageEntry* local = nullptr;
        for (const auto& kv : app.imageCache) {
            if (kv.first.find("pixel.png") != std::string::npos &&
                kv.first.rfind("http", 0) != 0) {
                local = &kv.second;
            }
        }
        const bool localOk = local && !local->failed && local->bitmap && local->width == 4 &&
                             local->height == 3;

        auto remoteEntry = [&app, &remote]() -> const App::ImageEntry* {
            auto it = app.imageCache.find(remote);
            return it == app.imageCache.end() ? nullptr : &it->second;
        };
        const App::ImageEntry* r0 = remoteEntry();
        const bool remoteStarted = r0 && r0->pending;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (std::chrono::steady_clock::now() < deadline) {
            const App::ImageEntry* r = remoteEntry();
            if (!r || !r->pending) break;
            QEventLoop loop;
            QTimer::singleShot(20, &loop, &QEventLoop::quit);
            loop.exec();
        }
        const App::ImageEntry* r1 = remoteEntry();
        const bool remoteOk = r1 && !r1->pending && !r1->failed && r1->bitmap &&
                              r1->width == 4 && r1->height == 3;

        // The .docx: a Mermaid diagram becomes a PNG part when the raster succeeds and a code
        // block of its source when it does not. Zip stores member names uncompressed, so the
        // part's NAME in the file bytes is the reading.
        {
            std::ofstream md(facDir / "diagram.md", std::ios::binary);
            md << "# diagram\n\n```mermaid\ngraph TD\n  A --> B\n```\n";
        }
        check(loadDocumentFromPath(app, (facDir / "diagram.md").u8string()),
              "factory: the diagram document loaded");
        const std::filesystem::path docxPath = facDir / "diagram.docx";
        const bool docxOk = exportDocxFile(app, docxPath.wstring());
        std::string docxBytes;
        {
            std::ifstream in(docxPath, std::ios::binary);
            docxBytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        const bool docxImage = docxBytes.find("word/media/") != std::string::npos;

        std::printf("factory listening=%d served=%d local=%d remote_started=%d remote=%d "
                    "still_factory=%d docx=%d docx_bytes=%zu docx_image=%d\n",
                    listening, served, localOk, remoteStarted, remoteOk, stillFactory, docxOk,
                    docxBytes.size(), docxImage);
        check(localOk && stillFactory,
              "factory: a local image laid out outside paint gets a 4x3 bitmap, not a cached "
              "failure");
        check(listening && remoteStarted,
              "factory: layout outside paint starts the remote download");
        check(remoteOk && served >= 1,
              "factory: the download completes through the post sink with a 4x3 bitmap -- "
              "the success path, which a null target used to cache as failed");
        check(docxOk && docxImage,
              "factory: a .docx export outside paint embeds the Mermaid diagram as an image "
              "part, not its source");

        app.imageCache.clear();
        app.imageCacheBytes = 0;
        std::filesystem::remove_all(facDir, facEc);
    }

    // ---- TINTA'S FRAME ON THE QT WINDOW (slice 18) -----------------------------------------
    //
    // The window draws src/render_frame.cpp, the retired main_d2d.cpp's render() body. Two claims.
    // The LAYOUT CHUNKS: the frame lays a long document out viewport-first and continues in
    // WM_APP_LAYOUT_CHUNK messages, which the Qt post sink refused until this slice -- so a raw
    // grab must show an INCOMPLETE layout, and pumping the event loop must complete it. And the
    // OVERLAYS: help and search are drawn by the shared frame, so switching each on must change
    // the window's pixels where nothing else changed.
    {
        QtAppWindow frameWindow(app);
        frameWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        frameWindow.resize(700, 500);
        check(frameWindow.openDocument(tallUtf8), "frame: the tall fixture opens");
        // window.grab() directly, NOT grabWindow: the point is the state before any chunk ran.
        const QImage raw = frameWindow.grab().toImage();
        (void)raw;
        const bool partialAfterFirstFrame = !app.layoutComplete;
        const float partialHeight = app.contentHeight;
        int pumps = 0;
        for (; pumps < 2000 && !app.layoutComplete; ++pumps) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        const bool completedByChunks = app.layoutComplete;
        const float fullHeight = app.contentHeight;

        const QImage plain = grabWindow(frameWindow);
        const QImage plainAgain = grabWindow(frameWindow);
        app.showHelp = true;
        const QImage withHelp = grabWindow(frameWindow);
        app.showHelp = false;
        const QImage helpClosed = grabWindow(frameWindow);
        app.showSearch = true;
        const QImage withSearch = grabWindow(frameWindow);
        app.showSearch = false;
        const QImage searchClosed = grabWindow(frameWindow);
        // THE ANIMATED CORNER, MASKED BY MEASUREMENT. Something in the frame animates in the
        // bottom-right corner (measured: x=675, y=484..485 of 700x500 on Linux), so two UNCHANGED
        // grabs differ there, and by more the more frames pass -- 2 pixels between consecutive
        // grabs, 4 a few grabs later on Windows. A count tolerance chased that and lost. So the
        // region where the two unchanged grabs differ is measured, padded by 12 px, and excluded
        // from every comparison below; outside it the frames must be identical.
        int maskL = 1 << 30, maskT = 1 << 30, maskR = -1, maskB = -1;
        for (int y = 0; y < plain.height() && y < plainAgain.height(); ++y)
            for (int x = 0; x < plain.width() && x < plainAgain.width(); ++x)
                if (plain.pixel(x, y) != plainAgain.pixel(x, y)) {
                    maskL = std::min(maskL, x - 12); maskT = std::min(maskT, y - 12);
                    maskR = std::max(maskR, x + 12); maskB = std::max(maskB, y + 12);
                }
        auto masked = [&](int x, int y) {
            return x >= maskL && x <= maskR && y >= maskT && y <= maskB;
        };
        auto differing = [&](const QImage& a, const QImage& b) {
            int n = 0;
            for (int y = 0; y < a.height() && y < b.height(); ++y)
                for (int x = 0; x < a.width() && x < b.width(); ++x)
                    if (!masked(x, y) && a.pixel(x, y) != b.pixel(x, y)) ++n;
            return n;
        };
        const int maskArea = maskR < 0 ? 0 : (maskR - maskL + 1) * (maskB - maskT + 1);
        // THE SECOND PLAIN GRAB IS THE REFERENCE. The first one, taken right after the layout
        // finished, differs from the next by a couple of pixels (measured: 2 on Linux) with
        // nothing changed in between, so it is not yet the settled frame. That drift is printed
        // as frame_control's plain_vs_again and deliberately not asserted.
        const int helpDiff = differing(plainAgain, withHelp);
        const int searchDiff = differing(plainAgain, withSearch);
        // A PIXEL count and not QImage::operator==. The first version used ==, read false, and
        // the control below showed both closed frames differ from the plain one by 0 sampled
        // pixels -- so == was judging something other than the pixels this claim is about.
        // Outside the mask the drift is zero by construction, so the revert is exact there, and
        // the mask has to be SMALL or it would be hiding the overlays themselves.
        const int drift = differing(plain, plainAgain);
        const bool reverts = differing(plainAgain, helpClosed) == 0 &&
                             differing(plainAgain, searchClosed) == 0 && maskArea < 2500;
        std::printf("frame_control mask=%d,%d..%d,%d area=%d plain_vs_again=%d "
                    "help_closed_diff=%d search_closed_diff=%d\n",
                    maskL, maskT, maskR, maskB, maskArea, drift,
                    differing(plainAgain, helpClosed), differing(plainAgain, searchClosed));
        std::printf("frame partial_first=%d partial_h=%.1f pumps=%d completed=%d full_h=%.1f "
                    "help_diff=%d search_diff=%d reverts=%d\n",
                    partialAfterFirstFrame, partialHeight, pumps, completedByChunks, fullHeight,
                    helpDiff, searchDiff, reverts);
        check(partialAfterFirstFrame && completedByChunks && fullHeight > partialHeight,
              "frame: the first frame lays the tall document out only in part, and the posted "
              "WM_APP_LAYOUT_CHUNK messages finish it through the post sink");
        // ABOVE THE DRIFT, NOT ABOVE ZERO. A first version asserted `> 0`, and with the help
        // overlay planted out of the frame it stayed green: help_diff read 2, which is the
        // animated corner's drift and nothing else. So each overlay must move more than ten
        // times the measured drift plus a floor -- far above it, and far below either reading.
        check(helpDiff > drift * 10 + 100,
              "frame: the help overlay draws on the Qt window -- showHelp changes its pixels");
        check(searchDiff > drift * 10 + 100,
              "frame: the search box draws on the Qt window -- showSearch changes its pixels");
        check(reverts,
              "frame: closing each overlay gives back the identical frame, so the differences "
              "above are the overlays and not drift between grabs");
    }

    // ---- ONE ROW OF CAPTION BUTTONS (slice 19) ---------------------------------------------
    //
    // tinta's tab strip IS its window's caption and draws minimise, maximise and close. This
    // window keeps the native title bar, so since the frame went shared it showed two rows. The
    // window sets App::nativeCaption and the strip leaves the caption to the native frame.
    {
        QtAppWindow captionWindow(app);
        captionWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        captionWindow.resize(800, 500);
        grabWindow(captionWindow);  // so app.width is this window's
        const float stripMid = chromeTopHeight(app) * 0.5f;
        int hits = 0;
        for (float x = 0.0f; x < static_cast<float>(app.width); x += 2.0f) {
            if (captionHitTest(app, x, stripMid) != 0) ++hits;
        }
        const tinta::RectF closeRect = captionButtonRect(app, 2);
        const float islandLeft = captionIslandLeft(app);
        const float islandGap = static_cast<float>(app.width) - islandLeft;

        // COUNTER-CASE, same window, same instrument: with the flag cleared the close button
        // must be hittable again, so the zero above is a reading and not a dead hit test.
        app.nativeCaption = false;
        const int closeHitWithout = captionHitTest(app, static_cast<float>(app.width) - 10.0f,
                                                   stripMid);
        const float islandGapWithout = static_cast<float>(app.width) - captionIslandLeft(app);
        app.nativeCaption = true;

        std::printf("caption native=%d hits=%d close_w=%.1f island_gap=%.1f "
                    "without: close_hit=%d island_gap=%.1f\n",
                    app.nativeCaption, hits, closeRect.right - closeRect.left, islandGap,
                    closeHitWithout, islandGapWithout);
        check(app.nativeCaption, "caption: the Qt window sets App::nativeCaption");
        check(hits == 0 && closeRect.right - closeRect.left == 0.0f,
              "caption: the tab strip neither hit-tests nor sizes a caption button anywhere "
              "across the window");
        check(islandGap < islandGapWithout - 100.0f,
              "caption: the pin and the strip reflow to the right edge, freed of the three "
              "buttons' width");
        check(closeHitWithout == 3,
              "caption: COUNTER-CASE -- with the flag cleared the close button is hittable, "
              "so the zero above could have read otherwise");
    }

    // ---- PRINT THROUGH QPrinter (slice 20) -----------------------------------------------
    //
    // printDocumentTo is the half of the Qt printDocument a test can reach: the dialog is the
    // other half and no test opens it. A QPrinter in PdfFormat needs no printer, so the job's
    // pages can be read back out of the file, which is the reading this block takes.
    //
    // THE CJK FIXTURE is the one include/print.h and src/print_qt.cpp said the QPrinter arm was
    // waiting for. What it can tell apart: a CJK page printed with a CJK-capable face embeds
    // that face's font program, and the same document with the CJK lines removed does not
    // embed it. If this host has no CJK-capable font at all the claim cannot be decided, and
    // the block says so by name rather than passing.
    {
        QtAppWindow printerWindow(app);
        printerWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        printerWindow.resize(900, 700);

        auto pdfPages = [](const std::string& bytes) {
            int n = 0;
            for (size_t at = bytes.find("/Type /Page"); at != std::string::npos;
                 at = bytes.find("/Type /Page", at + 1)) {
                if (bytes.compare(at, 12, "/Type /Pages") != 0) ++n;
            }
            return n;
        };
        auto baseFonts = [](const std::string& bytes) {
            std::vector<std::string> names;
            for (size_t at = bytes.find("/BaseFont /"); at != std::string::npos;
                 at = bytes.find("/BaseFont /", at + 1)) {
                size_t start = at + 11;
                const size_t plus = bytes.find('+', start);
                const size_t end = bytes.find_first_of(" /\r\n>", start);
                if (plus != std::string::npos && plus < end) start = plus + 1;  // subset tag
                names.push_back(bytes.substr(start, end - start));
            }
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());
            return names;
        };
        auto printToPdf = [&](const std::string& docUtf8, const std::filesystem::path& out,
                              std::string& bytes) {
            if (!printerWindow.openDocument(docUtf8)) return false;
            grabWindow(printerWindow);
            QPrinter printer(QPrinter::HighResolution);
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOutputFileName(QString::fromStdWString(out.wstring()));
            printer.setPageSize(QPageSize(QPageSize::A4));
            printer.setPageOrientation(QPageLayout::Portrait);
            const bool ok = printDocumentTo(app, printer);
            std::ifstream in(out, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return ok;
        };

        // 1. PAGE COUNT against the preview's pagination of the same document on the same paper.
        std::string tallPdf;
        const std::filesystem::path tallOut =
            std::filesystem::temp_directory_path() / "qinta_entry_qprinter_tall.pdf";
        const bool tallOk = printToPdf(tallUtf8, tallOut, tallPdf);
        app.printPreviewPaper = 0;
        app.printPreviewLandscape = false;
        openPrintPreview(app);
        const int previewPages = static_cast<int>(app.printPreviewBounds.size()) - 1;
        closePrintPreview(app);
        const int printedPages = pdfPages(tallPdf);
        // The printed page is the paper: a full-page QPrinter reports the A4 sheet, 595 x 842 pt.
        const bool a4Box = tallPdf.find("/MediaBox [0 0 595") != std::string::npos;

        // 2. THE CJK FIXTURE against its own CJK-free control.
        const std::filesystem::path cjkFixture =
            std::filesystem::path(TINTA_SOURCE_DIR) / "tests" / "fixtures" / "print-cjk.md";
        std::string cjkText;
        {
            std::ifstream in(cjkFixture, std::ios::binary);
            cjkText.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        std::string latinText;
        for (size_t pos = 0; pos < cjkText.size();) {
            const size_t nl = cjkText.find('\n', pos);
            const std::string line = cjkText.substr(pos, nl == std::string::npos ? std::string::npos
                                                                                  : nl - pos + 1);
            bool ascii = true;
            for (unsigned char ch : line) ascii = ascii && ch < 0x80;
            if (ascii) latinText += line;
            pos = nl == std::string::npos ? cjkText.size() : nl + 1;
        }
        const std::filesystem::path latinPath =
            std::filesystem::temp_directory_path() / "qinta_entry_print_latin.md";
        {
            std::ofstream out(latinPath, std::ios::binary);
            out << latinText;
        }
        std::string cjkPdf, latinPdf;
        const bool cjkOk = printToPdf(cjkFixture.u8string(),
                                      std::filesystem::temp_directory_path() /
                                          "qinta_entry_qprinter_cjk.pdf",
                                      cjkPdf);
        const bool latinOk = printToPdf(latinPath.u8string(),
                                        std::filesystem::temp_directory_path() /
                                            "qinta_entry_qprinter_latin.pdf",
                                        latinPdf);
        const std::vector<std::string> cjkFonts = baseFonts(cjkPdf);
        const std::vector<std::string> latinFonts = baseFonts(latinPdf);
        std::vector<std::string> cjkOnly;
        std::set_difference(cjkFonts.begin(), cjkFonts.end(), latinFonts.begin(),
                            latinFonts.end(), std::back_inserter(cjkOnly));
        const bool embeds = cjkPdf.find("/FontFile2") != std::string::npos ||
                            cjkPdf.find("/FontFile3") != std::string::npos;
        const bool hostHasCjk =
            !QFontDatabase::families(QFontDatabase::SimplifiedChinese).isEmpty() &&
            !QFontDatabase::families(QFontDatabase::Japanese).isEmpty();
        std::string cjkOnlyList;
        for (const auto& f : cjkOnly) cjkOnlyList += f + " ";

        std::printf("qprinter tall_ok=%d printed=%d preview=%d a4_box=%d cjk_ok=%d latin_ok=%d "
                    "cjk_fonts=%zu latin_fonts=%zu cjk_only=[%s] embeds=%d host_cjk=%d\n",
                    tallOk, printedPages, previewPages, a4Box, cjkOk, latinOk, cjkFonts.size(),
                    latinFonts.size(), cjkOnlyList.c_str(), embeds, hostHasCjk);
        check(tallOk && printedPages == previewPages && previewPages >= 3,
              "qprinter: printDocumentTo prints as many pages as the preview paginates for A4");
        check(a4Box, "qprinter: the printed page is the A4 sheet, not an inset printable area");
        if (hostHasCjk) {
            check(cjkOk && latinOk && embeds && !cjkOnly.empty(),
                  "qprinter: the CJK fixture embeds a font program its CJK-free control does "
                  "not -- the fallback face that draws the CJK text");
        } else {
            std::printf("qprinter SKIP: this host has no CJK-capable font, so the fixture "
                        "cannot tell a correct page from boxes\n");
        }

        std::error_code qpEc;
        std::filesystem::remove(tallOut, qpEc);
        std::filesystem::remove(latinPath, qpEc);
    }

    // ---- THE FOUR LAUNCH FLAGS (slice 21) ---------------------------------------------------
    //
    // --stats, --tabbed, --new and --edit, through the two functions main() calls
    // (src/qt_launch.cpp), with options from the SAME parse main() runs. --new and --edit are what
    // the two self-relaunches pass (include/launch_options.h), so this is also where a second
    // window started by Ctrl+Shift+N, or by a note opened in its own window, ends up.
    {
        QtAppWindow launchWindow(app);
        launchWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        launchWindow.resize(800, 500);
        const bool statsBefore = app.showStats, tabbedBefore = app.forceTabStrip;

        const tinta::LaunchOptions flags =
            tinta::parseLaunchOptions(std::vector<std::string>{"--stats", "--tabbed"});
        tinta::applyLaunchFlags(app, flags);
        const bool flagsSet = app.showStats && app.forceTabStrip;
        app.showStats = statsBefore;
        app.forceTabStrip = tabbedBefore;

        // --new: an untitled editor, even with a document already open in the window.
        check(launchWindow.openDocument(tmpUtf8), "launch: a document is open before --new");
        tinta::enterLaunchMode(app, parse_qt_launch({"--cascade", "--new"}));
        const bool quickNote = app.editMode && app.currentFile.empty();
        exitEditMode(app);

        // --edit: the loaded document, in the editor.
        check(launchWindow.openDocument(tmpUtf8), "launch: the document reopens for --edit");
        tinta::enterLaunchMode(app, parse_qt_launch({"--cascade", "--edit", tmpUtf8}));
        const bool editing = app.editMode && app.currentFile == tmpUtf8;
        exitEditMode(app);

        // Neither: nothing changes mode. The counter-case that keeps the two above from passing
        // because enterLaunchMode always enters the editor.
        tinta::enterLaunchMode(app, parse_qt_launch({"--cascade"}));
        const bool untouched = !app.editMode;

        std::printf("launch flags=%d quick_note=%d editing=%d untouched=%d\n", flagsSet,
                    quickNote, editing, untouched);
        check(flagsSet, "launch: --stats and --tabbed set App::showStats and App::forceTabStrip");
        check(quickNote, "launch: --new opens an untitled quick note in the editor");
        check(editing, "launch: --edit puts the loaded document in the editor");
        check(untouched, "launch: COUNTER-CASE -- with neither flag the window stays in reading mode");
    }

    // ---- THE ALWAYS-ON-TOP PIN (slice 22) --------------------------------------------------
    //
    // The pin sits at the right end of the tab strip. A click on it goes through the real mouse
    // router into src/tabs.cpp, which asks the window sink and flips App::alwaysOnTop only when
    // the window took it. The reading is the NATIVE window's flag, not App's field, because a
    // field that flipped while the window stayed put is the defect this is here to catch.
    {
        QtAppWindow pinWindow(app);
        pinWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        pinWindow.resize(800, 500);
        check(pinWindow.openDocument(tmpUtf8), "pin: a document is open, so the strip is up");
        grabWindow(pinWindow);
        const bool before = app.alwaysOnTop;
        const QPointF pin(static_cast<float>(app.width) - dpi(app, 17.0f),
                          chromeTopHeight(app) * 0.5f);
        auto click = [&]() {
            sendMouse(pinWindow, QEvent::MouseButtonPress, pin, Qt::LeftButton, Qt::LeftButton);
            sendMouse(pinWindow, QEvent::MouseButtonRelease, pin, Qt::LeftButton, Qt::NoButton);
        };
        auto nativeOnTop = [&]() {
            return pinWindow.windowHandle() &&
                   pinWindow.windowHandle()->flags().testFlag(Qt::WindowStaysOnTopHint);
        };
        click();
        const bool onAfterFirst = app.alwaysOnTop && nativeOnTop();
        click();
        const bool offAfterSecond = !app.alwaysOnTop && !nativeOnTop();
        std::printf("pin before=%d on_after_first=%d off_after_second=%d\n", before,
                    onAfterFirst, offAfterSecond);
        check(!before, "pin: the window starts not on top");
        check(onAfterFirst,
              "pin: a click on the pin puts the native window on top, and App agrees");
        check(offAfterSecond, "pin: a second click takes it off again");
    }

    // ---- THE SINGLE-INSTANCE HAND-OFF (slice 23) ------------------------------------------
    //
    // A second launch hands its document to the running window over QLocalSocket. The client
    // blocks on its own socket, so it runs on a SECOND THREAD while this one pumps the server's
    // events -- the same shape as two processes, and the only way one process can be both ends.
    // The channel name is this test's own, so a real qinta running on the machine is never
    // involved.
    {
        QtAppWindow joinWindow(app);
        joinWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        joinWindow.resize(800, 500);
        check(joinWindow.openDocument(tmpUtf8), "join: the running window has a document open");
        grabWindow(joinWindow);
        const QString channel = QStringLiteral("qinta-entry-test-") +
                                QString::number(QCoreApplication::applicationPid());

        // CONTROL FIRST: nobody listening, so the send must fail and the launch would open its
        // own window. A send that always reported success would make main() exit with no window.
        const bool sentToNobody = tinta::sendToRunningInstance(channel, tallUtf8, 300);

        const size_t tabsBefore = app.tabs.size();
        std::string received;
        // main()'s own callback, not a copy of it: a copy here could not fail on a defect in
        // main()'s (qt_launch.h says why openArrivedDocument exists).
        tinta::InstanceServer server([&app, &joinWindow, &received](const std::string& path) {
            received = path;
            tinta::openArrivedDocument(app, joinWindow, path);
        });
        const bool listening = server.listen(channel);
        std::atomic<int> sendResult{-1};
        std::thread client([&channel, &tallUtf8, &sendResult]() {
            sendResult = tinta::sendToRunningInstance(channel, tallUtf8, 3000) ? 1 : 0;
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (sendResult.load() == -1 && std::chrono::steady_clock::now() < deadline) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        client.join();
        const bool joined = app.tabs.size() > tabsBefore && app.currentFile == tallUtf8;
        std::printf("join nobody=%d listening=%d sent=%d received_match=%d tabs=%zu->%zu "
                    "current_is_sent=%d\n",
                    sentToNobody, listening, sendResult.load(), received == tallUtf8, tabsBefore,
                    app.tabs.size(), app.currentFile == tallUtf8);
        check(!sentToNobody,
              "join: CONTROL -- with nobody listening the send fails, so the launch opens its "
              "own window");
        check(listening && sendResult.load() == 1 && received == tallUtf8,
              "join: a running window receives the second launch's absolute path and "
              "acknowledges it");
        check(joined,
              "join: the handed-over document opens as a new tab and becomes the current file");

        // A hand-off into a window SHOWING THE START PAGE replaces it, as the Finder's open does
        // and as the start page's own recent-file click does: the start page holds no document,
        // so a tab of it left beside the handed-over one is a pathless tab nobody asked for.
        const std::filesystem::path handoffPath =
            std::filesystem::temp_directory_path() / "qinta-handoff-start-page.md";
        { std::ofstream out(handoffPath, std::ios::binary); out << "# handed over\n"; }
        const std::string handoffUtf8 = handoffPath.string();
        tabOpenStartPage(app);
        const bool handoffOnStartPage = startPageActive(app);
        const size_t tabsBeforeStart = app.tabs.size();
        tinta::openArrivedDocument(app, joinWindow, handoffUtf8);
        const bool startReplaced = app.tabs.size() == tabsBeforeStart &&
                                   app.currentFile == handoffUtf8 && !startPageActive(app);
        std::printf("join start_page=%d tabs=%zu->%zu current_is_handed=%d\n", handoffOnStartPage,
                    tabsBeforeStart, app.tabs.size(), app.currentFile == handoffUtf8);
        check(handoffOnStartPage && startReplaced,
              "join: a document handed to a window on the start page replaces the start page, "
              "one tab in and one out");
        std::error_code handoffEc;
        std::filesystem::remove(handoffPath, handoffEc);
    }

    // ---- macOS FINDER: THE FILE-OPEN EVENT (2026-10-13) ---------------------------------------
    //
    // Finder's double-click and Open With start no second process: LaunchServices sends the running
    // qinta a QFileOpenEvent, and a cold launch gets one too, with no file argument. There were 0
    // handlers, so on macOS the app opened nothing from the Finder at all. The event is SENT TO
    // qApp here, as the platform sends it, and main()'s own routing runs: FileOpenRouter into
    // openFileOpenEventDocument. Qt sends no such event off macOS, so this block can run anywhere.
    //
    // Four arms: the window joins (a tab), the start page gives way (what a cold launch meets),
    // with "Open files in tabs" off the second process Windows would start is started instead,
    // and with tabs off the start page still takes the document. Each is asserted on what it must NOT do as well, so one branch cannot
    // satisfy another's check.
    {
        QtAppWindow openWindow(app);
        openWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        openWindow.resize(800, 500);
        check(openWindow.openDocument(tmpUtf8), "file-open: the window has a document open");

        const std::filesystem::path openDir =
            std::filesystem::temp_directory_path() / "qinta_file_open_probe";
        std::error_code openEc;
        std::filesystem::remove_all(openDir, openEc);
        std::filesystem::create_directories(openDir, openEc);
        const std::string joinPath = (openDir / "finder-join.md").string();
        const std::string shellPath = (openDir / "finder-start-page.md").string();
        const std::string tabsOffPath = (openDir / "finder-tabs-off.md").string();
        for (const std::string& p : {joinPath, shellPath, tabsOffPath}) {
            std::ofstream(p, std::ios::binary) << "# opened from the Finder\n\nA body.\n";
        }
        auto sendFileOpen = [](const std::string& path) {
            QFileOpenEvent event(QString::fromStdString(path));
            return QCoreApplication::sendEvent(qApp, &event);
        };
        auto pathlessTabs = [&app]() {
            int n = 0;
            for (const App::DocTab& tab : app.tabs) n += tab.path.empty() ? 1 : 0;
            return n;
        };

        // CONTROL FIRST: no router installed, so the event must open nothing. This is master's
        // behaviour, and the check that tells a red below apart from a router that never ran.
        const size_t tabsBeforeControl = app.tabs.size();
        sendFileOpen(joinPath);
        const bool controlOpenedNothing =
            app.tabs.size() == tabsBeforeControl && app.currentFile != joinPath;

        tinta::FileOpenRouter router([&app, &openWindow](const std::string& path) {
            tinta::openFileOpenEventDocument(app, openWindow, path);
        });
        qApp->installEventFilter(&router);

        // Arm 1: a document is showing, so the event joins as a new tab.
        const size_t tabsBeforeJoin = app.tabs.size();
        const bool joinConsumed = sendFileOpen(joinPath);
        const bool joined = app.tabs.size() == tabsBeforeJoin + 1 && app.currentFile == joinPath;

        // Arm 2: the start page is showing (Ctrl+T's empty tab), as on a cold Finder launch. The
        // document replaces it: one tab in, one tab out, and no pathless tab left behind.
        tabOpenStartPage(app);
        const bool onStartPage = startPageActive(app);
        const size_t tabsBeforeShell = app.tabs.size();
        const int pathlessBeforeShell = pathlessTabs();
        sendFileOpen(shellPath);
        const bool shellReplaced = app.tabs.size() == tabsBeforeShell &&
                                   app.currentFile == shellPath &&
                                   pathlessTabs() == pathlessBeforeShell - 1;
        const int pathlessAfterShell = pathlessTabs();

        // Arm 3: tabs off. The window must not take the document; a plain `qinta <path>` is
        // started, and parsed back it is a launch that takes its own decisions -- no --cascade.
        std::vector<std::vector<std::string>> secondLaunches;
        tinta::setRelaunchHookForTests([&secondLaunches](const std::vector<std::string>& args) {
            secondLaunches.push_back(args);
        });
        const bool savedOpenInTabs = app.openInTabs;
        app.openInTabs = false;
        const size_t tabsBeforeOff = app.tabs.size();
        sendFileOpen(tabsOffPath);
        app.openInTabs = savedOpenInTabs;
        tinta::setRelaunchHookForTests(nullptr);
        const bool offLaunched = secondLaunches.size() == 1 &&
                                 parse_qt_launch(secondLaunches[0]).inputFile == tabsOffPath &&
                                 !parse_qt_launch(secondLaunches[0]).cascade;
        const bool offStayed = app.tabs.size() == tabsBeforeOff && app.currentFile != tabsOffPath;
        const size_t tabsAfterShell = tabsBeforeOff;  // arm 3 opens no tab, so arm 2's count
        const size_t offLaunches = secondLaunches.size();

        // Arm 4: tabs off AND the start page showing -- a cold Finder launch for a user with tabs
        // off. The start page still takes it: no second launch, which would leave this window
        // empty beside the document's. Found end to end, not by the three arms above, which all
        // passed with the tabs check made first.
        tinta::setRelaunchHookForTests([&secondLaunches](const std::vector<std::string>& args) {
            secondLaunches.push_back(args);
        });
        secondLaunches.clear();
        tabOpenStartPage(app);
        const bool offOnStartPage = startPageActive(app);
        app.openInTabs = false;
        sendFileOpen(tabsOffPath);
        app.openInTabs = savedOpenInTabs;
        tinta::setRelaunchHookForTests(nullptr);
        const bool offShellTook = offOnStartPage && secondLaunches.empty() &&
                                  app.currentFile == tabsOffPath;

        qApp->removeEventFilter(&router);
        std::printf("fileopen control_nothing=%d consumed=%d joined=%d start_page=%d "
                    "shell_replaced=%d tabs=%zu->%zu pathless=%d->%d off_launches=%zu "
                    "off_launched=%d off_stayed=%d\n",
                    controlOpenedNothing, joinConsumed, joined, onStartPage, shellReplaced,
                    tabsBeforeShell, tabsAfterShell, pathlessBeforeShell, pathlessAfterShell,
                    offLaunches, offLaunched, offStayed);
        check(controlOpenedNothing,
              "file-open: CONTROL -- with no router installed the event opens nothing");
        check(joinConsumed && joined,
              "file-open: a Finder open joins the window as a new tab and becomes the current file");
        check(onStartPage && shellReplaced,
              "file-open: on the start page the document replaces it rather than opening beside it");
        check(offLaunched,
              "file-open: with tabs off a plain second launch of the path is started");
        check(offStayed, "file-open: with tabs off the running window does not take the document");
        std::printf("fileopen off_start_page=%d off_shell_launches=%zu off_shell_took=%d\n",
                    offOnStartPage, secondLaunches.size(), offShellTook);
        check(offShellTook,
              "file-open: with tabs off the start page still takes the document, launching nothing");
        std::filesystem::remove_all(openDir, openEc);
    }

    // ---- THE macOS MENU BAR (2026-10-13) ------------------------------------------------------
    //
    // main() installs it on macOS only, but it is built and driven here on every platform: on the
    // offscreen platform a QMenuBar is an ordinary widget whose actions trigger() like the native
    // ones. What is asserted is the design in menu_bar_qt.h -- an item with a key REPLAYS the key,
    // so a click does what the keystroke does in each mode -- and the ShortcutOverride answer that
    // keeps those keys with the window. Whether Cocoa respects that answer is not something this
    // binary can see; it was measured with real posted keystrokes against the packaged app.
    {
        QtAppWindow menuWindow(app);
        menuWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        menuWindow.resize(800, 500);
        check(menuWindow.openDocument(tmpUtf8), "menu bar: the window has a document open");
        grabWindow(menuWindow);

        QMenuBar bar;
        tinta::populateMenuBar(bar, app, menuWindow);
        const QKeySequence ctrlA(QKeyCombination(Qt::ControlModifier, Qt::Key_A));
        auto actionFor = [&bar](const QKeySequence& keys) -> QAction* {
            for (QAction* top : bar.actions()) {
                if (!top->menu()) continue;
                for (QAction* item : top->menu()->actions()) {
                    if (item->shortcut() == keys) return item;
                }
            }
            return nullptr;
        };
        int menus = 0, items = 0, keyed = 0;
        for (QAction* top : bar.actions()) {
            if (!top->menu()) continue;
            ++menus;
            for (QAction* item : top->menu()->actions()) {
                if (item->isSeparator()) continue;
                ++items;
                if (!item->shortcut().isEmpty()) ++keyed;
            }
        }
        // File 7 (Settings moves to the application menu on macOS), Edit 4, View 1, Help 1; Export
        // and Help have no key.
        const bool shaped = menus == 4 && items == 13 && keyed == 11;

        // Arm 1, reading: the menu's Select All and the key select the same thing.
        app.hasSelection = false;
        QAction* selectAll = actionFor(ctrlA);
        if (selectAll) selectAll->trigger();
        const bool menuSelected = app.hasSelection && app.selAnchor == 0 &&
                                  app.selFocus == app.docText.size() && app.selFocus > 0;
        app.hasSelection = false;
        {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
            QCoreApplication::sendEvent(&menuWindow, &press);
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_A, Qt::ControlModifier);
            QCoreApplication::sendEvent(&menuWindow, &release);
        }
        const bool keySelected = app.hasSelection && app.selFocus == app.docText.size();
        app.hasSelection = false;

        // Arm 2, editing: the same item selects the EDITOR's buffer, not the document. The
        // counter-case first: the drawn menu's CTX_SELECT_ALL, called directly as an item with no
        // key would be, does NOT -- which is why an item with a key replays it.
        const bool editBefore = app.editMode;
        const std::wstring editorTextBefore = app.editorText;
        app.editMode = true;
        app.editorText = L"menu bar select all";
        app.editorHasSelection = false;
        invokeMenuCommand(app, CTX_SELECT_ALL);
        const bool commandMissedEditor = !app.editorHasSelection;
        app.hasSelection = false;
        if (selectAll) selectAll->trigger();
        const bool menuSelectedEditor = app.editorHasSelection && app.editorSelStart == 0 &&
                                        app.editorSelEnd == app.editorText.size();
        app.editorHasSelection = false;
        app.editorText = editorTextBefore;
        app.editMode = editBefore;

        // Arm 3, an item with no key runs the drawn menu's command.
        app.showHelp = false;
        for (QAction* top : bar.actions()) {
            if (!top->menu()) continue;
            for (QAction* item : top->menu()->actions()) {
                if (item->text() == QString::fromWCharArray(tr(app, "ctx.help"))) item->trigger();
            }
        }
        const bool helpOpened = app.showHelp;
        app.showHelp = false;

        // Arm 4, ShortcutOverride. Qt ignores the event before it asks, so this does too: an event
        // left accepted would read as claimed whatever the window answered.
        auto claimed = [&menuWindow](Qt::Key key, Qt::KeyboardModifiers mods) {
            QKeyEvent ask(QEvent::ShortcutOverride, key, mods);
            ask.ignore();
            QCoreApplication::sendEvent(&menuWindow, &ask);
            return ask.isAccepted();
        };
        const bool claimsRouted = claimed(Qt::Key_A, Qt::ControlModifier) &&
                                  claimed(Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier) &&
                                  claimed(Qt::Key_Comma, Qt::ControlModifier);
        const bool leavesPlatform = !claimed(Qt::Key_Q, Qt::ControlModifier) &&
                                    !claimed(Qt::Key_H, Qt::ControlModifier) &&
                                    !claimed(Qt::Key_M, Qt::ControlModifier);

        // Arm 5, ZEN MODE'S MAC KEY. F11 never reaches the window on a default Mac (a media key;
        // fn+F11 is Show Desktop), so the View menu carries ⌃⌘F. The window must let it go to the
        // menu -- kept, the router reads it as ⌘F and opens search -- and must still keep ⌘F.
        QAction* zenItem = actionFor(QKeySequence(tinta::zenMenuKey()));
        const bool zenKeyToMenu = !claimed(Qt::Key_F, Qt::MetaModifier | Qt::ControlModifier);
        const bool searchKeyKept = claimed(Qt::Key_F, Qt::ControlModifier);
        // The click, offscreen only: it goes full screen through the window's own sink, which on a
        // real platform covers the desktop (the zen_real block above skips for the same reason).
        int zenOn = -1, zenOff = -1;
        if (zenItem && QApplication::platformName() == QLatin1String("offscreen")) {
            const bool zenBefore = app.zenMode;
            zenItem->trigger();
            QApplication::processEvents();
            zenOn = app.zenMode && menuWindow.isFullScreen() ? 1 : 0;
            zenItem->trigger();
            QApplication::processEvents();
            zenOff = !app.zenMode && !menuWindow.isFullScreen() ? 1 : 0;
            app.zenMode = zenBefore;
        }
        std::printf("menubar zen_item=%d zen_key_to_menu=%d search_key_kept=%d zen_on=%d "
                    "zen_off=%d\n", zenItem ? 1 : 0, zenKeyToMenu, searchKeyKept, zenOn, zenOff);
        check(zenItem && zenItem->text() == QString::fromWCharArray(tr(app, "shortcuts.zen")),
              "menu bar: View has a Zen mode item on Meta+Ctrl+F (⌃⌘F)");
        check(zenKeyToMenu, "menu bar: the window lets ⌃⌘F go to the View menu");
        check(searchKeyKept, "menu bar: COUNTER-CASE -- the window still keeps ⌘F for search");
        if (zenOn >= 0) {
            check(zenOn == 1, "menu bar: the Zen mode item turns zen mode on, full screen");
            check(zenOff == 1, "menu bar: and the item again turns it off");
        }

        // Arm 6, FIND AND REPLACE'S MAC KEY. The router's key is Ctrl+H, ⌘H on macOS, which
        // Cocoa's Hide takes. Edit shows ⌥⌘F and replays Ctrl+H, so from the reader it opens the
        // editor with the replace row, as the key does. The window lets ⌥⌘F go to the menu.
        QAction* replaceItem = actionFor(QKeySequence(tinta::findReplaceMenuKey()));
        const bool replaceKeyToMenu = !claimed(Qt::Key_F, Qt::AltModifier | Qt::ControlModifier);
        const bool replaceEditBefore = app.editMode;
        app.searchReplaceMode = false;
        if (replaceItem) replaceItem->trigger();
        const bool replaceOpened = app.editMode && app.showSearch && app.searchReplaceMode;
        // Back to reading, and no animating residue: entering edit mode can push a signal chip,
        // and a draining chip broke a later frame run on Linux once (698cbfd).
        app.showSearch = false;
        app.searchActive = false;
        app.searchReplaceMode = false;
        app.editMode = replaceEditBefore;
        app.signalChips.clear();
        app.signalHoverChip = 0;
        disarmTimer(TIMER_NOTIFICATION);
        std::printf("menubar replace_item=%d replace_key_to_menu=%d replace_opened=%d\n",
                    replaceItem ? 1 : 0, replaceKeyToMenu, replaceOpened);
        check(replaceItem && replaceItem->text() == QString::fromWCharArray(tr(app, "menu.find_replace")),
              "menu bar: Edit has Find and Replace on Alt+Ctrl+F (⌥⌘F)");
        check(replaceKeyToMenu, "menu bar: the window lets ⌥⌘F go to the Edit menu");
        check(replaceOpened,
              "menu bar: Find and Replace opens the editor with the replace row, as Ctrl+H does");
        check(app.signalChips.empty() && !signalsNeedTicks(app),
              "menu bar: the find-and-replace arm leaves no animating signal chip");

        // Arm 7, THE LABELS FOLLOW THE UI LANGUAGE. Settings moves currentLanguageIndex and asks
        // for a repaint; the bar relabels on the window's next paint. Read the German text, then
        // back to the starting language, so a bar that never relabels cannot pass either half.
        auto titleOf = [&bar](const char* key) -> QString {
            for (QAction* top : bar.actions())
                if (top->data().toString() == QLatin1String(key)) return top->text();
            return {};
        };
        // German is seeded by loadLanguageOverrides, as main() does at startup; without it every
        // de/fr/it string is English and this arm would read English as "German".
        loadLanguageOverrides();
        const int langBefore = app.currentLanguageIndex;
        const QString editBeforeText = titleOf("menu.edit");
        app.currentLanguageIndex = languageIndexById("de");
        grabWindow(menuWindow);
        const QString editGerman = titleOf("menu.edit");
        const QString zenGerman = zenItem ? zenItem->text() : QString();
        app.currentLanguageIndex = langBefore;
        grabWindow(menuWindow);
        const QString editAfter = titleOf("menu.edit");
        std::printf("menubar relabel before=[%s] de=[%s] zen_de=[%s] after=[%s]\n",
                    editBeforeText.toUtf8().constData(), editGerman.toUtf8().constData(),
                    zenGerman.toUtf8().constData(), editAfter.toUtf8().constData());
        check(editGerman == QStringLiteral("Bearbeiten") &&
                  zenGerman == QString::fromWCharArray(tr(languageIndexById("de"), "shortcuts.zen")),
              "menu bar: a language change relabels the titles and items on the next paint");
        check(!editBeforeText.isEmpty() && editAfter == editBeforeText,
              "menu bar: and changing back restores the starting language's labels");

        std::printf("menubar menus=%d items=%d keyed=%d menu_sel=%d key_sel=%d "
                    "command_missed_editor=%d menu_sel_editor=%d help=%d claims=%d leaves=%d\n",
                    menus, items, keyed, menuSelected, keySelected, commandMissedEditor,
                    menuSelectedEditor, helpOpened, claimsRouted, leavesPlatform);
        check(shaped, "menu bar: File, Edit, View and Help, 13 items, 11 of them showing a key");
        check(selectAll && menuSelected && keySelected,
              "menu bar: Select All from the menu selects the document, as Ctrl+A does");
        check(commandMissedEditor,
              "menu bar: COUNTER-CASE -- CTX_SELECT_ALL called directly misses the editor");
        check(menuSelectedEditor,
              "menu bar: while editing, the menu's Select All selects the editor's buffer");
        check(helpOpened, "menu bar: an item with no key runs the drawn menu's command");
        check(claimsRouted, "menu bar: the window claims keys it routes, so a menu cannot take them");
        check(leavesPlatform, "menu bar: Cmd+Q, Cmd+H and Cmd+M stay the platform's");
    }

    // ---- TAB TEAR-OFF (slice 24), AND THE RELAUNCHES RUN AT LAST --------------------------
    //
    // A drag below the strip detaches the tab under the ghost card; back over the strip it
    // snaps home; a release over this window keeps it, a release anywhere else opens it in a new
    // window at the drop point. All through the real mouse router. The new window is a RELAUNCH,
    // and inside this test the relaunched binary would be qt_entry_tests itself, so the hook
    // (include/window_launch_qt.h) records the argument list instead of starting anything --
    // which is also what finally lets the two slice-21 launchers be run, not just linked.
    {
        std::vector<std::vector<std::string>> relaunches;
        tinta::setRelaunchHookForTests(
            [&relaunches](const std::vector<std::string>& args) { relaunches.push_back(args); });

        // The slice-21 launchers, through the hook, parsed back the way the new process would.
        launchQuickNoteWindow();
        launchDocumentWindow(L"note.md");
        const bool quickNoteArgs =
            relaunches.size() == 2 && parse_qt_launch(relaunches[0]).quickNote &&
            parse_qt_launch(relaunches[0]).cascade;
        const bool documentArgs = relaunches.size() == 2 &&
                                  parse_qt_launch(relaunches[1]).startInEditMode &&
                                  parse_qt_launch(relaunches[1]).inputFile == "note.md";
        relaunches.clear();

        QtAppWindow dragWindow(app);
        dragWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
        dragWindow.resize(800, 500);
        check(dragWindow.openDocument(tmpUtf8), "tearoff: the first tab's document opens");
        tabOpenPath(app, tallUtf8, true);
        grabWindow(dragWindow);
        const size_t tabsAtStart = app.tabs.size();
        const float stripMid = chromeTopHeight(app) * 0.5f;
        const QPointF onFirstTab(dpi(app, 60.0f), stripMid);
        const QPointF below(dpi(app, 60.0f), 300.0f);
        auto move = [&](const QPointF& p) {
            sendMouse(dragWindow, QEvent::MouseMove, p, Qt::NoButton, Qt::LeftButton);
        };

        sendMouse(dragWindow, QEvent::MouseButtonPress, onFirstTab, Qt::LeftButton,
                  Qt::LeftButton);
        const std::string draggedPath = app.currentFile;  // the press activated the first tab
        move(below);
        const bool detached = app.tabDragDetached && dragWindow.ghostShowing();
        move(onFirstTab);
        const bool snappedHome = !app.tabDragDetached && !dragWindow.ghostShowing();
        move(below);
        sendMouse(dragWindow, QEvent::MouseButtonRelease, below, Qt::LeftButton, Qt::NoButton);
        const bool keptOnThisWindow =
            relaunches.empty() && app.tabs.size() == tabsAtStart && !dragWindow.ghostShowing();

        // Again, released well left of the window: a new window at the drop point.
        sendMouse(dragWindow, QEvent::MouseButtonPress, onFirstTab, Qt::LeftButton,
                  Qt::LeftButton);
        const std::string tornPath = app.currentFile;
        move(below);
        const QPointF away(-600.0f, 300.0f);
        move(away);
        sendMouse(dragWindow, QEvent::MouseButtonRelease, away, Qt::LeftButton, Qt::NoButton);
        bool tornOff = false;
        if (relaunches.size() == 1) {
            const tinta::LaunchOptions torn = parse_qt_launch(relaunches[0]);
            tornOff = torn.cascade && torn.forceTabStrip && torn.hasPos &&
                      torn.inputFile == tornPath;
        }
        const bool closedLocally = app.tabs.size() == tabsAtStart - 1 && !dragWindow.ghostShowing();

        tinta::setRelaunchHookForTests(nullptr);
        std::printf("tearoff quick_note_args=%d document_args=%d detached=%d snapped_home=%d "
                    "kept=%d torn_off=%d closed_locally=%d tabs=%zu->%zu dragged_is_torn=%d\n",
                    quickNoteArgs, documentArgs, detached, snappedHome, keptOnThisWindow, tornOff,
                    closedLocally, tabsAtStart, app.tabs.size(), draggedPath == tornPath);
        check(quickNoteArgs && documentArgs,
              "relaunch: Ctrl+Shift+N and a note window pass arguments the new process reads as "
              "--new and --edit <path>");
        check(detached, "tearoff: a drag below the strip detaches the tab and shows its ghost card");
        check(snappedHome, "tearoff: back over the strip the tab snaps home and the ghost goes");
        check(keptOnThisWindow,
              "tearoff: released over this window, the tab stays and nothing is launched");
        check(tornOff,
              "tearoff: released away from the window, it relaunches --cascade --tabbed --pos X Y "
              "with the dragged document");
        check(closedLocally, "tearoff: and the torn-off tab leaves this window");
    }

    // THE SEARCH BAR'S NEIGHBOURS FOLLOW THE BAR (slice 27). The bar slides down below the tab
    // strip -- chromeTopHeight + 20 -- while the folder-search toggle beside it, the folder
    // results panel under it and the I-beam region over its text field each recomputed the
    // bar's geometry with a bare 20 and no strip, so with the strip showing the toggle sat 40
    // dip above the bar it belongs to, and hovering the field gave the arrow.
    //
    // THE WITNESS IS PIXELS, because a formula checked against a copy of itself cannot fail.
    // The window is grabbed with search hidden and again with it shown: the rows that change
    // down the window's centre column are the drawn BAR, and the rows that change down the
    // toggle's column are the drawn TOGGLE. Both bands come from the paint; neither from the
    // geometry under test. (A first version read the bar from searchReplaceHits, which the paint
    // records only in edit mode -- where the toggle is disabled -- so it measured nothing.)
    {
        QtAppWindow searchWindow(app);
        sendResize(searchWindow, 900, 600);
        check(loadDocumentFromPath(app, tmp.u8string()), "search probe: the document loaded");
        app.zenMode = false;
        app.editMode = false;
        app.folderSearchEnabled = false;
        app.showSearch = false;
        const QImage hidden = grabWindow(searchWindow);
        app.showSearch = true;
        app.searchActive = true;
        app.searchAnimation = 1.0f;
        const QImage shown = grabWindow(searchWindow);
        const double dpr = shown.devicePixelRatio() > 0 ? shown.devicePixelRatio() : 1.0;
        // Rows (in app coordinates) where the two grabs differ down device column `col`.
        auto changedBand = [&](int col, float& top, float& bottom) {
            int first = -1, last = -1;
            for (int y = 0; y < shown.height() && y < hidden.height(); ++y) {
                if (shown.pixel(col, y) != hidden.pixel(col, y)) {
                    if (first < 0) first = y;
                    last = y;
                }
            }
            top = (float)(first / dpr);
            bottom = (float)(last / dpr);
            return first >= 0;
        };
        float barTop = 0, barBottom = 0;
        const bool barSeen =
            changedBand((int)(shown.width() / 2), barTop, barBottom);
        check(barSeen, "search probe: showing search changed the centre column, so the bar is drawn");
        check(chromeTopHeight(app) > 0.0f &&
                  barTop >= chromeTopHeight(app),
              "search probe: the drawn bar sits below the tab strip");
        if (barSeen) {
            const float midY = (barTop + barBottom) * 0.5f;
            // The bar is centred and dpi(500) wide here (900 > 540), so its right edge is known
            // from the window alone; the toggle's column is 8 + 15 dip past it.
            const float barRight = (app.width + std::min(dpi(app, 500.0f), app.width - dpi(app, 40.0f))) * 0.5f;
            const float toggleX = barRight + dpi(app, 8.0f) + dpi(app, 15.0f);
            float toggleTop = 0, toggleBottom = 0;
            const bool toggleSeen = changedBand((int)(toggleX * dpr), toggleTop, toggleBottom);
            const float toggleMid = (toggleTop + toggleBottom) * 0.5f;
            std::printf("search_bar drawn=%.1f..%.1f mid=%.1f strip=%.1f toggle_drawn=%.1f..%.1f "
                        "toggle_hit_at_bar_mid=%d\n",
                        barTop, barBottom, midY, chromeTopHeight(app), toggleTop, toggleBottom,
                        folderSearchToggleAt(app, toggleX, midY) ? 1 : 0);
            check(toggleSeen && std::abs(toggleMid - midY) <= dpi(app, 3.0f),
                  "the folder-search toggle is DRAWN centred on the drawn bar, below the tab strip");
            check(folderSearchToggleAt(app, toggleX, midY),
                  "and its hit box is where it is drawn: a click at the bar's middle hits it");
            const QPointF field((app.width - dpi(app, 500.0f)) * 0.5f + dpi(app, 60.0f), midY);
            searchWindow.setCursor(Qt::ArrowCursor);
            sendMouse(searchWindow, QEvent::MouseMove, field);
            std::printf("search_field_hover cursor=%d\n",
                        static_cast<int>(searchWindow.cursor().shape()));
            check(searchWindow.cursor().shape() == Qt::IBeamCursor,
                  "hovering the search field's text, below the tab strip, gives the I-beam");

            // EDIT MODE WITH THE PREVIEW HIDDEN: the editor takes the whole window, so the bar
            // centres over all of it. It used to centre over width * split - 3 regardless --
            // the left half -- because only editorPaneWidth knows the preview can be off.
            //
            // Read off the pixels along the bar's middle row, and from the RIGHT end of the
            // longest changed run. Not the left end: the first run of this probe read 11..427
            // where the bar was 20..427, because the editor's own gutter changes in columns
            // 11..19 when search opens and merges into the bar's run. The right end met the
            // model to the pixel, so it is the edge that measures the bar.
            const bool previewBefore = app.editorShowPreview;
            app.editMode = true;
            app.editorShowPreview = false;
            app.showSearch = false;
            const QImage editHidden = grabWindow(searchWindow);
            app.showSearch = true;
            app.searchAnimation = 1.0f;
            const QImage editShown = grabWindow(searchWindow);
            const int rowDev = (int)(midY * dpr);
            int bestStart = -1, bestEnd = -1, runStart = -1;
            for (int x = 0; x <= editShown.width(); ++x) {
                const bool diff = x < editShown.width() && x < editHidden.width() &&
                                  editShown.pixel(x, rowDev) != editHidden.pixel(x, rowDev);
                if (diff && runStart < 0) runStart = x;
                if (!diff && runStart >= 0) {
                    if (x - 1 - runStart > bestEnd - bestStart) { bestStart = runStart; bestEnd = x - 1; }
                    runStart = -1;
                }
            }
            const float barRightDrawn = (float)((bestEnd + 1) / dpr);
            const float expectedRight =
                (app.width + std::min(dpi(app, 500.0f), app.width - dpi(app, 40.0f))) * 0.5f;
            std::printf("search_edit_nopreview run=%d..%d bar_right=%.1f expected=%.1f\n", bestStart,
                        bestEnd, barRightDrawn, expectedRight);
            check(bestEnd >= 0 && std::abs(barRightDrawn - expectedRight) <= dpi(app, 3.0f),
                  "in edit mode with the preview hidden, the search bar centres over the whole editor");
            app.editMode = false;
            app.editorShowPreview = previewBefore;
        }
        app.showSearch = false;
        app.searchActive = false;
        app.searchAnimation = 0.0f;
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
