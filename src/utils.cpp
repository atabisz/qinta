#include "utils.h"
#include "clipboard.h"
#include "render.h"
#include "i18n.h"
#include "signals.h"
#include "platform_paths.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

// THIS FILE NO LONGER INCLUDES windows.h, AND THE THREE REMOVALS THAT ACHIEVED IT ARE
// THE WHOLE SLICE. It had read as one Windows feature file for a long time and was
// actually three things:
//
//   1. measureText(IDWriteTextFormat*) -- a DirectWrite function whose only four
//      callers are in d2d_init.cpp. Moved there. It is why this file needed dwrite.h.
//   2. launchQuickNoteWindow -- a self-relaunch passing one flag the Qt arm honours and
//      one it refuses by name. Moved to src/quicknote_win32.cpp, which explains why its
//      Qt half is deferred rather than written. It is why this file needed shellapi.h.
//      ("flags only WinMain parses" stood here until 2026-09-21; there is one shared
//      parse now, src/launch_options.cpp, and the blocker is `--new`, not the parsing.)
//   3. Everything below, which never needed either: two binary searches over layout
//      buckets, a title string, a link click, and a path canonicalisation.
//
// Nothing here was rewritten to make that true. The two Win32 calls that remain in
// spirit are routed rather than removed -- SetWindowTextW through win32_api.h's
// setWindowTitle, ShellExecuteA through platform::openExternalUrl -- so both still do
// exactly what they did on Windows.
//
// The paragraph this replaces claimed the clipboard was among what is left here. It
// stopped being true when copyToClipboard moved to src/clipboard_win32.cpp; the
// correction is here rather than at the bottom of the file where the old note about
// it sits.
//
// The text helpers that never needed Windows moved earlier still, to
// src/text_utils.cpp and src/text_utils_app.cpp; the latter argues at length about
// the seam overload that used to sit at the top of this file, including a correction
// about what tests/text_factory_tests.cpp does and does not prove.

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


// THE ONE PLACE A WINDOW CAPTION IS COMPUTED, as of 2026-10-12. It had 13 callers and was
// one of FOUR implementations: src/editor.cpp built its own dirty-marker title with a
// verbatim copy of the basename extraction below, and src/main_qt.cpp and src/qt_window.cpp
// each called QWidget::setWindowTitle directly with the words in the opposite order and a
// full path where this puts a file name. All three are gone and call this instead.
//
// THE DIRTY MARKER MOVED HERE FROM editor.cpp, and that is a deliberate behaviour change on
// WINDOWS as well as a fix off it. editor.cpp wrote the `* ` once, at the transition to
// dirty, so any later call to this function -- a tab switch, a start-page return, a draft
// recovery -- recomputed a title WITHOUT it and the asterisk vanished while the buffer was
// still unsaved. Deriving it here from the state instead means the marker is a function of
// `editorDirty` rather than of which event fired last.
//
// NOT ROUTED THROUGH i18n's title.plain / title.dirty, which exist (src/i18n.cpp:186-187)
// and say exactly `Qinta - %1$s` and `Qinta - * %1$s` -- with zero callers. Measured rather
// than assumed: nothing in src/ consumes a `%1$s` key and the tree has no
// format-substitution helper, so adopting them means writing that machinery for a change the
// translation table shows to be identical in all four languages. Recorded as a finding for a
// localisation slice; this comment is the pointer that keeps the keys from reading as live.
void updateWindowTitle(App& app) {
    std::wstring title = L"Qinta";
    std::wstring name;
    if (!app.currentFile.empty()) {
        std::wstring wpath = toWide(app.currentFile);
        size_t lastSep = wpath.find_last_of(L"\\/");
        name = lastSep != std::wstring::npos ? wpath.substr(lastSep + 1) : wpath;
    } else if (app.editMode) {
        // Quick note (Ctrl+N): no backing file until the first save
        name = tr(app, "title.untitled");
    }
    if (!name.empty()) {
        // The `* ` only ever accompanied a name, and a bare `Qinta - *` would be a caption
        // about nothing: editor.cpp's version could not reach that state because it ran
        // inside the editor, and neither can this one, because an empty name means no file
        // and no edit buffer.
        const bool dirty = app.editMode && app.editorDirty;
        title = dirty ? L"Qinta - * " + name : L"Qinta - " + name;
    }
    // win32_api.h's seam. It reaches include/window_sink.h on every platform -- there is one
    // window stack now -- and that header carries the argument for why a one-off call is
    // routed at all.
    setWindowTitle(app.hwnd, title.c_str());
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
        platform::openExternalUrl(app.hoveredLink);
    }
}

