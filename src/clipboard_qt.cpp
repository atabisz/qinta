// The Qt half of the clipboard seam (include/clipboard.h).
//
// QtGui is enough here, unlike file_dialog_qt.cpp: QClipboard is reached from
// QGuiApplication, so this seam costs no new Qt module. That is worth stating
// because the file-picker seam's Widgets requirement was presented as the price of
// having a real second implementation, and it would be easy to read that as the
// price of every seam. It is not; it was the price of that one.
//
// TWO WAYS THIS IS WEAKER THAN THE WINDOWS HALF, both measured rather than assumed.
//
// FIRST, QClipboard::setText RETURNS NOTHING. Windows reports whether the data was
// accepted and utils.cpp:160 propagated it; Qt offers no such signal. So `true` here
// means "we asked and there was an application to ask", not "the clipboard now holds
// this text" -- a weaker claim wearing the same bool. The two ways to return false
// are therefore the only ones that exist: empty input, and no QGuiApplication. On
// Windows a third exists (SetClipboardData refused), and code that branches on false
// must not assume it has seen all three.
//
// SECOND, IT WAS EXPECTED THAT QClipboard MIGHT NORMALIZE LINE ENDINGS before this
// function sees them, which would matter at exactly one of the three call sites -- see
// include/clipboard.h. MEASURED: it does not. tests/clipboard_tests.cpp writes a CRLF
// and reads back all 13 characters here, the same as on Windows.
//
// That was measured under the OFFSCREEN QPA plugin, whose clipboard never leaves the
// process, so it says nothing about xcb or wayland. The expectation is therefore
// downgraded rather than deleted, and this file still does not compensate in either
// direction: adding CRs back would be inventing data, and stripping them would move a
// transformation into the seam that the header keeps out of it on purpose.
//
// std::wstring crosses this boundary through QString's own conversions and not
// through a cast, because wchar_t is 16 bits on Windows and 32 on Linux. A
// reinterpret_cast would work on exactly one of the two platforms this file is for.

#include "clipboard.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QString>

namespace platform {

std::wstring clipboardText() {
    // Null when no QGuiApplication exists -- which is a real state for a test binary
    // that links this file without constructing an application, not a defensive
    // check. It reaches the caller as an empty string, the same way a failed
    // OpenClipboard does on Windows.
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) return std::wstring();
    return clipboard->text().toStdWString();
}

bool setClipboardText(const std::wstring& text) {
    if (text.empty()) return false;
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) return false;
    clipboard->setText(QString::fromStdWString(text));
    return true;  // see the header note: Qt gives us nothing better than this
}

}  // namespace platform
