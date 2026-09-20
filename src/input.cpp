#include "input.h"
#include "clipboard.h"
#include "platform_paths.h"
#include "annotations.h"
#include "drafts.h"
#include "editrail.h"
#include "startpage.h"
#include "tabs.h"
#include "document.h"
#include "editor.h"
#include "file_utils.h"
#include "utils.h"
#include "search.h"
#include "d2d_init.h"
#include "settings.h"
#include "render.h"
#include "overlays.h"
#include "signals.h"
#include "tableedit.h"
#include "pandoc.h"
#include "print.h"
#include "export.h"
#include "i18n.h"
#include "selection.h"
#include "file_dialog.h"
// Named directly rather than relied on through app.h, which happens to include it:
// this file now calls setCursorShape, captureMouse, releaseMouse and
// releaseMouseIfCaptured, and an include that arrives by accident is one someone can
// remove from a header two files away without a single error here.
#include "win32_api.h"

// INT_MIN, for themeChooserHitAt's "nothing" sentinel below. NOT a consequence of
// this pass -- that sentinel is older than the port, and it compiled here only
// because MSVC's <windows.h> happens to pull the C limits in. The census could not
// SEE the gap while this file was fatal on <windowsx.h>: a stopped read reports
// `errors 0`, so it hides ordinary portability defects exactly as well as it hides
// Win32 ones. Clearing a fatal include is therefore a discovery step, and this is
// what it discovered.
#include <climits>

// <windowsx.h> AND <shellapi.h> ARE BOTH GONE, and they had to go together.
//
// They sat on these two adjacent lines, and each was a FATAL include for
// scripts/census-nonportable-linux.sh -- g++ stops at the first header it cannot
// find, so removing either one alone left the census printing
// `fatal <the other one>, errors 0` and moved the reading by nothing. That is why
// the entry-point signature change and the ShellExecuteW routing are one slice
// and not two: the row this file has shown for the whole port is not a small
// number, it is NO number, and only both halves together make it print one.
//
// windowsx.h was 59 GET_*_LPARAM uses -- see include/input.h on why this read 55 for
// a day, and why 55 is a true number about something else -- re-extracting coordinates inside handlers
// that now take them; shellapi.h was five ShellExecuteW calls, four of which are
// platform_paths.h seam calls above, plus the HDROP enumeration that moved into
// the message pump. Nothing was stubbed or reimplemented to get here.
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

// The four cached cursor handles that used to live here are GONE, and the comment
// that justified them -- "avoid calling LoadCursor() on every WM_MOUSEMOVE" -- went
// with them because win32_api.h's setCursorShape now holds the identical four
// statics and says why that is lifetime-neutral. There were FIVE, not four: a
// function-local `static HCURSOR replaceIBeam = LoadCursor(nullptr, IDC_IBEAM)` sat
// at the old :1162 loading the same system cursor a file-scope static two hundred
// lines up already held. Both spellings were correct and neither leaked, which is
// exactly why nothing ever reported the duplicate; naming a SHAPE instead of a
// handle is what made it impossible to write twice.

static const App::TaskRect* taskRectAt(const App& app);
static void startPageInvoke(App& app, HWND hwnd, int id);
static bool tableCopyButtonAt(const App& app, int mouseX, int mouseY);
static bool diagramPngButtonAt(const App& app, int mouseX, int mouseY);
static bool tableFitButtonAt(const App& app, int mouseX, int mouseY);
static bool diagramFitButtonAt(const App& app, int mouseX, int mouseY);
static void toggleFitBlock(App& app, HWND hwnd, unsigned key);

// Drops the link-peek popup and its rendered bitmap
static void clearLinkPeek(App& app) {
    app.linkPeekActive = false;
    app.linkPeekUrl.clear();
    app.linkPeekBitmap.reset();
}

// Inline image under the document point, or nullptr (viewer lightbox)
static const App::LayoutBitmap* layoutBitmapAt(const App& app, float docX,
                                               float docY) {
    for (const auto& lb : app.layoutBitmaps) {
        if (lb.bitmap && docX >= lb.destRect.left &&
            docX <= lb.destRect.right && docY >= lb.destRect.top &&
            docY <= lb.destRect.bottom) {
            return &lb;
        }
    }
    return nullptr;
}

static bool cursorPointInRect(float x, float y, const tinta::RectF& rect) {
    return x >= rect.left && x <= rect.right &&
           y >= rect.top && y <= rect.bottom;
}

static void setCursorFromHits(const std::vector<std::pair<tinta::RectF, int>>& hits,
                              float x, float y, bool textFields = false,
                              bool sliders = false) {
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
        if (!cursorPointInRect(x, y, it->first)) continue;
        int action = it->second;
        if (sliders && (action == SET_SLIDER_READING || action == SET_SLIDER_ZEN)) {
            setCursorShape(CursorShape::SizeWE);
        } else if (textFields && action >= TE_FIELD_BG && action <= TE_FIELD_NAME) {
            setCursorShape(CursorShape::IBeam);
        } else {
            setCursorShape(CursorShape::Hand);
        }
        return;
    }
    setCursorShape(CursorShape::Arrow);
}

static void setPrintPreviewCursor(const App& app, float x, float y) {
    if (cursorPointInRect(x, y, app.printPreviewPrintBtn) ||
        cursorPointInRect(x, y, app.printPreviewCancelBtn)) {
        setCursorShape(CursorShape::Hand);
        return;
    }
    for (int i = 0; i < PRINT_PAPER_COUNT; i++) {
        if (cursorPointInRect(x, y, app.printPreviewPaperBtn[i])) {
            setCursorShape(CursorShape::Hand);
            return;
        }
    }
    for (const auto& rect : app.printPreviewOrientBtn) {
        if (cursorPointInRect(x, y, rect)) {
            setCursorShape(CursorShape::Hand);
            return;
        }
    }
    setCursorShape(CursorShape::Arrow);
}

// Resolve the chooser hit under a point (rects from the last paint);
// INT_MIN = nothing
static int themeChooserHitAt(const App& app, float x, float y) {
    for (const auto& hit : app.themeChooserHits) {
        if (x >= hit.first.left && x <= hit.first.right &&
            y >= hit.first.top && y <= hit.first.bottom) {
            return hit.second;
        }
    }
    return INT_MIN;
}

static void setThemeChooserCursor(const App& app, float x, float y) {
    setCursorShape(themeChooserHitAt(app, x, y) != INT_MIN ? CursorShape::Hand
                                                           : CursorShape::Arrow);
}

static void setTocCursor(App& app, float x, float y) {
    // Shares the floating card's geometry (t13); the pin and close
    // controls get the hand as well
    if (cursorPointInRect(x, y, app.tocPinRect) ||
        cursorPointInRect(x, y, app.tocCloseRect) ||
        tocItemIndexAt(app, x, y) >= 0) {
        setCursorShape(CursorShape::Hand);
        return;
    }
    setCursorShape(CursorShape::Arrow);
}

static void setFolderBrowserCursor(const App& app, float x, float y) {
    FolderBrowserMetrics g = folderBrowserMetrics(app);
    if (x < g.panelX || x > g.panelX + g.panelWidth) {
        setCursorShape(CursorShape::Arrow);
        return;
    }

    bool inHeader = y >= g.headerY && y <= g.headerY + g.headerH;

    if (app.folderBrowserEditingPath) {
        setCursorShape(inHeader ? CursorShape::IBeam : CursorShape::Arrow);
        return;
    }
    if (inHeader && y >= g.btnY && y <= g.btnY + g.btnSize &&
        ((x >= g.pinBtnX && x <= g.pinBtnX + g.btnSize) ||
         (x >= g.folderBtnX && x <= g.folderBtnX + g.btnSize) ||
         (x >= g.fileBtnX && x <= g.fileBtnX + g.btnSize))) {
        setCursorShape(CursorShape::Hand);
        return;
    }
    if (inHeader && x >= g.cardLeft + dpi(app, 6.0f) &&
        x < g.folderBtnX - dpi(app, 2.0f)) {
        setCursorShape(CursorShape::Hand);
        return;
    }

    if (app.folderBrowserNaming != 0 &&
        y >= g.listStartY && y <= g.listStartY + g.itemHeight) {
        setCursorShape(CursorShape::IBeam);
        return;
    }
    if (folderItemIndexAt(app, x, y) >= 0) {
        setCursorShape(CursorShape::Hand);
        return;
    }
    setCursorShape(CursorShape::Arrow);
}

static void setSearchCursor(const App& app, float x, float y) {
    float barWidth = std::min(dpi(app, 500.0f), app.width - dpi(app, 40.0f));
    float barHeight = dpi(app, 44.0f);
    float centerWidth = (float)app.width;
    if (app.editMode) {
        float paneWidth = editorPaneWidth(app);
        barWidth = std::min(barWidth, paneWidth - dpi(app, 40.0f));
        centerWidth = paneWidth;
    }
    float barX = (centerWidth - barWidth) / 2.0f;
    float barY = dpi(app, 20.0f) * app.searchAnimation -
                 barHeight * (1.0f - app.searchAnimation);
    float textX = barX + dpi(app, 42.0f);
    float textRight = barX + barWidth - dpi(app, 120.0f);
    if (x >= textX && x <= textRight && y >= barY && y <= barY + barHeight) {
        setCursorShape(CursorShape::IBeam);
    } else {
        setCursorShape(CursorShape::Arrow);
    }
}

// Drag auto-scroll (#83): while a selection drag sits in the edge band,
// TIMER_SELECT_SCROLL scrolls the document and drags the focus offset along
void handleSelectScrollTimer(App& app, HWND hwnd) {
    if (!app.selecting || app.selAutoScrollVel == 0.0f || app.editMode) {
        disarmTimer(hwnd, TIMER_SELECT_SCROLL);
        app.selAutoScrollVel = 0.0f;
        return;
    }
    float maxScroll = std::max(0.0f, app.contentHeight - app.height);
    float next = std::clamp(app.scrollY + app.selAutoScrollVel, 0.0f, maxScroll);
    if (next == app.scrollY) return;  // pinned at an end; keep the timer for reversals
    app.scrollY = next;
    app.targetScrollY = next;

    // app.mouseX/Y hold the drag position — with mouse capture they update
    // even outside the window, and holding still keeps the last position
    float docX = ((float)app.mouseX - documentViewportX(app)) + app.scrollX;
    float docY = (float)app.mouseY + app.scrollY;
    size_t off = selectionOffsetAtPoint(app, docX, docY);
    if (app.selectionMode == App::SelectionMode::Word) {
        size_t ws, we;
        selectionWordRange(app, off, ws, we);
        app.selAnchor = std::min(app.selAnchorRangeStart, ws);
        app.selFocus = std::max(app.selAnchorRangeEnd, we);
    } else if (app.selectionMode == App::SelectionMode::Line) {
        size_t ls, le;
        if (selectionLineRange(app, off, ls, le)) {
            app.selAnchor = std::min(app.selAnchorRangeStart, ls);
            app.selFocus = std::max(app.selAnchorRangeEnd, le);
        }
    } else {
        app.selFocus = off;
    }
    requestRepaint(hwnd);
}

// Loads a document into the viewer and resets per-document state.
// Shared by the folder browser's item clicks, path input, and new-file flow.
bool openDocumentInViewer(App& app, const std::wstring& fullPath) {
    std::ifstream file(fullPath);
    if (!file) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto result = parseDocument(app.parser, buffer.str(), fullPath);
    if (!result.success) return false;

    // Review annotations (#126): keep the raw source and derive marks from it
    app.sourceText = buffer.str();
    annotationsParseSource(app);
    if (app.annotEditorOpen) annotationEditorCancel(app);
    if (app.showLightbox) closeLightbox(app);
    clearLinkPeek(app);
    app.fitBlocks.clear();
    app.startPageEmbeddedOpen = false;  // a real document takes the view

    // Reading position memory (#77): keep the old document's position,
    // resume the new one's
    if (!app.currentFile.empty()) {
        persistReadingPosition(app.currentFile, app.scrollY, app.zoomFactor);
    }

    app.root = result.root;
    app.parseTimeUs = result.parseTimeUs;
    app.currentFile = platform::toUtf8(fullPath);
    // Start page recents: every open (including tab switches) refreshes
    // the document's spot at the head of the list
    persistRecentFile(app.currentFile);
    app.scrollY = 0;
    app.scrollX = 0;
    app.targetScrollY = 0;
    app.targetScrollX = 0;
    {
        Settings stored = loadSettings();
        app.pendingScrollRestore = findReadingPosition(stored, app.currentFile);
        // Per-document zoom (#zoom-memory): a document remembered at a
        // different zoom reopens there
        float zoom = findReadingZoom(stored, app.currentFile);
        if (zoom > 0.0f && fabsf(zoom - app.zoomFactor) > 0.01f) {
            app.zoomFactor = zoom;
            app.appliedZoomFactor = zoom;
            updateTextFormats(app);
        }
    }
    app.focusMermaidOnNextLayout = isMermaidDocumentPath(fullPath);
    app.contentHeight = 0;
    app.verticalScrollbarVisible = false;
    app.scrollbarContentHeight = 0.0f;
    app.docText.clear();
    std::wstring().swap(app.docTextLower);
    app.searchMatches.clear();
    clearSelection(app);  // offsets belong to the old docText
    app.layoutDirty = true;
    updateFileWriteTime(app);
    updateWindowTitle(app);
    return true;
}

// launchDocumentWindow HAS LEFT THIS FILE. It is src/quicknote_win32.cpp now,
// declared under _WIN32 in include/utils.h, because it is a THIRD instance of
// the deferral that file argues at length -- a self-relaunch passing flags only
// WinMain parses. It is not here because of ShellExecuteW; the other four
// ShellExecuteW calls in this file DID become seam calls in the same slice.
// The difference is that those four have working Qt halves and this one's would
// start a process that ignores --cascade and --edit.

// N key / context menu: open the folder browser with the new-file naming
// row already active
static void startNewFileFlow(App& app, HWND hwnd) {
    if (!app.showFolderBrowser) {
        app.showFolderBrowser = true;
        app.folderBrowserAnimation = 0;
        if (!app.currentFile.empty()) {
            app.folderBrowserPath = getDirectoryFromFile(app.currentFile);
        } else {
            wchar_t cwd[MAX_PATH];
            if (GetCurrentDirectoryW(MAX_PATH, cwd)) {
                app.folderBrowserPath = cwd;
            }
        }
        populateFolderItems(app);
    }
    app.folderBrowserNaming = 1;  // new file
    app.folderBrowserInput.clear();
    app.folderBrowserInputSelectAll = false;
    app.folderBrowserInputError = false;
    app.folderInputJustOpened = true;
    app.folderBrowserScroll = 0.0f;
    updateBlinkTimer(app);
    resetCursorBlink(app);
    requestRepaint(hwnd);
}

// --- Folder browser path/name input (#52) ---

static void closeFolderBrowserInput(App& app) {
    app.folderBrowserEditingPath = false;
    app.folderBrowserNaming = 0;
    app.folderBrowserInput.clear();
    app.folderBrowserInputSelectAll = false;
    app.folderBrowserInputError = false;
    updateBlinkTimer(app);
}

// clipboardLine IS GONE, replaced at all THREE call sites by the seam's own spelling:
// two here (handleKeyDown's Ctrl+V and handleCharInput's) and one in editor.cpp. The census that scoped this seam
// said TWO, and the reason it said two is worth keeping: the grep that produced it
// filtered out `^src/input.cpp:35` to drop its now-HISTORICAL definition at :351, and
// that pattern also swallows every line from 3500 to 3599 -- where one of the calls
// lived. A line-number prefix is a prefix, not a line number, and the build is what
// caught it.
//
// The replacement is:
// platform::flattenToSingleLine(platform::clipboardText()). It was the only one of the
// three reads that is not a normalization -- it maps CR, LF and TAB each to ONE space,
// so a CRLF becomes TWO spaces. include/clipboard.h keeps that, and keeping it is the
// reason the seam hands back raw text instead of normalizing on the way out.

// Trims whitespace and the quotes Explorer's "Copy as path" adds
static std::wstring cleanPathInput(std::wstring s) {
    auto trim = [](std::wstring& t) {
        size_t a = t.find_first_not_of(L" ");
        size_t b = t.find_last_not_of(L" ");
        t = (a == std::wstring::npos) ? L"" : t.substr(a, b - a + 1);
    };
    trim(s);
    if (s.length() >= 2 && s.front() == L'"' && s.back() == L'"') {
        s = s.substr(1, s.length() - 2);
        trim(s);
    }
    return s;
}

// Enter in the path box: a directory browses there, a file opens
static void commitFolderBrowserPath(App& app) {
    std::wstring path = cleanPathInput(app.folderBrowserInput);
    if (path.empty()) {
        closeFolderBrowserInput(app);
        return;
    }
    wchar_t expanded[MAX_PATH];
    if (ExpandEnvironmentStringsW(path.c_str(), expanded, MAX_PATH) > 0) {
        path = expanded;
    }

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        app.folderBrowserInputError = true;
        return;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        // Strip a trailing separator (but keep drive roots like C:\)
        while (path.length() > 3 && (path.back() == L'\\' || path.back() == L'/')) {
            path.pop_back();
        }
        app.folderBrowserPath = path;
        populateFolderItems(app);
        closeFolderBrowserInput(app);
        return;
    }
    if (isSupportedDropPath(path)) {
        if (app.tabNewTabIntent) {
            app.tabNewTabIntent = false;
            tabOpenPath(app, app.hwnd, toUtf8(path), true);
            closeFolderBrowserInput(app);
        } else if (openDocumentInViewer(app, path)) {
            // Preview mode: the browser stays open beside the document —
            // close it by clicking the document, pressing B, or Esc
            closeFolderBrowserInput(app);
        } else {
            app.folderBrowserInputError = true;
        }
    } else {
        app.folderBrowserInputError = true;
    }
}

// Enter in the naming box: creates the file/folder in the browsed directory.
// A new folder is entered; a new file opens straight into edit mode.
static void commitFolderBrowserNaming(App& app) {
    std::wstring name = cleanPathInput(app.folderBrowserInput);
    if (name.empty()) {
        closeFolderBrowserInput(app);
        return;
    }
    if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos ||
        name == L"." || name == L"..") {
        app.folderBrowserInputError = true;
        return;
    }

    bool isFolder = app.folderBrowserNaming == 2;
    if (!isFolder && name.find(L'.') == std::wstring::npos) {
        name += L".md";
    }
    std::wstring fullPath = app.folderBrowserPath;
    if (!fullPath.empty() && fullPath.back() != L'\\' && fullPath.back() != L'/') {
        fullPath += L'\\';
    }
    fullPath += name;

    if (isFolder) {
        if (!CreateDirectoryW(fullPath.c_str(), nullptr)) {
            app.folderBrowserInputError = true;
            return;
        }
        app.folderBrowserPath = fullPath;
        populateFolderItems(app);
        closeFolderBrowserInput(app);
        return;
    }

    // CREATE_NEW fails if the file already exists instead of truncating it
    HANDLE h = CreateFileW(fullPath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        app.folderBrowserInputError = true;
        return;
    }
    CloseHandle(h);
    // The new note opens in its own window, offset from this one, so the
    // document being read stays exactly where it is (#74)
    closeFolderBrowserInput(app);
    app.showFolderBrowser = false;
    app.folderBrowserAnimation = 0;
    // GUARDED, AND THE GUARD IS THE ANSWER AT THE SITE, exactly as the
    // launchQuickNoteWindow guard in editor.cpp answers it for Ctrl+Shift+N (cited by
    // name, not line: that block moved twice in one day): off Windows, creating a note
    // from the folder browser makes the FILE and opens no window for it, because
    // the relaunch has no Qt half. The file exists and is findable; what is
    // missing is visible rather than silently substituted with a wrong window.
#ifdef _WIN32
    launchDocumentWindow(fullPath);
#endif
    if (app.hwnd) requestRepaint(app.hwnd);
}

// Ctrl+wheel zoom. The scroll anchor scales immediately, but text format
// recreation (~47 COM objects) + full relayout is applied on the leading
// tick and then coalesced via TIMER_ZOOM_APPLY while the wheel keeps spinning.
static void applyZoomDelta(App& app, float delta) {
    float oldZoom = app.zoomFactor;
    app.zoomFactor = std::max(0.5f, std::min(3.0f, app.zoomFactor + delta * 0.1f));
    float zoomRatio = app.zoomFactor / oldZoom;
    app.scrollY *= zoomRatio;
    app.targetScrollY *= zoomRatio;
    if (!app.zoomApplyPending) {
        updateTextFormats(app);
        app.zoomApplyPending = true;
        armTimer(app.hwnd, TIMER_ZOOM_APPLY, 80);
    }
}

// --- Theme editor helpers ---

static std::wstring colorToHexW(const tinta::Color& c) {
    wchar_t buf[8];
    swprintf_s(buf, L"%02X%02X%02X",
               (int)(c.r * 255.0f + 0.5f), (int)(c.g * 255.0f + 0.5f),
               (int)(c.b * 255.0f + 0.5f));
    return buf;
}

