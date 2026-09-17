#include "utils.h"

#include <windows.h>
#include <shellapi.h>

// THE ONE PIECE OF utils.cpp THAT COULD NOT BE PORTED, SEPARATED SO THE REST COULD BE.
//
// Ctrl+N spawns a second Tinta on an untitled quick note by re-launching this very
// executable with two flags. That is a genuinely Windows-shaped operation right now,
// and NOT because ShellExecuteW has no counterpart -- QProcess::startDetached is the
// obvious one. It is because of the ARGUMENT: `--cascade --new` is parsed only in
// WinMain (src/main_d2d.cpp:1488 and :1495), and src/main_qt.cpp accepts a file
// argument and nothing else. Its own header comment says so. So a Qt half written
// today would start a process that ignores both flags and opens a plain empty window
// in the wrong position -- a silent success, which is the failure this tree keeps
// cataloguing. Deferring it is the honest option and this file is the deferral.
//
// The unblocking work is a command-line parser on the Qt side, not a process seam.
// The same question governs the OTHER self-relaunch, tabs.cpp:1650, which is why
// neither was answered here: one parser answers both, and answering one at a time
// invents a second mechanism.
//
// WHY A NEW FILE AND NOT src/main_d2d.cpp, WHERE THE FLAGS ARE PARSED -- the obvious
// home, and a measured trap. CMakeLists.txt:1251-1252 sets INPUT_TEST_SOURCES to
// ${SOURCES} minus src/main_d2d.cpp, and five test targets build from it
// (input_tests, file_path_tests, code_block_layout_tests, blockquote_layout_tests,
// theme_tests). Two of those reach this symbol through editor.cpp and input.cpp, so
// putting it beside its parser would break all five. In SOURCES as its own file it is
// inherited by every one of them for a single line.
//
// The body below is byte-identical to the utils.cpp original. Nothing here is
// improved while it is being moved; the GetModuleFileNameW failure still returns
// silently, because that is what shipped and a moved function is not the place to
// change a failure mode.
void launchQuickNoteWindow() {
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return;
    ShellExecuteW(nullptr, L"open", exePath, L"--cascade --new", nullptr,
                  SW_SHOWNORMAL);
}
