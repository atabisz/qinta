#include "utils.h"

#include "launch_options.h"
#include "text_utils.h"

#include <QCoreApplication>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <string>
#include <vector>

// THE QT ARMS OF TWO SELF-RELAUNCHES: Ctrl+Shift+N's new window and the folder browser's new
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

void relaunch(const std::vector<std::string>& args) {
    QStringList list;
    for (const std::string& a : args) list << QString::fromStdString(a);
    QProcess::startDetached(QCoreApplication::applicationFilePath(), list);
}

}  // namespace

void launchQuickNoteWindow() { relaunch(tinta::quickNoteRelaunchArgs()); }

void launchDocumentWindow(const std::wstring& fullPath) {
    relaunch(tinta::documentRelaunchArgs(toUtf8(fullPath)));
}
