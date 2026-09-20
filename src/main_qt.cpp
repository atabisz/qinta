// tinta's Qt entry point: the file that replaces WinMain when the port is done.
//
// It holds main() and nothing else. Everything it does is one call into
// src/qt_init.cpp or one construction of the window in src/qt_window.cpp, which is
// deliberate and is the opposite of how the Direct2D side grew: src/main_d2d.cpp is
// 2,000+ lines because WinMain, the window procedure, the paint path and the command
// line all ended up in one file, and that is a large part of why nothing in the tree
// can test a tinta window. A main() with no logic in it has nothing to test, which is
// the point -- the logic is next door where a test can reach it.
//
// WHAT IT DOES NOT DO YET, and why each one is absent rather than forgotten:
//
//   * No settings load. loadSettings() is portable (src/settings.cpp is in
//     tests/portable-sources.txt) but the ~30 field assignments that follow it in
//     WinMain reach members whose consumers are still Windows-only, so applying them
//     here would set state nothing reads.
//   * No loadCustomThemes(). It is portable and it would work, and it would change
//     nothing: without a settings load the theme index stays App's own default, so
//     the call would populate a registry this window never indexes into.
//   * A FILE ARGUMENT AND NOTHING ELSE. argv[1], if it is there, is opened as a
//     document; every switch WinMain parses -- --printpages, --exporthtml,
//     --exportdocx, --exportpdf, --edit, --new, --pos -- still lands in a code path
//     that does not compile off Windows, so none of them is accepted here.
//
//     TWO CLAIMS THAT USED TO END THIS BULLET ARE BOTH DISCHARGED, and the second one
//     was WRONG rather than merely stale. "It needs the document renderer first" went
//     when tests/qt_entry_tests.cpp began laying out and painting a parsed document on
//     both platforms. What replaced it -- "what is missing is four lines here that read
//     argv[1] and put the parse result in app.root" -- was an estimate I never checked,
//     and checking it is what killed it: the load logic on the Windows side is two
//     lambdas INSIDE WinMain (src/main_d2d.cpp's `loadDocumentContent` and
//     `loadFile`), so there was no
//     function to call, and the second of them opens its stream with the MSVC wide-path
//     ifstream extension that does not compile off Windows at all. The real cost was a
//     new portable function, loadDocumentFromPath in src/file_utils.cpp, and the four
//     lines below are what is left of the estimate once that existed.
//
//     A FIGURE INSIDE A COMMENT IS STILL A CLAIM. This one read as a considered
//     estimate for exactly one commit and was never measured until somebody tried to
//     spend it.
//   * No exec() loop features: no menus, no tabs, no input. src/input.cpp is the
//     deepest Windows file in the tree (344 native mentions at the last census).
//
// So `qinta some.md` opens a window with that document drawn in it, and `qinta`
// with no argument opens the same window showing the placeholder line. The sentence
// that stood here said "this opens a window with the theme background and a line of
// text in it. That is a shell" -- true until this commit, and the reason it is
// corrected rather than deleted is that it named the limit README.md's basis F
// measures, so a reader who remembers it needs to be told here that it moved.
//
// WHAT IT IS STILL NOT is a viewer: no menus, no tabs, no input, no scrolling, no
// resize relayout outside paintEvent. One document, drawn once, and you can close the
// window.
//
// "AND NO MATHS" USED TO END THAT LIST, with src/math_stub.cpp named as the reason.
// It is off the list: src/math_render.cpp is ported, the stub is deleted, and this
// binary typesets equations. Recorded rather than quietly dropped, because the missing
// item was one of the limits README.md's basis F measures.
#include <QApplication>

#include <cstdio>

#include "app.h"
#include "file_utils.h"
#include "qt_init.h"
#include "qt_window.h"

int main(int argc, char** argv) {
    // QApplication rather than QGuiApplication: QtAppWindow is a QWidget, and the
    // file-dialog seam (src/file_dialog_qt.cpp) is QWidgets too.
    QApplication qapp(argc, argv);
    QApplication::setApplicationName(QStringLiteral("tinta"));

    // App is 1,848 lines of header and holds a qmd::MarkdownParser by value, which is
    // why merely constructing one pulls src/markdown.cpp and md4c onto the link line.
    // Measured from a link map, not read off the declaration -- and this tree has now
    // learned that same fact from a linker three separate times.
    App app;

    // Before the window, because the first paint can arrive during show().
    installQtBackends(app);

    // BEFORE the window too, and that ordering is load-bearing: QtAppWindow chooses its
    // branch from App::root at paint time, and the first paint can arrive inside show().
    // Loading afterwards would work by luck -- the placeholder for one frame, then a
    // repaint nobody requested.
    //
    // A FAILED LOAD IS NOT FATAL. App::root stays null, the window shows the placeholder,
    // and the reason goes to stderr. That matches the Windows side, where a file that
    // will not open lands on the start page rather than refusing to launch.
    QString title = QStringLiteral("tinta");
    if (argc > 1) {
        if (loadDocumentFromPath(app, argv[1])) {
            title = QStringLiteral("%1 - tinta").arg(QString::fromLocal8Bit(argv[1]));
        } else {
            std::fprintf(stderr, "tinta: cannot open '%s'\n", argv[1]);
        }
    }

    QtAppWindow window(app);
    window.resize(1000, 700);
    window.setWindowTitle(title);
    window.show();

    const int rc = QApplication::exec();

    // Explicit, because App's own teardown is inside #ifdef _WIN32. Off Windows
    // nothing else would release the factory, and the wrapper holds Qt objects that
    // want to die before QApplication does.
    releaseQtBackends(app);
    return rc;
}
