#include "utils.h"

#include <windows.h>
#include <shellapi.h>

// THE PIECES OF THE TREE THAT COULD NOT BE PORTED YET, SEPARATED SO THE REST COULD BE.
//
// TWO functions now, not one. launchDocumentWindow arrived 2026-09-20 from
// src/input.cpp, where it was the only ShellExecuteW call of five that did NOT become
// a platform_paths.h seam call in the same slice -- the other four routed onto
// openInDefaultApplication and a new revealInFileManager. It is here for the reason
// this whole file exists, and it makes the count in the paragraph about `--cascade`
// THREE rather than two: this one passes `--cascade --edit "<path>"`.
//
// The file is still named for the quick note because renaming it would churn
// CMakeLists.txt, tests/portable-sources.txt and README figures for no reading; the
// header comment is the honest description of what it holds.
//
// Ctrl+N spawns a second Tinta on an untitled quick note by re-launching this very
// executable with two flags. That is a genuinely Windows-shaped operation right now,
// and NOT because ShellExecuteW has no counterpart -- QProcess::startDetached is the
// obvious one. It is because of the ARGUMENT: `--cascade --new` is parsed only in
// WinMain's argument loop in src/main_d2d.cpp -- grep the flag strings, do not trust
// a line number, for the reason this file spends four paragraphs on below -- and
// src/main_qt.cpp accepts a file
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
// MIDDLE link is the expensive one: src/input.cpp, where that state lives, is 4,394
// lines. IT IS NO LONGER A FATAL CENSUS ROW, and that changed on 2026-09-20 in the
// same slice that moved launchDocumentWindow here: <windowsx.h> and <shellapi.h> both
// left it, and the row now reads `fatal 0, errors 98` -- a number where the whole port
// has only ever seen a stopped read. That is progress and it is also a smaller step
// than it sounds, because 98 errors is still a file that does not compile off Windows;
// what the port gained is the ability to SEE the residue and count it down. So this is
// still not a slice waiting on a parser -- it is waiting on the window -- but the
// window's cost is now measurable instead of unknown.
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
// The same question governs the OTHER self-relaunch, the ShellExecuteW inside
// src/tabs.cpp's tabDragEnd, which is why
// neither was answered here: one parser answers both, and answering one at a time
// invents a second mechanism. That site wants MORE than this one -- it passes
// `--cascade --tabbed --pos X Y` plus a path -- so it is the stronger statement of the
// same blocker, not a second instance of it.
//
// EVERY LINE NUMBER IN THIS FILE WAS STALE -- four of four, not the three I expected
// when I started checking, and the fourth was the one worth naming because it sat in a
// paragraph that reads as settled fact. The WinMain parse sites read :1488 and :1495,
// were corrected to :1495 and :1502, and BY THE NEXT SLICE WERE :1552 and :1559. The
// tabs.cpp relaunch read :1650, was corrected to :1639, and moved again.
// `CMakeLists.txt:1251-1252` had become two lines mid-comment about selection_tests;
// the set and its REMOVE_ITEM are at :1605-1606, and that one has held.
//
// THE FIX FOR A ROTTING CITATION IS NOT A FRESHER CITATION, and that is the lesson the
// first version of this paragraph missed. I re-derived all four, wrote the new numbers
// down with some satisfaction, and TWO OF THE FOUR WERE WRONG AGAIN ONE SLICE LATER --
// because the thing that rots them is any edit ABOVE them in a 1,900-line file, which
// is a near-certainty rather than a risk. So the line numbers are gone from the prose
// above and replaced with FUNCTION NAMES and FLAG STRINGS, which are greppable, are
// what each claim is actually about, and cannot drift. The one citation kept in line
// form is the CMakeLists pair, because it is the only one whose claim genuinely is
// "these two adjacent lines", and it is the only one of the four that never moved.
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

// Spawns a second Tinta window, offset from this one, showing the given document
// in edit mode (#74: creating a note no longer replaces the document you were
// reading). Body byte-identical to the src/input.cpp original, for the reason
// stated above launchQuickNoteWindow: a moved function is not the place to change
// a failure mode, so the GetModuleFileNameW failure still returns silently.
void launchDocumentWindow(const std::wstring& fullPath) {
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return;
    std::wstring params = L"--cascade --edit \"" + fullPath + L"\"";
    ShellExecuteW(nullptr, L"open", exePath, params.c_str(), nullptr, SW_SHOWNORMAL);
}
