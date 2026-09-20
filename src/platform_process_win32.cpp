// The Windows half of the process seam. Its counterpart is
// src/platform_process_qt.cpp, which is written to be correct on all three platforms and
// is the file this one collapses into when the Direct2D backend goes -- the same
// relationship src/platform_paths_win32.cpp has with src/platform_paths_qt.cpp, and
// CMakeLists.txt:74-76 says so for that pair.
//
// Every Win32 call here came out of src/pandoc.cpp's pandocWorker verbatim, and the
// semantics are preserved exactly: CREATE_NO_WINDOW, the caller's timeout,
// TerminateProcess on overrun, the exit code from GetExitCodeProcess, both handles
// closed. What changed is only where the command line comes from -- the caller now hands
// over an argument list and tinta::win32QuoteArgument does the quoting that the call site
// used to do by hand.

#include "platform_process.h"

#include "win32_command_line.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace platform {

ProcessResult runProcessAndWait(const std::wstring& exe,
                                const std::vector<std::wstring>& args,
                                int timeoutMs) {
    ProcessResult result;

    // CreateProcessW writes into its lpCommandLine buffer, so this must be a writable
    // copy we own. std::wstring::data() is non-const from C++17 and the string is a local
    // -- the original call site kept its own copy with the same comment for the same
    // reason.
    std::wstring cmd = tinta::win32CommandLine(exe, args);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        // launched stays false. Nothing ran, so the caller still owns whatever it staged.
        return result;
    }
    result.launched = true;

    // A negative timeout means wait forever, which is INFINITE here rather than a huge
    // number: the cast of a negative int to DWORD would happen to produce a very large
    // value on this ABI, and relying on that is the kind of accident that reads as
    // intentional later.
    const DWORD wait = timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs;
    if (WaitForSingleObject(pi.hProcess, wait) == WAIT_OBJECT_0) {
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        result.exitCode = (int)code;
    } else {
        result.timedOut = true;
        TerminateProcess(pi.hProcess, 1);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return result;
}

unsigned long currentProcessId() {
    return GetCurrentProcessId();
}

} // namespace platform
