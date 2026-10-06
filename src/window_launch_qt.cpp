#include "utils.h"

#include "launch_options.h"
#include "window_launch_qt.h"
#include "text_utils.h"

#include <QCoreApplication>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>
#include <string>
#include <vector>

// THE QT ARMS OF THE THREE SELF-RELAUNCHES (the third, the torn-off tab, since slice 24): Ctrl+Shift+N's new window and the folder browser's new
// note in its own window. src/quicknote_win32.cpp is the Windows arm and passes the same flags
// through ShellExecuteW. Until 2026-10-06 both refused on this stack, for a reason the Windows
// file still records: `--new` was refused by name here, so a relaunch would have started a
// window that declined to be a quick note. src/qt_launch.cpp honours --new and --edit now.
//
// NO SINGLE-INSTANCE HAND-OFF IS NEEDED, which is the other half of that reason. Both relaunches
// pass --cascade, and --cascade is exactly what makes WinMain skip joining an existing window.
// So the new process opens its own window on either stack and no IPC is involved.
//
// The argument lists come from include/launch_options.h, where a test reads each one back
// through the same parse the new process runs. A failed start returns silently, as the Windows
// arm's GetModuleFileNameW failure does: what shipped is not changed while it is ported.

namespace {

std::function<void(const std::vector<std::string>&)>& relaunchHook() {
    static std::function<void(const std::vector<std::string>&)> hook;
    return hook;
}

void relaunch(const std::vector<std::string>& args) {
    if (relaunchHook()) {
        relaunchHook()(args);
        return;
    }
    QStringList list;
    for (const std::string& a : args) list << QString::fromStdString(a);
    QProcess::startDetached(QCoreApplication::applicationFilePath(), list);
}

}  // namespace

void tinta::setRelaunchHookForTests(std::function<void(const std::vector<std::string>&)> hook) {
    relaunchHook() = std::move(hook);
}

void launchQuickNoteWindow() { relaunch(tinta::quickNoteRelaunchArgs()); }

void launchDocumentWindow(const std::wstring& fullPath) {
    relaunch(tinta::documentRelaunchArgs(toUtf8(fullPath)));
}

// The torn-off tab (slice 24): a new window at screen (x, y) holding the dragged document, with
// the tab row kept, as src/quicknote_win32.cpp's arm passes it.
void launchTornOffTabWindow(int x, int y, const std::string& utf8Path) {
    relaunch(tinta::tornOffRelaunchArgs(x, y, utf8Path));
}