static bool hexToColorW(const std::wstring& s, tinta::Color& out) {
    if (s.size() != 6) return false;
    unsigned v = 0;
    for (wchar_t ch : s) {
        v <<= 4;
        if (ch >= L'0' && ch <= L'9') v |= (unsigned)(ch - L'0');
        else if (ch >= L'a' && ch <= L'f') v |= (unsigned)(ch - L'a' + 10);
        else if (ch >= L'A' && ch <= L'F') v |= (unsigned)(ch - L'A' + 10);
        else return false;
    }
    out = hexColor(v);
    return true;
}

// The six editable slots, in TE_FIELD_* / themeEditorHex order
static tinta::Color* themeEditorSlot(App& app, int field) {
    D2DTheme& t = app.themeEditorTheme;
    switch (field) {
        case 0: return &t.background;
        case 1: return &t.text;
        case 2: return &t.heading;
        case 3: return &t.link;
        case 4: return &t.accent;
        case 5: return &t.codeBackground;
    }
    return nullptr;
}

// Copies a base theme's colors into the working copy and refreshes the
// hex field strings (name and focus are preserved)
static void themeEditorAdoptBase(App& app, int baseIndex) {
    app.themeEditorBase = baseIndex;
    const D2DTheme& bt = themeAt(baseIndex);
    app.themeEditorTheme = bt;
    app.themeEditorFont = bt.fontFamily;
    for (int i = 0; i < 6; i++) {
        app.themeEditorHex[i] = colorToHexW(*themeEditorSlot(app, i));
    }
}

// Seeds themes.ini with a comment header if it does not exist, then opens
// it in the default editor. Shared by settings and the theme editor.
static void openThemesIniFile() {
    std::wstring p = getSettingsPath();
    size_t slash = p.find_last_of(L'\\');
    if (slash == std::wstring::npos) return;
    p = p.substr(0, slash + 1) + L"themes.ini";
    if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::ofstream f(p);
        f << "; Custom themes: [theme] sections, RRGGBB colors.\n";
    }
    platform::openInDefaultApplication(configFilePathForShell(p));
}

// editCurrent seeds the editor with the active theme's name so saving
// updates that custom theme in place (built-ins save as a copy)
static void openThemeEditor(App& app, bool editCurrent) {
    enumerateSystemFontFamilies(app);
    themeEditorAdoptBase(app, app.currentThemeIndex);
    app.themeEditorName = editCurrent ? app.theme.name : L"My theme";
    app.themeEditorField = 6;  // name focused first
    app.themeEditorFontScroll = 0.0f;
    app.showSettings = false;
    app.showThemeEditor = true;
    requestRepaint(app.hwnd);
}

static void closeThemeEditor(App& app, bool backToSettings) {
    app.showThemeEditor = false;
    if (backToSettings) {
        app.showSettings = true;
        app.settingsAnimation = 1.0f;  // no re-animation on return
    }
    requestRepaint(app.hwnd);
}

// Reading-width sliders: map a mouse X onto the stored track rect (#82)
static void settingsSliderApply(App& app, int which, float mx) {
    const tinta::RectF& track =
        app.settingsSliderTrack[which == SET_SLIDER_ZEN ? 1 : 0];
    float t = (mx - track.left) / std::max(1.0f, track.right - track.left);
    int pct = 30 + (int)(t * 70.0f + 0.5f);
    pct = std::max(30, std::min(100, pct));
    if (which == SET_SLIDER_ZEN) app.zenWidthPct = pct;
    else app.readingWidthPct = pct;
    app.layoutDirty = true;
    requestRepaint(app.hwnd);
}

// Applies one settings-overlay action (id from app.settingsHits)
// Shortcut editor: bind the armed action to `key` and land in the custom
// profile so the change persists via [Keys]
static void shortcutAssign(App& app, KeyBinding key) {
    int row = app.shortcutEditorRow;
    if (row < 0 || row >= KEY_ACTION_COUNT) return;
    std::string value = keyIniName(key);
    bool found = false;
    for (auto& kv : app.keyOverrides) {
        if (kv.first == KEY_ACTIONS[row].name) {
            kv.second = value;
            found = true;
            break;
        }
    }
    if (!found) app.keyOverrides.push_back({KEY_ACTIONS[row].name, value});
    Settings s;
    s.keyOverrides = app.keyOverrides;
    s.keyProfile = "custom";
    applyKeymap(app, s);
    app.shortcutEditorRow = -1;
}

static void settingsAction(App& app, HWND hwnd, int action) {
    // Shortcut profile picks (checked first: their base is above the
    // language pick base)
    if (action >= SET_KEYS_PICK_BASE) {
        int pick = action - SET_KEYS_PICK_BASE;
        const char* ids[] = {"windows", "vim", "custom"};
        if (pick >= 0 && pick < 3) {
            Settings s;
            s.keyOverrides = app.keyOverrides;  // session [Keys] for custom
            s.keyProfile = ids[pick];
            applyKeymap(app, s);  // also mirrors into app.keyProfile
        }
        app.settingsKeysOpen = false;
        requestRepaint(hwnd);
        return;
    }
    // Language dropdown picks: 0 = Auto, then the registry languages
    if (action >= SET_LANG_PICK_BASE) {
        int pick = action - SET_LANG_PICK_BASE;
        if (pick == 0) {
            app.languageSetting = -1;
            app.currentLanguageIndex = detectSystemLanguage();
        } else if (pick - 1 < languageCount()) {
            app.languageSetting = pick - 1;
            app.currentLanguageIndex = pick - 1;
        }
        app.settingsLangOpen = false;
        requestRepaint(hwnd);
        return;
    }
    if (action != SET_LANG_DROPDOWN) app.settingsLangOpen = false;
    if (action != SET_KEYS_DROPDOWN) app.settingsKeysOpen = false;
    switch (action) {
        case SET_SECTION_GENERAL: app.settingsSection = 0; break;
        case SET_SECTION_APPEARANCE: app.settingsSection = 1; break;
        case SET_SECTION_EDITOR: app.settingsSection = 2; break;
        case SET_TOGGLE_FOLDERSEARCH:
            app.folderSearchEnabled = !app.folderSearchEnabled;
            if (!app.folderSearchEnabled) clearFolderSearch(app);
            break;
        case SET_TOGGLE_BROWSEFOCUS:
            app.browserFocusPath = !app.browserFocusPath;
            break;
        case SET_TOGGLE_OPENTABS:
            app.openInTabs = !app.openInTabs;
            // Takes effect for the very next Explorer open, no window
            // closing required; also keeps older windows' exit saves
            // from clobbering it (they no longer write this flag)
            persistOpenInTabs(app);
            break;
        case SET_TOGGLE_ASSISTS:
            app.editorAssists = !app.editorAssists;
            persistEditorMode(app);
            break;
        case SET_TOGGLE_HEADRULES:
            app.headingRules = !app.headingRules;
            // Layout shifts (the rule adds height), so a repaint alone
            // is not enough
            app.layoutDirty = true;
            break;
        case SET_LOCATE_PANDOC: {
            // No restoreDirectory: this site never passed OFN_NOCHANGEDIR, so
            // browsing here still moves the process's current directory. Carried
            // rather than corrected -- see file_dialog.h on why the seam preserves
            // the divergence instead of unifying it.
            platform::FileDialogRequest request;
            request.owner = hwnd;
            request.filters = {{L"pandoc.exe", L"pandoc.exe"},
                               {L"Programs (*.exe)", L"*.exe"}};
            std::optional<platform::FileDialogResult> picked =
                platform::requestOpenPath(request);
            if (picked) {
                pandocSetUserPath(app, picked->path);
            }
            break;
        }
        case SET_TOGGLE_FOLLOW:
            app.followSystemTheme = !app.followSystemTheme;
            if (app.followSystemTheme) {
                if (app.theme.isDark) app.darkThemeIndex = app.currentThemeIndex;
                else app.lightThemeIndex = app.currentThemeIndex;
                postWindowMessage(hwnd, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
            }
            break;
        case SET_TOGGLE_WRAP:
            app.editorWordWrap = !app.editorWordWrap;
            app.clearEditorLineLayoutCache();
            break;
        case SET_TOGGLE_PREVIEW:
            app.editorShowPreview = !app.editorShowPreview;
            app.layoutDirty = true;
            break;
        case SET_OPEN_THEMES:
            app.showSettings = false;
            app.showThemeChooser = true;
            app.themeChooserAnimation = 0;
            break;
        case SET_NEW_THEME:
            openThemeEditor(app, false);
            break;
        case SET_EDIT_THEME:
            openThemeEditor(app, true);
            break;
        case SET_TOC_LEFT: app.tocOnLeft = true; break;
        case SET_LANG_DROPDOWN:
            app.settingsLangOpen = !app.settingsLangOpen;
            break;
        case SET_KEYS_DROPDOWN:
            app.settingsKeysOpen = !app.settingsKeysOpen;
            break;
        case SET_EDIT_KEYS:
            app.showSettings = false;
            app.showShortcutEditor = true;
            app.shortcutEditorRow = -1;
            break;
        case SET_TOC_RIGHT: app.tocOnLeft = false; break;
        case SET_OPEN_INI:
            platform::openInDefaultApplication(
                configFilePathForShell(getSettingsPath()));
            break;
        case SET_OPEN_THEMES_INI:
            openThemesIniFile();
            break;
        case SET_OPEN_LANGS_INI:
            openLanguagesIniFile();
            break;
    }
    requestRepaint(hwnd);
}

void handleMouseWheel(App& app, HWND hwnd, float wheelDelta) {
    // Lightbox: the wheel zooms the image
    if (app.showLightbox) {
        float delta = wheelDelta;
        float factor = delta > 0 ? 1.15f : 1.0f / 1.15f;
        app.lightboxZoom =
            std::max(0.2f, std::min(app.lightboxZoom * factor, 10.0f));
        requestRepaint(hwnd);
        return;
    }

    // Scrolling moves the document under a stationary cursor: drop any
    // link peek so it cannot go stale
    if (app.linkPeekActive || !app.linkPeekUrl.empty()) {
        clearLinkPeek(app);
        disarmTimer(hwnd, TIMER_LINK_PEEK);
    }

    // Theme editor: wheel scrolls the font list
    if (app.showThemeEditor) {
        float delta = wheelDelta;
        const tinta::RectF& list = app.themeEditorFontListRect;
        if (app.mouseX >= list.left && app.mouseX <= list.right &&
            app.mouseY >= list.top && app.mouseY <= list.bottom) {
            app.themeEditorFontScroll -= delta * dpi(app, 78.0f);
            if (app.themeEditorFontScroll < 0) app.themeEditorFontScroll = 0;
            requestRepaint(hwnd);
        }
        return;
    }

    // Settings overlay: consume the wheel (nothing scrolls yet)
    if (app.showSettings) return;

    // Print preview: the wheel flips pages
    if (app.showPrintPreview) {
        // Only the direction matters here, and dividing by a positive
        // constant preserves it, so the notch form answers this as the raw
        // delta did -- including a sub-notch scroll, which is positive in
        // both forms and rounds to zero in neither.
        printPreviewSetPage(app,
                            app.printPreviewPage + (wheelDelta < 0 ? 1 : -1));
        return;
    }
    if (app.showContextMenu) {
        app.showContextMenu = false;
        app.contextMenuAnimation = 0;
        app.hoveredContextMenuItem = -1;
    }
    if (app.showTabMenu) closeTabMenu(app);
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    float delta = wheelDelta;

    // Help overlay scroll
    if (app.showHelp) {
        app.helpScroll -= delta * dpi(app, 60.0f);
        if (app.helpScroll < 0) app.helpScroll = 0;
        requestRepaint(hwnd);
        return;
    }

    // Edit mode: the wheel scrolls the editor from either pane — the preview
    // follows through the scroll-anchor sync, so the panes cannot drift
    // apart and both sides always respond (#77)
    if (app.editMode) {
        float sepX = app.editorShowPreview
            ? app.width * app.editorSplitRatio
            : static_cast<float>(app.width);
        if (ctrl && app.mouseX < sepX) {
            applyZoomDelta(app, delta);
            requestRepaint(hwnd);
            return;
        }
        if (!ctrl) {
            float editorMax = std::max(0.0f, app.editorContentHeight - (float)app.height);
            if (delta < 0 && app.editorScrollY >= editorMax - 1.0f) {
                // Editor is at its end: scroll the page's remaining tail
                // directly (#85); the sync allows this overshoot. Same
                // sheet-aware clamp as the sync, or the overshoot dies in
                // the gap between the two formulas.
                float maxScroll =
                    std::max(0.0f, app.contentHeight + dpi(app, 18.0f) -
                                       editSheetRect(app).bottom);
                app.scrollY = std::min(app.scrollY - delta * dpi(app, 60.0f), maxScroll);
                app.targetScrollY = app.scrollY;
                requestRepaint(hwnd);
                return;
            }
            handleEditorMouseWheel(app, hwnd, delta);
            return;
        }
        // Ctrl over the preview pane: fall through to document zoom
    }

    // Handle folder browser scroll (not when Ctrl is held — that's zoom)
    if (app.showFolderBrowser && !ctrl) {
        float panelWidth = folderBrowserPanelWidth(app);
        float panelX = -panelWidth * (1.0f - app.folderBrowserAnimation);
        if (app.mouseX >= panelX && app.mouseX <= panelX + panelWidth) {
            // Scroll folder list
            app.folderBrowserScroll -= delta * dpi(app, 60.0f);
            float itemHeight = dpi(app, 28.0f);
            float headerHeight = dpi(app, 48.0f);
            float listHeight = app.height - headerHeight - dpi(app, 20.0f);
            float totalItemsHeight = app.folderItems.size() * itemHeight +
                (app.folderBrowserNaming != 0 ? itemHeight : 0.0f);
            float maxScroll = std::max(0.0f, totalItemsHeight - listHeight);
            app.folderBrowserScroll = std::max(0.0f, std::min(app.folderBrowserScroll, maxScroll));
            requestRepaint(hwnd);
            return;
        }
    }

    // Handle TOC scroll (not when Ctrl is held — that's zoom)
    if (app.showToc && !ctrl) {
        float panelWidth = tocPanelWidth(app);
        float panelX = tocPanelX(app, panelWidth);
        if (app.mouseX >= panelX && app.mouseX <= panelX + panelWidth) {
            app.tocScroll -= delta * dpi(app, 60.0f);
            float itemHeight = dpi(app, 28.0f);
            float headerHeight = dpi(app, 48.0f);
            float listHeight = app.height - headerHeight - dpi(app, 20.0f);
            float totalItemsHeight = app.headings.size() * itemHeight;
            float maxScroll = std::max(0.0f, totalItemsHeight - listHeight);
            app.tocScroll = std::max(0.0f, std::min(app.tocScroll, maxScroll));
            requestRepaint(hwnd);
            return;
        }
    }

    if (ctrl) {
        // Zoom in/out — scale scroll position to keep content anchored
        applyZoomDelta(app, delta);
    } else if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        // Shift+wheel: horizontal scroll (wheel up = left), same as tilt wheels
        app.targetScrollX -= delta * dpi(app, 60.0f);
        float maxScrollX = std::max(
            0.0f, app.contentWidth - documentViewportWidth(app));
        app.targetScrollX = std::max(0.0f, std::min(app.targetScrollX, maxScrollX));
        app.scrollX = app.targetScrollX;
    } else {
        // Normal scroll
        app.targetScrollY -= delta * dpi(app, 60.0f);
        float maxScroll = std::max(0.0f, app.contentHeight - app.height);
        app.targetScrollY = std::max(0.0f, std::min(app.targetScrollY, maxScroll));
        app.scrollY = app.targetScrollY;
    }

    requestRepaint(hwnd);
}

void handleMouseHWheel(App& app, HWND hwnd, float wheelDelta) {
    // Horizontal scroll
    float delta = wheelDelta * dpi(app, 60.0f);
    app.targetScrollX += delta;

    float maxScrollX = std::max(
        0.0f, app.contentWidth - documentViewportWidth(app));
    app.targetScrollX = std::max(0.0f, std::min(app.targetScrollX, maxScrollX));
    app.scrollX = app.targetScrollX;

    requestRepaint(hwnd);
}

