#include "utils.h"

#include <windows.h>
#include <shellapi.h>

// THE ONE PIECE OF utils.cpp THAT COULD NOT BE PORTED, SEPARATED SO THE REST COULD BE.
//
// Ctrl+N spawns a second Tinta on an untitled quick note by re-launching this very
// executable with two flags. That is a genuinely Windows-shaped operation right now,
// and NOT because ShellExecuteW has no counterpart -- QProcess::startDetached is the
// obvious one. It is because of the ARGUMENT: `--cascade --new` is parsed only in
// WinMain (src/main_d2d.cpp:1495 and :1502), and src/main_qt.cpp accepts a file
// argument and nothing else. Its own header comment says so. So a Qt half written
// today would start a process that ignores both flags and opens a plain empty window
// in the wrong position -- a silent success, which is the failure this tree keeps
// cataloguing. Deferring it is the honest option and this file is the deferral.
//
// THE SENTENCE THAT STOOD HERE UNDERSTATED ITS OWN BLOCKER, and the correction is the
// only thing a later slice needs from this paragraph. It read: "The unblocking work is
// a command-line parser on the Qt side, not a process seam." A parser is necessary and
// it is nowhere near sufficient, because THERE IS NOTHING FOR THE PARSED FLAGS TO
// REACH. src/main_qt.cpp is 111 lines which by its own header have "no menus, no tabs,
// no input" and no settings load; `--cascade` and `--pos` want window placement,
// `--new` wants the new-window-vs-new-tab decision, and `--tabbed` wants a tab bar.
// None of that state exists on the Qt side yet, so a parser written today would parse
// flags nothing can honour -- which is the SAME silent success the paragraph above
// rejects, moved one step earlier and made harder to see.
//
// The real chain is parser -> Qt window/tab/placement state -> process seam, and the
// MIDDLE link is the expensive one: src/input.cpp, where that state lives, is 4,361
// lines and is a fatal census row on `windowsx.h`. So this is not a slice waiting on a
// parser; it is waiting on the window.
//
// AND THE KIND OF WORK THAT MIDDLE LINK NEEDS IS NOT WHAT THIS PORT'S NAME SUGGESTS.
// Under the native-mention pattern the README publishes beside its figures --
// `\b(ID2D1|IDWrite|IWIC|D2D1_|DWRITE_|WIC[A-Z])[A-Za-z0-9_]*|D2D1::` -- src/input.cpp
// scores ZERO, all lines and code-only alike. Nothing in it is Direct2D or DirectWrite
// at all; it is fatal on WINDOW AND MESSAGE handling, which is a different port with a
// different seam and no Qt counterpart written yet. Worth stating here because "remove
// Direct2D" makes every remaining blocker sound like a rendering job, and the one
// standing between this file and its Qt half is not one.
//
// The same question governs the OTHER self-relaunch, src/tabs.cpp:1638, which is why
// neither was answered here: one parser answers both, and answering one at a time
// invents a second mechanism. That site wants MORE than this one -- it passes
// `--cascade --tabbed --pos X Y` plus a path -- so it is the stronger statement of the
// same blocker, not a second instance of it.
//
// EVERY LINE NUMBER IN THIS FILE WAS STALE -- four of four, not the three I expected
// when I started checking, and the fourth is the one worth naming because it is in a
// paragraph that reads as settled fact. The WinMain parse sites read :1488 and :1495
// and are :1495 and :1502. The tabs.cpp relaunch read :1650 and is :1638. And
// `CMakeLists.txt:1251-1252`, cited below as where INPUT_TEST_SOURCES is defined, is
// now two lines in the middle of an unrelated comment about selection_tests -- the set
// and its REMOVE_ITEM are at :1605-1606.
//
// A CITATION IS A CLAIM THAT A LINE SAYS SOMETHING, and it is the only kind of claim in
// this tree that rots without anyone editing it: every one of these went wrong because
// a DIFFERENT file grew. Nothing checks them, which is why four of four had drifted
// while the prose wrapped around them stayed true -- and the prose staying true is what
// makes them dangerous, because a reader who spot-checks the argument finds it correct
// and has no reason to spot-check the addresses.
//
// WHY A NEW FILE AND NOT src/main_d2d.cpp, WHERE THE FLAGS ARE PARSED -- the obvious
// home, and a measured trap. CMakeLists.txt:1605-1606 sets INPUT_TEST_SOURCES to
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
