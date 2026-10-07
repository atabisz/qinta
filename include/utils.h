#ifndef TINTA_UTILS_H
#define TINTA_UTILS_H

#include "app.h"
// The text helpers that never needed Windows now live here. Included rather
// than re-declared so every existing call site keeps working unchanged.
#include "text_utils.h"
#include <string>
#include <string_view>

// Simple inline element rendering
struct InlineSpan {
    std::wstring text;
    tinta::Color color;
    tinta::TextFormat* format;
    std::string linkUrl;
    bool underline;
};

// The IDWriteTextFormat* overload HAS LEFT THIS HEADER -- it is include/d2d_init.h
// now, beside the four calls in updateTextFormats that were its only callers. This
// is the seam-handle version, and it is the one the rest of the tree measures with.
float measureText(App& app, const std::wstring& text, tinta::TextFormat* format);
std::wstring_view textViewForRect(const App& app, const App::TextRect& tr);

const App::TextRect* findTextRectAt(const App& app, int x, int y);

void updateWindowTitle(App& app);
// Ctrl+N: spawns a second Tinta window on an untitled quick note.
//
// GUARDED, AND THE GUARD IS THE POINT. Defined in src/quicknote_win32.cpp, which
// explains at length why there is no off-Windows half yet -- one of the two flags it
// passes cannot be honoured there. Declaring it unconditionally would let a portable
// caller compile and then fail at link time, or worse, tempt a no-op half. Off
// Windows the name does not exist, so a new caller there is a compile error at the
// site that wants it, which is where the question actually needs answering.
//
// "the flags it passes have no parser in main_qt.cpp" stood here until 2026-09-21 and
// is now false: src/launch_options.cpp is one parse called by both entry points, and
// `--cascade` is honoured on the Qt arm. `--new` is refused by name, which is the
// remaining blocker and a better one.
//
// launchDocumentWindow is the SAME DEFERRAL and shares the guard: it relaunches
// with `--cascade --edit "<path>"`, so it is the third of the three sites that
// Qt-side window and tab state would unblock together -- the parser half of that
// sentence is delivered.
//
// launchTornOffTabWindow is the FOURTH relaunch, moved out of tabs.cpp's tab drag
// 2026-09-23, and the same deferral: it passes `--cascade --tabbed --pos X Y "<path>"`.
// (x, y) is the screen position for the new window; the path is UTF-8.
//
// THE FIRST TWO ARE UNGUARDED SINCE 2026-10-06: src/window_launch_qt.cpp is their Qt arm, and
// src/qt_launch.cpp honours --new and --edit, so the deferral above is discharged for them. The
// tear-off stays guarded -- its only caller is the Windows tab drag, which is also the only
// drag that detaches.
void launchQuickNoteWindow();
void launchDocumentWindow(const std::wstring& fullPath);
// Unguarded too since slice 24: the Qt tab drag detaches and drops now, so it is a caller.
void launchTornOffTabWindow(int x, int y, const std::string& utf8Path);
// THE OTHER ARM OF THE SAME GUARD: where the Win32 window stack opens a window, every
// other stack refuses by name -- a chip and a line on stderr, src/print_qt.cpp's two
// channels. `what` names the feature on stderr; the chip is English only, for the reason
// print_qt.cpp gives. Declared only here so tinta cannot reach it.
void refuseWindowFeature(App& app, const char* what, const wchar_t* chip);
// openUrl IS GONE. Its ShellExecuteA body is platform::openExternalUrl in
// include/platform_paths.h, with both halves already written; handleLinkClick below
// is the only thing that ever called it.
// Resolve UTF-8 document paths without a MAX_PATH-sized buffer (#203).
std::wstring absoluteFilePath(const std::string& path);
void copyFilePath(App& app, HWND hwnd, const std::string& path);

void scrollToHeadingY(App& app, float headingY);
bool scrollToHeadingId(App& app, const std::string& id);
void handleLinkClick(App& app);

#endif // TINTA_UTILS_H