void handleMouseMove(App& app, HWND hwnd, int mouseX, int mouseY) {
    bool mouseMoved = app.mouseX != mouseX || app.mouseY != mouseY;
    app.mouseX = mouseX;
    app.mouseY = mouseY;

    bool iconHover = appMenuButtonAt(app, (float)app.mouseX, (float)app.mouseY);
    if (iconHover != app.appMenuHover) {
        app.appMenuHover = iconHover;
        requestRepaint(hwnd);
    }
    if (iconHover && appMenuAvailable(app) && app.tabDragIndex < 0 &&
        !app.editorSelecting && !app.draggingSeparator && !app.selecting) {
        setCursorShape(CursorShape::Hand);
        return;
    }
    // An armed tab drag owns the mouse until release (capture held)
    if (app.tabDragIndex >= 0) {
        tabDragMove(app, hwnd, app.mouseX, app.mouseY);
        return;
    }

    // Lightbox: drag pans the image
    if (app.showLightbox) {
        if (app.lightboxDragging) {
            int dx = app.mouseX - app.lightboxDragStartX;
            int dy = app.mouseY - app.lightboxDragStartY;
            if (abs(dx) > 3 || abs(dy) > 3) app.lightboxDragMoved = true;
            app.lightboxPanX = app.lightboxDragPanX + (float)dx;
            app.lightboxPanY = app.lightboxDragPanY + (float)dy;
            requestRepaint(hwnd);
        }
        tinta::RectF img = lightboxImageRect(app);
        bool over = (float)app.mouseX >= img.left &&
                    (float)app.mouseX <= img.right &&
                    (float)app.mouseY >= img.top &&
                    (float)app.mouseY <= img.bottom;
        setCursorShape(over || app.lightboxDragging ? CursorShape::Hand : CursorShape::Arrow);
        return;
    }

    // Tab strip hover (close-button reveal) and switcher row hover
    {
        int newHover = -1;
        bool overStripButton = false;  // + and chevron want the hand too
        if ((float)app.mouseY < chromeTopHeight(app)) {
            for (const App::TabHit& hit : app.tabHits) {
                if (app.mouseX >= hit.rect.left &&
                    app.mouseX <= hit.rect.right &&
                    app.mouseY >= hit.rect.top &&
                    app.mouseY <= hit.rect.bottom) {
                    if (hit.index >= 0) {
                        newHover = hit.index;
                    } else {
                        overStripButton = true;
                    }
                    break;
                }
            }
        }
        if (newHover != app.hoveredTab) {
            app.hoveredTab = newHover;
            requestRepaint(hwnd);
        }
        if (app.captionButtonHover) {
            // The pointer is back in the client area
            app.captionButtonHover = 0;
            requestRepaint(hwnd);
        }
        if (app.showTabSwitcher) {
            int rowHover = -1;
            for (const auto& [rect, index] : app.tabSwitcherHits) {
                if (app.mouseX >= rect.left && app.mouseX <= rect.right &&
                    app.mouseY >= rect.top && app.mouseY <= rect.bottom) {
                    rowHover = index;
                    break;
                }
            }
            if (rowHover != app.tabSwitcherHover) {
                app.tabSwitcherHover = rowHover;
                requestRepaint(hwnd);
            }
            setCursorShape(rowHover >= 0 ? CursorShape::Hand : CursorShape::Arrow);
            return;
        }
        if (newHover >= 0 || overStripButton) {
            setCursorShape(CursorShape::Hand);
            return;
        }
    }

    // Print preview: no hover states; document hit-testing below would run
    // against print-layout coordinates anyway
    if (app.showPrintPreview) {
        setPrintPreviewCursor(app, (float)app.mouseX, (float)app.mouseY);
        return;
    }

    // Modal overlays own the cursor while open. Their hit rectangles are
    // rebuilt during render, so cursor and mouse-up use the same geometry.
    if (app.showThemeEditor) {
        setCursorFromHits(app.themeEditorHits, (float)app.mouseX,
                          (float)app.mouseY, true);
        return;
    }
    // Tab menu open: hover tracks its items only (the menu sits above
    // every panel, like the strip that spawned it)
    if (app.showTabMenu) {
        int item = tabMenuItemAt(app, (float)app.mouseX, (float)app.mouseY);
        if (item != app.tabMenuHover) {
            app.tabMenuHover = item;
            requestRepaint(hwnd);
        }
        setCursorShape(item >= 0 ? CursorShape::Hand : CursorShape::Arrow);
        return;
    }
    if (app.showShortcutEditor) {
        setCursorFromHits(app.shortcutHits, (float)app.mouseX,
                          (float)app.mouseY);
        return;
    }
    if (app.editMode && app.confirmExitPending) {
        setCursorFromHits(app.confirmExitHits, (float)app.mouseX,
                          (float)app.mouseY);
        return;
    }

    // Settings: drag a slider if one is held, and show a hand over controls
    // instead of the document's text caret
    if (app.showSettings) {
        if (app.settingsDragSlider) {
            settingsSliderApply(app, app.settingsDragSlider, (float)app.mouseX);
            setCursorShape(CursorShape::SizeWE);
            return;
        }
        setCursorFromHits(app.settingsHits, (float)app.mouseX,
                          (float)app.mouseY, false, true);
        return;
    }

    // Context menu open: hover tracks menu items only — document hover
    // (code-block copy button, link underline) stays suppressed underneath
    if (app.showContextMenu) {
        bool repaint = updateContextMenuHover(app, (float)app.mouseX,
                                             (float)app.mouseY, mouseMoved);
        int item = app.hoveredContextMenuItem;
        if (app.hoveredCodeBlock != -1) {
            app.hoveredCodeBlock = -1;
            repaint = true;
        }
        if (!app.hoveredLink.empty()) {
            app.hoveredLink.clear();
            repaint = true;
        }
        setCursorShape(contextMenuItemEnabled(app, item) ? CursorShape::Hand : CursorShape::Arrow);
        if (repaint) requestRepaint(hwnd);
        return;
    }

    // Help overlay scrollbar dragging
    if (app.helpScrollbarDragging) {
        setCursorShape(CursorShape::Arrow);
        float maxScroll = std::max(0.0f, app.helpContentHeight - app.helpVisibleHeight);
        if (maxScroll > 0) {
            float sbHeight = app.helpVisibleHeight / app.helpContentHeight * app.helpVisibleHeight;
            sbHeight = std::max(sbHeight, dpi(app, 20.0f));
            float trackHeight = app.helpVisibleHeight - sbHeight;

            float deltaY = (float)app.mouseY - app.helpScrollbarDragStartY;
            float scrollDelta = (trackHeight > 0) ? (deltaY / trackHeight) * maxScroll : 0;
            app.helpScroll = app.helpScrollbarDragStartScroll + scrollDelta;
            app.helpScroll = std::max(0.0f, std::min(app.helpScroll, maxScroll));
            requestRepaint(hwnd);
        }
        return;
    }
    if (app.showHelp) {
        setCursorShape(CursorShape::Arrow);
        return;
    }

    if (app.showSearch) {
        bool overResult = false;
        for (const auto& hit : app.folderResultHits) {
            if (cursorPointInRect((float)app.mouseX, (float)app.mouseY, hit.rect)) {
                overResult = true;
                break;
            }
        }
        if (folderSearchToggleAt(app, (float)app.mouseX, (float)app.mouseY) || overResult) {
            setCursorShape(CursorShape::Hand);
        } else {
            setSearchCursor(app, (float)app.mouseX, (float)app.mouseY);
        }
        return;
    }

    if (app.showFolderBrowser) {
        FolderBrowserMetrics fbm = folderBrowserMetrics(app);
        bool inside = app.mouseX >= fbm.panelX &&
                      app.mouseX <= fbm.panelX + fbm.panelWidth;
        if (inside || !app.browserPinned) {
            setFolderBrowserCursor(app, (float)app.mouseX, (float)app.mouseY);
            if (inside) requestRepaint(hwnd);  // row hover
            return;
        }
        // Pinned (#156): the document beside the panel keeps its hover
    }
    if (app.showToc) {
        float tocW = tocPanelWidth(app);
        float tocX = tocPanelX(app, tocW);
        bool inside = app.mouseX >= tocX && app.mouseX <= tocX + tocW;
        if (inside || !app.tocPinned) {
            setTocCursor(app, (float)app.mouseX, (float)app.mouseY);
            if (inside) requestRepaint(hwnd);  // row hover
            return;
        }
    }
    if (app.showThemeChooser) {
        setThemeChooserCursor(app, (float)app.mouseX, (float)app.mouseY);
        // Hover live-preview (design 12b): resting on a card repaints the
        // app behind the scrim in that theme; leaving restores the
        // committed one. The chooser chrome itself stays on the committed
        // theme via themeChooserPreviewBase.
        int hit = themeChooserHitAt(app, (float)app.mouseX,
                                    (float)app.mouseY);
        int hoverTheme = hit >= 0 ? hit : -1;
        if (hoverTheme >= 0 && hoverTheme != app.currentThemeIndex) {
            if (app.themeChooserPreviewBase < 0) {
                app.themeChooserPreviewBase = app.currentThemeIndex;
            }
            applyTheme(app, hoverTheme);
            requestRepaint(hwnd);
        } else if (hoverTheme < 0 && app.themeChooserPreviewBase >= 0) {
            applyTheme(app, app.themeChooserPreviewBase);
            app.themeChooserPreviewBase = -1;
            requestRepaint(hwnd);
        }
        return;
    }

    // Annotation editor: hand over its buttons, beam over the note field
    if (app.annotEditorOpen && !app.editMode) {
        if (annotationEditorButtonAt(app, (float)app.mouseX,
                                     (float)app.mouseY) != 0) {
            setCursorShape(CursorShape::Hand);
        } else if (annotationEditorContains(app, (float)app.mouseX,
                                            (float)app.mouseY)) {
            setCursorShape(CursorShape::IBeam);
        } else {
            setCursorShape(CursorShape::Arrow);
        }
        return;
    }

    // Edit mode: handle separator drag, editor selection, cursor shape
    if (app.editMode) {
        handleEditorMouseMove(app, hwnd, app.mouseX, app.mouseY);
        // Replace-bar controls override the pane cursor: hand over the
        // buttons, text beam over the two input fields (#119 feedback)
        if (app.showSearch && app.searchReplaceMode &&
            !app.draggingSeparator && !app.editorSelecting) {
            for (const auto& hit : app.searchReplaceHits) {
                if ((float)app.mouseX >= hit.first.left &&
                    (float)app.mouseX <= hit.first.right &&
                    (float)app.mouseY >= hit.first.top &&
                    (float)app.mouseY <= hit.first.bottom) {
                    setCursorShape(hit.second >= 3 ? CursorShape::Hand : CursorShape::IBeam);
                    return;
                }
            }
        }
        // If mouse is in the preview pane and not dragging separator, fall through for link hover etc.
        if ((float)app.mouseX < documentViewportX(app) ||
            app.draggingSeparator || app.editorSelecting) return;
        // Quick-note empty state: hover feedback for the Open button
        if (quickNoteEmptyStateActive(app) && app.editorShowPreview) {
            const tinta::RectF& r = app.quickNoteButtonRect;
            bool over = (float)app.mouseX >= r.left &&
                        (float)app.mouseX <= r.right &&
                        (float)app.mouseY >= r.top &&
                        (float)app.mouseY <= r.bottom;
            if (over != app.quickNoteButtonHover) {
                app.quickNoteButtonHover = over;
                requestRepaint(hwnd);
            }
            if (over) {
                setCursorShape(CursorShape::Hand);
                return;
            }
        }
        // For preview pane, adjust mouseX to be relative to preview offset
        // but we leave the existing code to work with document coordinates
    }

    float previewOffsetX = documentViewportX(app);
    float docX = (app.mouseX - previewOffsetX) + app.scrollX;
    float docY = app.mouseY + app.scrollY;

    // Text selection dragging: the focus follows the caret offset under
    // the mouse; word/line modes union the current word/line with the
    // originally clicked one (#83)
    if (app.selecting) {
        // Dragging near/past the top or bottom edge scrolls the document
        // (viewer only — the edit-mode preview is slaved to the editor)
        if (!app.editMode) {
            float band = 40.0f;
            float vel = 0.0f;
            if (app.mouseY < band) {
                vel = -(band - (float)app.mouseY) * 0.35f;
            } else if ((float)app.mouseY > app.height - band) {
                vel = ((float)app.mouseY - (app.height - band)) * 0.35f;
            }
            if (vel != 0.0f && app.selAutoScrollVel == 0.0f) {
                armTimer(hwnd, TIMER_SELECT_SCROLL, 16);
            } else if (vel == 0.0f && app.selAutoScrollVel != 0.0f) {
                disarmTimer(hwnd, TIMER_SELECT_SCROLL);
            }
            app.selAutoScrollVel = vel;
        }
        size_t off = selectionOffsetAtPoint(app, docX, docY);
        if (app.selectionMode == App::SelectionMode::Word) {
            size_t ws, we;
            selectionWordRange(app, off, ws, we);
            app.selAnchor = std::min(app.selAnchorRangeStart, ws);
            app.selFocus = std::max(app.selAnchorRangeEnd, we);
            app.hasSelection = true;
        } else if (app.selectionMode == App::SelectionMode::Line) {
            size_t ls, le;
            if (selectionLineRange(app, off, ls, le)) {
                app.selAnchor = std::min(app.selAnchorRangeStart, ls);
                app.selFocus = std::max(app.selAnchorRangeEnd, le);
                app.hasSelection = true;
            }
        } else {
            app.selFocus = off;
        }
        requestRepaint(hwnd);
        return;
    }

    // Vertical scrollbar dragging
    if (app.scrollbarDragging) {
        float maxScroll = std::max(0.0f, app.contentHeight - app.height);
        if (maxScroll > 0 && app.contentHeight > app.height) {
            float sbHeight = (float)app.height / app.contentHeight * app.height;
            sbHeight = std::max(sbHeight, 30.0f);
            float trackHeight = app.height - sbHeight;

            float deltaY = (float)app.mouseY - app.scrollbarDragStartY;
            float scrollDelta = (deltaY / trackHeight) * maxScroll;
            app.scrollY = app.scrollbarDragStartScroll + scrollDelta;
            app.scrollY = std::max(0.0f, std::min(app.scrollY, maxScroll));
            app.targetScrollY = app.scrollY;
            requestRepaint(hwnd);
        }
        return;
    }

    // Horizontal scrollbar dragging
    if (app.hScrollbarDragging) {
        float viewportWidth = documentViewportWidth(app);
        float maxScroll = std::max(0.0f, app.contentWidth - viewportWidth);
        if (maxScroll > 0 && app.contentWidth > viewportWidth) {
            float sbWidth = viewportWidth / app.contentWidth * viewportWidth;
            sbWidth = std::max(sbWidth, 30.0f);
            float trackWidth = viewportWidth - sbWidth;

            float deltaX = (float)app.mouseX - app.hScrollbarDragStartX;
            float scrollDelta = (deltaX / trackWidth) * maxScroll;
            app.scrollX = app.hScrollbarDragStartScroll + scrollDelta;
            app.scrollX = std::max(0.0f, std::min(app.scrollX, maxScroll));
            app.targetScrollX = app.scrollX;
            requestRepaint(hwnd);
        }
        return;
    }

    // Check vertical scrollbar hover. The bar draws at the viewport's
    // right edge, which a right-docked TOC pushes left of the window edge.
    bool wasHovered = app.scrollbarHovered;
    app.scrollbarHovered = false;
    if (app.verticalScrollbarVisible) {
        float sbWidth = dpi(app, 14.0f);  // hit area
        float sbRight = documentViewportX(app) + documentViewportWidth(app);
        if (app.mouseX >= sbRight - sbWidth && app.mouseX <= sbRight) {
            app.scrollbarHovered = true;
        }
    }

    // Check horizontal scrollbar hover
    bool wasHHovered = app.hScrollbarHovered;
    app.hScrollbarHovered = false;
    float viewportX = documentViewportX(app);
    float viewportWidth = documentViewportWidth(app);
    if (app.contentWidth > viewportWidth) {
        float sbHeight = dpi(app, 14.0f);  // hit area
        if (app.mouseY >= app.height - sbHeight &&
            app.mouseX >= viewportX &&
            app.mouseX <= viewportX + viewportWidth) {
            app.hScrollbarHovered = true;
        }
    }

    // Check link hover
    std::string prevHoveredLink = app.hoveredLink;
    tinta::RectF hoveredLinkBounds{};
    app.hoveredLink.clear();
    for (const auto& lr : app.linkRects) {
        if (docX >= lr.bounds.left && docX <= lr.bounds.right &&
            docY >= lr.bounds.top && docY <= lr.bounds.bottom) {
            app.hoveredLink = lr.url;
            hoveredLinkBounds = lr.bounds;
            break;
        }
    }

    // Start page hover drives its row and button highlights
    if (startPageActive(app)) {
        int hit = startPageHitAt(app, (float)app.mouseX, (float)app.mouseY);
        if (hit != app.startPageHover) {
            app.startPageHover = hit;
            requestRepaint(hwnd);
        }
    } else if (app.startPageHover != 0) {
        app.startPageHover = 0;
    }

    // Link peek: dwelling on a local .md link arms the preview timer
    if (app.hoveredLink != prevHoveredLink) {
        clearLinkPeek(app);
        bool peekable = !app.editMode &&
                        (app.hoveredLink.rfind("fileref-ok:", 0) == 0 ||
                         app.hoveredLink.rfind("wiki:", 0) == 0);
        if (peekable) {
            app.linkPeekUrl = app.hoveredLink;
            app.linkPeekAnchorDoc = hoveredLinkBounds;
            armTimer(hwnd, TIMER_LINK_PEEK, 450);
        } else {
            disarmTimer(hwnd, TIMER_LINK_PEEK);
        }
    }

    // Check if over text
    bool wasOverText = app.overText;
    app.overText = (findTextRectAt(app, (int)docX, (int)docY) != nullptr);

    // Check if hovering over any code block (show copy button on whole block)
    int prevHoveredCodeBlock = app.hoveredCodeBlock;
    app.hoveredCodeBlock = -1;
    for (int i = 0; i < (int)app.codeBlocks.size(); i++) {
        const auto& cb = app.codeBlocks[i];
        if (docX >= cb.bounds.left && docX <= cb.bounds.right &&
            docY >= cb.bounds.top && docY <= cb.bounds.bottom) {
            app.hoveredCodeBlock = i;
            break;
        }
    }

    // Table hover (shows the copy-as-TSV button)
    int prevHoveredTable = app.hoveredTable;
    app.hoveredTable = -1;
    for (int i = 0; i < (int)app.tableRects.size(); i++) {
        const auto& tb = app.tableRects[i];
        if (docX >= tb.bounds.left && docX <= tb.bounds.right &&
            docY >= tb.bounds.top && docY <= tb.bounds.bottom) {
            app.hoveredTable = i;
            break;
        }
    }

    // Annotation hover: rail squares first, then tinted text (#126)
    int prevHoveredAnnotation = app.hoveredAnnotation;
    app.hoveredAnnotation = -1;
    if (!app.annotations.empty() && !app.editMode) {
        app.hoveredAnnotation =
            annotationRailHit(app, (float)app.mouseX, (float)app.mouseY);
        if (app.hoveredAnnotation < 0) {
            app.hoveredAnnotation = annotationAtDocPoint(app, docX, docY);
        }
    }

    // Update cursor (using cached handles)
    if (app.showFolderBrowser) {
        float panelWidth = folderBrowserPanelWidth(app);
        float panelX = -panelWidth * (1.0f - app.folderBrowserAnimation);
        bool inPanel = (app.mouseX >= panelX && app.mouseX <= panelX + panelWidth);
        if (inPanel && app.hoveredFolderIndex >= 0) {
            setCursorShape(CursorShape::Hand);
        } else {
            setCursorShape(CursorShape::Arrow);
        }
        // Only invalidate when mouse is over the panel (hover tracking needed)
        if (inPanel)
            requestRepaint(hwnd);
    } else if (app.showToc) {
        float panelWidth = tocPanelWidth(app);
        float panelX = tocPanelX(app, panelWidth);
        bool inPanel = (app.mouseX >= panelX && app.mouseX <= panelX + panelWidth);
        if (inPanel && app.hoveredTocIndex >= 0) {
            setCursorShape(CursorShape::Hand);
        } else {
            setCursorShape(CursorShape::Arrow);
        }
        // Only invalidate when mouse is over the panel (hover tracking needed)
        if (inPanel)
            requestRepaint(hwnd);
    } else if (app.scrollbarHovered || app.scrollbarDragging ||
        app.hScrollbarHovered || app.hScrollbarDragging) {
        setCursorShape(CursorShape::Arrow);
    } else if (signalMouseMove(app, (float)app.mouseX, (float)app.mouseY)) {
        setCursorShape(CursorShape::Hand);
    } else if (startPageActive(app) && app.startPageHover != 0) {
        setCursorShape(CursorShape::Hand);
    } else if (!app.annotationMarks.empty() &&
               (annotationRailHit(app, (float)app.mouseX,
                                  (float)app.mouseY) >= 0 ||
                annotationCopyButtonHit(app, (float)app.mouseX,
                                        (float)app.mouseY))) {
        setCursorShape(CursorShape::Hand);
    } else if (taskRectAt(app) != nullptr) {
        setCursorShape(CursorShape::Hand);
    } else if (app.hoveredCodeBlock >= 0) {
        // Show hand cursor only when over the copy button area
        const auto& cb = app.codeBlocks[app.hoveredCodeBlock];
        float btnW = dpi(app, 72.0f);
        float btnH = dpi(app, 26.0f);
        float btnPad = 8.0f * app.contentScale * app.zoomFactor;
        float rightEdge = std::min(cb.bounds.right,
                                   app.scrollX + documentViewportWidth(app));
        float btnX = rightEdge - btnW - btnPad;
        float btnY = cb.bounds.top + btnPad;
        if ((docX >= btnX && docX <= btnX + btnW &&
             docY >= btnY && docY <= btnY + btnH) ||
            diagramPngButtonAt(app, app.mouseX, app.mouseY) ||
            diagramFitButtonAt(app, app.mouseX, app.mouseY)) {
            setCursorShape(CursorShape::Hand);
        } else if (app.overText) {
            setCursorShape(CursorShape::IBeam);
        } else {
            setCursorShape(CursorShape::Arrow);
        }
    } else if (app.hoveredTable >= 0 &&
               (tableCopyButtonAt(app, app.mouseX, app.mouseY) ||
                tableFitButtonAt(app, app.mouseX, app.mouseY))) {
        setCursorShape(CursorShape::Hand);
    } else if (!app.hoveredLink.empty()) {
        setCursorShape(CursorShape::Hand);
    } else if (!app.editMode && layoutBitmapAt(app, docX, docY) != nullptr) {
        setCursorShape(CursorShape::Hand);
    } else if (app.overText) {
        setCursorShape(CursorShape::IBeam);
    } else {
        setCursorShape(CursorShape::Arrow);
    }

    if (wasHovered != app.scrollbarHovered ||
        wasHHovered != app.hScrollbarHovered ||
        prevHoveredLink != app.hoveredLink ||
        prevHoveredCodeBlock != app.hoveredCodeBlock ||
        prevHoveredTable != app.hoveredTable ||
        prevHoveredAnnotation != app.hoveredAnnotation) {
        requestRepaint(hwnd);
    }
}


// --- Right-click context menu ---

static void closeSearchIfOpen(App& app) {
    if (!app.showSearch) {
        std::wstring().swap(app.docTextLower);
        return;
    }
    app.showSearch = false;
    app.searchActive = false;
    app.searchQuery.clear();
    app.searchMatches.clear();
    app.searchReplaceMode = false;
    app.replaceFieldActive = false;
    std::wstring().swap(app.docTextLower);
    app.searchAnimation = 0;
    clearFolderSearch(app);
    updateBlinkTimer(app);
}

static void invokeContextMenuAction(App& app, HWND hwnd, int item) {
    switch (item) {
        case CTX_COPY:
            if (app.hasSelection && app.selAnchor != app.selFocus) {
                std::wstring selected = selectionTextForRange(
                    app, std::min(app.selAnchor, app.selFocus),
                    std::max(app.selAnchor, app.selFocus));
                if (selected.empty()) break;
                platform::setClipboardText(selected);
                app.hasSelection = false;
                signalPushKey(app, SIG_INFO, SIGI_COPY, "toast.copied");
            }
            break;
        case CTX_SELECT_ALL:
            if (app.root) {
                // docText only exists for laid-out content — finish first
                ensureLayoutComplete(app);
                app.selAnchor = 0;
                app.selFocus = app.docText.size();
                app.hasSelection = true;
            }
            break;
        case CTX_ANNOTATE:
            annotationBeginCreate(app);
            break;
        case CTX_QUICK_NOTE:
            closeSearchIfOpen(app);
            if (startPageActive(app)) startPageInvoke(app, hwnd, 2);
            else tabOpenQuickNote(app, hwnd);
            break;
        case CTX_OPEN:
            openFileDialog(app, hwnd);
            break;
        case CTX_SAVE:
            if (app.editMode) saveEditorFile(app, hwnd);
            break;
        case CTX_SAVE_AS:
            saveFileAs(app, hwnd);
            break;
        case CTX_EXIT:
            postWindowMessage(hwnd, WM_CLOSE, 0, 0);
            break;
        case CTX_NEW:
            closeSearchIfOpen(app);
            startNewFileFlow(app, hwnd);
            break;
        case CTX_PRINT:
            closeSearchIfOpen(app);
            if (app.editMode) editorReparse(app, true);
            openPrintPreview(app, hwnd);
            break;
        case CTX_EXPORT:
            closeSearchIfOpen(app);
            if (app.editMode) editorReparse(app, true);
            exportDocumentAs(app, hwnd);
            break;
        case CTX_EDIT:
            closeSearchIfOpen(app);
            enterEditMode(app);
            break;
        case CTX_SEARCH:
            if (app.showSearch) { app.searchActive = true; break; }
            app.showSearch = true;
            app.searchActive = true;
            app.searchAnimation = 0;
            app.searchQuery.clear();
            app.searchMatches.clear();
            app.searchCurrentIndex = 0;
            app.searchJustOpened = false;
            updateBlinkTimer(app);
            break;
        case CTX_TOC:
            closeSearchIfOpen(app);
            ensureLayoutComplete(app);
            app.showToc = true;
            app.tocAnimation = 0;
            app.tocScroll = 0;
            app.tocFilter.clear();
            app.hoveredTocIndex = -1;
            break;
        case CTX_BROWSE:
            closeSearchIfOpen(app);
            app.showFolderBrowser = true;
            closeFolderBrowserInput(app);
            app.folderBrowserAnimation = 0;
            if (!app.currentFile.empty()) {
                app.folderBrowserPath = getDirectoryFromFile(app.currentFile);
            } else {
                wchar_t cwd[MAX_PATH];
                if (GetCurrentDirectoryW(MAX_PATH, cwd)) {
                    app.folderBrowserPath = cwd;
                }
            }
            populateFolderItems(app);
            break;
        case CTX_COPY_PATH:
            copyFilePath(app, hwnd, app.currentFile);
            break;
        case CTX_REVEAL: {
            std::wstring fullPath = absoluteFilePath(app.currentFile);
            if (!fullPath.empty()) platform::revealInFileManager(fullPath);
            break;
        }
        case CTX_THEME:
            closeSearchIfOpen(app);
            app.showThemeChooser = true;
            app.themeChooserAnimation = 0;
            break;
        case CTX_SETTINGS:
            closeSearchIfOpen(app);
            app.showSettings = true;
            app.settingsAnimation = 0;
            break;
        case CTX_HELP:
            closeSearchIfOpen(app);
            app.showHelp = true;
            app.helpAnimation = 0;
            break;
    }
}

