// The input method on qinta's window: CJK and other composed input into the editor.
//
// Until 2026-10-12 the window never set Qt::WA_InputMethodEnabled, so Qt kept every input
// method off for it and a CJK one could not compose into the editor at all; the comments that
// said only the candidate window's POSITION was lost were wrong. This drives the same overrides
// the platform input method calls (QtAppWindow::inputMethodQuery, inputMethodEvent) with the
// events Qt delivers, since no real input method can be scripted from a test.
#include "qt_test_app.h"

#include <QImage>
#include <QInputMethodEvent>
#include <QRect>

#include "editor.h"
#include "file_utils.h"
#include "qt_window.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void sendInputMethod(QtAppWindow& window, const QString& preedit, const QString& commit) {
    QInputMethodEvent event(preedit, {});
    event.setCommitString(commit);
    QCoreApplication::sendEvent(&window, &event);
}

int differingPixels(const QImage& a, const QImage& b, const QRect& box) {
    int n = 0;
    const QRect r = box.intersected(a.rect()).intersected(b.rect());
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x)
            if (a.pixel(x, y) != b.pixel(x, y)) ++n;
    return n;
}
}  // namespace

int main(int argc, char** argv) {
    QtTestApp qt(argc, argv);
    if (!qt.ok()) return 2;

    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "qinta-ime-test";
    fs::create_directories(dir);
    const fs::path doc = dir / "ime.md";
    std::ofstream(doc, std::ios::binary) << "# Input method\n\nabc\n";

    auto state = std::make_unique<App>();
    App& app = *state;
    if (!QtTestApp::install(app)) return 2;
    check(loadDocumentFromPath(app, doc.string()), "CONTROL: the fixture document loads");

    QtAppWindow window(app);
    window.resize(900, 600);
    window.show();

    check(window.testAttribute(Qt::WA_InputMethodEnabled),
          "the window lets Qt run an input method at all (Qt::WA_InputMethodEnabled)");

    // The viewer: every letter is a shortcut, so the input method is off and its text ignored.
    window.grab();
    check(!window.inputMethodQuery(Qt::ImEnabled).toBool(),
          "in the viewer the input method is off, so J, K and T stay shortcuts");
    const std::string sourceBefore = app.sourceText;
    sendInputMethod(window, QString(), QStringLiteral("\u4E2D"));
    check(app.sourceText == sourceBefore && app.imePreedit.empty(),
          "in the viewer, input method text changes nothing");

    // The editor: on, with the caret where the candidate window should open.
    enterEditMode(app);
    check(app.editMode, "CONTROL: the editor opened");
    window.grab();
    check(window.inputMethodQuery(Qt::ImEnabled).toBool(), "in the editor the input method is on");
    const QRect caret = window.inputMethodQuery(Qt::ImCursorRectangle).toRect();
    check(caret.width() > 0 && caret.height() > 0 && window.rect().contains(caret),
          "the caret rectangle the candidate window is placed from is real and inside the window");

    // A composition in progress: shown, not inserted.
    const std::wstring textBefore = app.editorText;
    const QImage withoutPreedit = window.grab().toImage();
    sendInputMethod(window, QStringLiteral("\u4E2D\u6587"), QString());
    check(app.imePreedit == L"\u4E2D\u6587", "the composition in progress is kept as the preedit");
    check(app.editorText == textBefore, "and is not inserted until it is committed");
    const QImage withPreedit = window.grab().toImage();
    const QRect around = caret.adjusted(0, 0, caret.height() * 3, 0);
    check(differingPixels(withoutPreedit, withPreedit, around) > 0,
          "the composition is drawn at the caret");

    // The commit: inserted at the caret, and the preedit cleared.
    const size_t cursorBefore = app.editorCursorPos;
    sendInputMethod(window, QString(), QStringLiteral("\u4E2D\u6587"));
    check(app.imePreedit.empty(), "a commit clears the preedit");
    check(app.editorText.find(L"\u4E2D\u6587") != std::wstring::npos &&
              app.editorCursorPos == cursorBefore + 2,
          "and inserts the committed text at the caret, moving it past");
    window.grab();
    const QRect moved = window.inputMethodQuery(Qt::ImCursorRectangle).toRect();
    check(moved.left() > caret.left(),
          "the caret rectangle follows the caret, so the next candidate window opens after the text");

    window.close();
    releaseQtBackends(app);
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    std::printf("ime: %d failures\n", failures);
    return failures ? 1 : 0;
}
