#include "utils.h"

#include <windows.h>
#include <shellapi.h>

// THE PIECES OF THE TREE THAT COULD NOT BE PORTED YET, SEPARATED SO THE REST COULD BE.
//
// THREE functions now. launchTornOffTabWindow arrived 2026-09-23 from src/tabs.cpp's
// tab drag, byte-identical, and its note is at its definition. TWO stood here before
// that: launchDocumentWindow arrived 2026-09-20 from
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
// obvious one. It is because of the ARGUMENT.
//
// THE PARSING HALF OF THAT ARGUMENT IS DONE, 2026-09-21, and the two paragraphs that
// stood here are corrected rather than deleted because the CONCLUSION survives and only
// its reason moved. They said `--cascade --new` "is parsed only in WinMain's argument
// loop in src/main_d2d.cpp" and that src/main_qt.cpp "accepts a file argument and
// nothing else". Both are now false. There is ONE parse -- src/launch_options.cpp,
// called by WinMain and by main_qt with the same thirteen options -- and the Qt arm
// HONOURS `--cascade` against a real window, with a test that puts two windows exactly
// kCascadeStep apart and reads the coordinates back off the widgets.
//
// What has NOT changed is that Ctrl+N cannot be ported, and the reason is now one flag
// rather than two: `--new` is REFUSED BY NAME on the Qt arm -- "quick notes need the
// window manager (src/input.cpp)" -- because the new-window-vs-new-tab decision and the
// single-instance handoff live in src/input.cpp and have no Qt counterpart. So a Qt half
// written today would start a process that opens a correctly cascaded window and then
// declines, out loud, to make it a quick note. That is no longer a SILENT success, which
// was the sharpest argument this file used to carry; it is a loud refusal, which is
// better, and it is still not the feature. Deferring it remains the honest option and
// this file is the deferral.
//
// THE SENTENCE THAT STOOD HERE UNDERSTATED ITS OWN BLOCKER, and then its own correction
// was half refuted in turn -- so the record is three layers deep and all three are kept,
// because the middle layer is the one that was right for the wrong span. Layer one read:
// "The unblocking work is a command-line parser on the Qt side, not a process seam."
// Layer two answered that a parser is necessary and nowhere near sufficient, because
// THERE IS NOTHING FOR THE PARSED FLAGS TO REACH -- main_qt.cpp had no menus, no tabs, no
// input and no settings load, so `--cascade` and `--pos` had no window placement to want,
// `--new` had no new-window decision, `--tabbed` had no tab bar.
//
// LAYER THREE, 2026-09-21: that was true of FOUR FLAGS and is now true of TWO. `--cascade`
// and `--pos` reach real window state on the Qt arm and are tested against read-back
// geometry; `--light` does too. `--new` and `--tabbed` still reach nothing, and they are
// refused by name rather than accepted. So the shape of the claim held -- a parser alone
// would have been a silent success -- and its scope was wrong by half, which is exactly
// the failure mode this tree records as "the residue of a correction is least
// trustworthy": the clause a correction carries forward is the part nobody re-measures.
// The flags this file's own sites pass are `--cascade --new`, `--cascade --edit` and
// (since 2026-09-23, when the tab drag's relaunch moved here) `--cascade --tabbed --pos`,
// so in every case the FIRST flag is now honoured and a LATER one is the blocker --
// `--pos` is honoured too, and `--tabbed` is refused.
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
// The same question governs the OTHER self-relaunch, which sat in src/tabs.cpp's
// tabDragEnd until 2026-09-23 and is now launchTornOffTabWindow at the bottom of this
// file -- moved here for the reason the other two are here, so the one mechanism
// stays in one place: one parser answers all of them, and answering one at a time
// invents a second mechanism. That site wants MORE than the others -- it passes
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

// Spawns a new Tinta window at screen (x, y) holding the one document dragged out of
// this window's tab strip. Moved from src/tabs.cpp's tabDragEnd 2026-09-23 with the
// body byte-identical apart from the three values now arriving as parameters, for the
// reason stated above launchQuickNoteWindow -- including the `%hs` over a UTF-8 path,
// which reads the bytes in the ANSI code page and is what shipped. The drag's caller
// still closes its local tab whether or not this spawned anything, as it did before.
void launchTornOffTabWindow(int x, int y, const std::string& utf8Path) {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        wchar_t args[MAX_PATH * 2];
        swprintf_s(args, _countof(args),
                   L"--cascade --tabbed --pos %d %d \"%hs\"",
                   x, y, utf8Path.c_str());
        ShellExecuteW(nullptr, L"open", exePath, args, nullptr,
                      SW_SHOWNORMAL);
    }
}