void handleContextMenu(App& app, HWND hwnd, int mouseX, int mouseY,
                       bool fromKeyboard) {
    // App panels own both mouse buttons in either reading or editing mode.
    if (app.showSettings || app.showHelp || app.showThemeChooser ||
        app.showThemeEditor || app.showShortcutEditor || app.showPrintPreview ||
        app.confirmExitPending || app.createRefPending || app.showLightbox) return;
    closeContextMenu(app);
    if (fromKeyboard) {
        // Keyboard menu key: open near the viewport center. The caller
        // recognised the menu key -- Win32 spells it as a (-1, -1) position --
        // and did the screen-to-client conversion for a real right-click, so
        // both arms arrive here in client space exactly as they used to.
        mouseX = app.width / 2;
        mouseY = app.height / 2;
    }

    // A right-click on a tab opens the tab menu (works in edit mode and
    // above panels too — the strip stays interactive there)
    if (!fromKeyboard && !app.showPrintPreview && !app.confirmExitPending &&
        (float)mouseY < chromeTopHeight(app)) {
        int index = tabContextMenuIndexAt(app, (float)mouseX, (float)mouseY);
        if (index >= 0) {
            openTabMenu(app, index, (float)mouseX, (float)mouseY);
            requestRepaint(hwnd);
        }
        return;  // strip right-clicks never reach the document menu
    }

    // Raw editor insert menu (design t9): the right-click moves the
    // caret and the INSERT entries drop their markdown there
    if (app.editMode && !app.confirmExitPending &&
        !fromKeyboard && (float)mouseX >= editRailWidth(app) &&
        (float)mouseX < editorPaneWidth(app)) {
        openEditCtxMenu(app, hwnd, (float)mouseX, (float)mouseY);
        requestRepaint(hwnd);
        return;
    }

    // Document menu: viewer mode only, and never on top of a modal
    // overlay. The search bar is fine — it shares the viewport rather
    // than covering it.
    if (app.editMode || app.showThemeChooser || app.showHelp ||
        app.showPrintPreview || app.showSettings || app.showThemeEditor) {
        return;
    }
    // Side panels are not modal (#185): a right-click over the document
    // beside a PINNED panel opens the menu there, and beside an unpinned
    // one it dismisses the panel like any outside click first. Over the
    // panel itself there is no document to act on.
    if (app.showToc || app.showFolderBrowser) {
        float docLeft = documentViewportX(app);
        bool overDocument = (float)mouseX >= docLeft &&
                            (float)mouseX < docLeft + documentViewportWidth(app);
        if (!overDocument) return;
        if (app.showToc && !app.tocPinned) {
            app.showToc = false;
            app.tocAnimation = 0;
            app.tocFilter.clear();
        }
        if (app.showFolderBrowser && !app.browserPinned) {
            app.showFolderBrowser = false;
            app.folderBrowserAnimation = 0;
            closeFolderBrowserInput(app);
        }
    }
    openContextMenu(app, (float)mouseX, (float)mouseY);
    requestRepaint(hwnd);
}

static void toggleApplicationMenu(App& app, HWND hwnd, bool keyboard = false) {
    if (!appMenuAvailable(app)) return;
    if (app.showContextMenu && app.applicationMenu) {
        closeContextMenu(app);
    } else {
        closeSearchIfOpen(app);
        closeTabMenu(app);
        app.showTabSwitcher = false;
        app.editCtxOpen = false;
        app.showHelp = false;
        app.showSettings = false;
        if (app.themeChooserPreviewBase >= 0) {
            applyTheme(app, app.themeChooserPreviewBase);
            app.themeChooserPreviewBase = -1;
        }
        app.showThemeChooser = false;
        tableEditCommit(app);
        openContextMenu(app, dpi(app, 4.0f), chromeTopHeight(app), true);
        if (keyboard) {
            app.contextMenuKeyboard = true;
            app.hoveredContextMenuItem = nextContextMenuItem(app, -1, 1);
        }
    }
    requestRepaint(hwnd);
}

void handleMouseDown(App& app, HWND hwnd, int mouseX, int mouseY) {
    if (appMenuButtonAt(app, (float)mouseX, (float)mouseY)) {
        toggleApplicationMenu(app, hwnd);
        app.swallowNextMouseUp = true;
        return;
    }
    // Context menu: a click lands on an item or dismisses the menu; either
    // way the click is consumed
    if (app.showContextMenu) {
        int clickX = mouseX;
        int clickY = mouseY;
        int item = contextMenuItemAt(app, (float)clickX, (float)clickY);
        closeContextMenu(app);
        app.swallowNextMouseUp = true;
        if (item >= 0 && contextMenuItemEnabled(app, item)) {
            invokeContextMenuAction(app, hwnd, item);
        }
        requestRepaint(hwnd);
        return;
    }


    // Title-bar tab strip and its switcher sit above every overlay
    {
        int mx = mouseX;
        int my = mouseY;
        // Tab context menu: a click lands on an item or dismisses it
        if (tabMenuMouseDown(app, hwnd, mx, my)) return;
        if (app.showTabSwitcher) {
            tabSwitcherMouseDown(app, hwnd, mx, my);
            app.swallowNextMouseUp = true;
            return;
        }
        // With the floating sheet the top band right of the source column
        // belongs to the desk gap and the page, not the strip — except
        // the caption island (pin + window buttons) floating on it
        bool overSheetBand = editorPreviewVisible(app) &&
                             (float)mx >= editorPaneWidth(app) &&
                             (float)mx < captionIslandLeft(app);
        if ((float)my < chromeTopHeight(app) && !overSheetBand &&
            !app.confirmExitPending) {
            tabStripMouseDown(app, hwnd, mx, my, false);
            app.swallowNextMouseUp = true;
            return;
        }
    }
    // Signal chips ride above everything: presses on a chip, the bell,
    // or the open tray act; any other press parks the draining chips
    {
        float mx = (float)mouseX;
        float my = (float)mouseY;
        if (signalMouseDown(app, hwnd, mx, my)) {
            app.swallowNextMouseUp = true;
            return;
        }
        signalTuckPassive(app);
    }

    // Lightbox: press on the image arms a pan, elsewhere closes
    if (app.showLightbox) {
        int mx = mouseX;
        int my = mouseY;
        tinta::RectF img = lightboxImageRect(app);
        if ((float)mx >= img.left && (float)mx <= img.right &&
            (float)my >= img.top && (float)my <= img.bottom) {
            app.lightboxDragging = true;
            app.lightboxDragMoved = false;
            app.lightboxDragStartX = mx;
            app.lightboxDragStartY = my;
            app.lightboxDragPanX = app.lightboxPanX;
            app.lightboxDragPanY = app.lightboxPanY;
            captureMouse(hwnd);
        } else {
            closeLightbox(app);
        }
        requestRepaint(hwnd);
        return;
    }

    // Theme and shortcut editors: everything acts on the release
    if (app.showThemeEditor || app.showShortcutEditor) return;
    // Unsaved-changes dialog is modal: buttons act on the release
    if (app.editMode && app.confirmExitPending) return;
    // Create-missing-reference dialog: buttons act on the release; the
    // press must not start a document selection underneath it
    if (app.createRefPending) return;

    // Settings: sliders drag from the press; everything else acts on release
    if (app.showSettings) {
        float mx = (float)mouseX;
        float my = (float)mouseY;
        for (const auto& hit : app.settingsHits) {
            if ((hit.second == SET_SLIDER_READING || hit.second == SET_SLIDER_ZEN) &&
                mx >= hit.first.left && mx <= hit.first.right &&
                my >= hit.first.top && my <= hit.first.bottom) {
                app.settingsDragSlider = hit.second;
                captureMouse(hwnd);
                settingsSliderApply(app, hit.second, mx);
                return;
            }
        }
        return;
    }
    // Print preview: controls act on the release
    if (app.showPrintPreview) return;

    // Annotation editor is modal: buttons act, outside dismisses (#126)
    if (app.annotEditorOpen && !app.editMode) {
        float mx = (float)mouseX;
        float my = (float)mouseY;
        app.swallowNextMouseUp = true;
        int btn = annotationEditorButtonAt(app, mx, my);
        if (btn == 1) {
            annotationEditorConfirm(app, hwnd);
        } else if (btn == 2) {
            annotationEditorCancel(app);
        } else if (btn == 3) {
            annotationEditorDelete(app, hwnd);
        } else if (!annotationEditorContains(app, mx, my)) {
            annotationEditorCancel(app);
        }
        requestRepaint(hwnd);
        return;
    }

    // Help overlay: check scrollbar click
    if (app.showHelp) {
        float maxScroll = std::max(0.0f, app.helpContentHeight - app.helpVisibleHeight);
        if (maxScroll > 0) {
            int clickX = mouseX;
            int clickY = mouseY;
            // Scrollbar hit area: right edge of panel
            float panelWidth = std::min(dpi(app, 520.0f), app.width - dpi(app, 40.0f));
            float panelRight = (app.width + panelWidth) / 2;
            float sbHitWidth = dpi(app, 16.0f);
            if (clickX >= panelRight - sbHitWidth && clickX <= panelRight &&
                clickY >= app.helpScrollbarTop && clickY <= app.helpScrollbarTop + app.helpVisibleHeight) {
                app.helpScrollbarDragging = true;
                app.helpScrollbarDragStartY = (float)clickY;
                app.helpScrollbarDragStartScroll = app.helpScroll;
                captureMouse(hwnd);

                // Jump if clicked outside thumb
                float sbHeight = app.helpVisibleHeight / app.helpContentHeight * app.helpVisibleHeight;
                sbHeight = std::max(sbHeight, dpi(app, 20.0f));
                float sbY = app.helpScrollbarTop + (app.helpScroll / maxScroll * (app.helpVisibleHeight - sbHeight));
                if (clickY < sbY || clickY > sbY + sbHeight) {
                    float trackHeight = app.helpVisibleHeight - sbHeight;
                    float clickPos = (float)clickY - app.helpScrollbarTop - sbHeight / 2;
                    clickPos = std::max(0.0f, std::min(clickPos, trackHeight));
                    app.helpScroll = (trackHeight > 0) ? (clickPos / trackHeight) * maxScroll : 0;
                    app.helpScrollbarDragStartScroll = app.helpScroll;
                    app.helpScrollbarDragStartY = (float)clickY;
                }
                requestRepaint(hwnd);
            }
        }
        return;
    }

    // The chooser acts on release; do not place an editor caret under it.
    if (app.showThemeChooser) return;

    // Edit mode: route to editor or preview
    if (app.editMode) {
        int x = mouseX;
        int y = mouseY;
        // Find & replace bar: clicks focus a field or press a button (#121)
        if (app.showSearch && app.searchReplaceMode) {
            for (const auto& hit : app.searchReplaceHits) {
                if ((float)x >= hit.first.left && (float)x <= hit.first.right &&
                    (float)y >= hit.first.top && (float)y <= hit.first.bottom) {
                    app.swallowNextMouseUp = true;
                    switch (hit.second) {
                        case 1: app.replaceFieldActive = false; break;
                        case 2: app.replaceFieldActive = true; break;
                        case 3: editorReplaceCurrent(app, hwnd); break;
                        case 4: editorReplaceAll(app, hwnd); break;
                    }
                    requestRepaint(hwnd);
                    return;
                }
            }
        }
        // Everything left of the preview edge — pane and seam — belongs
        // to the editor handler (the seam is the split handle)
        if ((float)x < documentViewportX(app)) {
            handleEditorMouseDown(app, hwnd, x, y);
            return;
        }
        // Quick-note empty state: the Open button in the preview pane
        if (quickNoteEmptyStateActive(app) && app.editorShowPreview &&
            (float)x >= app.quickNoteButtonRect.left &&
            (float)x <= app.quickNoteButtonRect.right &&
            (float)y >= app.quickNoteButtonRect.top &&
            (float)y <= app.quickNoteButtonRect.bottom) {
            app.swallowNextMouseUp = true;
            openFileDialog(app, hwnd);
            return;
        }
        // In-place table editing (#148): cells and the + affordances
        // catch preview presses before selection handling
        if (app.editorShowPreview) {
            float docX = (float)x - documentViewportX(app) + app.scrollX;
            float docY = (float)y + app.scrollY;
            if (tableEditMouseDown(app, hwnd, docX, docY)) {
                app.swallowNextMouseUp = true;
                return;
            }
            if (app.tableEditActive) {
                // A press anywhere else in the preview commits the cell
                tableEditCommit(app);
                requestRepaint(hwnd);
            }
        }
        // Fall through for preview pane clicks
    }

    // Annotation rail: squares open the note editor, the pinned button
    // copies every annotation for an agent (#126)
    if (!app.editMode && !app.showThemeChooser && !app.showHelp &&
        !app.confirmExitPending && !app.annotations.empty()) {
        float mx = (float)mouseX;
        float my = (float)mouseY;
        bool underSearchBar =
            app.showSearch && my < chromeTopHeight(app) + dpi(app, 70.0f);
        if (!underSearchBar) {
            if (annotationCopyButtonHit(app, mx, my)) {
                app.swallowNextMouseUp = true;
                annotationsCopyForAgent(app, hwnd);
                requestRepaint(hwnd);
                return;
            }
            int railIdx = annotationRailHit(app, mx, my);
            if (railIdx >= 0) {
                app.swallowNextMouseUp = true;
                // A parked square (annotation off-screen) scrolls to its
                // text; a square level with its text opens the editor
                bool onText = false;
                for (const auto& mark : app.annotationMarks) {
                    if (mark.index == railIdx) {
                        onText = mark.onText;
                        break;
                    }
                }
                if (onText) {
                    annotationOpenEditor(app, railIdx);
                } else {
                    annotationScrollTo(app, railIdx);
                }
                requestRepaint(hwnd);
                return;
            }
        }
    }

    // Folder search: the bar's toggle button and result-panel clicks
    if (app.showSearch && !app.editMode) {
        float clickX = (float)mouseX;
        float clickY = (float)mouseY;
        if (folderSearchToggleAt(app, clickX, clickY)) {
            app.folderSearchEnabled = !app.folderSearchEnabled;
            if (!app.folderSearchEnabled) {
                clearFolderSearch(app);
            } else {
                performSearch(app);  // re-arms the scan timer
            }
            requestRepaint(hwnd);
            return;
        }
        for (const auto& hit : app.folderResultHits) {
            if (clickX >= hit.rect.left && clickX <= hit.rect.right &&
                clickY >= hit.rect.top && clickY <= hit.rect.bottom &&
                hit.fileIndex >= 0 && hit.fileIndex < (int)app.folderResults.size()) {
                // Open the file and land on the first match of the same query
                if (openDocumentInViewer(app, app.folderResults[hit.fileIndex].fullPath)) {
                    app.folderResults.clear();
                    app.folderResultHits.clear();
                    ensureLayoutComplete(app);
                    performSearch(app);
                    if (!app.searchMatches.empty()) {
                        app.searchCurrentIndex = 0;
                        scrollToCurrentMatch(app);
                    }
                }
                requestRepaint(hwnd);
                return;
            }
        }
    }
    // Pinned side panels only claim presses inside their own envelope.
    {
        bool browserClaims = false;
        if (app.showFolderBrowser) {
            FolderBrowserMetrics fbm = folderBrowserMetrics(app);
            browserClaims = !app.browserPinned ||
                            ((float)app.mouseX >= fbm.panelX &&
                             (float)app.mouseX <= fbm.panelX + fbm.panelWidth);
        }
        bool tocClaims = false;
        if (app.showToc) {
            float tocW = tocPanelWidth(app);
            float tocX = tocPanelX(app, tocW);
            tocClaims = !app.tocPinned ||
                        ((float)app.mouseX >= tocX &&
                         (float)app.mouseX <= tocX + tocW);
        }
        if (app.showThemeChooser || browserClaims || tocClaims) {
            // The content scrollbar stays usable beside a side panel: a
            // click on it dismisses an unpinned folder browser ("back to
            // the document") and simply works alongside the TOC.
            // Recompute the band at the click: scrollbarHovered freezes
            // stale while the mouse is over a panel (its mousemove returns
            // before the hover update), and a stale true here turned panel
            // clicks into scrollbar drags bound to the mouse.
            float viewportX = documentViewportX(app);
            float viewportWidth = documentViewportWidth(app);
            bool vClick = false, hClick = false;
            if (app.verticalScrollbarVisible) {
                float sbWidth = dpi(app, 14.0f);
                float sbRight = viewportX + viewportWidth;
                vClick = (float)app.mouseX >= sbRight - sbWidth &&
                         (float)app.mouseX <= sbRight;
            }
            if (!vClick && app.contentWidth > viewportWidth) {
                float sbHeight = dpi(app, 14.0f);
                hClick = (float)app.mouseY >= app.height - sbHeight &&
                         (float)app.mouseX >= viewportX &&
                         (float)app.mouseX <= viewportX + viewportWidth;
            }
            if (!app.showThemeChooser && (vClick || hClick)) {
                if (app.showFolderBrowser && !app.browserPinned) {
                    app.showFolderBrowser = false;
                    closeFolderBrowserInput(app);
                }
                // The drag start below keys off the hover flags, which
                // freeze while the mouse crosses a panel - refresh them
                // from this click's own geometry
                app.scrollbarHovered = vClick;
                app.hScrollbarHovered = hClick;
                // fall through to the scrollbar handling below
            } else {
                return;
            }
        }
    }

    app.mouseDown = true;
    app.mouseX = mouseX;
    app.mouseY = mouseY;
    captureMouse(hwnd);
    float previewOffsetX = documentViewportX(app);
    float docX = (app.mouseX - previewOffsetX) + app.scrollX;
    float docY = app.mouseY + app.scrollY;

    // Check if clicking vertical scrollbar
    if (app.scrollbarHovered && app.verticalScrollbarVisible) {
        app.scrollbarDragging = true;
        app.scrollbarDragStartY = (float)app.mouseY;
        app.scrollbarDragStartScroll = app.scrollY;

        // Check if clicking in track (not thumb) - jump to position
        float maxScroll = std::max(0.0f, app.contentHeight - app.height);
        float sbHeight = (float)app.height / app.contentHeight * app.height;
        sbHeight = std::max(sbHeight, 30.0f);
        float sbY = (maxScroll > 0) ? (app.scrollY / maxScroll * (app.height - sbHeight)) : 0;

        // If clicked outside thumb, jump
        if (app.mouseY < sbY || app.mouseY > sbY + sbHeight) {
            float trackHeight = app.height - sbHeight;
            float clickPos = (float)app.mouseY - sbHeight / 2;
            clickPos = std::max(0.0f, std::min(clickPos, trackHeight));
            app.scrollY = (clickPos / trackHeight) * maxScroll;
            app.targetScrollY = app.scrollY;
            app.scrollbarDragStartScroll = app.scrollY;
            app.scrollbarDragStartY = (float)app.mouseY;
        }
        requestRepaint(hwnd);
    }
    // Check if clicking horizontal scrollbar
    else if (app.hScrollbarHovered &&
             app.contentWidth > documentViewportWidth(app)) {
        app.hScrollbarDragging = true;
        app.hScrollbarDragStartX = (float)app.mouseX;
        app.hScrollbarDragStartScroll = app.scrollX;

        // Check if clicking in track (not thumb) - jump to position
        float viewportX = documentViewportX(app);
        float viewportWidth = documentViewportWidth(app);
        float localMouseX = app.mouseX - viewportX;
        float maxScroll = std::max(0.0f, app.contentWidth - viewportWidth);
        float sbWidth = viewportWidth / app.contentWidth * viewportWidth;
        sbWidth = std::max(sbWidth, 30.0f);
        float sbX = (maxScroll > 0)
            ? (app.scrollX / maxScroll * (viewportWidth - sbWidth))
            : 0;

        // If clicked outside thumb, jump
        if (localMouseX < sbX || localMouseX > sbX + sbWidth) {
            float trackWidth = viewportWidth - sbWidth;
            float clickPos = localMouseX - sbWidth / 2;
            clickPos = std::max(0.0f, std::min(clickPos, trackWidth));
            app.scrollX = (clickPos / trackWidth) * maxScroll;
            app.targetScrollX = app.scrollX;
            app.hScrollbarDragStartScroll = app.scrollX;
            app.hScrollbarDragStartX = (float)app.mouseX;
        }
        requestRepaint(hwnd);
    } else {
        // Start page: clicks act on its controls, there is nothing to
        // select underneath. The capture from this press is let go up
        // front: the Open action runs a modal picker whose release never
        // reaches this window.
        if (startPageActive(app)) {
            releaseMouseIfCaptured(hwnd);
            int hit = startPageHitAt(app, (float)mouseX,
                                     (float)mouseY);
            app.swallowNextMouseUp = true;
            if (hit) startPageInvoke(app, hwnd, hit);
            return;
        }
        // Capture the mouse for the duration of a selection drag: without
        // it, mouse events stop at the window edge and the drag can neither
        // auto-scroll past the viewport nor finalize outside the window
        captureMouse(hwnd);
        // Detect double/triple clicks
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - app.lastClickTime).count();

        // Check if this is a repeated click (within 500ms and 5px of last click)
        bool isRepeatedClick = (elapsed < 500 &&
            std::abs(app.mouseX - app.lastClickX) < 5 &&
            std::abs(app.mouseY - app.lastClickY) < 5);

        if (isRepeatedClick) {
            app.clickCount = std::min(app.clickCount + 1, 3);
        } else {
            app.clickCount = 1;
        }

        app.lastClickTime = now;
        app.lastClickX = app.mouseX;
        app.lastClickY = app.mouseY;

        // Handle based on click count
        if (app.clickCount == 2) {
            // Double-click: select the word at the caret offset
            size_t off = selectionOffsetAtPoint(app, docX, docY);
            size_t ws, we;
            selectionWordRange(app, off, ws, we);
            if (we > ws) {
                app.selectionMode = App::SelectionMode::Word;
                app.selAnchorRangeStart = ws;
                app.selAnchorRangeEnd = we;
                app.selAnchor = ws;
                app.selFocus = we;
                app.selecting = true;
                app.hasSelection = true;
            }
        } else if (app.clickCount == 3) {
            // Triple-click: select the visual line
            size_t off = selectionOffsetAtPoint(app, docX, docY);
            size_t ls, le;
            if (selectionLineRange(app, off, ls, le) && le > ls) {
                app.selectionMode = App::SelectionMode::Line;
                app.selAnchorRangeStart = ls;
                app.selAnchorRangeEnd = le;
                app.selAnchor = ls;
                app.selFocus = le;
                app.selecting = true;
                app.hasSelection = true;
            }
        } else if ((GetKeyState(VK_SHIFT) & 0x8000) && app.hasSelection) {
            // Shift+click: keep the anchor, move the focus (Typora-style
            // extension; the drag can keep adjusting it)
            app.selectionMode = App::SelectionMode::Normal;
            app.selecting = true;
            app.selShiftExtend = true;
            app.selFocus = selectionOffsetAtPoint(app, docX, docY);
        } else {
            // Single click: anchor a fresh selection at the caret offset
            app.selectionMode = App::SelectionMode::Normal;
            app.selecting = true;
            app.selAnchor = app.selFocus = selectionOffsetAtPoint(app, docX, docY);
            app.hasSelection = false;
        }
        requestRepaint(hwnd);
    }
}

