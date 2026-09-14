#include "search.h"
#include "utils.h"
#include "render.h"
#include "platform_paths.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

namespace {
constexpr size_t kNoTextRect = std::numeric_limits<size_t>::max();

struct FolderScanMsg {
    int generation = 0;
    std::vector<App::FolderFileResult> files;
};

std::wstring utf8ToWideString(const std::string& bytes) {
    return platform::fromUtf8(bytes);
}
}

void clearFolderSearch(App& app) {
    app.folderSearchGeneration++;   // orphan any scan in flight
    app.folderResults.clear();
    app.folderResultHits.clear();
    if (app.hwnd) disarmTimer(app.hwnd, TIMER_FOLDER_SEARCH);
}

void startFolderSearchScan(App& app) {
    if (app.editMode || !app.folderSearchEnabled || !app.showSearch ||
        app.searchQuery.empty() || app.currentFile.empty()) {
        return;
    }
    int generation = ++app.folderSearchGeneration;
    std::wstring queryLower = toLower(app.searchQuery);
    std::filesystem::path current(toWide(app.currentFile));
    std::filesystem::path dir = current.parent_path();
    // UTF-8 rather than wide, and converted here rather than in the loop, because
    // the only thing it is used for is platform::documentPathsEqual below, which
    // takes UTF-8. Once per scan instead of once per candidate file.
    std::string currentNameUtf8 = platform::toUtf8(current.filename().wstring());
    HWND hwnd = app.hwnd;

    std::thread([generation, queryLower, dir, currentNameUtf8, hwnd] {
        auto* msg = new FolderScanMsg{generation, {}};
        std::error_code ec;
        int scanned = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (msg->files.size() >= 12 || scanned >= 200) break;
            if (!entry.is_regular_file(ec)) continue;
            std::wstring ext = entry.path().extension().wstring();
            for (auto& c : ext) c = (wchar_t)std::towlower(c);
            if (ext != L".md" && ext != L".markdown") continue;
            // Skip the document you are already searching. This replaces _wcsicmp,
            // an MSVC extension with no POSIX spelling, and it is deliberately the
            // platform:: SEAM rather than a local fold: these are filesystem names,
            // so "Notes.md" and "notes.md" are one file on Windows and two on Linux
            // and BOTH answers are right. themes.cpp reasons its way to the opposite
            // conclusion for theme names, and the contrast is the useful part --
            // platform_paths.h carries the full argument for both.
            //
            // Comparing bare filenames rather than full paths is sound here because
            // the iteration is over `dir`, which IS the current file's parent: inside
            // one directory a filename identifies a file.
            //
            // No Windows behaviour changes. _wcsicmp folded per code unit under
            // LC_CTYPE and the Windows seam folds UTF-8 bytes under the same, and
            // tinta never calls setlocale, so both run in the "C" locale and fold
            // ASCII and nothing else. Off Windows the comparison is exact, which is
            // the seam's whole point rather than a regression.
            if (platform::documentPathsEqual(
                    platform::toUtf8(entry.path().filename().wstring()), currentNameUtf8)) {
                continue;
            }
            if (entry.file_size(ec) > 1024 * 1024) continue;
            scanned++;

            std::ifstream file(entry.path(), std::ios::binary);
            if (!file) continue;
            std::string bytes((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
            std::wstring content = utf8ToWideString(bytes);
            std::wstring lower = content;
            for (auto& c : lower) c = (wchar_t)std::towlower(c);

            App::FolderFileResult result;
            size_t pos = 0;
            while ((pos = lower.find(queryLower, pos)) != std::wstring::npos) {
                result.totalMatches++;
                if (result.matches.size() < 3) {
                    size_t lineStart = content.rfind(L'\n', pos);
                    lineStart = (lineStart == std::wstring::npos) ? 0 : lineStart + 1;
                    size_t lineEnd = content.find(L'\n', pos);
                    if (lineEnd == std::wstring::npos) lineEnd = content.size();
                    size_t snipStart = pos > lineStart + 40 ? pos - 40 : lineStart;
                    size_t snipEnd = std::min(lineEnd, pos + queryLower.size() + 70);
                    App::FolderMatch m;
                    m.snippet = content.substr(snipStart, snipEnd - snipStart);
                    for (auto& c : m.snippet) {
                        if (c == L'\r' || c == L'\t') c = L' ';
                    }
                    m.matchStart = pos - snipStart;
                    m.matchLen = queryLower.size();
                    result.matches.push_back(std::move(m));
                }
                pos += queryLower.size();
            }
            if (result.totalMatches > 0) {
                result.fileName = entry.path().filename().wstring();
                result.fullPath = entry.path().wstring();
                msg->files.push_back(std::move(result));
            }
        }
        // The `delete` on failure is why postWindowMessage returns bool and returns
        // false off Windows: ownership of `msg` transfers to the handler only if the
        // post is accepted, so an off-Windows no-op that claimed success would leak
        // it. This line is already the correct off-Windows behaviour, unchanged.
        if (!postWindowMessage(hwnd, WM_APP_FOLDER_SEARCH, 0, (LPARAM)msg)) delete msg;
    }).detach();
}

void completeFolderSearch(App& app, void* results) {
    auto* msg = static_cast<FolderScanMsg*>(results);
    if (!msg) return;
    if (msg->generation != app.folderSearchGeneration ||
        !app.showSearch || app.editMode || !app.folderSearchEnabled) {
        delete msg;
        return;
    }
    app.folderResults = std::move(msg->files);
    app.folderResultHits.clear();
    delete msg;
    if (app.hwnd) requestRepaint(app.hwnd);
}

void performSearch(App& app) {
    app.searchMatches.clear();
    app.searchCurrentIndex = 0;
    app.searchMatchCursor = 0;

    // Folder-wide results follow the query, debounced on a timer so fast
    // typing doesn't spawn a scan per keystroke
    if (!app.editMode && app.folderSearchEnabled && app.hwnd) {
        if (app.searchQuery.empty()) {
            clearFolderSearch(app);
        } else if (!app.currentFile.empty()) {
            armTimer(app.hwnd, TIMER_FOLDER_SEARCH, 250);
        }
    }

    if (app.searchQuery.empty() || !app.root) return;

    // docText and textRects must cover the whole document before searching
    ensureLayoutComplete(app);

    // Use layout-built document text when available
    if (app.docText.empty()) {
        extractText(app.root, app.docText);
    }
    if (app.docText.empty()) return;
    if (app.docTextLower.empty()) {
        app.docTextLower = toLower(app.docText);
    }

    std::wstring queryLower = toLower(app.searchQuery);
    const std::wstring& textLower = app.docTextLower;

    // Estimate match count to avoid repeated vector reallocation
    app.searchMatches.reserve(64);

    size_t pos = 0;
    while ((pos = textLower.find(queryLower, pos)) != std::wstring::npos) {
        App::SearchMatch match;
        match.textRectIndex = kNoTextRect;
        match.startPos = pos;
        match.length = app.searchQuery.length();
        match.highlightRect = tinta::rectF(0, 0, 0, 0);
        app.searchMatches.push_back(match);
        pos += app.searchQuery.length();
    }

    mapSearchMatchesToLayout(app);
}

void mapSearchMatchesToLayout(App& app) {
    for (auto& match : app.searchMatches) {
        match.textRectIndex = kNoTextRect;
        match.highlightRect = tinta::rectF(0, 0, 0, 0);
    }
    if (app.searchMatches.empty()) return;
    if (app.textRects.empty()) return;

    size_t matchIndex = 0;
    for (size_t textRectIndex = 0; textRectIndex < app.textRects.size();
         textRectIndex++) {
        const auto& tr = app.textRects[textRectIndex];
        size_t rectStart = tr.docStart;
        size_t rectEnd = rectStart + tr.docLength;
        if (rectEnd <= rectStart) continue;

        while (matchIndex < app.searchMatches.size()) {
            const auto& m = app.searchMatches[matchIndex];
            size_t mEnd = m.startPos + m.length;
            if (mEnd <= rectStart) {
                matchIndex++;
                continue;
            }
            break;
        }

        size_t mi = matchIndex;
        while (mi < app.searchMatches.size()) {
            auto& m = app.searchMatches[mi];
            if (m.startPos >= rectEnd) break;

            size_t mEnd = m.startPos + m.length;
            size_t overlapStart = std::max(rectStart, m.startPos);
            size_t overlapEnd = std::min(rectEnd, mEnd);
            if (overlapStart < overlapEnd) {
                float totalWidth = tr.rect.right - tr.rect.left;
                float charWidth = totalWidth / static_cast<float>(tr.docLength);
                float startX =
                    tr.rect.left + static_cast<float>(overlapStart - rectStart) * charWidth;
                float endX =
                    startX + static_cast<float>(overlapEnd - overlapStart) * charWidth;
                tinta::RectF fragment =
                    tinta::rectF(startX, tr.rect.top, endX, tr.rect.bottom);

                if (m.textRectIndex == kNoTextRect) {
                    m.textRectIndex = textRectIndex;
                    m.highlightRect = fragment;
                } else {
                    m.highlightRect.left = std::min(m.highlightRect.left, fragment.left);
                    m.highlightRect.top = std::min(m.highlightRect.top, fragment.top);
                    m.highlightRect.right = std::max(m.highlightRect.right, fragment.right);
                    m.highlightRect.bottom = std::max(m.highlightRect.bottom, fragment.bottom);
                }
            }

            if (mEnd <= rectEnd) {
                mi++;
            } else {
                break;
            }
        }

        matchIndex = mi;
    }
}

void scrollToCurrentMatch(App& app) {
    if (app.searchMatches.empty() || app.searchCurrentIndex < 0 ||
        app.searchCurrentIndex >= (int)app.searchMatches.size()) return;

    const auto& match = app.searchMatches[app.searchCurrentIndex];

    bool hasLayoutBounds = match.textRectIndex != kNoTextRect;
    float estimatedY = hasLayoutBounds ? match.highlightRect.top : -1.0f;

    // Fallback: estimate based on character ratio
    if (estimatedY < 0.0f) {
        if (app.docText.empty()) return;
        float positionRatio = (float)match.startPos / (float)app.docText.length();
        estimatedY = positionRatio * app.contentHeight;
    }

    // Center this position in viewport (account for search bar)
    float searchBarHeight = 60.0f;
    app.targetScrollY = estimatedY - (app.height - searchBarHeight) / 2.0f;

    // Clamp scroll
    float maxScroll = std::max(0.0f, app.contentHeight - app.height);
    app.targetScrollY = std::max(0.0f, std::min(app.targetScrollY, maxScroll));
    app.scrollY = app.targetScrollY;

    if (hasLayoutBounds) {
        float viewportWidth = documentViewportWidth(app);
        float viewportLeft = app.scrollX;
        float viewportRight = viewportLeft + viewportWidth;
        if (viewportWidth > 0.0f &&
            (match.highlightRect.left < viewportLeft ||
             match.highlightRect.right > viewportRight)) {
            float matchCenter =
                (match.highlightRect.left + match.highlightRect.right) * 0.5f;
            app.targetScrollX = matchCenter - viewportWidth * 0.5f;
        } else {
            app.targetScrollX = app.scrollX;
        }

        float maxScrollX = std::max(0.0f, app.contentWidth - viewportWidth);
        app.targetScrollX =
            std::max(0.0f, std::min(app.targetScrollX, maxScrollX));
        app.scrollX = app.targetScrollX;
    }
}
