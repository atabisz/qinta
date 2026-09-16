#include "utils.h"
#include "render.h"
#include "i18n.h"
#include "signals.h"
#include "platform_paths.h"

#include <windows.h>
#include <shellapi.h>
#include <algorithm>

// What is left in this file is the Windows FEATURE half: the window title, the
// shell handoffs, the clipboard and path canonicalisation. The text helpers that
// never needed any of that moved to src/text_utils.cpp, where a non-Windows
// compiler builds them and tests/text_utils_tests.cpp executes them.

// THE SEAM OVERLOAD HAS LEFT THIS FILE -- it is src/text_utils_app.cpp now, and that
// file argues why at length. The short version, because a reader looking for it here
// deserves better than silence: a linker reading of qinta's closure found this
// function was the ONLY symbol render.cpp needed out of utils.cpp, and utils.cpp is
// `fatal error: windows.h` off Windows. Moving eighteen lines that already named no
// DirectWrite type was cheaper than porting a Windows feature file to reach them.
//
// The native overload below stayed, and staying is the point rather than the leftover:
// 4 call sites still hold an app.native*Format and have nothing to hand the seam
// version.
//
// This comment used to claim the two overloads' agreement was asserted by
// tests/text_factory_tests.cpp. CORRECTED while moving the seam half: that test calls
// neither overload. It asserts the layer beneath -- the two FACTORIES returning identical
// TextMetrics -- from which the overloads' agreement follows only while both bodies stay
// thin. src/text_utils_app.cpp carries the full version of this correction.
float measureText(App& app, const std::wstring& text, IDWriteTextFormat* format) {
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.length(),
        format, 10000.0f, 100.0f, &layout);
    if (!layout) return 0;

    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    layout->Release();
    return metrics.widthIncludingTrailingWhitespace;
}

static const App::LineBucket* findLineBucketAt(const App& app, float y) {
    if (app.lineBuckets.empty()) return nullptr;

    int lo = 0;
    int hi = (int)app.lineBuckets.size() - 1;
    const float tolerance = 5.0f;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const auto& line = app.lineBuckets[mid];
        if (y < line.top - tolerance) {
            hi = mid - 1;
        } else if (y > line.bottom + tolerance) {
            lo = mid + 1;
        } else {
            return &line;
        }
    }
    return nullptr;
}

std::wstring_view textViewForRect(const App& app, const App::TextRect& tr) {
    if (tr.docStart >= app.docText.size()) return {};
    size_t len = tr.docLength;
    if (tr.docStart + len > app.docText.size()) {
        len = app.docText.size() - tr.docStart;
    }
    return std::wstring_view(app.docText.data() + tr.docStart, len);
}

const App::TextRect* findTextRectAt(const App& app, int x, int y) {
    const auto* line = findLineBucketAt(app, (float)y);
    if (!line) return nullptr;

    for (size_t idx : line->textRectIndices) {
        const auto& tr = app.textRects[idx];
        if (x >= tr.rect.left && x <= tr.rect.right &&
            y >= tr.rect.top && y <= tr.rect.bottom) {
            return &tr;
        }
    }
    return nullptr;
}


void updateWindowTitle(App& app) {
    std::wstring title = L"Tinta";
    if (!app.currentFile.empty()) {
        std::wstring wpath = toWide(app.currentFile);
        size_t lastSep = wpath.find_last_of(L"\\/");
        if (lastSep != std::wstring::npos)
            title = L"Tinta - " + wpath.substr(lastSep + 1);
        else
            title = L"Tinta - " + wpath;
    } else if (app.editMode) {
        // Quick note (Ctrl+N): no backing file until the first save
        title = L"Tinta - ";
        title += tr(app, "title.untitled");
    }
    SetWindowTextW(app.hwnd, title.c_str());
}

void launchQuickNoteWindow() {
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return;
    ShellExecuteW(nullptr, L"open", exePath, L"--cascade --new", nullptr,
                  SW_SHOWNORMAL);
}

void openUrl(const std::string& url) {
    if (!url.empty()) {
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void scrollToHeadingY(App& app, float headingY) {
    // The title-bar tab strip draws over the document's top band, so the
    // landing margin must clear it or the heading arrives half-hidden
    float targetY = headingY - chromeTopHeight(app) - dpi(app, 14.0f);
    float maxScroll = std::max(0.0f, app.contentHeight - app.height);
    app.scrollY = std::max(0.0f, std::min(targetY, maxScroll));
    app.targetScrollY = app.scrollY;
    // Not every click path repaints afterwards (a bare link click outside a
    // selection doesn't) — internal jumps must show immediately
    requestRepaint(app.hwnd);
}

bool scrollToHeadingId(App& app, const std::string& id) {
    // Headings populate during the viewport-first background layout; an
    // early click on an anchor into a not-yet-laid-out section would miss.
    // Finish the layout synchronously before declaring the target absent.
    for (int attempt = 0; attempt < 2; attempt++) {
        for (const auto& h : app.headings) {
            if (h.id == id) {
                scrollToHeadingY(app, h.y);
                return true;
            }
        }
        if (app.layoutComplete && !app.layoutDirty) break;
        ensureLayoutComplete(app);
    }
    return false;
}

void handleLinkClick(App& app) {
    if (app.hoveredLink.empty()) return;
    if (app.hoveredLink[0] == '#') {
        scrollToHeadingId(app, app.hoveredLink.substr(1));
    } else {
        openUrl(app.hoveredLink);
    }
}

bool copyToClipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty()) return false;
    size_t size = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return false;
    wchar_t* dest = (wchar_t*)GlobalLock(hMem);
    if (!dest) {
        GlobalFree(hMem);
        return false;
    }
    memcpy(dest, text.c_str(), size);
    GlobalUnlock(hMem);
    if (!OpenClipboard(hwnd)) {
        GlobalFree(hMem);
        return false;
    }
    bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, hMem);
    if (!copied) GlobalFree(hMem);  // ownership transfers only on success
    CloseClipboard();
    return copied;
}

std::wstring absoluteFilePath(const std::string& path) {
    if (path.empty()) return {};
    std::wstring wide = toWide(path);
    DWORD capacity = GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
    if (!capacity) return {};
    std::wstring full(capacity, L'\0');
    DWORD length = GetFullPathNameW(wide.c_str(), capacity, full.data(), nullptr);
    if (!length || length >= capacity) return {};
    full.resize(length);
    return full;
}

void copyFilePath(App& app, HWND hwnd, const std::string& path) {
    if (copyToClipboard(hwnd, absoluteFilePath(path)))
        signalPushKey(app, SIG_INFO, SIGI_COPY, "toast.copied");
}
