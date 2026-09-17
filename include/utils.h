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

float measureText(App& app, const std::wstring& text, IDWriteTextFormat* format);
// Seam-handle overload. The plumbing below carries tinta::TextFormat* now, and
// unwrapping once here beats unwrapping at every measure site.
float measureText(App& app, const std::wstring& text, tinta::TextFormat* format);
std::wstring_view textViewForRect(const App& app, const App::TextRect& tr);

const App::TextRect* findTextRectAt(const App& app, int x, int y);

void updateWindowTitle(App& app);
// Ctrl+N: spawns a second Tinta window on an untitled quick note
void launchQuickNoteWindow();
void openUrl(const std::string& url);
// Resolve UTF-8 document paths without a MAX_PATH-sized buffer (#203).
std::wstring absoluteFilePath(const std::string& path);
void copyFilePath(App& app, HWND hwnd, const std::string& path);

void scrollToHeadingY(App& app, float headingY);
bool scrollToHeadingId(App& app, const std::string& id);
void handleLinkClick(App& app);

#endif // TINTA_UTILS_H
