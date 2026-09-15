#ifndef TINTA_TEXT_UTILS_H
#define TINTA_TEXT_UTILS_H

#include "markdown.h"

#include <string>

// The text helpers out of utils.cpp that never needed Windows.
//
// Nothing here may include "app.h", <windows.h>, or any Direct2D or DirectWrite
// header -- the same restriction platform_paths.h carries, and for the same
// reason. utils.h includes app.h, so every one of these functions was reachable
// only from a translation unit that could never compile off Windows, however
// platform-free the function itself happened to be. isWordBoundary is 28
// character comparisons; slugifyHeading is the GitHub anchor rules; neither has
// any business depending on the Windows GUI stack.
//
// What splitting them off buys, stated precisely because the obvious larger
// claim is FALSE: it makes these five functions executable and asserted off
// Windows, and that is all. It does NOT make src/selection.cpp testable. That
// was the stated reason for the split and it was wrong -- see the note on
// portable_compile_only in CMakeLists.txt for what actually blocks an App, and
// tests/text_utils_tests.cpp for what this unit does assert.
//
// utils.h includes this header, so no existing call site had to change; the 19
// files that call toWide still see it exactly where they always did.

// UTF-8 -> wide, via the platform_paths.h conversion seam.
std::wstring toWide(const std::string& str);

// Wide -> UTF-8, the same seam in the other direction. Declared HERE and no
// longer in editor.h: the body was a one-line forwarder sitting in the tree's
// largest Windows-only file, so every off-Windows caller had to resolve it out of
// 3,100 lines of editor. editor.h now includes this header, so no call site
// changed -- the same trick that let toWide move without touching its 19 callers.
std::string toUtf8(const std::wstring& wstr);

// Lowercases in place per towlower(). Not locale- or script-aware beyond what
// the C library offers, which is what the search and filter call sites want.
std::wstring toLower(const std::wstring& str);

// Word/line boundary helper (offset-based word/line selection lives in
// selection.h). A boundary is whitespace or one of the ASCII punctuation
// characters a double-click should stop on.
bool isWordBoundary(wchar_t c);

// Heading text -> GitHub-compatible anchor slug, UTF-8. Non-ASCII letters are
// kept so CJK headings form valid anchors; punctuation is stripped in every
// script. Empty in, empty out.
std::string slugifyHeading(const std::wstring& text);

// Flatten an element tree to plain text, appending to `out`. Ruby annotations
// are omitted and only the base text is kept.
//
// qmd:: is spelled out because the using-directive that made it unnecessary is
// in app.h (line 84), and not including app.h is the entire point of this
// header. Callers that do include app.h see the same type either way.
void extractText(const qmd::ElementPtr& elem, std::wstring& out);

#endif // TINTA_TEXT_UTILS_H
