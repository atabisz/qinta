#ifndef TINTA_PLATFORM_PROCESS_H
#define TINTA_PLATFORM_PROCESS_H

#include <string>
#include <vector>

// The sixth seam of the platform layer: running a child process to completion.
//
// Same restriction as platform_paths.h, for the same reason: nothing in this header may
// include <windows.h>, "app.h", or any Direct2D or DirectWrite header. src/pandoc.cpp is
// the caller, and the point of this file is that pandoc.cpp stops knowing how a process
// is started.
//
// WHY A SEAM AND NOT AN ENTRY IN win32_api.h. That header's rule is stated at its top
// and it is absolute -- "NOTHING HERE PRETENDS TO WORK", off Windows its members are
// no-ops or honest failures. A member here has to genuinely RUN pandoc on Linux, which
// fails that test in the one direction that matters. win32_types.h is no home either:
// it refuses functions outright. So this is the shape the paths, keyboard, image,
// file-dialog and clipboard seams already established five times -- a header with no
// platform includes, two implementations, and a PLATFORM_PROCESS_SOURCE switch in
// CMakeLists.txt.
//
// Paths and arguments are std::wstring to match the rest of the tree, and for the reason
// platform_paths.h:16-20 gives: on Windows these hold UTF-16, on POSIX UTF-32, and each
// implementation converts at the OS boundary. This seam does not begin the string
// migration and does not prejudge it.
namespace platform {

// What happened to a child process. Three fields rather than a bool because the caller
// can distinguish all three and one of them is not a failure of the child at all.
//
// `launched` false means the OS refused to start it -- a missing executable, a
// permissions refusal. Nothing ran, so nothing was written, and a caller that staged an
// input file still has to clean it up.
//
// `timedOut` true means it started, outlived the limit, and WE killed it. Distinct from
// a non-zero exit because the child chose neither.
//
// `exitCode` is only meaningful when `launched && !timedOut`. A child that died on a
// signal or an unhandled exception reports NON-ZERO here on both platforms rather than a
// separate flag: Windows hands back the exception code from GetExitCodeProcess, and the
// Qt arm maps QProcess::CrashExit onto a non-zero code. That collapse is deliberate and
// it is safe for every caller this seam has, because each asks the same question -- did
// the conversion succeed -- and a crashed child has not succeeded. A caller that needs
// to tell a crash from a refusal needs a fourth field, not a reinterpretation of this
// one.
struct ProcessResult {
    bool launched = false;
    bool timedOut = false;
    int exitCode = -1;
};

// Run `exe` with `args`, wait up to `timeoutMs`, and kill it if it overruns.
//
// ARGUMENTS ARE A LIST, NOT A COMMAND LINE, and that is the load-bearing decision in
// this header. CreateProcessW takes one string and QProcess takes a program plus a list,
// so one of the two arms must convert -- and the conversion belongs on the Windows side
// for exactly the reason findExecutableOnPath takes a bare name rather than
// `pandoc.exe`: quoting is a Windows calling convention, and a caller spelling it out
// would be writing one platform's rules into portable code.
//
// It was already writing them. The call site this replaces built its command line by
// hand and could not quote a double quote, so it rewrote every `"` in the document title
// to `'` to protect its own concatenation. The list makes the workaround unnecessary and
// the character now reaches the child intact -- the one behaviour change in the slice
// that introduced this file, tested in both directions rather than assumed.
//
// Each argument arrives at the child as ONE argv entry, whatever it contains. Spaces,
// quotes, trailing backslashes and the empty string are the quoter's problem, not the
// caller's; see win32_command_line.h for the rule and the test that pins it.
//
// NO CONSOLE WINDOW. On Windows this passes CREATE_NO_WINDOW, because the caller is a
// GUI application and a flashing console is a visible defect. There is nothing to do off
// Windows -- a child started by a Qt application inherits no terminal -- so this is a
// property the two arms hold for different reasons rather than a flag one of them
// ignores.
//
// SYNCHRONOUS, deliberately. The caller already owns the asynchrony: it runs this on a
// std::thread and posts a message when it returns. Threading is standard C++ and needs
// no platform arm, so the seam stays the narrow part -- only the two things that
// genuinely differ per platform are behind it.
ProcessResult runProcessAndWait(const std::wstring& exe,
                                const std::vector<std::wstring>& args,
                                int timeoutMs);

// The current process's id, as the OS numbers it.
//
// Here rather than in platform_paths.h even though its one caller builds a FILE NAME out
// of it, because what it asks for is a property of the running process and not of the
// filesystem. It replaces GetCurrentProcessId, which has no standard spelling: <thread>
// offers a thread id and <filesystem> offers no unique-name helper at all.
//
// The value is only ever interpolated into a temp file name, so the guarantee this makes
// is the weak one that need is: distinct concurrently-running instances get distinct
// numbers. It is NOT a handle, it is NOT stable across runs, and nothing should compare
// it against a stored one.
unsigned long currentProcessId();

} // namespace platform

#endif // TINTA_PLATFORM_PROCESS_H
