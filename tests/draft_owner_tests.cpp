// draftOwnerIsLiveSibling -- the predicate draftsScanForRecovery uses to decide that a draft on
// disk belongs to a RUNNING sibling window and must not be offered for recovery.
//
// Written 2026-10-07, when tinta was retired, because the predicate matched the image name
// against the literal "tinta.exe" (Windows) and "tinta" (/proc), and the program that runs it is
// qinta. So every live sibling read as dead on both hosts and its drafts were offered to the
// next window as crash leftovers. It now compares against THIS process's own image, which is
// what "a sibling" means and survives the next rename.
//
// Four arms, each able to go the other way: this process; a second copy of this executable
// (the case the bug broke -- a different pid, the same image); a live process of another image;
// and that process's pid after it exits.
#include "drafts.h"
#include "qt_test_app.h"

#include <QCoreApplication>
#include <QProcess>
#include <QStringList>

#include <cstdio>
#include <iostream>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

// Started with stdin held open and killed by the parent, so its lifetime is the parent's
// choice rather than a sleep that could expire mid-check.
bool startIdle(QProcess& process, const QString& program, const QStringList& args) {
    process.start(program, args);
    return process.waitForStarted(10000);
}
}

int main(int argc, char** argv) {
    // The second-copy arm: wait on stdin until the parent closes it.
    if (argc == 2 && std::string(argv[1]) == "--idle") {
        while (std::fgetc(stdin) != EOF) {}
        return 0;
    }
    QtTestApp qt(argc, argv);
    if (!qt.ok()) return 2;

    const unsigned long self = static_cast<unsigned long>(QCoreApplication::applicationPid());
    check(draftOwnerIsLiveSibling(self), "this process reads as a live sibling of itself");

    QProcess sibling;
    check(startIdle(sibling, QCoreApplication::applicationFilePath(), {"--idle"}),
          "CONTROL: a second copy of this executable started");
    const unsigned long siblingPid = static_cast<unsigned long>(sibling.processId());
    check(siblingPid != 0 && siblingPid != self, "CONTROL: the second copy has its own pid");
    check(draftOwnerIsLiveSibling(siblingPid),
          "a second running copy of this executable is a live sibling");

    QProcess other;
#ifdef _WIN32
    const bool otherStarted = startIdle(other, "cmd.exe", {});
#else
    const bool otherStarted = startIdle(other, "cat", {});
#endif
    check(otherStarted, "CONTROL: a process of another image started");
    const unsigned long otherPid = static_cast<unsigned long>(other.processId());
    check(otherPid != 0, "CONTROL: the other process has a pid");
    check(!draftOwnerIsLiveSibling(otherPid), "a live process of another image is not a sibling");

    other.kill();
    check(other.waitForFinished(10000), "CONTROL: the other process exited");
    check(!draftOwnerIsLiveSibling(otherPid), "an exited pid is not a sibling");

    sibling.closeWriteChannel();
    if (!sibling.waitForFinished(10000)) sibling.kill();

    std::cout << "Draft owner: " << failures << " failures\n";
    return failures ? 1 : 0;
}
