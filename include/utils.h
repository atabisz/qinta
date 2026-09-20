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
// explains at length why there is no off-Windows half yet -- the flags it passes
// have no parser in main_qt.cpp. Declaring it unconditionally would let a portable
// caller compile and then fail at link time, or worse, tempt a no-op half. Off
// Windows the name does not exist, so a new caller there is a compile error at the
// site that wants it, which is where the question actually needs answering.
//
// launchDocumentWindow is the SAME DEFERRAL and shares the guard: it relaunches
// with `--cascade --edit "<path>"`, so it is the third of the three sites that one
// Qt-side parser plus Qt-side window state would unblock together.
#ifdef _WIN32
void launchQuickNoteWindow();
void launchDocumentWindow(const std::wstring& fullPath);
#endif
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
