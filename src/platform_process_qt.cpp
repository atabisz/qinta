// The all-platform half of the process seam, and the file src/platform_process_win32.cpp
// collapses into when the Direct2D backend goes. Named _qt rather than _posix for the
// reason CMakeLists.txt:74-76 gives about the paths seam: QProcess is correct on Windows
// too, so there is no third arm to write and no POSIX-only file to keep in step.

#include "platform_process.h"

#include <QCoreApplication>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace platform {

ProcessResult runProcessAndWait(const std::wstring& exe,
                                const std::vector<std::wstring>& args,
                                int timeoutMs) {
    ProcessResult result;

    QStringList qargs;
    qargs.reserve((int)args.size());
    for (const std::wstring& arg : args) {
        qargs << QString::fromStdWString(arg);
    }

    QProcess proc;
    proc.setProgram(QString::fromStdWString(exe));
    proc.setArguments(qargs);

    // NO QUOTING HAPPENS HERE, and that is the point of the seam taking a list. QProcess
    // hands the arguments to the OS as a vector on POSIX, and on Windows applies the same
    // CommandLineToArgvW rule win32_command_line.h implements. A caller that had built a
    // command line by hand would have to un-build it here.
    proc.start();

    // TWO FAILURES LOOK THE SAME AT THIS CALL AND THEY ARE NOT THE SAME, so QProcess's
    // own error code separates them. FailedToStart means the OS refused -- no such
    // executable, no permission -- which is the Win32 arm's "CreateProcessW returned
    // false" and must leave `launched` false so the caller knows nothing ran. Anything
    // else means the child exists but did not get going inside the budget, which is an
    // overrun and is reported as one.
    if (!proc.waitForStarted(timeoutMs)) {
        if (proc.error() == QProcess::FailedToStart) return result;
        result.launched = true;
        result.timedOut = true;
        proc.kill();
        proc.waitForFinished(2000);
        return result;
    }
    result.launched = true;

    if (!proc.waitForFinished(timeoutMs)) {
        result.timedOut = true;
        proc.kill();
        // Reaped rather than abandoned: QProcess's destructor would otherwise block, and
        // the wait here is bounded so a child that ignores the kill cannot hang the
        // caller's thread forever.
        proc.waitForFinished(2000);
        return result;
    }

    if (proc.exitStatus() == QProcess::CrashExit) {
        // -1 for "died abnormally", which is unambiguous because ProcessResult's contract
        // makes exitCode meaningful only when launched && !timedOut -- both true here. It
        // is non-zero, which is the property every caller of this seam actually tests, and
        // it deliberately does not invent a signal number QProcess does not report.
        result.exitCode = -1;
    } else {
        result.exitCode = proc.exitCode();
    }
    return result;
}

unsigned long currentProcessId() {
    // QCoreApplication::applicationPid() is static and does not require an instance, which
    // matters because the one caller builds a temp file name during startup-independent
    // work. It is getpid() and GetCurrentProcessId() under one name.
    return (unsigned long)QCoreApplication::applicationPid();
}

} // namespace platform