// Resolves an Obsidian [[wiki link]] target against the current file's
// folder and opens it: the name as written, then with .md / .markdown
// appended, then a case-insensitive scan of the directory listing.
static bool openWikiLink(App& app, const std::string& target) {
    if (app.editMode || app.currentFile.empty()) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    std::wstring wideTarget = toWide(target);
    fs::path dir = fs::path(toWide(app.currentFile)).parent_path();

    auto tryOpen = [&](const fs::path& p) {
        return fs::exists(p, ec) && !fs::is_directory(p, ec) &&
               openDocumentInViewer(app, p.wstring());
    };
    if (tryOpen(dir / wideTarget)) return true;
    if (tryOpen(dir / (wideTarget + L".md"))) return true;
    if (tryOpen(dir / (wideTarget + L".markdown"))) return true;

    auto lower = [](std::wstring s) {
        for (auto& c : s) c = (wchar_t)std::towlower(c);
        return s;
    };
    std::wstring want = lower(wideTarget);
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::wstring stem = lower(entry.path().stem().wstring());
        std::wstring name = lower(entry.path().filename().wstring());
        if (stem == want || name == want) {
            if (openDocumentInViewer(app, entry.path().wstring())) return true;
        }
    }
    // Target doesn't exist: leave the document as-is (a viewer doesn't
    // create notes the way Obsidian would)
    return false;
}

// Link jumps remember where they came from so mouse-back (XBUTTON1) can
// return to the same document and position
static void navRecordJump(App& app, const std::string& fromPath,
                          float fromScroll) {
    if (fromPath.empty()) return;
    app.navBack.push_back({fromPath, fromScroll});
    if (app.navBack.size() > 64) app.navBack.erase(app.navBack.begin());
    app.navForward.clear();
}

// A live reference: markdown joins the window as a tab (#127), other
// text files open with their registered application (#162). Returns
// true when the target opened inside Tinta.
static bool openFileRefTarget(App& app, HWND hwnd, const std::string& path) {
    if (!qmd::fileRefIsMarkdown(path)) {
        // The hwnd this passed as ShellExecuteW's owner window is dropped: it
        // only parented the error dialog Windows shows when nothing is
        // registered, and the seam is fire-and-forget by contract on both
        // platforms. The window it would have parented is the one being replaced.
        platform::openInDefaultApplication(toWide(path));
        return false;
    }
    if (_stricmp(app.currentFile.c_str(), path.c_str()) != 0) {
        navRecordJump(app, app.currentFile, app.scrollY);
    }
    tabOpenPath(app, hwnd, path, true);
    return true;
}

// Start page actions (design t7). Ids match startPageHitAt: 1 open,
// 2 new document, 3 browse, 4 clear, 5 shortcuts, 10+i recent, 20+i learn.
static void startPageInvoke(App& app, HWND hwnd, int id) {
    if (id == 1) {
        openFileDialog(app, hwnd);
    } else if (id == 2) {
        // New document: the note takes over this window (#121 semantics);
        // the keystroke's WM_CHAR must not type into the fresh editor
        app.swallowCharsUntil = std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(150);
        tabsInit(app);
        enterQuickNoteMode(app);
        app.tabs[app.activeTab].title = tr(app, "title.untitled");
        updateWindowTitle(app);
    } else if (id == 3) {
        if (!app.showFolderBrowser) {
            closeSearchIfOpen(app);
            app.showFolderBrowser = true;
            app.folderBrowserAnimation = 0;
            wchar_t cwd[MAX_PATH];
            if (GetCurrentDirectoryW(MAX_PATH, cwd)) {
                app.folderBrowserPath = cwd;
            }
            populateFolderItems(app);
        }
    } else if (id == 4) {
        clearRecentFiles();
        app.startPageRecents.clear();
        app.startPageHover = 0;
    } else if (id == 5) {
        app.showHelp = true;
    } else if (id >= 10 && id < 20) {
        int index = id - 10;
        if (index < (int)app.startPageRecents.size()) {
            std::string path = app.startPageRecents[index].path;
            if (GetFileAttributesW(toWide(path).c_str()) ==
                INVALID_FILE_ATTRIBUTES) {
                // Vanished since it was recorded: drop the stale entry
                Settings stored = loadSettings();
                for (size_t i = 0; i < stored.recentFiles.size(); i++) {
                    if (_stricmp(stored.recentFiles[i].path.c_str(),
                                 path.c_str()) == 0) {
                        stored.recentFiles.erase(
                            stored.recentFiles.begin() + i);
                        break;
                    }
                }
                saveSettings(stored);
                app.startPageRecents.erase(
                    app.startPageRecents.begin() + index);
                app.startPageHover = 0;
            } else {
                tabsInit(app);
                int shellId = app.tabs[app.activeTab].id;
                tabOpenPath(app, hwnd, path);
                for (int i = 0; i < (int)app.tabs.size(); i++) {
                    if (app.tabs[i].id == shellId) {
                        tabCloseIndex(app, hwnd, i);
                        break;
                    }
                }
            }
        }
    } else if (id >= 20 && id < 23) {
        startPageOpenEmbedded(app, hwnd, id - 20);
    }
    requestRepaint(hwnd);
}

// Create-missing-reference dialog outcome: 1 = create the file and start
// typing in a fresh tab, 2 = cancel
static void createRefAction(App& app, HWND hwnd, int action) {
    app.createRefPending = false;
    if (action == 1 && !app.createRefPath.empty()) {
        std::filesystem::path target(toWide(app.createRefPath));
        std::error_code ec;
        if (target.has_parent_path()) {
            std::filesystem::create_directories(target.parent_path(), ec);
        }
        std::ofstream out(target, std::ios::binary);
        if (out) {
            out.close();
            // The layout cached this path as missing; the ghost turns live
            app.fileRefCache.erase(app.createRefPath);
            // A markdown target opens in a tab ready to type; other text
            // files hand off to their registered application (#162)
            if (openFileRefTarget(app, hwnd, app.createRefPath)) {
                enterEditMode(app);
            }
        }
    }
    app.createRefPath.clear();
    requestRepaint(hwnd);
}

// Lands on entry.path with the least motion: the same document is scroll
// only, an open tab is activated, anything else replaces the current
// document; the scroll position rides the #77 deferred restore
static bool navOpenEntry(App& app, HWND hwnd, const App::NavEntry& entry) {
    if (_stricmp(app.currentFile.c_str(), entry.path.c_str()) != 0) {
        bool found = false;
        for (size_t i = 0; i < app.tabs.size(); i++) {
            const std::string& existing =
                (int)i == app.activeTab ? app.currentFile : app.tabs[i].path;
            if (_stricmp(existing.c_str(), entry.path.c_str()) == 0) {
                tabActivate(app, hwnd, (int)i);
                found = true;
                break;
            }
        }
        if (!found && !openDocumentInViewer(app, toWide(entry.path))) {
            return false;  // deleted since the jump: caller tries older entries
        }
    }
    app.pendingScrollRestore = entry.scrollY;
    requestRepaint(hwnd);
    return true;
}

void navigateBack(App& app, HWND hwnd) {
    if (app.editMode) return;
    while (!app.navBack.empty()) {
        App::NavEntry entry = app.navBack.back();
        app.navBack.pop_back();
        App::NavEntry here{app.currentFile, app.scrollY};
        if (navOpenEntry(app, hwnd, entry)) {
            if (!here.path.empty()) app.navForward.push_back(here);
            return;
        }
    }
}

void navigateForward(App& app, HWND hwnd) {
    if (app.editMode) return;
    while (!app.navForward.empty()) {
        App::NavEntry entry = app.navForward.back();
        app.navForward.pop_back();
        App::NavEntry here{app.currentFile, app.scrollY};
        if (navOpenEntry(app, hwnd, entry)) {
            if (!here.path.empty()) app.navBack.push_back(here);
            return;
        }
    }
}

// Task checkbox under the mouse, or nullptr (viewer mode only)
static const App::TaskRect* taskRectAt(const App& app) {
    if (app.editMode) return nullptr;
    float docX = (app.mouseX - documentViewportX(app)) + app.scrollX;
    float docY = app.mouseY + app.scrollY;
    for (const auto& task : app.taskRects) {
        if (docX >= task.rect.left && docX <= task.rect.right &&
            docY >= task.rect.top && docY <= task.rect.bottom) {
            return &task;
        }
    }
    return nullptr;
}

