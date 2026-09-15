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
//   * No command line. Every switch WinMain parses -- --printpages, --exporthtml,
//     --exportdocx, --exportpdf, --edit, --new, --pos -- lands in a code path that
//     does not compile off Windows. A file argument is the first one to add, and it
//     needs the document renderer first.
//   * No exec() loop features: no menus, no tabs, no input. src/input.cpp is the
//     deepest Windows file in the tree (344 native mentions at the last census).
//
// So this opens a window with the theme background and a line of text in it. That is
// a shell, and README.md carries the measured list of what stands between it and a
// document.
#include <QApplication>

#include "app.h"
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

    QtAppWindow window(app);
    window.resize(1000, 700);
    window.setWindowTitle(QStringLiteral("tinta"));
    window.show();

    const int rc = QApplication::exec();

    // Explicit, because App's own teardown is inside #ifdef _WIN32. Off Windows
    // nothing else would release the factory, and the wrapper holds Qt objects that
    // want to die before QApplication does.
    releaseQtBackends(app);
    return rc;
}