// copyToClipboard IS GONE. Its body moved to src/clipboard_win32.cpp UNCHANGED and was
// the seam's only Windows write until that file was deleted on 2026-10-07 -- including the GlobalFree that editor.cpp's
// near-copy was missing, so this function's careful version is the one that survived.
// The paragraph at the top of this file still lists "the clipboard" among what is left
// here; it no longer is, and that sentence is corrected there rather than here.

// THE .lexically_normal() IS LOAD-BEARING AND IS THE ONE THING HERE THAT COULD HAVE
// SILENTLY CHANGED BEHAVIOUR, so it gets the comment rather than the two-call
// GetFullPathNameW dance it replaces.
//
// GetFullPathNameW does two jobs at once: it makes a path absolute AND it collapses
// `.` and `..`, purely lexically, without touching the disk. The three obvious
// replacements each get a different subset of that:
//
//   absolute()                       absolute, does NOT collapse (libstdc++)
//   weakly_canonical()               collapses, but hits the filesystem and
//                                    resolves symlinks -- a different function
//   absolute().lexically_normal()    absolute, collapses, never touches the disk
//
// Only the third is the same function. The second is the tempting one and would be
// wrong in a way no test here would catch: every input in tests/file_path_tests.cpp
// names a path that does not exist, and weakly_canonical's whole point is consulting
// the ones that do.
//
// MEASUREMENT WORTH RECORDING, because it makes the Windows test a weaker witness
// than it looks: MSVC implements absolute() by calling GetFullPathNameW, so on
// Windows absolute() ALREADY collapses and the suffix changes nothing. libstdc++
// does not, so off Windows the suffix is the only thing keeping the two platforms
// in agreement -- and no test runs there, because this file does not link into a
// portable target (see tests/portable-sources.txt). The suffix is therefore correct
// by construction and asserted only on the platform that does not need it. Said out
// loud rather than left as a green tick.
//
// src/inline_style.cpp:69 already normalises this way, so this is the tree's
// existing spelling rather than a new one.
//
// SINCE 2026-10-11 THE RESOLUTION IS platform::absolutePath, and this function only converts
// the encoding. The formula above is still the POSIX answer, but on Windows libstdc++'s
// absolute() prepends the current drive to `\\server\share` and `\\?\` paths (the MinGW build
// found it), so the Windows answer moved into the paths seam as GetFullPathNameW. The paragraph
// above about MSVC calling GetFullPathNameW is why that changes nothing for MSVC builds.
std::wstring absoluteFilePath(const std::string& path) {
    if (path.empty()) return {};
    return platform::absolutePath(toWide(path));
}

void copyFilePath(App& app, HWND hwnd, const std::string& path) {
    (void)hwnd;
    if (platform::setClipboardText(absoluteFilePath(path)))
        signalPushKey(app, SIG_INFO, SIGI_COPY, "toast.copied");
}

void refuseWindowFeature(App& app, const char* what, const wchar_t* chip) {
    std::fprintf(stderr, "qinta: %s is not available in the Qt build yet -- it needs the "
                         "Win32 window stack\n", what);
    signalPush(app, SIG_WARN, SIGI_WARNING, chip);
}