// Flips a - [ ] / - [x] mark in the file on disk. Parse offsets were
// computed on CR-stripped text, so the position is remapped against the
// raw bytes; the neighbors are verified so a file that changed since the
// last layout is left alone. The file watcher then reloads the view.
static void toggleTaskOnDisk(App& app, HWND hwnd, size_t markOffset, bool wasChecked) {
    if (app.currentFile.empty()) return;
    std::wstring widePath = toWide(app.currentFile);
    std::ifstream in(widePath, std::ios::binary);
    if (!in) return;
    std::string disk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    size_t translated = 0, diskPos = std::string::npos;
    for (size_t i = 0; i < disk.size(); i++) {
        if (disk[i] == '\r') continue;
        if (translated == markOffset) { diskPos = i; break; }
        translated++;
    }
    if (diskPos == std::string::npos || diskPos == 0 || diskPos + 1 >= disk.size()) return;
    char c = disk[diskPos];
    bool checked = (c == 'x' || c == 'X');
    if (disk[diskPos - 1] != '[' || disk[diskPos + 1] != ']' ||
        checked != wasChecked || (!checked && c != ' ')) {
        return;
    }
    disk[diskPos] = checked ? ' ' : 'x';

    std::ofstream out(widePath, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.write(disk.data(), (std::streamsize)disk.size());
    out.close();
    handleFileWatchTimer(app, hwnd);  // reload immediately instead of on the timer
}

// True when (mouseX, mouseY) is on the hovered code block's copy button
static bool codeCopyButtonAt(const App& app, int mouseX, int mouseY) {
    if (app.hoveredCodeBlock < 0 ||
        app.hoveredCodeBlock >= (int)app.codeBlocks.size()) {
        return false;
    }
    const auto& cb = app.codeBlocks[app.hoveredCodeBlock];
    float previewOffsetX = documentViewportX(app);
    float docX = (mouseX - previewOffsetX) + app.scrollX;
    float docY = mouseY + app.scrollY;
    float btnW = dpi(app, 72.0f);
    float btnH = dpi(app, 26.0f);
    float btnPad = 8.0f * app.contentScale * app.zoomFactor;
    float rightEdge =
        std::min(cb.bounds.right, app.scrollX + documentViewportWidth(app));
    float btnX = rightEdge - btnW - btnPad;
    float btnY = cb.bounds.top + btnPad;
    return docX >= btnX && docX <= btnX + btnW &&
           docY >= btnY && docY <= btnY + btnH;
}

// True when (mouseX, mouseY) is on the hovered diagram's copy-as-image
// button (drawn left of the source copy button)
static bool diagramPngButtonAt(const App& app, int mouseX, int mouseY) {
    if (app.hoveredCodeBlock < 0 ||
        app.hoveredCodeBlock >= (int)app.codeBlocks.size()) {
        return false;
    }
    const auto& cb = app.codeBlocks[app.hoveredCodeBlock];
    if (!cb.isDiagram) return false;
    float previewOffsetX = documentViewportX(app);
    float docX = (mouseX - previewOffsetX) + app.scrollX;
    float docY = mouseY + app.scrollY;
    float btnW = dpi(app, 72.0f);
    float btnH = dpi(app, 26.0f);
    float btnPad = 8.0f * app.contentScale * app.zoomFactor;
    float rightEdge =
        std::min(cb.bounds.right, app.scrollX + documentViewportWidth(app));
    float copyX = rightEdge - btnW - btnPad;
    float pngW = dpi(app, 52.0f);
    float pngX = copyX - pngW - dpi(app, 6.0f);
    float btnY = cb.bounds.top + btnPad;
    return docX >= pngX && docX <= pngX + pngW &&
           docY >= btnY && docY <= btnY + btnH;
}

// True when (mouseX, mouseY) is on the hovered table's copy-as-TSV button
static bool tableCopyButtonAt(const App& app, int mouseX, int mouseY) {
    if (app.hoveredTable < 0 ||
        app.hoveredTable >= (int)app.tableRects.size()) {
        return false;
    }
    const auto& tb = app.tableRects[app.hoveredTable];
    float previewOffsetX = documentViewportX(app);
    float docX = (mouseX - previewOffsetX) + app.scrollX;
    float docY = mouseY + app.scrollY;
    float btnW = dpi(app, 88.0f);
    float btnH = dpi(app, 26.0f);
    float btnPad = 8.0f * app.contentScale * app.zoomFactor;
    float rightEdge =
        std::min(tb.bounds.right, app.scrollX + documentViewportWidth(app));
    float btnX = rightEdge - btnW - btnPad;
    float btnY = tb.bounds.top + btnPad;
    return docX >= btnX && docX <= btnX + btnW &&
           docY >= btnY && docY <= btnY + btnH;
}

// True when (mouseX, mouseY) is on the hovered diagram's fit-to-width
// toggle (drawn left of the copy-as-image button)
static bool diagramFitButtonAt(const App& app, int mouseX, int mouseY) {
    if (app.hoveredCodeBlock < 0 ||
        app.hoveredCodeBlock >= (int)app.codeBlocks.size()) {
        return false;
    }
    const auto& cb = app.codeBlocks[app.hoveredCodeBlock];
    if (!cb.isDiagram || !cb.fitCandidate) return false;
    float previewOffsetX = documentViewportX(app);
    float docX = (mouseX - previewOffsetX) + app.scrollX;
    float docY = mouseY + app.scrollY;
    float btnW = dpi(app, 72.0f);
    float btnH = dpi(app, 26.0f);
    float btnPad = 8.0f * app.contentScale * app.zoomFactor;
    float rightEdge =
        std::min(cb.bounds.right, app.scrollX + documentViewportWidth(app));
    float copyX = rightEdge - btnW - btnPad;
    float pngW = dpi(app, 52.0f);
    float pngX = copyX - pngW - dpi(app, 6.0f);
    float fitW = dpi(app, 44.0f);
    float fitX = pngX - fitW - dpi(app, 6.0f);
    float btnY = cb.bounds.top + btnPad;
    return docX >= fitX && docX <= fitX + fitW &&
           docY >= btnY && docY <= btnY + btnH;
}

// True when (mouseX, mouseY) is on the hovered table's fit-to-width
// toggle (drawn left of the copy-as-TSV button)
static bool tableFitButtonAt(const App& app, int mouseX, int mouseY) {
    if (app.hoveredTable < 0 ||
        app.hoveredTable >= (int)app.tableRects.size()) {
        return false;
    }
    const auto& tb = app.tableRects[app.hoveredTable];
    if (!tb.fitCandidate) return false;
    float previewOffsetX = documentViewportX(app);
    float docX = (mouseX - previewOffsetX) + app.scrollX;
    float docY = mouseY + app.scrollY;
    float btnW = dpi(app, 88.0f);
    float btnH = dpi(app, 26.0f);
    float btnPad = 8.0f * app.contentScale * app.zoomFactor;
    float rightEdge =
        std::min(tb.bounds.right, app.scrollX + documentViewportWidth(app));
    float btnX = rightEdge - btnW - btnPad;
    float fitW = dpi(app, 44.0f);
    float fitX = btnX - fitW - dpi(app, 6.0f);
    float btnY = tb.bounds.top + btnPad;
    return docX >= fitX && docX <= fitX + fitW &&
           docY >= btnY && docY <= btnY + btnH;
}

// Flip a table/diagram between natural size and fit-to-width and relayout
static void toggleFitBlock(App& app, HWND hwnd, unsigned key) {
    auto it = std::find(app.fitBlocks.begin(), app.fitBlocks.end(), key);
    if (it != app.fitBlocks.end()) {
        app.fitBlocks.erase(it);
    } else {
        app.fitBlocks.push_back(key);
    }
    app.layoutDirty = true;
    requestRepaint(hwnd);
}

void handleMouseUp(App& app, HWND hwnd, int mouseX, int mouseY) {
    // A tab drag ends on release (its press already consumed the click)
    if (app.tabDragIndex >= 0) {
        tabDragEnd(app, hwnd, mouseX, mouseY);
    }
    // The press captured the mouse (selection drags need it); the release
    // must let go no matter which branch handles it. A branch that
    // returned early used to leak the capture, which silently killed the
    // caption buttons' non-client hit-testing until the next full click.
    releaseMouseIfCaptured(hwnd);
    // Release belonging to a context-menu item click: already handled
    if (app.swallowNextMouseUp) {
        app.swallowNextMouseUp = false;
        return;
    }

    // Lightbox: a plain click closes, a pan drag just ends
    if (app.showLightbox) {
        if (app.lightboxDragging) {
            app.lightboxDragging = false;
            releaseMouseIfCaptured(hwnd);
            if (!app.lightboxDragMoved) closeLightbox(app);
            requestRepaint(hwnd);
        }
        return;
    }

    // Create-missing-reference dialog: buttons act, clicks elsewhere
    // cancel (unlike unsaved-changes, nothing is at stake)
    if (app.createRefPending) {
        float mx = (float)mouseX;
        float my = (float)mouseY;
        for (const auto& hit : app.createRefHits) {
            if (mx >= hit.first.left && mx <= hit.first.right &&
                my >= hit.first.top && my <= hit.first.bottom) {
                createRefAction(app, hwnd, hit.second);
                return;
            }
        }
        createRefAction(app, hwnd, 2);
        return;
    }

    // Unsaved-changes dialog: resolve its buttons; clicks elsewhere keep
    // the dialog up (it is modal, and dismissal must be deliberate)
    if (app.editMode && app.confirmExitPending) {
        float mx = (float)mouseX;
        float my = (float)mouseY;
        for (const auto& hit : app.confirmExitHits) {
            if (mx >= hit.first.left && mx <= hit.first.right &&
                my >= hit.first.top && my <= hit.first.bottom) {
                confirmExitAction(app, hwnd, hit.second);
                return;
            }
        }
        return;
    }

    // Shortcut editor clicks: arm a row for capture, reset, or close
    if (app.showShortcutEditor) {
        float mx = (float)mouseX;
        float my = (float)mouseY;
        for (const auto& hit : app.shortcutHits) {
            if (mx < hit.first.left || mx > hit.first.right ||
                my < hit.first.top || my > hit.first.bottom) continue;
            if (hit.second == 100) {
                // Reset: back to the stock bindings, still custom
                app.keyOverrides.clear();
                Settings s;
                s.keyProfile = "custom";
                applyKeymap(app, s);
                app.shortcutEditorRow = -1;
            } else {
                app.shortcutEditorRow = hit.second;
            }
            requestRepaint(hwnd);
            return;
        }
        // Click outside the rows: close back to settings
        app.showShortcutEditor = false;
        app.shortcutEditorRow = -1;
        app.showSettings = true;
        app.settingsAnimation = 0;
        requestRepaint(hwnd);
        return;
    }

    // Theme editor clicks
    if (app.showThemeEditor) {
        float mx = (float)mouseX;
        float my = (float)mouseY;
        for (const auto& hit : app.themeEditorHits) {
            if (mx < hit.first.left || mx > hit.first.right ||
                my < hit.first.top || my > hit.first.bottom) continue;
            int action = hit.second;
            if (action >= TE_FONT_BASE) {
                int idx = action - TE_FONT_BASE;
                if (idx >= 0 && idx < (int)app.systemFontFamilies.size()) {
                    app.themeEditorFont = app.systemFontFamilies[idx];
                }
            } else if (action >= TE_FIELD_BG && action <= TE_FIELD_CODEBG) {
                app.themeEditorField = action - TE_FIELD_BG;
            } else if (action == TE_FIELD_NAME) {
                app.themeEditorField = 6;
            } else if (action == TE_BASE_PREV || action == TE_BASE_NEXT) {
                int n = themeCount();
                int base = app.themeEditorBase + (action == TE_BASE_NEXT ? 1 : n - 1);
                themeEditorAdoptBase(app, base % n);
            } else if (action == TE_DARK) {
                app.themeEditorTheme.isDark = !app.themeEditorTheme.isDark;
            } else if (action == TE_OPEN_INI) {
                openThemesIniFile();
            } else if (action == TE_CANCEL) {
                closeThemeEditor(app, true);
                return;
            } else if (action == TE_SAVE) {
                std::wstring name = app.themeEditorName.empty()
                    ? L"My theme" : app.themeEditorName;
                const D2DTheme& base = themeAt(app.themeEditorBase);
                // Carry the base theme's heading face so deriving from a
                // theme with headingfont= (Abyss included) keeps its look
                std::wstring baseHeading =
                    (base.headingFontFamily && base.headingFontFamily[0])
                        ? base.headingFontFamily : L"";
                int idx = saveCustomTheme(app.themeEditorTheme, name,
                    app.themeEditorFont, base.codeFontFamily, baseHeading);
                if (idx >= 0) {
                    // THE PREVIEW CACHE MUST GROW FOR THE CHOOSER: saving a custom theme
                    // makes themeCount() bigger, and ensureThemePreviewFormats early-returns
                    // on a non-empty vector, so the chooser would keep showing the old
                    // count until a restart. Emptying it reopens that lazy door and
                    // applyTheme below rebuilds through updateTextFormats.
                    //
                    // clear() ALONE, and it was not alone until this commit. A hand-written
                    // per-slot loop stood here resetting three storages, three natives and
                    // three wrapper pointers before the clear -- the FOURTH copy of a release
                    // that already existed in App::releaseOverlayFormats, in the Qt
                    // initialiser and in the vector's own destructor. Every line of it was
                    // redundant: clear() destroys the elements, which destroys the Seated<>
                    // storages, which destroys the wrappers, and D2DTextFormat Releases in
                    // its destructor. Same number of Releases before and after.
                    //
                    // IT IS ALSO WHAT MADE THIS SITE INVISIBLE. The port's reconnaissance
                    // grepped for the native chrome members and concluded they were read in
                    // exactly two files, include/app.h and src/d2d_init.cpp. This was the
                    // third, 2,700 lines into an input handler, and nothing found it until
                    // deleting the members turned it into three compiler errors. The same
                    // clear()-alone correction was made for the image cache one slice ago
                    // (see App::releaseImageCache) and called the explicit per-entry release
                    // "the fourth hand-written half of the same pairing" -- this is another.
                    app.themePreviewFormats.clear();
                    applyTheme(app, idx);
                    persistThemeChoice(app);
                }
                closeThemeEditor(app, false);
                return;
            }
            requestRepaint(hwnd);
            return;
        }
        return;
    }

    // Settings overlay: resolve against the hit rects stored during render
    if (app.showSettings) {
        if (app.settingsDragSlider) {
            app.settingsDragSlider = 0;
            releaseMouse();
            return;
        }
        float mx = (float)mouseX;
        float my = (float)mouseY;
        // Reverse: later-drawn controls (the language popup) sit on top
        for (auto it = app.settingsHits.rbegin(); it != app.settingsHits.rend(); ++it) {
            if (mx >= it->first.left && mx <= it->first.right &&
                my >= it->first.top && my <= it->first.bottom) {
                settingsAction(app, hwnd, it->second);
                return;
            }
        }
        if (app.settingsLangOpen || app.settingsKeysOpen) {
            app.settingsLangOpen = false;  // click elsewhere collapses them
            app.settingsKeysOpen = false;
            requestRepaint(hwnd);
        }
        return;
    }

    // Print preview: buttons and format chips
    if (app.showPrintPreview) {
        float mx = (float)mouseX;
        float my = (float)mouseY;
        auto hit = [&](const tinta::RectF& r) {
            return mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom;
        };
        if (hit(app.printPreviewPrintBtn)) {
            printPreviewConfirm(app, hwnd);
            return;
        }
        if (hit(app.printPreviewCancelBtn)) {
            closePrintPreview(app, hwnd);
            return;
        }
        for (int i = 0; i < PRINT_PAPER_COUNT; i++) {
            if (hit(app.printPreviewPaperBtn[i])) {
                printPreviewSetFormat(app, i, app.printPreviewLandscape);
                return;
            }
        }
        if (hit(app.printPreviewOrientBtn[0])) {
            printPreviewSetFormat(app, app.printPreviewPaper, false);
        } else if (hit(app.printPreviewOrientBtn[1])) {
            printPreviewSetFormat(app, app.printPreviewPaper, true);
        }
        return;
    }
    // Help scrollbar release
    if (app.helpScrollbarDragging) {
        app.helpScrollbarDragging = false;
        releaseMouse();
        requestRepaint(hwnd);
        return;
    }

    if (app.showHelp) return;

    // Theme chooser click handling
    if (app.showThemeChooser) {
        float clickX = (float)mouseX;
        float clickY = (float)mouseY;
        int hit = themeChooserHitAt(app, clickX, clickY);

        auto restorePreview = [&]() {
            if (app.themeChooserPreviewBase >= 0) {
                applyTheme(app, app.themeChooserPreviewBase);
                app.themeChooserPreviewBase = -1;
            }
        };

        if (hit == -1) {
            // "Follow Windows" toggle
            restorePreview();
            app.followSystemTheme = !app.followSystemTheme;
            if (app.followSystemTheme) {
                // Adopt the current theme as this mode's preference, then
                // snap to whatever the system mode wants
                if (app.theme.isDark) app.darkThemeIndex = app.currentThemeIndex;
                else app.lightThemeIndex = app.currentThemeIndex;
                postWindowMessage(hwnd, WM_SETTINGCHANGE, 0,
                                  (LPARAM)L"ImmersiveColorSet");
            }
            requestRepaint(hwnd);
            return;
        }
        if (hit == -3) {
            // Footer: open the theme editor on the committed theme
            restorePreview();
            app.showThemeChooser = false;
            app.themeChooserAnimation = 0;
            openThemeEditor(app, true);
            requestRepaint(hwnd);
            return;
        }
        if (hit >= 0) {
            // Commit the pick (the hover preview already applied it)
            applyTheme(app, hit);
            app.themeChooserPreviewBase = -1;
            // While auto mode is on, picking a card records it as the
            // preference for that card's light/dark class
            if (app.followSystemTheme) {
                if (themeAt(hit).isDark) app.darkThemeIndex = hit;
                else app.lightThemeIndex = hit;
            }
            // New windows spawned from here on come up in this theme
            persistThemeChoice(app);
            app.showThemeChooser = false;
            app.themeChooserAnimation = 0;
        } else {
            // Close (x) or a click on empty panel / outside: put the
            // committed theme back and dismiss
            restorePreview();
            app.showThemeChooser = false;
            app.themeChooserAnimation = 0;
        }
        requestRepaint(hwnd);
        return;
    }

    // Edit mode: route to editor
    if (app.editMode && (app.draggingSeparator || app.editorSelecting ||
                         app.editorScrollbarDragging)) {
        int x = mouseX;
        int y = mouseY;
        handleEditorMouseUp(app, hwnd, x, y);
        return;
    }

    releaseMouse();

    // A scrollbar drag ends at this release no matter where it lands -
    // the panel branches below must not run first and leave the drag
    // latched, which bound the scroll to plain mouse movement
    if (app.scrollbarDragging || app.hScrollbarDragging) {
        app.scrollbarDragging = false;
        app.hScrollbarDragging = false;
        app.mouseDown = false;
        app.selecting = false;
        requestRepaint(hwnd);
        return;
    }

    // TOC click handling
    if (app.showToc) {
        int clickX = mouseX;
        int clickY = mouseY;

        float panelWidth = tocPanelWidth(app);
        float panelX = tocPanelX(app, panelWidth);

        if (clickX >= panelX && (float)clickX <= panelX + panelWidth) {
            // Click inside panel
            // Hit-test the click's own coordinates (#114 pattern) - the
            // render-time hover can be a frame stale on fast move+click
            int hit = tocItemIndexAt(app, (float)clickX, (float)clickY);
            if (cursorPointInRect((float)clickX, (float)clickY,
                                  app.tocPinRect)) {
                // Pin toggle (#156): pinned survives document clicks
                app.tocPinned = !app.tocPinned;
            } else if (cursorPointInRect((float)clickX, (float)clickY,
                                  app.tocCloseRect)) {
                app.showToc = false;
                app.tocAnimation = 0;
                app.tocFilter.clear();
            } else if (hit >= 0 && hit < (int)app.headings.size()) {
                // Scroll document to heading
                ensureLayoutComplete(app);
                if (hit >= (int)app.headings.size()) {
                    requestRepaint(hwnd);
                    return;
                }
                scrollToHeadingY(app, app.headings[hit].y);
                // The clicked heading is the active one even when the
                // scroll cannot physically reach it (short documents);
                // the next real scroll hands back to the spy
                app.tocSpyOverride = hit;
                app.tocSpyOverrideScroll = app.targetScrollY;

                // Close TOC - a pinned panel stays for the next jump
                if (!app.tocPinned) {
                    app.showToc = false;
                    app.tocAnimation = 0;
                    app.tocFilter.clear();
                }
            }
            requestRepaint(hwnd);
            return;
        }
        if (!app.tocPinned) {
            // Click outside panel = close TOC
            app.showToc = false;
            app.tocAnimation = 0;
            app.tocFilter.clear();
            requestRepaint(hwnd);
            return;
        }
        // Pinned (#156): the click belongs to the document - fall through
    }

    // Folder browser click handling
    if (app.showFolderBrowser) {
        int clickX = mouseX;
        int clickY = mouseY;

        // Calculate panel bounds (must match render code)
        float panelWidth = folderBrowserPanelWidth(app);
        float panelX = -panelWidth * (1.0f - app.folderBrowserAnimation);

        // Check if click is inside panel
        if (clickX >= panelX && clickX <= panelX + panelWidth) {
            // Card geometry shared with the renderer (t13)
            FolderBrowserMetrics g = folderBrowserMetrics(app);
            bool inHeader = clickY >= g.headerY && clickY <= g.headerY + g.headerH;

            // An active input keeps focus when clicked, anything else cancels it
            if (app.folderBrowserEditingPath || app.folderBrowserNaming != 0) {
                bool onInput = app.folderBrowserEditingPath
                    ? inHeader
                    : (clickY >= g.listStartY &&
                       clickY <= g.listStartY + g.itemHeight);
                if (onInput) {
                    app.folderBrowserInputSelectAll = false;
                } else {
                    closeFolderBrowserInput(app);
                }
                requestRepaint(hwnd);
                return;
            }

            if (cursorPointInRect((float)clickX, (float)clickY,
                                  app.folderPinRect)) {
                // Pin toggle (#156)
                app.browserPinned = !app.browserPinned;
                requestRepaint(hwnd);
                return;
            }
            if (inHeader && clickY >= g.btnY && clickY <= g.btnY + g.btnSize &&
                clickX >= g.folderBtnX && clickX <= g.fileBtnX + g.btnSize) {
                // + folder / + file buttons: start naming a new item
                app.folderBrowserNaming =
                    (clickX < g.folderBtnX + g.btnSize + dpi(app, 2.0f)) ? 2 : 1;
                app.folderBrowserInput.clear();
                app.folderBrowserInputSelectAll = false;
                app.folderBrowserInputError = false;
                app.folderBrowserScroll = 0.0f;
                updateBlinkTimer(app);
                resetCursorBlink(app);
                requestRepaint(hwnd);
                return;
            }
            if (inHeader) {
                // Breadcrumb segments navigate to their ancestor folder
                for (const auto& crumb : app.folderCrumbHits) {
                    if (!cursorPointInRect((float)clickX, (float)clickY,
                                           crumb.first)) {
                        continue;
                    }
                    std::wstring rebuilt;
                    int kept = 0;
                    size_t pos = 0;
                    const std::wstring& full = app.folderBrowserPath;
                    while (pos <= full.size() && kept < crumb.second) {
                        size_t sep = full.find_first_of(L"\\/", pos);
                        std::wstring part = sep == std::wstring::npos
                                                ? full.substr(pos)
                                                : full.substr(pos, sep - pos);
                        if (!part.empty()) {
                            if (!rebuilt.empty()) rebuilt += L'\\';
                            rebuilt += part;
                            kept++;
                        }
                        if (sep == std::wstring::npos) break;
                        pos = sep + 1;
                    }
                    if (!rebuilt.empty()) {
                        if (rebuilt.back() == L':') rebuilt += L'\\';
                        app.folderBrowserPath = rebuilt;
                        populateFolderItems(app);
                        app.folderBrowserScroll = 0.0f;
                    }
                    requestRepaint(hwnd);
                    return;
                }
                // Anywhere else on the row (the pencil included) becomes an
                // edit box with everything selected, so click + paste +
                // Enter jumps straight to a new path (#52)
                app.folderBrowserEditingPath = true;
                app.folderBrowserInput = app.folderBrowserPath;
                app.folderBrowserInputSelectAll = true;
                app.folderBrowserInputError = false;
                updateBlinkTimer(app);
                resetCursorBlink(app);
                requestRepaint(hwnd);
                return;
            }

            // Hit-test items at the CLICK position: the render-time hover
            // index lags a paint behind the mouse, so trusting it here
            // opened the previously highlighted row on fast move-and-click
            int clickedItem = folderItemIndexAt(app, (float)clickX, (float)clickY);
            if (clickedItem >= 0 && clickedItem < (int)app.folderItems.size()) {
                const auto& item = app.folderItems[clickedItem];

                if (item.isDirectory) {
                    // Navigate into folder
                    if (item.name == L"..") {
                        // Go up to parent
                        app.folderBrowserPath = getParentPath(app.folderBrowserPath);
                    } else {
                        // Enter subdirectory
                        if (app.folderBrowserPath.back() != L'\\' && app.folderBrowserPath.back() != L'/') {
                            app.folderBrowserPath += L'\\';
                        }
                        app.folderBrowserPath += item.name;
                    }
                    populateFolderItems(app);
                } else {
                    // Open document
                    std::wstring fullPath = app.folderBrowserPath;
                    if (fullPath.back() != L'\\' && fullPath.back() != L'/') {
                        fullPath += L'\\';
                    }
                    fullPath += item.name;

                    // Preview mode: open the file but keep the browser up,
                    // so the list doubles as a file picker — the current
                    // file is highlighted, and the browser closes on a
                    // document click, B, or Esc. A Ctrl+click (or a pending
                    // Ctrl+T / + intent) opens into a new tab instead.
                    bool ctrlHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    if (app.tabNewTabIntent || ctrlHeld) {
                        app.tabNewTabIntent = false;
                        tabOpenPath(app, hwnd, toUtf8(fullPath), true);
                    } else {
                        openDocumentInViewer(app, fullPath);
                    }
                }
            }
            requestRepaint(hwnd);
            return;
        }
        if (!app.browserPinned) {
            // Click outside panel = close browser
            closeFolderBrowserInput(app);
            app.showFolderBrowser = false;
            app.folderBrowserAnimation = 0;
            requestRepaint(hwnd);
            return;
        }
        // Pinned (#156): the click belongs to the document - fall through
    }


    if (app.scrollbarDragging) {
        app.scrollbarDragging = false;
        requestRepaint(hwnd);
    } else if (app.hScrollbarDragging) {
        app.hScrollbarDragging = false;
        requestRepaint(hwnd);
    } else if (codeCopyButtonAt(app, app.mouseX, app.mouseY)) {
        // Releasing anywhere else on a code block must NOT consume the
        // mouse-up: a drag selection ending on the block would never
        // finalize, stay "selecting", and follow the next mouse move
        platform::setClipboardText(app.codeBlocks[app.hoveredCodeBlock].codeText);
        {
            // The Copied pill keeps its code-block anchor (t13)
            tinta::RectF anchor = app.codeBlocks[app.hoveredCodeBlock].bounds;
            signalPush(app, SIG_SUCCESS, SIGI_CHECK,
                       tr(app, "codeblock.copied"), L"", L"", SIGA_NONE,
                       SIGC_NONE, true, &anchor);
        }
        app.hoveredCodeBlock = -1;
        app.selecting = false;
        requestRepaint(hwnd);
    } else if (diagramFitButtonAt(app, app.mouseX, app.mouseY)) {
        toggleFitBlock(app, hwnd,
                       app.codeBlocks[app.hoveredCodeBlock].fitKey);
        app.selecting = false;
    } else if (diagramPngButtonAt(app, app.mouseX, app.mouseY)) {
        // Diagram image button: 2x raster to the clipboard
        if (copyDiagramImage(
                app, toUtf8(app.codeBlocks[app.hoveredCodeBlock].codeText))) {
            tinta::RectF anchor = app.codeBlocks[app.hoveredCodeBlock].bounds;
            signalPush(app, SIG_SUCCESS, SIGI_CHECK,
                       tr(app, "diagram.copied"), L"", L"", SIGA_NONE,
                       SIGC_NONE, true, &anchor);
        }
        app.hoveredCodeBlock = -1;
        app.selecting = false;
        requestRepaint(hwnd);
    } else if (tableFitButtonAt(app, app.mouseX, app.mouseY)) {
        toggleFitBlock(app, hwnd,
                       app.tableRects[app.hoveredTable].fitKey);
        app.selecting = false;
    } else if (tableCopyButtonAt(app, app.mouseX, app.mouseY)) {
        // Table copy button: cells joined by tabs paste into a
        // spreadsheet as a grid
        platform::setClipboardText(app.tableRects[app.hoveredTable].tsv);
        {
            tinta::RectF anchor = app.tableRects[app.hoveredTable].bounds;
            signalPush(app, SIG_SUCCESS, SIGI_CHECK,
                       tr(app, "table.copied"), L"", L"", SIGA_NONE,
                       SIGC_NONE, true, &anchor);
        }
        app.hoveredTable = -1;
        app.selecting = false;
        requestRepaint(hwnd);
    } else if (app.selecting) {
        // Edge auto-scroll ends with the drag
        if (app.selAutoScrollVel != 0.0f) {
            disarmTimer(hwnd, TIMER_SELECT_SCROLL);
            app.selAutoScrollVel = 0.0f;
        }
        // Finalize selection based on mode
        if (app.selectionMode == App::SelectionMode::Word ||
            app.selectionMode == App::SelectionMode::Line) {
            // Word/Line selection: ranges were set during mouse down/move
        } else {
            // Normal selection: finalize the focus at the release point
            float previewOffsetX = documentViewportX(app);
            float docX = (app.mouseX - previewOffsetX) + app.scrollX;
            float docY = app.mouseY + app.scrollY;
            app.selFocus = selectionOffsetAtPoint(app, docX, docY);

            // Check if this was a meaningful drag (not just a click).
            // Shift+click extension counts without one.
            int dx = abs(app.mouseX - app.lastClickX);
            int dy = abs(app.mouseY - app.lastClickY);
            if ((dx > 5 || dy > 5 || app.selShiftExtend) &&
                app.selFocus != app.selAnchor) {
                app.hasSelection = true;
            } else if (const App::TaskRect* task = taskRectAt(app)) {
                // Click on a task checkbox: flip the mark in the file
                toggleTaskOnDisk(app, hwnd, task->markOffset, task->checked);
                app.hasSelection = false;
            } else if (!app.hoveredLink.empty()) {
                // It was just a click on a link
                if (app.hoveredLink.rfind("wiki:", 0) == 0) {
                    std::string fromPath = app.currentFile;
                    float fromScroll = app.scrollY;
                    if (openWikiLink(app, app.hoveredLink.substr(5))) {
                        navRecordJump(app, fromPath, fromScroll);
                    }
                } else if (app.hoveredLink.rfind("fileref-ok:", 0) == 0) {
                    // Live .md reference (#127): join as a tab
                    openFileRefTarget(app, hwnd, app.hoveredLink.substr(11));
                } else if (app.hoveredLink.rfind("fileref-missing:", 0) ==
                           0) {
                    // Ghost reference: offer to create the target
                    app.createRefPath = app.hoveredLink.substr(16);
                    app.createRefPending = true;
                } else {
                    handleLinkClick(app);
                }
                app.hasSelection = false;
            } else if (annotationAtDocPoint(app, docX, docY) >= 0) {
                // A click on annotated text opens its note editor (#126)
                annotationOpenEditor(app, annotationAtDocPoint(app, docX, docY));
                app.hasSelection = false;
            } else if (const App::LayoutBitmap* img =
                           layoutBitmapAt(app, docX, docY)) {
                // Inline image: view it full size
                openLightbox(app, img->bitmap);
                app.hasSelection = false;
            } else {
                app.hasSelection = false;
            }
        }
        requestRepaint(hwnd);
    } else if (!app.hoveredLink.empty()) {
        // Click on link
        if (app.hoveredLink.rfind("fileref-ok:", 0) == 0) {
            openFileRefTarget(app, hwnd, app.hoveredLink.substr(11));
        } else if (app.hoveredLink.rfind("fileref-missing:", 0) == 0) {
            app.createRefPath = app.hoveredLink.substr(16);
            app.createRefPending = true;
        } else {
            handleLinkClick(app);
        }
    } else if (!app.editMode) {
        // A click on annotated text opens its note editor (#126); a
        // click on an inline image opens the lightbox
        float docX = (app.mouseX - documentViewportX(app)) + app.scrollX;
        float docY = app.mouseY + app.scrollY;
        int annotHit = annotationAtDocPoint(app, docX, docY);
        if (annotHit >= 0) {
            annotationOpenEditor(app, annotHit);
        } else if (const App::LayoutBitmap* img =
                       layoutBitmapAt(app, docX, docY)) {
            openLightbox(app, img->bitmap);
        }
    }

    app.mouseDown = false;
    app.selecting = false;
    app.selShiftExtend = false;
    // Selection drags capture the mouse; the settings slider releases its
    // own capture earlier, so this is a no-op for everything else
    releaseMouseIfCaptured(hwnd);
}

// Zen mode: borderless fullscreen + centered reading column (F11)
static void toggleZenMode(App& app, HWND hwnd) {
    app.zenMode = !app.zenMode;
    if (app.zenMode) {
        app.zenRestorePlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, &app.zenRestorePlacement);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLongW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(hwnd, &app.zenRestorePlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    app.layoutDirty = true;
    requestRepaint(hwnd);
}

bool handleKeyDown(App& app, HWND hwnd, unsigned vk) {
    float pageSize = app.height * 0.8f;
    float maxScroll = std::max(0.0f, app.contentHeight - app.height);
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    // Lightbox: dismiss keys close it, everything else is swallowed
    if (app.showLightbox) {
        if (vk == VK_ESCAPE || vk == VK_RETURN ||
            vk == VK_SPACE) {
            closeLightbox(app);
            requestRepaint(hwnd);
        }
        return false;
    }

    // Annotation note editor owns the keyboard while open (#126)
    if (app.annotEditorOpen && !app.editMode) {
        annotationEditorKeyDown(app, hwnd, vk, ctrl);
        requestRepaint(hwnd);
        return false;
    }

    // Create-missing-reference dialog owns the keyboard while open
    if (app.createRefPending) {
        if (vk == 'Y' || vk == VK_RETURN) {
            app.swallowCharsUntil = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(150);
            createRefAction(app, hwnd, 1);
        } else if (vk == 'N' || vk == VK_ESCAPE) {
            app.swallowCharsUntil = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(150);
            createRefAction(app, hwnd, 2);
        }
        return false;
    }

    // Print preview captures the whole keyboard while open
    if (app.showPrintPreview) {
        switch (vk) {
            case VK_ESCAPE:
                closePrintPreview(app, hwnd);
                break;
            case VK_RETURN:
                printPreviewConfirm(app, hwnd);
                break;
            case 'P':  // a second Ctrl+P also proceeds to the dialog
                if (ctrl) printPreviewConfirm(app, hwnd);
                break;
            case VK_NEXT: case VK_DOWN: case VK_RIGHT: case VK_SPACE: case 'J':
                printPreviewSetPage(app, app.printPreviewPage + 1);
                break;
            case VK_PRIOR: case VK_UP: case VK_LEFT: case 'K':
                printPreviewSetPage(app, app.printPreviewPage - 1);
                break;
            case VK_HOME:
                printPreviewSetPage(app, 0);
                break;
            case VK_END:
                printPreviewSetPage(app, (int)app.printPreviewBounds.size() - 2);
                break;
        }
        return false;
    }

    if (!app.confirmExitPending && app.showContextMenu) {
        if (vk == VK_ESCAPE || vk == VK_F10) {
            closeContextMenu(app);
        } else if (vk == VK_DOWN || vk == VK_UP ||
                   vk == VK_HOME || vk == VK_END) {
            int current = (vk == VK_HOME || vk == VK_END)
                              ? -1 : app.hoveredContextMenuItem;
            app.contextMenuKeyboard = true;
            app.hoveredContextMenuItem = nextContextMenuItem(
                app, current, (vk == VK_UP || vk == VK_END) ? -1 : 1);
        } else if (vk == VK_RETURN || vk == VK_SPACE) {
            int item = app.hoveredContextMenuItem;
            closeContextMenu(app);
            app.swallowCharsUntil = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(150);
            if (contextMenuItemEnabled(app, item)) invokeContextMenuAction(app, hwnd, item);
        }
        requestRepaint(hwnd);
        return false;
    }
    // Shortcut editor: an armed row captures the next key; Esc cancels
    // the capture, then closes back to settings
    if (app.showShortcutEditor) {
        if (vk == VK_ESCAPE) {
            if (app.shortcutEditorRow >= 0) {
                app.shortcutEditorRow = -1;
            } else {
                app.showShortcutEditor = false;
                app.showSettings = true;
                app.settingsAnimation = 0;
            }
        } else if (app.shortcutEditorRow >= 0) {
            unsigned key = 0;
            if (vk == VK_TAB || vk == VK_SPACE ||
                (vk >= VK_F1 && vk <= VK_F12) ||
                (vk >= 'A' && vk <= 'Z') ||
                (vk >= '0' && vk <= '9')) {
                key = (unsigned)vk;
            }
            if (key) shortcutAssign(app, {(unsigned)key, false});
        }
        requestRepaint(hwnd);
        return false;
    }
    // Theme editor: Esc returns to settings; text arrives via WM_CHAR
    if (app.showThemeEditor) {
        if (vk == VK_ESCAPE) closeThemeEditor(app, true);
        return false;
    }
    // Settings overlay captures the keyboard while open
    if (app.showSettings) {
        if (vk == VK_ESCAPE || (ctrl && vk == VK_OEM_COMMA)) {
            app.showSettings = false;
            app.settingsAnimation = 0;
            requestRepaint(hwnd);
        }
        return false;
    }

    if (app.showHelp || app.showThemeChooser) {
        if (vk == VK_ESCAPE ||
            (!ctrl && app.showHelp && keyBindingMatches(app.keymap[KA_HELP], (unsigned)vk, false)) ||
            (!ctrl && app.showThemeChooser && keyBindingMatches(app.keymap[KA_THEME], (unsigned)vk, false))) {
            if (app.themeChooserPreviewBase >= 0) {
                applyTheme(app, app.themeChooserPreviewBase);
                app.themeChooserPreviewBase = -1;
            }
            app.showThemeChooser = false;
            app.showHelp = false;
            requestRepaint(hwnd);
            return true;
        } else if (app.showHelp) {
            float step = dpi(app, 60.0f);
            if (vk == VK_NEXT || vk == VK_PRIOR) step = app.helpVisibleHeight;
            if (vk == VK_DOWN || vk == VK_NEXT) app.helpScroll += step;
            if (vk == VK_UP || vk == VK_PRIOR) app.helpScroll -= step;
            app.helpScroll = std::max(0.0f, std::min(app.helpScroll,
                app.helpContentHeight - app.helpVisibleHeight));
        }
        requestRepaint(hwnd);
        return false;  // punctuation Help bindings still need WM_CHAR
    }
    if (appMenuAvailable(app)) {
        if (vk == VK_F10) {
            toggleApplicationMenu(app, hwnd, true);
            return false;
        }
        if (ctrl && vk == 'O') {
            openFileDialog(app, hwnd);
            return false;
        }
        if (ctrl && vk == VK_OEM_COMMA) {
            closeSearchIfOpen(app);
            app.showSettings = true;
            app.settingsAnimation = 0;
            requestRepaint(hwnd);
            return false;
        }
    }

    // Tab shortcuts work everywhere except modal capture states: Ctrl+Tab
    // cycles, Ctrl+W closes, Ctrl+T opens the browser into a new tab,
    // Ctrl+1..9 jumps (a dirty editor parks its buffer in the tab)
    if (ctrl && !app.confirmExitPending &&
        !(app.showFolderBrowser &&
          (app.folderBrowserEditingPath || app.folderBrowserNaming != 0))) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (vk == VK_TAB) {
            tabCycle(app, hwnd, shift ? -1 : 1);
            return false;
        }
        if (vk == 'W' && !app.editMode) {
            // In edit mode Ctrl+W keeps its documented word-wrap toggle
            tabCloseIndex(app, hwnd, app.activeTab);
            return false;
        }
        if (vk == VK_F4) {
            // Ctrl+F4 closes the tab in BOTH modes - the reachable close
            // while editing, where Ctrl+W means word wrap (#181)
            tabCloseIndex(app, hwnd, app.activeTab);
            return false;
        }
        if (vk == 'T') {
            // New tab = the start page (browser-style new-tab page); its
            // Open/Browse/recents land in this tab. The old open-into-a-
            // new-tab browser flow lives on as Ctrl+click in the browser.
            tabOpenStartPage(app, hwnd);
            return false;
        }
        if (vk >= '1' && vk <= '9' && app.tabs.size() > 1) {
            int index = (int)(vk - '1');
            if (index < (int)app.tabs.size()) tabActivate(app, hwnd, index);
            return false;
        }
    }
    if (app.showTabSwitcher && vk == VK_ESCAPE) {
        app.showTabSwitcher = false;
        requestRepaint(hwnd);
        return false;
    }
    // Esc aborts a tab drag in flight (ghost vanishes, tab snaps home)
    if (app.tabDragIndex >= 0 && vk == VK_ESCAPE) {
        tabDragCancel(app, hwnd);
        return false;
    }
    // An open tab menu takes Esc before anything else (edit mode too —
    // the strip stays interactive while editing)
    if (app.showTabMenu && vk == VK_ESCAPE) {
        closeTabMenu(app);
        requestRepaint(hwnd);
        return false;
    }

    // Function-key Help remains available while editing. Printable custom
    // bindings stay with the text editor, as do Tab and Space.
    if (app.editMode && !app.confirmExitPending && !ctrl &&
        vk >= VK_F1 && vk <= VK_F12 &&
        keyBindingMatches(app.keymap[KA_HELP], (unsigned)vk, false)) {
        app.showHelp = true;
        app.helpAnimation = 0;
        requestRepaint(hwnd);
        return true;
    }

    // Edit mode: Ctrl+C with preview pane selection should copy from preview
    if (app.editMode) {
        // An open table cell editor owns the keyboard (#148)
        if (tableEditKeyDown(app, hwnd, vk)) return false;
        if (ctrl && vk == 'C' && app.hasSelection &&
            app.selAnchor != app.selFocus) {
            std::wstring selected = selectionTextForRange(
                app, std::min(app.selAnchor, app.selFocus),
                std::max(app.selAnchor, app.selFocus));
            if (selected.empty()) return false;
            platform::setClipboardText(selected);
            signalPushKey(app, SIG_INFO, SIGI_COPY, "toast.copied");
            app.hasSelection = false;
            requestRepaint(hwnd);
            return false;
        }
        handleEditorKeyDown(app, hwnd, vk);
        return false;
    }

    // Folder browser path/name input captures the keyboard while active
    // (printable characters arrive separately via WM_CHAR)
    if (app.showFolderBrowser &&
        (app.folderBrowserEditingPath || app.folderBrowserNaming != 0)) {
        if (ctrl && vk == 'V') {
            std::wstring pasted = platform::flattenToSingleLine(platform::clipboardText());
            if (!pasted.empty()) {
                if (app.folderBrowserInputSelectAll) {
                    app.folderBrowserInput.clear();
                    app.folderBrowserInputSelectAll = false;
                }
                app.folderBrowserInput += pasted;
                app.folderBrowserInputError = false;
                resetCursorBlink(app);
            }
        } else if (ctrl && vk == 'A') {
            if (!app.folderBrowserInput.empty()) app.folderBrowserInputSelectAll = true;
        } else if (!ctrl) {
            switch (vk) {
                case VK_ESCAPE:
                    closeFolderBrowserInput(app);
                    break;
                case VK_RETURN:
                    if (app.folderBrowserEditingPath) commitFolderBrowserPath(app);
                    else commitFolderBrowserNaming(app);
                    break;
                case VK_BACK:
                    if (app.folderBrowserInputSelectAll) {
                        app.folderBrowserInput.clear();
                        app.folderBrowserInputSelectAll = false;
                    } else if (!app.folderBrowserInput.empty()) {
                        app.folderBrowserInput.pop_back();
                    }
                    app.folderBrowserInputError = false;
                    resetCursorBlink(app);
                    break;
            }
        }
        requestRepaint(hwnd);
        return false;
    }

    // Handle search-specific keys when search is active
    if (app.showSearch && app.searchActive) {
        switch (vk) {
            case VK_ESCAPE:
                // Close search
                app.showSearch = false;
                app.searchActive = false;
                app.searchQuery.clear();
                app.searchMatches.clear();
                std::wstring().swap(app.docTextLower);
                app.searchAnimation = 0;
                clearFolderSearch(app);
                updateBlinkTimer(app);
                requestRepaint(hwnd);
                return false;
            case VK_RETURN:
                // Cycle to next match
                if (!app.searchMatches.empty()) {
                    app.searchCurrentIndex = (app.searchCurrentIndex + 1) % (int)app.searchMatches.size();
                    scrollToCurrentMatch(app);
                    requestRepaint(hwnd);
                }
                return false;
            case VK_BACK:
                // Delete last character
                if (!app.searchQuery.empty()) {
                    app.searchQuery.pop_back();
                    resetCursorBlink(app);
                    performSearch(app);
                    if (!app.searchMatches.empty()) {
                        scrollToCurrentMatch(app);
                    }
                    requestRepaint(hwnd);
                }
                return false;
        }
    }

    if (ctrl) {
        switch (vk) {
            case 'P':
                // Print preview (edit mode handles its own Ctrl+P in the
                // editor path)
                openPrintPreview(app, hwnd);
                break;
            case 'S':
                // Ctrl+Shift+S: save the viewed file under a new name and
                // continue there (#121); plain Ctrl+S stays a no-op in the
                // viewer — there is nothing edited to save
                if (GetKeyState(VK_SHIFT) & 0x8000) {
                    saveFileAs(app, hwnd);
                }
                break;
            case 'H':
                // Find & replace implies editing: from the viewer, hop
                // into edit mode with the replace row already open (#121)
                enterEditMode(app);
                if (app.editMode) {
                    app.showSearch = true;
                    app.searchActive = true;
                    app.searchAnimation = 0;
                    app.searchQuery.clear();
                    app.editorSearchMatches.clear();
                    app.editorSearchCurrentIndex = 0;
                    app.searchCurrentIndex = 0;
                    app.searchJustOpened = true;
                    app.searchReplaceMode = true;
                    app.replaceFieldActive = false;
                    updateBlinkTimer(app);
                    requestRepaint(hwnd);
                }
                break;
            case 'N':
                // Quick note, Notepad model: Ctrl+N stays in this window
                // (the launcher or an empty tab is taken over in place, a
                // document gets a fresh note tab); Ctrl+Shift+N spawns a
                // separate window
                if (GetKeyState(VK_SHIFT) & 0x8000) {
                    launchQuickNoteWindow();
                } else if (app.currentFile.empty()) {
                    // The keystroke's WM_CHAR must not type into the
                    // editor the takeover just opened
                    app.swallowCharsUntil =
                        std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(150);
                    tabsInit(app);
                    enterQuickNoteMode(app);
                    app.tabs[app.activeTab].title = tr(app, "title.untitled");
                    updateWindowTitle(app);
                    requestRepaint(hwnd);
                } else {
                    app.swallowCharsUntil =
                        std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(150);
                    tabOpenQuickNote(app, hwnd);
                }
                break;
            case 'A': {
                // Select All - the whole docText range
                if (app.root) {
                    // docText only exists for laid-out content — finish first
                    ensureLayoutComplete(app);
                    app.selAnchor = 0;
                    app.selFocus = app.docText.size();
                    app.hasSelection = true;
                }
                break;
            }
            case 'C': {
                // Copy - copy selected text or all text if select all was used
                bool copied = false;
                if (app.hasSelection && app.selAnchor != app.selFocus) {
                    std::wstring selected = selectionTextForRange(
                        app, std::min(app.selAnchor, app.selFocus),
                        std::max(app.selAnchor, app.selFocus));
                    if (!selected.empty()) {
                        platform::setClipboardText(selected);
                        copied = true;
                    }
                    app.hasSelection = false;
                } else if (app.root) {
                    // If no selection, copy all
                    std::wstring allText;
                    extractText(app.root, allText);
                    platform::setClipboardText(allText);
                    copied = true;
                }
                // Show "Copied!" notification
                if (copied) {
                    signalPushKey(app, SIG_INFO, SIGI_COPY, "toast.copied");
                    requestRepaint(hwnd);
                }
                break;
            }
            case VK_OEM_COMMA:
                // Ctrl+, opens settings (viewer mode)
                if (!app.editMode) {
                    closeSearchIfOpen(app);
                    app.showSettings = !app.showSettings;
                    app.settingsAnimation = 0;
                    requestRepaint(hwnd);
                }
                break;
            case 'F':
                // Ctrl+F to open search
                if (!app.showSearch) {
                    app.showSearch = true;
                    app.searchActive = true;
                    app.searchAnimation = 0;
                    app.searchQuery.clear();
                    app.searchMatches.clear();
                    std::wstring().swap(app.docTextLower);
                    app.searchCurrentIndex = 0;
                    app.searchJustOpened = true;
                    updateBlinkTimer(app);
                }
                requestRepaint(hwnd);
                break;
        }
    } else {
        // TOC filter: printable keys narrow the heading list via WM_CHAR,
        // Backspace edits, Enter jumps to the first match. A pinned panel
        // hands the keyboard back to the document (#156).
        if (app.showToc && !app.editMode && !app.tocPinned) {
            if (vk == VK_BACK) {
                if (!app.tocFilter.empty()) app.tocFilter.pop_back();
                requestRepaint(hwnd);
                return false;
            }
            if (vk == VK_RETURN) {
                std::wstring needle = toLower(app.tocFilter);
                for (size_t i = 0; i < app.headings.size(); i++) {
                    const auto& heading = app.headings[i];
                    if (needle.empty() ||
                        toLower(heading.text).find(needle) !=
                            std::wstring::npos) {
                        ensureLayoutComplete(app);
                        scrollToHeadingY(app, heading.y);
                        app.tocSpyOverride = (int)i;
                        app.tocSpyOverrideScroll = app.targetScrollY;
                        app.showToc = false;
                        app.tocAnimation = 0;
                        app.tocFilter.clear();
                        break;
                    }
                }
                requestRepaint(hwnd);
                return false;
            }
            if ((vk >= 'A' && vk <= 'Z') ||
                (vk >= '0' && vk <= '9') || vk == VK_SPACE) {
                return false;  // swallowed here; WM_CHAR feeds the filter
            }
        }

        // Start page: Ctrl+O opens the picker, 1..5 the recent files, and
        // the newfile key does what its button chip promises: a new
        // document in place (elsewhere it opens the naming-row flow).
        // B and ? keep their keymapped meanings, which the page reuses.
        if (startPageActive(app)) {
            if (ctrl && vk == 'O') {
                startPageInvoke(app, hwnd, 1);
                return false;
            }
            if (!ctrl && keyBindingMatches(app.keymap[KA_NEWFILE], (unsigned)vk, false)) {
                startPageInvoke(app, hwnd, 2);
                return false;
            }
            if (!ctrl && vk >= '1' && vk <= '5' &&
                (int)(vk - '1') < (int)app.startPageRecents.size()) {
                startPageInvoke(app, hwnd, 10 + (int)(vk - '1'));
                return false;
            }
        }

        vk = translateActionKey(app.keymap, (unsigned)vk, false);
        switch (vk) {
            case ':':
            case '?':
                if (!app.showSearch &&
                    (!app.showFolderBrowser || app.browserPinned) &&
                    (!app.showToc || app.tocPinned)) {
                    if (vk == ':') enterEditMode(app);
                    else {
                        app.showHelp = true;
                        app.helpAnimation = 0;
                        requestRepaint(hwnd);
                    }
                    // The message loop skips TranslateMessage for this
                    // keystroke, so E cannot become the first edit (#195).
                    return true;
                }
                break;
            case VK_ESCAPE:
                // Priority: ContextMenu > Help > Search > FolderBrowser > TOC > Theme chooser > Quit
                if (app.showContextMenu) {
                    app.showContextMenu = false;
                    app.contextMenuAnimation = 0;
                } else if (app.showHelp) {
                    app.showHelp = false;
                    app.helpAnimation = 0;
                } else if (app.showSearch) {
                    app.showSearch = false;
                    app.searchActive = false;
                    app.searchQuery.clear();
                    app.searchMatches.clear();
                    app.searchAnimation = 0;
                    clearFolderSearch(app);
                    updateBlinkTimer(app);
                } else if (app.showFolderBrowser) {
                    app.showFolderBrowser = false;
                    app.folderBrowserAnimation = 0;
                } else if (app.showToc) {
                    if (!app.tocFilter.empty()) {
                        app.tocFilter.clear();  // first Esc clears the filter
                    } else {
                        app.showToc = false;
                        app.tocAnimation = 0;
                    }
                } else if (app.showThemeChooser) {
                    if (app.themeChooserPreviewBase >= 0) {
                        applyTheme(app, app.themeChooserPreviewBase);
                        app.themeChooserPreviewBase = -1;
                    }
                    app.showThemeChooser = false;
                    app.themeChooserAnimation = 0;
                } else if (app.zenMode) {
                    toggleZenMode(app, hwnd);
                } else {
                    PostQuitMessage(0);
                }
                break;
            case VK_F11:
                toggleZenMode(app, hwnd);
                break;
            case 'Q':
                // Pinned panels are part of the layout: Q still quits
                // through them (an unpinned panel's filter already
                // swallowed the key before this switch)
                if (!app.showThemeChooser && !app.showSearch &&
                    (!app.showFolderBrowser || app.browserPinned) &&
                    (!app.showToc || app.tocPinned)) {
                    PostQuitMessage(0);
                }
                break;
            case 'N':
                // New file: folder browser with the naming row active (#74)
                if (!app.showSearch && !app.showThemeChooser) {
                    startNewFileFlow(app, hwnd);
                }
                break;
            case 'B':
                // B to toggle folder browser - an open TOC no longer
                // blocks it; an unpinned TOC swallows B as filter input
                // before this switch anyway
                if (!app.showSearch && !app.showThemeChooser) {
                    app.showFolderBrowser = !app.showFolderBrowser;
                    closeFolderBrowserInput(app);
                    if (app.showFolderBrowser) {
                        app.folderBrowserAnimation = 0;
                        // Initialize to directory of current file, or working directory
                        if (!app.currentFile.empty()) {
                            app.folderBrowserPath = getDirectoryFromFile(app.currentFile);
                        } else {
                            wchar_t cwd[MAX_PATH];
                            if (GetCurrentDirectoryW(MAX_PATH, cwd)) {
                                app.folderBrowserPath = cwd;
                            }
                        }
                        populateFolderItems(app);
                        if (app.browserFocusPath) {
                            // #81: open with the path box active and
                            // selected, so paste + Enter jumps anywhere
                            app.folderBrowserEditingPath = true;
                            app.folderBrowserInput = app.folderBrowserPath;
                            app.folderBrowserInputSelectAll = true;
                            app.folderBrowserInputError = false;
                            app.folderInputJustOpened = true;
                            updateBlinkTimer(app);
                            resetCursorBlink(app);
                        }
                    }
                    requestRepaint(hwnd);
                }
                break;
            case VK_TAB:
                // The browser no longer blocks the TOC: both panels can
                // be up (and pinned) together (#156)
                if (!app.showSearch && !app.showThemeChooser) {
                    app.showToc = !app.showToc;
                    app.tocFilter.clear();
                    if (app.showToc) {
                        ensureLayoutComplete(app);  // headings list is built during layout
                        app.tocAnimation = 0;
                        app.tocScroll = 0;
                        app.hoveredTocIndex = -1;
                    }
                    requestRepaint(hwnd);
                }
                break;
            case 'T':
                if (!app.showSearch) {
                    if (app.showThemeChooser &&
                        app.themeChooserPreviewBase >= 0) {
                        // Closing mid-preview: the committed theme returns
                        applyTheme(app, app.themeChooserPreviewBase);
                        app.themeChooserPreviewBase = -1;
                    }
                    app.showThemeChooser = !app.showThemeChooser;
                    if (app.showThemeChooser) {
                        app.themeChooserAnimation = 0;
                    }
                }
                requestRepaint(hwnd);
                break;
            case 'A':
                // Annotate the current selection (#126); pinned panels
                // pass the key through like the rest (#185)
                if (!app.showSearch && !app.showThemeChooser &&
                    (!app.showToc || app.tocPinned) &&
                    (!app.showFolderBrowser || app.browserPinned)) {
                    annotationBeginCreate(app);
                    // The keystroke that opened the editor must not leak
                    // its character into the note box (#161)
                    if (app.annotEditorOpen) {
                        app.swallowCharsUntil =
                            std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(150);
                    }
                    requestRepaint(hwnd);
                }
                break;
            case 'F':
                // F to open search (when not in search mode)
                if (!app.showSearch && !app.showThemeChooser) {
                    app.showSearch = true;
                    app.searchActive = true;
                    app.searchAnimation = 0;
                    app.searchQuery.clear();
                    app.searchMatches.clear();
                    app.searchCurrentIndex = 0;
                    app.searchJustOpened = true;
                    updateBlinkTimer(app);
                    requestRepaint(hwnd);
                }
                break;
            case VK_UP:
            case 'K':
                if (!app.showSearch) {
                    app.targetScrollY -= dpi(app, 50.0f);
                }
                break;
            case VK_LEFT:
                // Horizontal scroll for over-wide content (#77): render
                // clamps against contentWidth, so this is a no-op when
                // everything fits the window
                if (!app.showSearch) {
                    app.targetScrollX = std::max(0.0f, app.targetScrollX - dpi(app, 60.0f));
                    app.scrollX = app.targetScrollX;
                }
                break;
            case VK_RIGHT:
                if (!app.showSearch) {
                    app.targetScrollX += dpi(app, 60.0f);
                    app.scrollX = app.targetScrollX;
                }
                break;
            case VK_DOWN:
            case 'J':
                if (!app.showSearch) {
                    app.targetScrollY += dpi(app, 50.0f);
                }
                break;
            case VK_PRIOR: // Page Up
                app.targetScrollY -= pageSize;
                break;
            case VK_NEXT: // Page Down
            case VK_SPACE:
                if (!app.showSearch) {
                    app.targetScrollY += pageSize;
                }
                break;
            case VK_HOME:
                app.targetScrollY = 0;
                break;
            case VK_END:
                // Jump-to-bottom needs the final content height
                ensureLayoutComplete(app);
                maxScroll = std::max(0.0f, app.contentHeight - app.height);
                app.targetScrollY = maxScroll;
                break;
            case 'S':
                if (!app.showSearch) {
                    app.showStats = !app.showStats;
                }
                break;
        }
    }

    app.targetScrollY = std::max(0.0f, std::min(app.targetScrollY, maxScroll));
    app.scrollY = app.targetScrollY;
    requestRepaint(hwnd);
    return false;
}

void handleCharInput(App& app, HWND hwnd, wchar_t character) {
    // The Enter that confirmed the create-reference dialog must not leak
    // a newline into the editor it just opened
    if (std::chrono::steady_clock::now() < app.swallowCharsUntil) return;

    // Annotation note editor takes all typing (#126)
    if (app.annotEditorOpen && !app.editMode) {
        annotationEditorChar(app, character);
        requestRepaint(hwnd);
        return;
    }

    // Print preview: all shortcuts are handled as key-downs
    if (app.showPrintPreview) return;

    // Shortcut editor: punctuation bindings arrive here (their virtual
    // keys are layout-dependent); letters/digits were taken on keydown
    if (app.showShortcutEditor) {
        wchar_t ch = (wchar_t)character;
        if (app.shortcutEditorRow >= 0 && ch > 32 && ch < 127 &&
            !iswalnum(ch)) {
            shortcutAssign(app, {(unsigned)ch, true});
            requestRepaint(hwnd);
        }
        return;  // the editor swallows all other typing
    }

    // Theme editor: route typing into the focused field
    if (app.showThemeEditor) {
        wchar_t ch = (wchar_t)character;
        int f = app.themeEditorField;
        if (f == 6) {  // name
            if (ch == 8) {
                if (!app.themeEditorName.empty()) app.themeEditorName.pop_back();
            } else if (ch >= 32 && ch != 127 && app.themeEditorName.size() < 24) {
                app.themeEditorName += ch;
            }
        } else if (f >= 0 && f < 6) {  // hex fields
            std::wstring& hex = app.themeEditorHex[f];
            if (ch == 8) {
                if (!hex.empty()) hex.pop_back();
            } else if (iswxdigit(ch) && hex.size() < 6) {
                hex += towupper(ch);
            }
            tinta::Color parsed;
            if (hexToColorW(hex, parsed)) {
                *themeEditorSlot(app, f) = parsed;
            }
        }
        requestRepaint(hwnd);
        return;
    }

    if (app.showContextMenu || app.showSettings || app.showThemeChooser) return;
    if (app.showHelp) {
        if (translateActionKey(app.keymap, (unsigned)character, true) == '?') {
            app.showHelp = false;
            app.helpAnimation = 0;
            requestRepaint(hwnd);
        }
        return;
    }

    // Edit mode: ':' enters edit mode, otherwise route to editor
    if (app.editMode) {
        // An open table cell editor takes the typing (#148)
        if (tableEditChar(app, (wchar_t)character)) return;
        handleEditorCharInput(app, hwnd, character);
        return;
    }

    // TOC filter: typing narrows the heading list (a pinned panel leaves
    // the keyboard with the document, #156)
    if (app.showToc && !app.showSearch && !app.tocPinned) {
        wchar_t ch = (wchar_t)character;
        if (ch >= 0x20 && ch != 127) {
            app.tocFilter += ch;
            app.tocScroll = 0;
            requestRepaint(hwnd);
        }
        return;
    }

    // ':' to enter edit mode, '?' to toggle help — when no overlay is
    // active. Pinned panels are layout, not overlays: commands still
    // reach the document through them (#185)
    if (!app.showSearch &&
        (!app.showFolderBrowser || app.browserPinned) &&
        (!app.showToc || app.tocPinned) && !app.showThemeChooser) {
        // Chinese IME full-width forms count as their ASCII keys, so the
        // shortcuts work without switching input modes (#85)
        wchar_t key = character;
        if (key == 0xFF1A) key = L':';  // ：
        if (key == 0xFF1F) key = L'?';  // ？
        wchar_t ch = (wchar_t)translateActionKey(app.keymap, (unsigned)key, true);
        if (ch == L':' && !app.showHelp) {
            enterEditMode(app);
            return;
        }
        if (ch == L'?') {
            app.showHelp = !app.showHelp;
            if (app.showHelp) {
                app.helpAnimation = 0;
            }
            requestRepaint(app.hwnd);
            return;
        }
        if (ch == L'F' && !app.showHelp && !app.editMode) {
            // A punctuation search binding (vim '/') resolved here
            app.showSearch = true;
            app.searchActive = true;
            app.searchAnimation = 0;
            app.searchQuery.clear();
            app.searchMatches.clear();
            app.searchCurrentIndex = 0;
            app.searchJustOpened = false;
            updateBlinkTimer(app);
            requestRepaint(app.hwnd);
            return;
        }
    }

    if (app.showFolderBrowser &&
        (app.folderBrowserEditingPath || app.folderBrowserNaming != 0)) {
        if (app.folderInputJustOpened) {
            // The keystroke that opened this input must not type into it
            app.folderInputJustOpened = false;
            return;
        }
        wchar_t ch = (wchar_t)character;
        if (ch >= 32 && ch != 127) {
            if (app.folderBrowserInputSelectAll) {
                app.folderBrowserInput.clear();
                app.folderBrowserInputSelectAll = false;
            }
            app.folderBrowserInput += ch;
            app.folderBrowserInputError = false;
            resetCursorBlink(app);
            requestRepaint(hwnd);
        }
        return;
    }

    if (app.showSearch && app.searchActive) {
        // Skip the character that opened search (F key)
        if (app.searchJustOpened) {
            app.searchJustOpened = false;
            return;
        }
        wchar_t ch = (wchar_t)character;
        // Ctrl+V arrives here as the SYN control character (#121)
        if (ch == 0x16) {
            std::wstring pasted = platform::flattenToSingleLine(platform::clipboardText());
            if (!pasted.empty()) {
                app.searchQuery += pasted;
                resetCursorBlink(app);
                performSearch(app);
                if (!app.searchMatches.empty()) {
                    app.searchCurrentIndex = 0;
                    scrollToCurrentMatch(app);
                }
                requestRepaint(hwnd);
            }
            return;
        }
        // Only handle printable characters (not control chars)
        if (ch >= 32 && ch != 127) {
            app.searchQuery += ch;
            resetCursorBlink(app);
            performSearch(app);
            if (!app.searchMatches.empty()) {
                app.searchCurrentIndex = 0;
                scrollToCurrentMatch(app);
            }
            requestRepaint(hwnd);
        }
    }
}

void handleDropFiles(App& app, HWND hwnd,
                     const std::vector<std::wstring>& paths) {
    // Dropped files open into tabs (the first one activates); a single
    // drop on a single-document window is where the tab strip first appears.
    //
    // The caller enumerated the drop and owns its handle, so the DragQueryFileW
    // loop and the DragFinish that used to live here are its business now. The
    // filtering is still this function's: an unsupported path is skipped, and
    // skipping every path is the same no-repaint outcome as an empty drop.
    bool activated = false;
    for (const std::wstring& path : paths) {
        if (!isSupportedDropPath(path)) continue;
        std::string filepath = platform::toUtf8(path);
        tabOpenPath(app, hwnd, filepath, !activated);
        activated = true;
    }
    if (activated) requestRepaint(hwnd);
}

void handleLinkPeekTimer(App& app, HWND hwnd) {
    disarmTimer(hwnd, TIMER_LINK_PEEK);
    if (app.linkPeekUrl.empty() || app.hoveredLink != app.linkPeekUrl) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path target;
    if (app.linkPeekUrl.rfind("fileref-ok:", 0) == 0) {
        target = toWide(app.linkPeekUrl.substr(11));
    } else if (app.linkPeekUrl.rfind("wiki:", 0) == 0 &&
               !app.currentFile.empty()) {
        std::wstring t = toWide(app.linkPeekUrl.substr(5));
        fs::path dir = fs::path(toWide(app.currentFile)).parent_path();
        if (fs::exists(dir / t, ec)) {
            target = dir / t;
        } else if (fs::exists(dir / (t + L".md"), ec)) {
            target = dir / (t + L".md");
        } else if (fs::exists(dir / (t + L".markdown"), ec)) {
            target = dir / (t + L".markdown");
        }
    }
    if (target.empty() || !fs::exists(target, ec)) return;

    // Place the panel to the right of the link, never past the right
    // edge; when the link leaves no room there, right-align it instead
    float viewX = documentViewportX(app);
    float chrome = chromeTopHeight(app);
    float rightLimit = viewX + documentViewportWidth(app) - dpi(app, 22.0f);
    float anchorRight =
        app.linkPeekAnchorDoc.right - app.scrollX + viewX + dpi(app, 14.0f);
    float anchorTop = app.linkPeekAnchorDoc.top - app.scrollY;
    float maxW = dpi(app, 520.0f);
    float h = std::min(dpi(app, 460.0f),
                       (float)app.height - chrome - dpi(app, 24.0f));
    float w = rightLimit - anchorRight;
    float px;
    if (w >= dpi(app, 300.0f)) {
        w = std::min(w, maxW);
        px = anchorRight;
    } else {
        w = std::min(maxW, rightLimit - viewX - dpi(app, 22.0f));
        px = rightLimit - w;
    }
    float py = std::max(chrome + dpi(app, 10.0f),
                        std::min(anchorTop - dpi(app, 30.0f),
                                 (float)app.height - h - dpi(app, 12.0f)));
    app.linkPeekPanel = tinta::rectF(px, py, px + w, py + h);

    float titleH = dpi(app, 22.0f);
    float contentH = h - titleH - 2.0f;
    std::shared_ptr<tinta::Bitmap> bitmap =
        renderPeekBitmap(app, target.wstring(), w - 2.0f, contentH);
    if (!bitmap) return;
    // The render may have shrunk to the target's real height
    app.linkPeekPanel.bottom = py + titleH + contentH + 2.0f;
    // Assigning over the previous handle releases it -- the explicit Release
    // that used to stand here was the same operation, spelled by hand.
    app.linkPeekBitmap = std::move(bitmap);
    app.linkPeekTitle = target.filename().wstring();
    app.linkPeekActive = true;
    requestRepaint(hwnd);
}

void handleFileWatchTimer(App& app, HWND hwnd) {
    // Refresh the deleted-on-disk flags driving the red-grey tab dots
    // (runs even when the active document is untitled or being edited)
    bool missingChanged = false;
    for (auto& tab : app.tabs) {
        const std::string& path =
            (&tab == &app.tabs[app.activeTab]) ? app.currentFile : tab.path;
        bool missing =
            !path.empty() &&
            GetFileAttributesW(toWide(path).c_str()) == INVALID_FILE_ATTRIBUTES;
        if (missing != tab.fileMissing) {
            tab.fileMissing = missing;
            missingChanged = true;
        }
    }
    if (missingChanged) requestRepaint(hwnd);

    // External edits to a file that has unsaved changes in some tab surface
    // immediately: jump to that tab and raise the unsaved-changes dialog
    // (Save and exit = your version wins, Discard = the disk version loads)
    if (!app.confirmExitPending) {
        for (size_t i = 0; i < app.tabs.size(); i++) {
            bool isActive = (int)i == app.activeTab;
            bool dirty = isActive
                             ? (app.editMode && app.editorDirty)
                             : (app.tabs[i].editMode && app.tabs[i].editorDirty);
            const std::string& path =
                isActive ? app.currentFile : app.tabs[i].path;
            if (!dirty || path.empty()) continue;
            HANDLE fileHandle = CreateFileW(
                toWide(path).c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                nullptr);
            if (fileHandle == INVALID_HANDLE_VALUE) continue;
            FILETIME current{};
            GetFileTime(fileHandle, nullptr, nullptr, &current);
            CloseHandle(fileHandle);
            FILETIME& stored =
                isActive ? app.lastFileWriteTime : app.tabs[i].lastWrite;
            if (CompareFileTime(&current, &stored) != 0) {
                stored = current;  // one prompt per external change
                if (!isActive) {
                    tabActivate(app, hwnd, (int)i);
                    app.lastFileWriteTime = current;
                }
                app.confirmExitPending = true;
                app.confirmExitOpenedAt = std::chrono::steady_clock::now();
                requestRepaint(hwnd);
                break;
            }
        }
    }

    if (app.currentFile.empty() || !app.fileWatchEnabled || app.editMode) return;

    std::wstring widePath = toWide(app.currentFile);
    HANDLE h = CreateFileW(widePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        FILETIME ft;
        GetFileTime(h, nullptr, nullptr, &ft);
        CloseHandle(h);
        if (CompareFileTime(&ft, &app.lastFileWriteTime) != 0) {
            app.lastFileWriteTime = ft;
            // Reload file
            std::ifstream file(widePath);
            if (file) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                auto result = parseDocument(app.parser, buffer.str(), app.currentFile);
                if (result.success) {
                    float savedScroll = app.scrollY;
                    float savedTargetScroll = app.targetScrollY;
                    app.root = result.root;
                    app.parseTimeUs = result.parseTimeUs;
                    app.layoutDirty = true;
                    app.scrollY = savedScroll;
                    app.targetScrollY = savedTargetScroll;
                    // Annotations re-derive from the fresh source (#126)
                    app.sourceText = buffer.str();
                    annotationsParseSource(app);
                    requestRepaint(hwnd);
                }
            }
        }
    }
}
