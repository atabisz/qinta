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

// The seam overload is now the IMPLEMENTATION rather than an adapter that unwrapped
// its argument and called the native one. It builds and measures its layout entirely
// through tinta::, so nothing in this function names a DirectWrite type.
//
// The native overload below is kept, not left behind: 4 call sites still hold an
// app.native*Format and have nothing to hand this one. The two are therefore required
// to agree, and that is asserted rather than assumed -- see tests/text_factory_tests.cpp.
float measureText(App& app, const std::wstring& text, tinta::TextFormat* format) {
    if (!app.textFactory || !format) return 0;
    std::unique_ptr<tinta::TextLayout> layout = app.textFactory->CreateTextLayout(
        text.c_str(), (uint32_t)text.length(), format, 10000.0f, 100.0f);
    if (!layout) return 0;

    // RAII, because tinta::TextLayout OWNS its native (text_d2d.h) -- the manual
    // Release the native path below still needs is gone here, not merely moved.
    tinta::TextMetrics metrics{};
    layout->GetMetrics(&metrics);
    return metrics.widthIncludingTrailingWhitespace;
}

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
    InvalidateRect(app.hwnd, nullptr, FALSE);
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
