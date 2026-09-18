#ifndef TINTA_APP_H
#define TINTA_APP_H

// WHY THIS HEADER IS GUARDED AT ALL, WHEN IT DESCRIBES A WINDOWS APP
//
// Measured 2026-09-14: of the 31 source files that had never been compiled off
// Windows, 28 failed with exactly ONE error -- `windows.h: No such file or
// directory`, at the line below -- and got no further. So no per-file port cost
// for the app's UI code had ever actually been measured; every number about it was
// a regex estimate. One line was hiding the whole question.
//
// It turns out to be cheap to answer. This header names 14 Direct2D/DirectWrite/WIC
// interfaces and names them ONLY as pointers (45 occurrences, 0 by value), so
// forward declarations suffice. It embeds tinta's own D2D* wrapper types by value
// -- which is why the first attempt at this reported that forward declarations
// could not work -- but those wrappers only store native pointers and dereference
// none of them inline, so render_d2d.h and text_d2d.h are declaration-only off
// Windows too. See their headers.
//
// This is the tree's existing convention, not a new one: geometry.h already has 11
// `_WIN32` guards and keymap.h defines the VK_ codes itself off Windows. app.h was
// the anomaly, with zero.
//
// NOTHING HERE MAKES THE APP PORTABLE. Off Windows this declares App and stops;
// the native members are incomplete pointers that cannot be created or used, and
// every .cpp that drives them still fails on its own Win32 calls. That failing is
// now the MEASUREMENT, which is the entire point.

// The Win32 value types (real ones on Windows, stand-ins off it) and the Win32 API
// calls tinta makes, in two layers. win32_types.h was extracted first, once five
// sibling headers turned out to need the same three types and ODR ruled out six
// local copies; win32_api.h sits on top of it and holds the calls, because a type
// can have a genuine stand-in off Windows and a CALL cannot. Each explains itself.
#include "win32_api.h"

// App's seam-wrapper storage. In its own header so it can be asserted off Windows
// with fake backends -- see seated.h and tests/seated_tests.cpp.
#include "seated.h"

#ifdef _WIN32
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwrite_2.h>
#include <wincodec.h>
#else
// Left incomplete on purpose. Every one of the 45 native members below is a
// pointer, so this is enough to declare them, and an inline dereference added
// later fails here rather than compiling into a portable build.
struct ID2D1Factory;
struct ID2D1HwndRenderTarget;
struct ID2D1DeviceContext;
struct ID2D1SolidColorBrush;
struct ID2D1StrokeStyle;
struct ID2D1PathGeometry;
struct ID2D1Bitmap;
struct IDWriteFactory;
struct IDWriteTextFormat;
struct IDWriteTextLayout;
// IDWriteTypography's forward declaration is gone with the two members that
// needed it -- App names no typography COM type at all now, on either platform.
struct IDWriteFontFallback;
struct IDWriteTextAnalyzer;
struct IWICImagingFactory;
#endif

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <utility>
#include <optional>

#include "geometry.h"
#include "markdown.h"
#include "keymap.h"
#include "settings_data.h"
#include "theme_data.h"
// Brings tinta::RenderTarget/Brush/Bitmap/StrokeStyle and their borrowed
// Direct2D wrappers, which App now holds alongside the natives. This is the
// commit that first makes a production translation unit include the seam at
// all -- before it, render_api.h reached only the two backend headers and the
// test harness, so 96.6% "coverage" described methods that existed rather
// than calls that used them.
#include <memory>

#include "render_d2d.h"

using namespace qmd;

// COM teardown, in one place because it is the only thing in this header that
// dereferences a native pointer inline -- 21 sites, in three shapes: a bare
// `if (p) p->Release();`, the same across four lines, and the same again with a
// `p = nullptr;` after it.
//
// Those 21 sites sit INSIDE functions that are otherwise portable: they clear
// caches, adjust byte accounting and erase map entries alongside the Release.
// Guarding the whole function off Windows would have thrown that away and left a
// Qt path with no cache management at all, and guarding 21 individual statements
// would have been 21 places for the two branches to drift. One helper is one
// guard.
//
// Off Windows this is not a stub that pretends to work. There is nothing to
// release, because nothing can create one of these objects -- the interfaces are
// forward-declared and incomplete above. Forgetting the pointer is the whole of
// the correct behaviour, and it is spelled out rather than left to a comment
// because the alternative reading (a silently skipped Release, i.e. a leak) is
// the one a reader will assume.
template <class T>
inline void releaseNative(T*& p) {
#ifdef _WIN32
    if (p) {
        p->Release();
        p = nullptr;
    }
#else
    p = nullptr;
#endif
}

// Timing helpers
using Clock = std::chrono::high_resolution_clock;
inline int64_t usElapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
}

// Timer IDs (TIMER_FILE_WATCH=1 lives in file_utils.h, TIMER_EDITOR_REPARSE=2 in editor.cpp)
#define TIMER_CURSOR_BLINK 3
#define TIMER_NOTIFICATION 4
#define TIMER_ZOOM_APPLY 5
#define TIMER_IMAGE_REFLOW 6
#define TIMER_FOLDER_SEARCH 7
#define TIMER_SELECT_SCROLL 8
#define TIMER_LINK_PEEK 9
#define TIMER_UPDATE_CHECK 10

// armTimer and disarmTimer used to be defined here. They moved to win32_api.h,
// which app.h includes, once it was clear they were not app.h's to own: SetTimer or
// KillTimer was named in 23 files, so a seam this header happened to discover while
// being ported belongs one layer down with the other API calls. The IDs above stay
// here, because who owns which timer IS app state.
//
// TENSE CORRECTED 2026-09-15: the 23 is the pre-routing figure. Every site in the
// tree now calls the seam, including the three armTimer and one disarmTimer calls in
// this header's own inline helpers below.

// Posted to continue an incomplete document layout in time-budgeted chunks
#define WM_APP_LAYOUT_CHUNK (WM_APP + 1)

// Posted by an image download worker thread when a remote image finished
// loading (lParam = AsyncImageResult*, ownership transfers to the handler)
#define WM_APP_IMAGE_READY (WM_APP + 2)

// Posted by the GPU warm-up thread once the D3D driver is initialized: the
// software render target used for the instant first paint is then swapped
// for a hardware one (cheap now that the driver is warm)
#define WM_APP_GPU_READY (WM_APP + 3)

// Posted by the folder-search worker with its scan results
// (lParam = FolderScanMsg*, ownership transfers to the handler)
#define WM_APP_FOLDER_SEARCH (WM_APP + 4)

// Posted by the update-check worker with the latest release tag
// (lParam = std::string* "X.Y.Z", ownership transfers to the handler;
// empty string = the check failed and is retried another day)
#define WM_APP_UPDATE_CHECK (WM_APP + 5)

// Pandoc worker finished; wParam = success (pandoc.cpp)
#define WM_APP_PANDOC_DONE (WM_APP + 6)

// Startup metrics
struct StartupMetrics {
    int64_t windowInitUs = 0;
    int64_t d2dInitUs = 0;
    int64_t dwriteInitUs = 0;
    int64_t renderTargetUs = 0;
    int64_t fileLoadUs = 0;
    int64_t showWindowUs = 0;
    int64_t consoleInitUs = 0;
    int64_t totalStartupUs = 0;
};

// Syntax highlighting token types
enum class SyntaxTokenType { Plain, Keyword, String, Comment, Number, Function, TypeName, Operator, ControlFlow };

// Forward declare App for dpi() helper
struct App;

// DPI scaling helper for UI chrome elements.
// Scales by contentScale only (not zoomFactor) so UI chrome tracks monitor DPI.
inline float dpi(const App& app, float value);

// Themes moved to theme_data.h, which is included above -- D2DTheme,
// CustomTheme, THEMES, themeAt, hexColor and the rest. That block was the last
// thing keeping src/themes.cpp (451 lines of palette data and an ini
// reader/writer) pinned to windows.h, and it could only move once its colour
// members stopped being D2D1_COLOR_F. themes.cpp no longer includes this header
// at all, and the non-Windows build compiles it on every probe run.
//
// Settings moved to settings_data.h, which is included above. It is pure
// configuration -- ints, bools, strings -- and needed no part of this header;
// keeping it here made every settings reader depend on windows.h and Direct2D.
//
// The sentinel is pinned to the SDK macro rather than trusted to match it. If a
// future SDK ever changes CW_USEDEFAULT, this fails at compile time instead of
// silently changing where windows open and what settings.ini means.
//
// Guarded, not stubbed. Defining CW_USEDEFAULT ourselves off Windows would make
// the assertion compare the sentinel against our own copy of the number, which
// passes unconditionally and checks nothing -- the assertion is only a reading
// where the real SDK is present. keymap.h draws the same line for the VK_ codes:
// it defines them off Windows precisely because code below USES them, and keeps
// the static_asserts under _WIN32 because only there do they measure anything.
#ifdef _WIN32
static_assert(kWindowPositionUseDefault == CW_USEDEFAULT,
              "kWindowPositionUseDefault must stay identical to CW_USEDEFAULT");
#endif

// Application state
struct App {
    // Win32
    HWND hwnd = nullptr;
    int width = 1024;
    int height = 768;
    bool running = true;

    // Direct2D
    //
    // THE NAMING RULE FOR EVERY PAIR BELOW, because it is what keeps this a
    // ~110-edit change instead of a ~1,600-edit one: the WRAPPER keeps the
    // original member name and the native COM pointer takes a `native` prefix.
    // So the ~1,500 sites that merely PASS a member to a drawing call bind to
    // the seam with no edit at all, and only the sites that call a COM method
    // on it, or take its address to create it, had to move to the native name.
    //
    // The wrappers BORROW (render_d2d.h says so at length): they neither AddRef
    // nor Release, the natives below are still the owners, and every existing
    // Release() site still runs against the native. A wrapper is refreshed --
    // reseated onto whatever native currently exists -- by the seat* helpers in
    // d2d_init.cpp, never by a drawing call.
    ID2D1Factory* d2dFactory = nullptr;
    ID2D1HwndRenderTarget* nativeRenderTarget = nullptr;
    Seated<tinta::RenderTarget, ID2D1RenderTarget> renderTargetStorage;
    // What the 566 drawing sites call. Null until seatRenderTarget() runs, and
    // null again the moment the native goes -- callers already null-check it.
    tinta::RenderTarget* renderTarget = nullptr;
    // The first render target is software: it creates in ~20 ms while a
    // hardware one pays ~200 ms of D3D device + GPU driver initialization.
    // The warm-up thread flips this and the target is recreated on
    // WM_APP_GPU_READY, after the first frame is already on screen.
    bool useHardwareRT = false;

    // Cached dashed stroke for mermaid edges (factory object: created once,
    // survives render-target recreation)
    ID2D1StrokeStyle* nativeDashedStrokeStyle = nullptr;
    Seated<tinta::StrokeStyle, ID2D1StrokeStyle> dashedStrokeStyleStorage;
    tinta::StrokeStyle* dashedStrokeStyle = nullptr;

    // Editor paint-path line layouts, keyed by line start offset. Cleared on
    // any text/format/width/wrap change; between changes the 500 ms cursor
    // blink and idle repaints stop reshaping every visible line.
    //
    // THE CACHE OWNS ITS LAYOUTS, and that is why this member is a unique_ptr
    // while cachedEditorLineLayout() hands back a bare pointer. Those two
    // functions in editor.cpp used to have the SAME return type and OPPOSITE
    // ownership -- createEditorLineLayout's caller had to Release and
    // cachedEditorLineLayout's caller had to not -- a distinction that lived
    // only in a comment. Now the types say it: one returns unique_ptr, the
    // other a borrowed raw pointer, and the eviction and clear paths below drop
    // a layout by erasing the entry rather than by remembering to release it.
    struct EditorCachedLayout {
        std::unique_ptr<tinta::TextLayout> layout;
        size_t len = 0;
        unsigned long long lastUsed = 0;
    };
    std::unordered_map<size_t, EditorCachedLayout> editorLineLayoutCache;
    unsigned long long editorLayoutUseClock = 0;
    static constexpr size_t EDITOR_LAYOUT_CACHE_MAX = 256;
    void clearEditorLineLayoutCache() {
        editorLineLayoutCache.clear();
        editorLayoutUseClock = 0;
    }
    ID2D1SolidColorBrush* nativeBrush = nullptr;
    Seated<tinta::Brush, ID2D1SolidColorBrush> brushStorage;
    // The scratch brush every draw call passes: 991 sites name it, of which only
    // 483 call a method (474 SetColor, 6 Release, 3 GetColor). SetColor and
    // GetColor are on tinta::Brush already and their arguments are tinta::Color
    // after the value-type sweep, so those 474 needed no edit either.
    tinta::Brush* brush = nullptr;
    // Stays native deliberately. Its 9 sites are 6 GetDevice, 2 Release and one
    // DrawTextLayout -- colour-emoji work with no seam equivalent, and it is
    // obtained by QueryInterface off the native target, not through the seam.
    ID2D1DeviceContext* deviceContext = nullptr;  // For color emoji rendering

    // WIC (Windows Imaging Component) for image loading
    IWICImagingFactory* wicFactory = nullptr;

    // Image cache
    struct ImageEntry {
        // SHARED, not a raw pointer with hand-written AddRef/Release, and that is the
        // ownership redesign render_api.h's exclusion list said CreateBitmap needed
        // before it could join the seam. Three parties want a say in this bitmap's
        // lifetime: the cache, which evicts on an LRU budget; every LayoutBitmap the
        // last relayout produced; and an open lightbox, which used to AddRef for
        // exactly this reason. shared_ptr states that instead of implementing it
        // three times, and the count is what decides -- a unique_ptr here would make
        // eviction while the lightbox is open a dangle rather than a compile error.
        //
        // The width/height beside it are NOT derivable from GetSize: on the SVG path
        // the bitmap is a 2x raster declared at 192 dpi, so GetSize returns the
        // intrinsic size while these hold it directly. See image_api.h on why
        // conflating the two silently breaks every SVG.
        std::shared_ptr<tinta::Bitmap> bitmap;
        int width = 0;
        int height = 0;
        bool failed = false;
        bool pending = false;  // remote image download in flight
        size_t bytes = 0;      // estimated decoded bitmap memory
        unsigned long long lastUsed = 0;
    };
    std::unordered_map<std::string, ImageEntry> imageCache;
    size_t imageCacheBytes = 0;
    unsigned long long imageCacheUseClock = 0;
    static constexpr size_t IMAGE_CACHE_BUDGET = 64ull * 1024ull * 1024ull;
    static constexpr size_t IMAGE_CACHE_MAX_ENTRIES = 256;

    void touchImageCacheEntry(ImageEntry& entry) {
        entry.lastUsed = ++imageCacheUseClock;
    }

    void storeImageCacheEntry(const std::string& key, ImageEntry entry) {
        entry.bytes = entry.bitmap && entry.width > 0 && entry.height > 0
            ? (size_t)entry.width * (size_t)entry.height * 4
            : 0;
        touchImageCacheEntry(entry);

        auto it = imageCache.find(key);
        if (it != imageCache.end()) {
            // No releaseNative here any more: the assignment below drops the cache's
            // reference, and a LayoutBitmap or an open lightbox still holding one
            // keeps the bitmap alive. The hand-written release this replaces was
            // CORRECT -- it dropped one of the two references openLightbox's AddRef
            // had established -- so this is not a bug fix. It is the same rule stated
            // once instead of paired by hand at four sites, which is what the eviction
            // loop below, invalidateDeviceBitmaps and releaseImageCache each had to
            // get right independently.
            imageCacheBytes -= it->second.bytes;
            it->second = std::move(entry);
            imageCacheBytes += it->second.bytes;
        } else {
            auto result = imageCache.emplace(key, std::move(entry));
            imageCacheBytes += result.first->second.bytes;
        }

        // Keep the just-stored key alive: layout may immediately use the
        // returned entry even when a single oversized image exceeds the cap.
        while (imageCache.size() > IMAGE_CACHE_MAX_ENTRIES ||
               imageCacheBytes > IMAGE_CACHE_BUDGET) {
            auto victim = imageCache.end();
            for (auto candidate = imageCache.begin(); candidate != imageCache.end(); ++candidate) {
                if (candidate->first == key || candidate->second.pending) continue;
                if (victim == imageCache.end() ||
                    candidate->second.lastUsed < victim->second.lastUsed) {
                    victim = candidate;
                }
            }
            if (victim == imageCache.end()) break;
            imageCacheBytes -= victim->second.bytes;
            imageCache.erase(victim);
        }
    }

    // Layout bitmaps (document coordinates)
    struct LayoutBitmap {
        // A COPY of the cache entry's shared_ptr, so a relayout that drops an entry
        // cannot pull the pixels out from under a paint that is still walking this
        // vector. It used to be an ID2D1Bitmap* borrowed from the cache, alongside a
        // `mutable Seated<tinta::Bitmap, ID2D1Bitmap>` that rebuilt a D2DBitmap
        // wrapper on every read.
        //
        // THAT RESEAT-ON-READ IS GONE, and the argument for it is worth keeping
        // because it still applies to what is left: a STORED tinta::Bitmap* aimed at
        // a sibling member keeps pointing at the pre-move address once this vector
        // reallocates (render.cpp both resize()s and push_back()s it), and the dangle
        // shows up only as a wrong picture. Reseating dodged that by never storing a
        // pointer into the element. A shared_ptr dodges it differently and better: it
        // points at the bitmap, not at a sibling, so moving the element moves a valid
        // handle. The four remaining native scalars on App still reseat, and
        // tests/bitmap_seam_tests.cpp still asserts they cannot go stale.
        std::shared_ptr<tinta::Bitmap> bitmap;
        tinta::RectF destRect{};
    };
    std::vector<LayoutBitmap> layoutBitmaps;

    // Image lightbox: clicking an inline image views it full size over a
    // dimmed backdrop; wheel zooms, drag pans, Esc or a click closes
    bool showLightbox = false;
    // Shares the image cache's bitmap for as long as the lightbox is open. The
    // comment here used to read `ID2D1Bitmap* nativeLightboxBitmap;  // AddRef'd
    // while open`, and openLightbox/closeLightbox were the AddRef and the Release --
    // this is that pair, spelled as the thing it was emulating. It is a member rather
    // than a Seated accessor for the same reason: there is no native pointer left to
    // wrap, so nothing can go stale.
    std::shared_ptr<tinta::Bitmap> lightboxBitmap;
    float lightboxZoom = 1.0f;              // multiplier over fit-to-view
    float lightboxPanX = 0.0f;
    float lightboxPanY = 0.0f;
    bool lightboxDragging = false;
    int lightboxDragStartX = 0;
    int lightboxDragStartY = 0;
    float lightboxDragPanX = 0.0f;
    float lightboxDragPanY = 0.0f;
    bool lightboxDragMoved = false;

    // DirectWrite
    IDWriteFactory* dwriteFactory = nullptr;
    IDWriteFontFallback* fontFallback = nullptr;  // For emoji font fallback
    // textAnalyzer is GONE: UAX#14 line-break analysis moved behind
    // tinta::TextFactory::AnalyzeLineBreaks, and D2DTextFactory owns its own analyzer,
    // created on first use rather than on every start.

    // The factory's wrapper is SEATED, unlike the four bitmaps' read-time accessors,
    // and the discriminator is a replacement count of ZERO: dwriteFactory is created
    // once by out-parameter in d2d_init.cpp and released once in shutdown(), with no
    // assignment anywhere else in the tree. The bitmaps had 8 mutation sites, which is
    // what made reseat-on-read the only shape there that could not be forgotten.
    //
    // Ownership is identical on both sides -- D2DTextFactory borrows the DWrite
    // factory exactly as D2DBitmap borrows a bitmap -- so it is the LIFETIME that
    // decides the access shape here, not the ownership. That is the sharper form of
    // what the bitmap slice taught: reseat-on-read is the answer to a native that gets
    // REPLACED, not to a borrowed wrapper as such. Rebuilding this one on every read
    // would rewrap at each of the 36 layout-creation sites and buy nothing.
    Seated<tinta::TextFactory, IDWriteFactory> textFactoryStorage;
    tinta::TextFactory* textFactory = nullptr;
    // EVERY TEXT FORMAT IN THIS STRUCT IS NOW A WRAPPER AND A STORAGE, WITH NO NATIVE.
    // The pair rule this block used to open with is gone along with its subject; it read
    // "native holds and owns, wrapper keeps the name the 106 DrawText sites already pass",
    // and it is quoted rather than dropped because the SITE COUNT is still the reason none
    // of this churned the call sites. Not one of those 106 changed across either move.
    //
    // The document formats went first: the eleven that used to head this block --
    // nativeTextFormat through nativeHeadingFormats[6], plus nativeSupSubFormat -- because
    // installDocumentTextFormats (include/text_formats.h) creates all sixteen through
    // tinta::TextFactory::CreateTextFormat and hands back something that already owns
    // itself. There was no native pointer to hold and nothing to seat.
    //
    // THE CHROME FORMATS HAVE NOW FOLLOWED, and the sentence that stood here predicted
    // exactly this: "The remaining natives here belong to updateOverlayFormats, which still
    // creates its formats with DirectWrite. That asymmetry is the honest state of the port
    // and not a design: it disappears when that function follows this one." It followed.
    // updateOverlayFormats and ensureThemePreviewFormats are deleted, installChromeFormats
    // and ensureThemePreviewFormats in include/text_formats.h create all twenty, and the
    // thirteen native members that used to sit in this block are gone with App::seatFormat
    // and App::seatTextFormats.
    //
    // WHAT DID NOT DISAPPEAR, because the prediction above was one clause too optimistic:
    // the chrome storages are still Seated<tinta::TextFormat, IDWriteTextFormat> where the
    // document storages are plain unique_ptrs. The coupling is a TEMPLATE ARGUMENT now and
    // nothing else -- no native is created, seated, compared or released anywhere in the
    // chrome path. Collapsing the type is a separate mechanical slice, kept separate on
    // purpose: it would touch all twenty members in the same commit as a behavioural port,
    // which is what makes a before/after chrome dump unreadable.

    // Overlay text formats (cached)

    // THE DOCUMENT FORMATS' STORAGE, and the type is the whole point of the change that
    // made it: a plain owning pointer, not Seated<>. Seated exists to wrap an
    // already-created native handle in place and keep that wrapping idempotent -- its
    // second member is a comparison key for "the same native I wrapped last time" -- and
    // once creation returns an owning portable pointer there is no native to compare.
    // Keeping the template would have left the coupling in the TYPE while removing it
    // from the call, which is the shape a census misses.
    std::unique_ptr<tinta::TextFormat> textFormatStorage;
    std::unique_ptr<tinta::TextFormat> headingFormatStorage;
    std::unique_ptr<tinta::TextFormat> codeFormatStorage;
    std::unique_ptr<tinta::TextFormat> boldFormatStorage;
    std::unique_ptr<tinta::TextFormat> italicFormatStorage;
    std::unique_ptr<tinta::TextFormat> boldItalicFormatStorage;
    std::unique_ptr<tinta::TextFormat> codeBoldFormatStorage;
    std::unique_ptr<tinta::TextFormat> codeItalicFormatStorage;
    std::unique_ptr<tinta::TextFormat> codeBoldItalicFormatStorage;
    std::unique_ptr<tinta::TextFormat> headingFormatsStorage[6];
    Seated<tinta::TextFormat, IDWriteTextFormat> searchTextFormatStorage;
    Seated<tinta::TextFormat, IDWriteTextFormat> themeTitleFormatStorage;
    Seated<tinta::TextFormat, IDWriteTextFormat> themeHeaderFormatStorage;

    tinta::TextFormat* textFormat = nullptr;
    tinta::TextFormat* headingFormat = nullptr;
    tinta::TextFormat* codeFormat = nullptr;
    tinta::TextFormat* boldFormat = nullptr;
    tinta::TextFormat* italicFormat = nullptr;
    tinta::TextFormat* boldItalicFormat = nullptr;
    tinta::TextFormat* codeBoldFormat = nullptr;
    tinta::TextFormat* codeItalicFormat = nullptr;
    tinta::TextFormat* codeBoldItalicFormat = nullptr;
    tinta::TextFormat* headingFormats[6] = {};
    tinta::TextFormat* searchTextFormat = nullptr;
    tinta::TextFormat* themeTitleFormat = nullptr;
    tinta::TextFormat* themeHeaderFormat = nullptr;

    // Same three-part shape as the flat formats above. unique_ptr storage makes
    // this struct move-only, which is all std::vector::resize() needs, and the
    // three wrapper pointers are assigned AFTER the one resize() so they cannot be
    // left pointing into a reallocated buffer.
    //
    // "SEATED" WAS THE WORD HERE AND IT NO LONGER MEANS ANYTHING: App::seatFormat and
    // App::seatTextFormats are deleted, and nothing on either backend seats a text
    // format. The ORDERING CONSTRAINT the sentence existed for is untouched and is why
    // it is reworded rather than deleted -- ensureThemePreviewFormats still resizes
    // once and fills afterwards, and tests/qt_entry_tests.cpp asserts it by POINTER
    // IDENTITY across a second call, which is the only assertion that would catch a
    // push_back-per-theme rewrite.
    struct ThemePreviewFormats {
        Seated<tinta::TextFormat, IDWriteTextFormat> nameStorage;
        Seated<tinta::TextFormat, IDWriteTextFormat> previewStorage;
        Seated<tinta::TextFormat, IDWriteTextFormat> codeStorage;
        tinta::TextFormat* name = nullptr;
        tinta::TextFormat* preview = nullptr;
        tinta::TextFormat* code = nullptr;
    };
    std::vector<ThemePreviewFormats> themePreviewFormats;

    // OpenType typography, BY VALUE since 2026-09-15.
    //
    // These were two IDWriteTypography*, created once in createTypography and
    // Released at shutdown. Measured before the change: all 12 call sites in
    // render.cpp passed one of exactly these two members, whole-range, and not one
    // read a feature back -- so the portable meaning of the pair is a short list of
    // four-character OpenType tags, which is what tinta::Typography is.
    //
    // Being values is the whole saving. There is nothing to create, nothing to
    // Release (the two releaseNative calls in releaseNative() below are gone), no
    // null to check at 12 call sites, and no ordering constraint against the DWrite
    // factory's lifetime. They also survive a device loss without being rebuilt,
    // which the COM objects did not.
    tinta::Typography bodyTypography;
    tinta::Typography codeTypography;

    // Folder browser text format
    Seated<tinta::TextFormat, IDWriteTextFormat> folderBrowserFormatStorage;
    tinta::TextFormat* folderBrowserFormat = nullptr;

    // TOC text formats
    Seated<tinta::TextFormat, IDWriteTextFormat> tocFormatStorage;
    tinta::TextFormat* tocFormat = nullptr;
    Seated<tinta::TextFormat, IDWriteTextFormat> tocFormatBoldStorage;
    tinta::TextFormat* tocFormatBold = nullptr;

    // Signal chips: subtitles, badges, and the tiny PINNED/HELD labels
    Seated<tinta::TextFormat, IDWriteTextFormat> signalSmallFormatStorage;
    tinta::TextFormat* signalSmallFormat = nullptr;

    // Stats overlay: UI chrome, so it must not follow the document zoom
    Seated<tinta::TextFormat, IDWriteTextFormat> statsFormatStorage;
    tinta::TextFormat* statsFormat = nullptr;

    // ELLIPSIS TRIMMING SIGNS, OWNED HERE BECAUSE THE SEAM DOES NOT SAY WHO OWNS THEM.
    //
    // TextFormat::SetTrimming takes an InlineObject* and text_api.h is silent on
    // lifetime -- and the two backends genuinely differ. DirectWrite AddRefs the sign
    // (which is why src/d2d_init.cpp:397 Releases its local reference immediately
    // afterwards and the object survives inside the format), while QtTextFormat records
    // `hasTrimmingSign = trimmingSign != nullptr` and keeps no pointer at all
    // (src/text_qt.cpp). So a caller that let its unique_ptr die after SetTrimming
    // would be correct on Qt and dangling on Windows, and the version that "works"
    // is the one that would never crash in testing.
    //
    // A vector on App rather than a fix to the seam: making ownership explicit in
    // text_api.h means changing the signature and all seven SetTrimming sites, which
    // is a separate change with its own reason to exist. This makes the CALLER
    // correct for either backend today, at the cost of one member, and it is the
    // container the Qt chrome installer fills. Three entries, one per trimmed row
    // format (folder browser, TOC, TOC bold).
    std::vector<std::unique_ptr<tinta::InlineObject>> trimmingSigns;

    // Markdown
    MarkdownParser parser;
    ElementPtr root;
    std::string currentFile;
    bool focusMermaidOnNextLayout = false;
    size_t parseTimeUs = 0;
    float contentHeight = 0;
    float contentWidth = 0;

    // State
    float scrollY = 0;
    float scrollX = 0;
    float targetScrollY = 0;
    float targetScrollX = 0;
    float contentScale = 1.0f;  // DPI scale
    float zoomFactor = 1.0f;    // User zoom (Ctrl+scroll)
    float appliedZoomFactor = 1.0f;  // zoomFactor last baked into text formats
    bool zoomApplyPending = false;   // TIMER_ZOOM_APPLY armed to coalesce zoom ticks
    bool darkMode = true;
    bool showStats = false;
    // Zen mode: borderless fullscreen with a centered reading column (F11)
    bool zenMode = false;
    WINDOWPLACEMENT zenRestorePlacement{};
    bool followSystemTheme = false;
    int lightThemeIndex = 0;
    int darkThemeIndex = 5;
    int currentThemeIndex = 5;  // Default to "Midnight" (first dark theme)
    D2DTheme theme = THEMES[5];

    // Theme chooser overlay (design t12): hit rects rebuilt each paint;
    // ids >= 0 pick a theme, -1 follow toggle, -2 close, -3 edit theme.
    // Hovering a card live-previews it; themeChooserPreviewBase remembers
    // the committed theme to restore on leave/close (-1 = not previewing).
    bool showThemeChooser = false;
    int hoveredThemeIndex = -1;
    float themeChooserAnimation = 0.0f;  // 0 to 1 for fade in
    std::vector<std::pair<tinta::RectF, int>> themeChooserHits;
    tinta::RectF themeChooserPanel{};
    int themeChooserPreviewBase = -1;

    // --- Tabbed interface (Win11 Notepad-style tabs in the title bar) ---
    // A tab keeps only what the document itself cannot restore: its path
    // and, when the user switches away mid-edit, the parked editor buffer.
    // View-mode scroll positions ride the existing reading-position memory.
    struct DocTab {
        int id = 0;                // stable identity across reorders/closes
        std::string path;          // empty = untitled quick note buffer
        std::wstring title;        // display name (file name / Untitled)
        bool editMode = false;     // switched away while editing
        bool editorDirty = false;
        bool fileMissing = false;  // deleted/renamed on disk (red-grey dot)
        FILETIME lastWrite{};      // for external-change conflicts (dirty tabs)
        std::wstring editorText;   // parked edit buffer (editMode only)
        float editorScrollY = 0.0f;
        size_t editorCursor = 0;
        bool wordWrap = true;
    };
    std::vector<DocTab> tabs;
    int activeTab = 0;
    bool showTabSwitcher = false;      // chevron dropdown (open-files list)
    int tabSwitcherHover = -1;
    int hoveredTab = -1;               // strip hover for close-button reveal
    int captionButtonHover = 0;        // 0 none, 1 min, 2 max, 3 close
    int captionButtonPressed = 0;
    bool tabNewTabIntent = false;      // Ctrl+T: next browser pick -> new tab

    // Caption button hit rects (client coords), refreshed by renderTabStrip
    tinta::RectF tabStripRects[3] = {};  // min, max, close
    struct TabHit {
        tinta::RectF rect{};
        int index = -1;    // tab index; -2 = plus, -3 = chevron
        tinta::RectF closeRect{};
        bool hasClose = false;
        // The lone title accepts right-clicks while remaining draggable.
        tinta::RectF contextRect{};
    };
    std::vector<TabHit> tabHits;       // refreshed by renderTabStrip
    // Pin button in the title bar: keeps this window above every other
    // (per-window, not persisted)
    bool alwaysOnTop = false;

    // Update-available chip (bottom right): non-blocking, once per
    // session, absent entirely for Store installs
    bool updateCheckEnabled = true;
    bool updateAvailable = false;
    bool updateDismissed = false;   // this session
    std::string updateVersion;      // "X.Y.Z" offered by the chip
    std::string updateLastCheck;    // mirrors Settings on save
    std::string updateDismissedVersion;
    tinta::RectF updateChipRect{};   // screen coords, zero while hidden
    tinta::RectF updateCloseRect{};
    // Draft recovery: crash leftovers found at startup, offered via a chip
    std::vector<std::wstring> recoveredDrafts;
    tinta::RectF draftChipRect{};    // screen coords, zero while hidden
    tinta::RectF draftCloseRect{};
    std::vector<std::pair<tinta::RectF, int>> tabSwitcherHits;
    // Closing a dirty tab routes through the unsaved-changes dialog; the
    // close completes from confirmExitAction once the user decides
    int pendingTabClose = -1;
    // Window close with dirty buffers: resolve them one dialog at a time,
    // then let the close proceed (Keep editing cancels it)
    bool pendingWindowClose = false;
    // Tab dragging: press arms a potential drag, moving past the threshold
    // starts reordering, leaving the strip vertically detaches the tab
    // into its own window
    int tabDragIndex = -1;       // pressed/dragged tab slot
    bool tabDragging = false;    // threshold crossed
    float tabDragOffsetX = 0.0f; // grab point within the tab
    int tabDragStartX = 0;
    int tabDragStartY = 0;
    // Pulled clear of the strip: the tab floats as a ghost card until
    // release decides (drop into another window / new window / snap back)
    bool tabDragDetached = false;
    HWND tabGhostWnd = nullptr;
    // A native move/size loop is in progress; the starting rect tells a
    // pure move (a possible strip drop) apart from a resize on exit
    bool windowMoveTracking = false;
    RECT windowMoveStartRect = {};
    // Right-click tab context menu (NPP-style close operations)
    bool showTabMenu = false;
    int tabMenuIndex = -1;          // tab the menu targets
    int tabMenuHover = -1;
    float tabMenuX = 0.0f;
    float tabMenuY = 0.0f;
    float tabMenuAnimation = 0.0f;
    // Bulk close (others / left / right) survives the per-tab
    // unsaved-changes dialogs by re-finding the kept tab by id
    int tabBulkCloseMode = 0;       // 0 none, 1 others, 2 left, 3 right
    int tabBulkKeepId = 0;
    int tabIdCounter = 0;           // DocTab.id source
    // Drag-out satellites keep the tab row even with a single tab
    bool forceTabStrip = false;
    // Quick-note empty state: "Open a file" button in the preview pane of
    // an untitled, still-empty buffer (rect refreshed by its renderer)
    tinta::RectF quickNoteButtonRect = {};
    bool quickNoteButtonHover = false;
    // Editor pane scrollbar dragging (#121)
    bool editorScrollbarDragging = false;
    float editorScrollbarDragStartY = 0.0f;
    float editorScrollbarDragStartScroll = 0.0f;
    // Only the primary window persists the tab session; satellites
    // (--cascade/--new/drag-outs) neither restore nor overwrite it
    bool sessionOwner = true;
    // The window icon drawn in the tab strip, decoded from the embedded .ico at
    // 32 px. A holder rather than a native pointer plus a Seated accessor, for the
    // reason lightboxBitmap gives below: there is no native left to wrap, so nothing
    // can go stale. The type is also what removes the #ifdef from the code that fills
    // it -- tabs.cpp asks image_api.h for a tinta::Bitmap and never names a D2D type.
    std::shared_ptr<tinta::Bitmap> titleIconBitmap;

    // Start page (design t7): the launcher that replaced the sample
    // document. It is the universal empty state: any tab with no
    // document and no edit buffer shows it, and Ctrl+T opens one fresh.
    // An embedded Learn document showing in place of the launcher;
    // cleared when anything else takes the view over
    bool startPageEmbeddedOpen = false;
    // Tracks appearance transitions so recents reload from settings.ini
    // each time the launcher comes back into view
    bool startPageShowing = false;
    struct RecentDoc { std::string path; unsigned long long when = 0; };
    std::vector<RecentDoc> startPageRecents;
    int startPageHover = 0;  // hit id under the mouse, 0 = none
    std::vector<std::pair<tinta::RectF, int>> startPageHits;  // rebuilt each paint
    // The hero icon, decoded from the same embedded .ico at 64 px -- a size that .ico
    // has no frame for, which is why the frame choice is image_api.h's stated policy
    // and not a decoder default. Same holder shape as titleIconBitmap above.
    std::shared_ptr<tinta::Bitmap> startPageIconBitmap;

    // Folder browser overlay
    bool showFolderBrowser = false;
    float folderBrowserAnimation = 0.0f;  // 0 to 1 for slide-in from left
    std::wstring folderBrowserPath;       // Current directory being browsed
    struct FolderItem {
        std::wstring name;
        bool isDirectory;
    };
    std::vector<FolderItem> folderItems;
    int hoveredFolderIndex = -1;
    float folderBrowserScroll = 0.0f;     // Scroll offset for folder list

    // Path header editing + new file/folder creation (#52)
    bool folderBrowserEditingPath = false;   // Path header is an edit box
    int folderBrowserNaming = 0;             // 0 = off, 1 = naming a new file, 2 = a new folder
    std::wstring folderBrowserInput;         // Single-line buffer shared by both inputs
    bool folderBrowserInputSelectAll = false; // Whole input selected: next keystroke replaces it
    bool folderInputJustOpened = false;      // Swallow the WM_CHAR of the key that opened it
    bool folderBrowserInputError = false;    // Last commit failed (bad path/name): red border

    // Shared document context menu / icon application menu
    bool showContextMenu = false;
    bool applicationMenu = false;
    bool appMenuHover = false;
    bool contextMenuKeyboard = false;
    // The mouse-up of a menu-item click must not reach the overlay handlers:
    // an action that opens the TOC or theme chooser would otherwise be
    // closed instantly by its own click's release landing "outside the panel"
    bool swallowNextMouseUp = false;
    float contextMenuX = 0.0f;        // Top-left, clamped into the window
    float contextMenuY = 0.0f;
    int hoveredContextMenuItem = -1;
    float contextMenuAnimation = 0.0f;

    // Help overlay
    bool showHelp = false;
    float helpAnimation = 0.0f;
    float helpScroll = 0.0f;
    float helpContentHeight = 0.0f;   // Total content height (set during render)
    float helpVisibleHeight = 0.0f;   // Visible area height (set during render)
    float helpScrollbarTop = 0.0f;    // Scrollbar track top Y (set during render)
    bool helpScrollbarDragging = false;
    float helpScrollbarDragStartY = 0;
    float helpScrollbarDragStartScroll = 0;

    // Settings overlay (Ctrl+,): section rail + rows of toggles/chips.
    // Hit rects are stored during render (print-preview pattern); each
    // entry pairs a rect with a SettingsAction id for the mouse-up test.
    bool showSettings = false;
    float settingsAnimation = 0.0f;
    int settingsSection = 0;  // 0 General, 1 Appearance, 2 Editor
    std::vector<std::pair<tinta::RectF, int>> settingsHits;
    int readingWidthPct = 100;  // mirrors Settings (#82); 100 = full width
    int zenWidthPct = 60;
    bool headingRules = true;   // mirrors Settings (#154)
    int settingsDragSlider = 0;         // SET_SLIDER_* while dragging, else 0
    tinta::RectF settingsSliderTrack[2]{};  // 0 window, 1 fullscreen (set in render)

    // Theme editor ("+ New" in settings): a working copy of a theme edited
    // through hex fields and a system-font list, previewed live in an ink
    // specimen. Saving goes through saveCustomTheme into themes.ini.
    bool showThemeEditor = false;
    D2DTheme themeEditorTheme{};        // working colors (string ptrs unused)
    std::wstring themeEditorName;
    std::wstring themeEditorFont;       // main font family
    std::wstring themeEditorHex[6];     // bg, text, heading, link, accent, code bg
    int themeEditorField = -1;          // focused field: 0..5 hex, 6 = name
    int themeEditorBase = 0;            // registry index the colors started from
    float themeEditorFontScroll = 0.0f;
    std::vector<std::wstring> systemFontFamilies;  // enumerated on first open
    std::vector<std::pair<tinta::RectF, int>> themeEditorHits;
    tinta::RectF themeEditorFontListRect{};  // for wheel routing (set in render)

    // UI language: languageSetting is the chosen registry index (-1 =
    // follow system), currentLanguageIndex is what tr() reads every call
    int languageSetting = -1;
    int currentLanguageIndex = 0;
    bool settingsLangOpen = false;      // language dropdown popup expanded
    tinta::RectF settingsLangBox{};      // dropdown value box (set in render)

    // Shortcut profile state (mirrors Settings.keyProfile; the dropdown in
    // Settings switches it live)
    std::string keyProfile = "windows";
    bool settingsKeysOpen = false;      // profile dropdown popup expanded
    tinta::RectF settingsKeysBox{};      // dropdown value box (set in render)
    // Session copy of the [Keys] overrides (the custom profile); the
    // shortcut editor edits these, WM_DESTROY writes them back
    std::vector<std::pair<std::string, std::string>> keyOverrides;
    // Shortcut editor overlay: click an action row, press its new key
    bool showShortcutEditor = false;
    int shortcutEditorRow = -1;         // row armed for key capture, -1 none
    std::vector<std::pair<tinta::RectF, int>> shortcutHits;  // rebuilt each paint

    // Resolved single-key bindings, indexed like KEY_ACTIONS (#77).
    // Filled by applyKeymap; each binding retains its input message kind.
    KeyBinding keymap[KEY_ACTION_COUNT] = {};

    // Reading position restore (#77): applied once enough of the document is
    // laid out for the target to be reachable (chunked layout grows
    // contentHeight, so applying immediately would clamp to a partial height)
    float pendingScrollRestore = -1.0f;

    // Mouse back/forward navigation: link jumps push where they came from;
    // XBUTTON1/Alt+Left walks back, XBUTTON2/Alt+Right forward, each entry
    // restoring its document and scroll position
    struct NavEntry {
        std::string path;
        float scrollY = 0.0f;
    };
    std::vector<NavEntry> navBack;
    std::vector<NavEntry> navForward;

    // Review annotations (#126 experiment): notes stored as HTML comments
    // in the source markdown; rendered as tinted text plus a marker rail
    // beside the scrollbar. The file is the only model — every change is
    // written to disk and re-derived on the watcher reload.
    struct Annotation {
        std::string quote;        // whitespace-collapsed anchor (may elide " ... ")
        std::string note;         // raw markdown note
        size_t commentStart = 0;  // byte range of the comment in sourceText
        size_t commentEnd = 0;
        int commentLine = 0;      // 1-based source line of the comment
        int lineFrom = 0;         // reported selection lines (block above comment)
        int lineTo = 0;
        size_t docStart = (size_t)-1;  // anchor in docText; (size_t)-1 = unresolved
        size_t docEnd = (size_t)-1;
        bool anchorTried = false;
    };
    std::vector<Annotation> annotations;
    std::string sourceText;       // raw markdown of currentFile (viewer)
    int hoveredAnnotation = -1;
    bool annotEditorOpen = false;

    // Link peek: dwelling on a local .md link previews the target, fully
    // rendered at the live theme beside the link
    std::string linkPeekUrl;    // hoveredLink value the dwell timer armed for
    std::wstring linkPeekTitle;
    bool linkPeekActive = false;
    // Peeking a plain-text file: the whole-file code block drops its card
    // chrome, since the peek panel already frames the content
    bool peekFrameless = false;
    ID2D1Bitmap* nativeLinkPeekBitmap = nullptr;  // owned while active
    tinta::RectF linkPeekAnchorDoc{};        // hovered link rect (doc coords)
    tinta::RectF linkPeekPanel{};            // placed panel (screen coords)
    int annotEditorIndex = -1;    // -1 = creating a new annotation
    std::wstring annotEditorText;
    size_t annotEditorCaret = 0;
    size_t annotPendingInsert = (size_t)-1;  // source offset for the new comment line
    std::string annotPendingQuote;
    int annotPendingLineFrom = 0;
    int annotPendingLineTo = 0;
    // Rebuilt each frame by renderAnnotations (screen coords, hit-testing)
    struct AnnotationMark {
        tinta::RectF square{};
        int index = -1;
        bool onText = false;  // level with visible text vs parked at an edge
    };
    std::vector<AnnotationMark> annotationMarks;
    tinta::RectF annotCopyBtnRect{};  // zero-sized while the rail is hidden

    // Print preview overlay. While it is open the document is held in print
    // layout — width, theme, zoom and scroll are hijacked by enterPrintLayout —
    // and the real screen state lives in printSaved. Screen dimensions and UI
    // scale must therefore be read from printSaved, not app.width/contentScale.
    struct PrintSavedView {
        int width = 0, height = 0;
        float contentScale = 1.0f, zoomFactor = 1.0f, appliedZoomFactor = 1.0f;
        float scrollX = 0, scrollY = 0, targetScrollX = 0, targetScrollY = 0;
        D2DTheme theme{};
        // Edit mode is suspended during print layout: the layout width comes
        // from documentViewportWidth, which in edit mode is the preview
        // pane — printing from the editor wrapped at half the page (#81)
        bool editMode = false;
    };
    bool showPrintPreview = false;
    PrintSavedView printSaved;
    std::vector<float> printPreviewBounds;    // page boundaries in doc Y (pages + 1)
    int printPreviewPage = 0;
    float printPreviewPageW = 794.0f;         // page size in DIPs (A4 fallback)
    float printPreviewPageH = 1123.0f;
    float printPreviewFit = 1.0f;             // page DIPs -> screen pixels
    std::vector<uint8_t> printPreviewPixels;  // current page, premultiplied BGRA
    unsigned printPreviewPxW = 0, printPreviewPxH = 0;
    int printPreviewPaper = -1;               // index into PRINT_PAPERS (-1: detect)
    bool printPreviewLandscape = false;
    tinta::RectF printPreviewPrintBtn{};       // hit rects (set during render)
    tinta::RectF printPreviewCancelBtn{};
    tinta::RectF printPreviewPaperBtn[4]{};
    tinta::RectF printPreviewOrientBtn[2]{};   // 0 portrait, 1 landscape

    // Blocks wider than the printable area (mermaid diagrams, wide tables)
    // are shrunk uniformly to fit the margins when printing; each band is a
    // vertical slice of the document drawn at `scale` about its own top-left
    struct PrintShrinkBand { float top, bottom, scale; };
    std::vector<PrintShrinkBand> printShrinkBands;

    // Table of contents overlay
    bool showToc = false;
    bool tocOnLeft = false;     // panel side (persisted)
    // Pinned panels (#156, persisted): no dismissal on document clicks,
    // the document keeps hover, selection, and the keyboard
    bool tocPinned = false;
    bool browserPinned = false;
    float tocAnimation = 0.0f;  // 0 to 1 slide-in from the chosen side
    // A clicked heading stays the active row even when the scroll cannot
    // reach it (short documents); any real scroll hands back to the spy
    int tocSpyOverride = -1;
    float tocSpyOverrideScroll = 0.0f;  // targetScrollY at the jump
    struct HeadingInfo {
        std::wstring text;
        int level;       // 1-6
        float y;         // document Y coordinate
        std::string id;  // GitHub-style slug for anchor links
    };
    std::vector<HeadingInfo> headings;
    std::unordered_map<std::string, int> headingSlugCounts;
    int hoveredTocIndex = -1;
    float tocScroll = 0.0f;
    // Typed while the panel is open: case-insensitive substring filter
    std::wstring tocFilter;

    // Mouse
    bool mouseDown = false;
    int mouseX = 0;
    int mouseY = 0;

    // Vertical scrollbar
    bool scrollbarHovered = false;
    bool scrollbarDragging = false;
    bool verticalScrollbarVisible = false;
    float scrollbarContentHeight = 0.0f;
    float scrollbarDragStartY = 0;
    float scrollbarDragStartScroll = 0;

    // Horizontal scrollbar
    bool hScrollbarHovered = false;
    bool hScrollbarDragging = false;
    float hScrollbarDragStartX = 0;
    float hScrollbarDragStartScroll = 0;

    // Links - tracked during render for click detection
    struct LinkRect {
        tinta::RectF bounds;
        std::string url;
    };
    std::vector<LinkRect> linkRects;
    std::string hoveredLink;

    // Code block info - tracked for copy button
    struct CodeBlockInfo {
        tinta::RectF bounds;       // Full background rect in document coordinates
        std::wstring codeText;    // The code content (diagram source for diagrams)
        bool isDiagram = false;   // adds the copy-as-image button
        unsigned fitKey = 0;      // per-layout ordinal for the fit toggle
        bool fitCandidate = false;
        bool fitActive = false;
    };
    std::vector<CodeBlockInfo> codeBlocks;
    int hoveredCodeBlock = -1;

    // Tables - tracked for the copy-as-TSV button
    struct TableInfo {
        tinta::RectF bounds{};  // document coordinates
        std::wstring tsv;      // cells joined by tabs, rows by newlines
        unsigned fitKey = 0;   // per-layout ordinal for the fit toggle
        bool fitCandidate = false;  // wider than the column at natural size
        bool fitActive = false;
        size_t sourceOffset = SIZE_MAX;  // Table element source (#148)
    };
    std::vector<TableInfo> tableRects;
    int hoveredTable = -1;

    // In-place table editing in the rendered preview (#148, edit mode).
    // Cells are identified by the table's source offset plus row/col so
    // they survive relayouts; rects are document coordinates.
    struct TableCellRect {
        size_t tableSrc = 0;  // Table element sourceOffset (first cell text)
        int row = 0, col = 0; // row 0 = header
        tinta::RectF rect{};
    };
    std::vector<TableCellRect> tableCellRects;  // filled in edit mode only
    bool tableEditActive = false;
    size_t tableEditSrc = 0;
    int tableEditRow = -1, tableEditCol = -1;
    std::wstring tableEditText;
    size_t tableEditCaret = 0;
    // Hover affordances: + row under the table, + column at its right edge
    size_t tableAddSrc = 0;              // table the buttons belong to
    tinta::RectF tableAddRowRect{};       // document coordinates
    tinta::RectF tableAddColRect{};

    // Fit-to-width overrides for oversized tables/diagrams, keyed by their
    // per-layout ordinal (cleared when the document changes)
    std::vector<unsigned> fitBlocks;
    unsigned layoutTableSeq = 0;
    unsigned layoutDiagramSeq = 0;

    // Text bounds - tracked for cursor changes and selection (document coordinates)
    struct TextRect {
        tinta::RectF rect;
        size_t docStart = 0;   // Start position in docText
        size_t docLength = 0;  // Length in docText
        // Index into layoutTextRuns of the run whose layout covers this
        // rect's doc range (SIZE_MAX = none; hit-testing interpolates).
        // Both vectors rebuild together on relayout, so the index is stable.
        size_t runIndex = (size_t)-1;
    };
    std::vector<TextRect> textRects;

    // Line buckets for fast hit-testing/selection
    struct LineBucket {
        float top = 0;
        float bottom = 0;
        float minX = 0;
        float maxX = 0;
        std::vector<size_t> textRectIndices;
    };
    std::vector<LineBucket> lineBuckets;

    // Search match info
    struct SearchMatch {
        size_t textRectIndex;       // Index into textRects
        size_t startPos;            // Character offset in text
        size_t length;              // Match length
        tinta::RectF highlightRect;  // Computed highlight bounds
    };
    std::vector<SearchMatch> searchMatches;
    bool overText = false;

    // Folder-wide search: sibling .md files matching the current query,
    // filled by a worker thread and shown beside the search bar
    bool folderSearchEnabled = true;
    // B opens the folder browser with the path box focused (#81)
    bool browserFocusPath = false;
    // Plain file launches join this window as tabs (settings toggle)
    bool openInTabs = true;
    int folderSearchGeneration = 0;
    struct FolderMatch {
        std::wstring snippet;
        size_t matchStart = 0;
        size_t matchLen = 0;
    };
    struct FolderFileResult {
        std::wstring fileName;
        std::wstring fullPath;
        std::vector<FolderMatch> matches;  // first few only
        int totalMatches = 0;
    };
    std::vector<FolderFileResult> folderResults;
    struct FolderResultHit {
        tinta::RectF rect{};
        int fileIndex = -1;
    };
    std::vector<FolderResultHit> folderResultHits;  // rebuilt each paint

    // Text selection (#83): a pair of character offsets into docText.
    // Highlights and copies derive from the same range, and offsets survive
    // relayout (zoom, resize) where pixel rects would not.
    bool selecting = false;
    size_t selAnchor = 0;  // where the drag started
    size_t selFocus = 0;   // where the mouse is now
    bool hasSelection = false;
    // Dragging past the viewport edge scrolls (px per TIMER_SELECT_SCROLL
    // tick, sign = direction)
    float selAutoScrollVel = 0.0f;
    // Shift+click extends: finalize as a selection even without a drag
    bool selShiftExtend = false;

    // Multi-click selection (double/triple click)
    std::chrono::steady_clock::time_point lastClickTime;
    int clickCount = 0;
    int lastClickX = 0, lastClickY = 0;
    enum class SelectionMode { Normal, Word, Line } selectionMode = SelectionMode::Normal;
    // The word/line originally clicked; drag extension unions the current
    // word/line with this range
    size_t selAnchorRangeStart = 0, selAnchorRangeEnd = 0;

    // Document text built during render (used for search/mapping)
    std::wstring docText;
    std::wstring docTextLower;

    // Cached space widths for common formats
    float spaceWidthText = 0.0f;
    float spaceWidthBold = 0.0f;
    float spaceWidthItalic = 0.0f;
    float spaceWidthCode = 0.0f;

    // Layout cache (document coordinates)
    struct LayoutTextRun {
        // SHARED, and the sharing is real rather than defensive.
        //
        // This was an owning `IDWriteTextLayout*` with explicit AddRef/Release, and
        // the plan of record was to retype it to `unique_ptr<tinta::TextLayout>` to
        // match what tinta::TextFactory::CreateTextLayout returns. Reading the
        // producers refuted that. math_render.cpp's mathBoxRetain AddRefs a layout
        // owned by a MathBox that g_mathCache keeps alive ACROSS FRAMES, then pushes
        // the same object into this vector, which releases it on the next
        // clearLayoutCache. Two owners, one object, deliberately -- reshaping a math
        // box per frame is exactly what that cache exists to avoid.
        //
        // So `unique_ptr` here would have been a false claim about lifetime that
        // compiles. shared_ptr is the exact translation of the refcount this member
        // already had, and unique_ptr converts to it implicitly, so the producers
        // that eventually move onto app.textFactory need no second spelling.
        std::shared_ptr<tinta::TextLayout> layout;
        tinta::PointF pos{};
        tinta::RectF bounds{};
        tinta::Color color{};
        size_t docStart = 0;
        size_t docLength = 0;
        bool selectable = false;
    };
    struct LayoutRect {
        tinta::RectF rect{};
        tinta::Color color{};
    };
    struct LayoutLine {
        tinta::PointF p1{};
        tinta::PointF p2{};
        tinta::Color color{};
        float stroke = 1.0f;
    };
    enum class LayoutShapeType {
        Rectangle,
        RoundedRectangle,
        Diamond,
        Stadium,
        Ellipse,
        Hexagon,
        // Arbitrary geometry built eagerly at layout time (pie slices,
        // arrowheads, crow's feet); `geometry` is in local space with the
        // origin at rect top-left, rect doubles as the culling bounds
        Path,
    };
    struct LayoutShape {
        LayoutShapeType type = LayoutShapeType::Rectangle;
        tinta::RectF rect{};
        tinta::Color fill{};
        tinta::Color stroke{};
        float strokeWidth = 1.0f;
        float radius = 0.0f;
        // Cached polygon outline in LOCAL space (origin = rect top-left),
        // built lazily on first draw. Local space keeps it valid across the
        // exterior-lane x-shift that moves rects after layout.
        //
        // A unique_ptr to the ABSTRACT seam type, which is what deleted the release
        // loop in clearLayoutCache below and the partial-rollback loop in
        // render.cpp. Unique rather than shared, and that is a measurement rather
        // than a default: every producer builds one path for one shape and the four
        // sites that push a LayoutShape declare it inside a `case` block and break
        // immediately, so no path is ever reachable from two owners. Contrast
        // LayoutTextRun::layout, which is genuinely SHARED -- the maths cache and a
        // frame's run own the same native layout on purpose -- so a unique_ptr there
        // would have been a false lifetime claim that compiled and then double-freed.
        std::unique_ptr<tinta::PathGeometry> geometry;
    };
    struct LayoutConnector {
        std::vector<tinta::PointF> points;
        tinta::RectF bounds{};
        tinta::Color color{};
        float stroke = 1.0f;
        float arrowSize = 8.0f;
        bool directed = true;
        bool dashed = false;
    };
    // Clickable task checkboxes (document coordinates)
    struct TaskRect {
        tinta::RectF rect{};
        size_t markOffset = SIZE_MAX;  // source offset of the [x]/[ ] mark char
        bool checked = false;
    };
    std::vector<TaskRect> taskRects;

    // Viewport width at the last layout: a change (folder browser toggle,
    // preview split drag) triggers a relayout in the render loop
    float lastViewportWidth = -1.0f;
    std::vector<LayoutTextRun> layoutTextRuns;
    std::vector<LayoutRect> layoutRects;
    std::vector<LayoutLine> layoutLines;
    std::vector<LayoutShape> layoutShapes;
    std::vector<LayoutConnector> layoutConnectors;
    bool layoutDirty = true;

    // Incremental layout: the first paint lays out ~2 viewports, the rest
    // continues in WM_APP_LAYOUT_CHUNK time slices (see render.cpp)
    bool layoutComplete = true;
    size_t layoutNextBlock = 0;   // next top-level block to lay out
    float layoutCursorY = 0.0f;   // y where the next block starts
    float layoutIndent = 0.0f;
    float layoutMaxWidth = 0.0f;
    size_t layoutTimeUs = 0;      // total layout time for the current cycle

    // Plain-text .md reference existence per resolved path (#127); cleared
    // with the layout so reload and file changes re-check the disk
    std::unordered_map<std::string, bool> fileRefCache;

    // Scroll sync anchors: source byte offset → rendered Y position
    struct ScrollAnchor {
        size_t sourceOffset;
        float renderedY;
    };
    std::vector<ScrollAnchor> scrollAnchors;
    std::vector<size_t> editorLineByteOffsets;  // UTF-8 byte offset per editor line

    size_t searchMatchCursor = 0;

    // Signal chips (design t13): one anatomy for every notification.
    // Active chips stack bottom-right and drain over a visible rail;
    // dismissed or drained chips park in the bell tray with their
    // actions still live.
    struct SignalChip {
        int id = 0;
        int severity = 0;      // SIG_INFO/SUCCESS/WARN/ERROR
        int icon = 0;          // SignalIcon
        std::wstring text;     // regular part
        std::wstring emph;     // bold tail (file name, shortcut)
        std::wstring traySub;  // context line once parked
        int action = 0;        // SignalAction run on chip/tray click
        int closeAction = 0;   // SignalClose run when X resolves state
        bool drains = true;    // false = stays until answered
        float remaining = 4.0f;   // drain seconds left
        bool pinned = false;
        float tuckT = -1.0f;      // >=0: parking animation progress
        bool anchored = false;    // mini pill at docAnchor (Copied)
        tinta::RectF docAnchor{};  // document coords when anchored
        // refreshed by the renderer each frame
        tinta::RectF rect{}, pinRect{}, closeRect{};
    };
    std::vector<SignalChip> signalChips;   // active, oldest first
    std::vector<SignalChip> signalTray;    // parked, newest first (cap 20)
    int signalIdCounter = 1;
    int signalUnseen = 0;                  // bell badge
    bool signalTrayOpen = false;
    int signalHoverChip = 0;               // id under cursor (drain hold)
    double signalLastTick = 0.0;           // seconds, drain integration
    tinta::RectF signalBellRect{};
    tinta::RectF signalTrayRect{}, signalTrayClearRect{};
    // The unsaved-exit / create-file prompt chip occupies the corner slot;
    // the chip stack starts above it (set by the prompt renderers)
    tinta::RectF promptChipRect{};
    // Close cross and pin on the floating Contents card (t13/#156)
    tinta::RectF tocCloseRect{};
    tinta::RectF tocPinRect{};
    tinta::RectF folderPinRect{};  // pin button on the browser card
    // Breadcrumb segments on the browser card: rect -> number of path
    // components the click keeps (refreshed by renderFolderBrowser)
    std::vector<std::pair<tinta::RectF, int>> folderCrumbHits;
    std::vector<std::pair<tinta::RectF, int>> signalTrayHits;  // action row: chip id

    // Cursor blink state, toggled by TIMER_CURSOR_BLINK (editor + search cursor)
    bool cursorBlinkOn = true;

    // Search overlay
    bool showSearch = false;
    float searchAnimation = 0.0f;
    std::wstring searchQuery;
    int searchCurrentIndex = 0;
    bool searchActive = false;
    bool searchJustOpened = false;  // Skip WM_CHAR after opening with F key

    // File watching (auto-reload)
    FILETIME lastFileWriteTime = {};
    bool fileWatchEnabled = true;

    // Edit mode
    bool editMode = false;
    float editorSplitRatio = 0.5f;
    bool draggingSeparator = false;
    float separatorDragStartX = 0;
    float separatorDragStartRatio = 0;

    // Double-ESC detection
    std::chrono::steady_clock::time_point lastEscTime;
    bool escPressedOnce = false;
    bool confirmExitPending = false;  // Unsaved-changes dialog is showing
    // Dialog open time: keys arriving within the grace window are ignored
    // so ESC-smashing lands on a visible, stable dialog
    std::chrono::steady_clock::time_point confirmExitOpenedAt;
    std::vector<std::pair<tinta::RectF, int>> confirmExitHits;  // rebuilt each paint

    // Create-missing-reference dialog: a clicked ghost ref offers to
    // create its target and start typing
    bool createRefPending = false;
    std::string createRefPath;  // resolved absolute target
    std::vector<std::pair<tinta::RectF, int>> createRefHits;  // rebuilt each paint
    // WM_CHARs arriving in this window are dropped so the keystroke that
    // resolved the dialog cannot type into the editor it just opened
    // (one keydown can yield more than one WM_CHAR)
    std::chrono::steady_clock::time_point swallowCharsUntil{};

    // Editor notification

    // Pandoc bridge (pandoc.cpp): resolved executable, the user's
    // settings override, and the single in-flight conversion guard
    std::wstring pandocExe;
    std::wstring pandocUserPath;
    bool pandocChecked = false;
    bool pandocRunning = false;

    // Unified editor (design t11): one raw buffer, live render beside it;
    // the left tool rail slides in with edit mode carrying the controls
    bool editorAssists = true;
    float editRailAnim = 0.0f;       // rail slide-in 0..1
    int editRailHover = 0;           // hit id under the mouse, 0 = none
    std::vector<std::pair<tinta::RectF, int>> editRailHits;  // rebuilt each paint

    // Raw editor insert menu (design t9): right-click drops markdown at
    // the caret; Table and Diagram open flyout submenus. The rail's
    // table/diagram buttons open the same submenus standalone.
    bool editCtxOpen = false;
    bool editCtxRailOnly = false;  // submenu only, anchored at the rail
    float editCtxX = 0.0f;
    float editCtxY = 0.0f;
    int editCtxHover = 0;
    int editCtxSub = 0;       // 0 none, 1 table grid, 2 diagram templates
    int editCtxSubHover = 0;
    int editCtxGridC = 0;     // hovered table size
    int editCtxGridR = 0;
    std::vector<std::pair<tinta::RectF, int>> editCtxHits;     // rebuilt each paint
    std::vector<std::pair<tinta::RectF, int>> editCtxSubHits;

    // Editor document
    std::wstring editorText;
    bool editorDirty = false;
    std::vector<size_t> editorLineStarts;
    float editorScrollX = 0.0f;  // horizontal scroll, non-wrap mode only (#77)

    // Editor view options (persisted)
    bool editorShowPreview = true;
    bool editorWordWrap = false;

    // Soft-wrap metrics: cumulative visual rows before each logical line
    // (editorRowStarts.size() == lines + 1). Only maintained while
    // editorWordWrap is on; rebuilt when text or wrap width changes.
    std::vector<size_t> editorRowStarts;
    size_t editorTotalRows = 0;
    float editorRowMetricsWidth = -1.0f;  // wrap width the metrics were built for

    // Editor cursor & selection
    size_t editorCursorPos = 0;
    int editorDesiredCol = -1;
    float editorDesiredX = -1.0f;  // desired caret x for Up/Down in wrap mode
    bool editorSelecting = false;
    size_t editorSelStart = 0;
    size_t editorSelEnd = 0;
    bool editorHasSelection = false;

    // Editor scroll
    float editorScrollY = 0;
    float editorContentHeight = 0;

    // Editor search
    struct EditorSearchMatch {
        size_t startPos;
        size_t length;
    };
    std::vector<EditorSearchMatch> editorSearchMatches;
    int editorSearchCurrentIndex = 0;

    // Editor find & replace (#121): Ctrl+H adds a replace row to search
    bool searchReplaceMode = false;
    std::wstring replaceText;
    bool replaceFieldActive = false;    // keyboard goes to the replace row
    // Hit rects refreshed by renderSearchOverlay: 1 query field,
    // 2 replace field, 3 Replace button, 4 Replace-all button
    std::vector<std::pair<tinta::RectF, int>> searchReplaceHits;

    // Undo/redo
    struct EditAction {
        enum Type { Insert, Delete };
        Type type;
        size_t position;
        std::wstring text;
        size_t cursorBefore, cursorAfter;
    };
    std::vector<EditAction> undoStack;
    std::vector<EditAction> redoStack;

    // Editor text format (monospace)
    //
    // supSubFormat is a DOCUMENT format that sits here rather than with its ten siblings,
    // and it is left where it is on purpose: moving a declaration is invisible in a diff
    // that also changes its type, and this one HAD a bug attached (see
    // releaseOverlayFormats below) that a reader should find at the same address twice.
    // Present tense there would now overstate it: the bug is fixed and the invariant is
    // asserted in tests/theme_tests.cpp. The pointer to the explanation is still the reason
    // this declaration stays put.
    std::unique_ptr<tinta::TextFormat> supSubFormatStorage;
    tinta::TextFormat* supSubFormat = nullptr;   // small size for ^sup^/~sub~
    Seated<tinta::TextFormat, IDWriteTextFormat> editorTextFormatStorage;
    tinta::TextFormat* editorTextFormat = nullptr;
    float editorCharWidth = 0.0f; // Measured monospace char width
    // Slim in-editor line-number gutter (design t11)
    Seated<tinta::TextFormat, IDWriteTextFormat> editorGutterFormatStorage;
    tinta::TextFormat* editorGutterFormat = nullptr;

    // Metrics
    StartupMetrics metrics;
    size_t drawCalls = 0;

    ~App() { shutdown(); }

    void clearLayoutCache() {
        // NO RELEASE LOOPS AT ALL NOW, and that is the whole visible payoff of the
        // two seam slices. run.layout is a shared_ptr and shape.geometry is a
        // unique_ptr, so each clear() below IS the release -- the loop that used to
        // sit here called releaseNative on every shape's raw ID2D1PathGeometry*, and
        // its twin in render.cpp unwound a half-built layout by hand.
        //
        // Worth stating why the deletion is safe rather than just tidy: the old loop
        // released and the clear() then destroyed, so a shape that was ever copied
        // out of this vector would have been a double-free waiting to happen. A
        // unique_ptr makes that shape not compile.
        layoutTextRuns.clear();
        layoutRects.clear();
        layoutLines.clear();
        layoutShapes.clear();
        layoutConnectors.clear();
        layoutBitmaps.clear();
        taskRects.clear();
        linkRects.clear();
        codeBlocks.clear();
        tableRects.clear();
        tableCellRects.clear();
        textRects.clear();
        lineBuckets.clear();
        docText.clear();
        docTextLower.clear();
        headings.clear();
        headingSlugCounts.clear();
        fileRefCache.clear();
        for (auto& a : annotations) {
            a.docStart = a.docEnd = (size_t)-1;
            a.anchorTried = false;  // re-anchor against the fresh docText
        }
    }

    // The eleven document formats' storage, dropped in ONE place. There is no native to
    // Release any more, so this is eleven pointer-and-storage pairs and nothing else.
    //
    // WHY IT IS A MEMBER HERE RATHER THAN THE BODY OF releaseDocumentTextFormats, which is
    // where it belongs on every other reading: a link measurement on 2026-09-18. Calling
    // that free function from App::shutdown() -- an inline member -- put its symbol in the
    // link closure of every target that includes app.h, and the build came back with
    // LNK2019 from more than twenty test executables that have no interest in a text
    // format. So the loop lives where App can reach it with no new dependency, and
    // releaseDocumentTextFormats() calls this and adds the space widths.
    void releaseDocumentFormats() {
        auto drop = [](std::unique_ptr<tinta::TextFormat>& storage,
                       tinta::TextFormat*& pointer) {
            pointer = nullptr;
            storage.reset();
        };
        drop(textFormatStorage, textFormat);
        drop(headingFormatStorage, headingFormat);
        drop(codeFormatStorage, codeFormat);
        drop(boldFormatStorage, boldFormat);
        drop(italicFormatStorage, italicFormat);
        drop(boldItalicFormatStorage, boldItalicFormat);
        drop(codeBoldFormatStorage, codeBoldFormat);
        drop(codeItalicFormatStorage, codeItalicFormat);
        drop(codeBoldItalicFormatStorage, codeBoldItalicFormat);
        drop(supSubFormatStorage, supSubFormat);
        for (int i = 0; i < 6; ++i) drop(headingFormatsStorage[i], headingFormats[i]);
    }

    void releaseOverlayFormats() {
        signalSmallFormatStorage.reset(); signalSmallFormat = nullptr;
        searchTextFormatStorage.reset(); searchTextFormat = nullptr;
        themeTitleFormatStorage.reset(); themeTitleFormat = nullptr;
        themeHeaderFormatStorage.reset(); themeHeaderFormat = nullptr;
        folderBrowserFormatStorage.reset(); folderBrowserFormat = nullptr;
        tocFormatStorage.reset(); tocFormat = nullptr;
        tocFormatBoldStorage.reset(); tocFormatBold = nullptr;
        statsFormatStorage.reset(); statsFormat = nullptr;
        // supSubFormat IS DELIBERATELY ABSENT FROM THIS FUNCTION -- do not add it back.
        // It is a DOCUMENT format: installDocumentTextFormats creates it alongside the other
        // ten, and releaseDocumentFormats above drops it. A line here releasing it as well
        // was this tree's superscript bug for several slices, and it read as correct at every
        // glance because nothing in EITHER function is wrong on its own -- the eleven
        // document formats and the ten chrome formats are both handled consistently, and this
        // one member was in both lists. What made it fatal is the call order on Windows:
        // updateTextFormats creates the document formats, then calls updateOverlayFormats,
        // whose first statement is this function, so the format died before anything drew and
        // inline_style.cpp's `if (app.supSubFormat)` never fired. The Qt path never calls this
        // function, so the two backends rendered ^sup^ differently and no test compared them.
        // The invariant is now asserted rather than described: tests/theme_tests.cpp checks
        // that this function leaves every document format standing.
        editorTextFormatStorage.reset(); editorTextFormat = nullptr;
        editorGutterFormatStorage.reset(); editorGutterFormat = nullptr;
        for (auto& fmt : themePreviewFormats) {
            fmt.nameStorage.reset(); fmt.name = nullptr;
            fmt.previewStorage.reset(); fmt.preview = nullptr;
            fmt.codeStorage.reset(); fmt.code = nullptr;
        }
        themePreviewFormats.clear();
    }

    void releaseImageCache() {
        // clear() alone: each entry's shared_ptr drops the cache's reference, and the
        // bitmap goes when the last holder does. The explicit per-entry release this
        // replaces was the fourth hand-written half of the same pairing.
        imageCache.clear();
        imageCacheBytes = 0;
        imageCacheUseClock = 0;
        // reset(), not releaseNative(): both icons are shared handles now, so the
        // bitmap goes when the last holder does -- the same replacement the image
        // cache above got, for the same reason.
        titleIconBitmap.reset();
        startPageIconBitmap.reset();
    }

    void shutdown() {
        lightboxBitmap.reset();
        releaseNative(nativeLinkPeekBitmap);
        clearLayoutCache();
        releaseOverlayFormats();
        releaseImageCache();
        releaseNative(wicFactory);
        releaseNative(nativeBrush);
        brushStorage.reset();
        brush = nullptr;
        releaseNative(nativeDashedStrokeStyle);
        dashedStrokeStyleStorage.reset();
        dashedStrokeStyle = nullptr;
        releaseNative(deviceContext);
        releaseNative(nativeRenderTarget);
        renderTargetStorage.reset();
        renderTarget = nullptr;
        releaseNative(fontFallback);
        // No releaseNative(textAnalyzer): the analyzer is now owned by D2DTextFactory
        // and released by ~D2DTextFactory when textFactoryStorage resets below.
        // A FOURTH hand-written copy of the eleven-format release pairing stood here, and
        // the compiler is what found it -- not the census and not the read of
        // updateTextFormats, both of which had already been through this file. It was
        // identical to the block in updateTextFormats and to releaseQtTextFormats' loop,
        // and like the first of those it nulled every native without Releasing it.
        //
        // Before the factory below, not after, and the order is kept deliberately even
        // though it no longer matters for correctness: D2DTextFormat Releases its own
        // native, and an IDWriteTextFormat holds its factory alive independently, so
        // either order is safe now. It was not safe to reason about when these were raw
        // pointers, which is why the sequence is preserved rather than tidied.
        releaseDocumentFormats();
        // bodyTypography and codeTypography used to be released here. They are
        // tinta::Typography values now and own nothing; deliberately not replaced
        // with a clear(), because they hold the feature sets the app always wants
        // and there is no device to lose them with.
        if (dwriteFactory) {
            releaseNative(dwriteFactory);
            // The wrapper borrows, so it has nothing to release -- but leaving the
            // seated pointer non-null would hand a post-shutdown caller a factory
            // wrapping a released COM object, which is the stale-wrapper failure the
            // bitmap accessors exist to prevent, arriving by the other door.
            textFactoryStorage.reset();
            textFactory = nullptr;
        }
        releaseNative(d2dFactory);
    }

    // --- seam wrapper seating ------------------------------------------------
    // The tinta::* pointers above are what the drawing sites call; these reseat
    // them onto whatever natives currently exist.
    //
    // NO TEXT FORMAT IS SEATED ANY MORE. seatFormat and seatTextFormats are both
    // deleted, and so are the thirteen native chrome members they read -- there is
    // no IDWriteTextFormat* left in this struct. Every text format in the tree is
    // now CREATED through tinta::TextFactory and handed back already owning itself,
    // by installDocumentTextFormats and installChromeFormats in
    // include/text_formats.h, so there is nothing to wrap in place.
    //
    // THE DELETION HAD TO HAPPEN IN THE SAME COMMIT AS THE CHROME PORT, and this is
    // the note the next person moving a format family should read first. seatFormat's
    // first act on a NULL native was storage.reset(), so once installChromeFormats
    // owns creation, a single surviving seatTextFormats() call would have destroyed
    // all twenty chrome formats immediately after they were built -- on the path that
    // runs after every theme change, every zoom step and every DPI change. It would
    // not have failed loudly: the formats would simply be null, chrome would stop
    // drawing, and no test in the tree renders chrome.
    //
    // What makes that a caught bug rather than a shipped one is mechanical and worth
    // stating: the natives went WITH the function, so the compiler rejects any
    // surviving seat call by name. The same paragraph stood here for the document
    // formats one slice ago and it was load-bearing then too. A comment would not
    // have caught it; the missing member does.
    //
    // The idempotence argument that used to live here belonged to seatFormat and left
    // with it: D2DTextFormat OWNS its native and Releases in its destructor, so a
    // blind reset-then-rewrap of an unchanged native would have Released the very
    // pointer the new wrapper was about to hold, which is why seatFormat compared
    // first. That property now lives in Seated<>::seat itself (include/seated.h),
    // where the render-target, brush and stroke-style seats below still rely on it.

    // The one seat function that needs a guard, and the reason is narrow: the
    // member is an ID2D1HwndRenderTarget* while D2DRenderTarget takes the base
    // ID2D1RenderTarget*, and a derived-to-base pointer conversion needs COMPLETE
    // types -- forward declarations cannot express the inheritance. (C++ has no
    // syntax for `struct Derived : Base;`.)
    //
    // The alternative was to make ID2D1HwndRenderTarget an alias OF
    // ID2D1RenderTarget off Windows, which needs no guard here at all. Rejected:
    // it would silently accept a device context where an hwnd target is required,
    // in the one build where the compiler is the only check there is.
    void seatRenderTarget() {
#ifdef _WIN32
        renderTargetStorage.seat<D2DRenderTarget>(nativeRenderTarget);
        renderTarget = renderTargetStorage.get();
#else
        // nativeRenderTarget is unconditionally null here -- the interface is
        // incomplete, so nothing can produce one -- and the storage is already
        // default-constructed empty. Stating the postcondition rather than
        // assuming it.
        renderTargetStorage.reset();
        renderTarget = nullptr;
#endif
    }

    // Plain assignment, like seatRenderTarget and unlike seatFormat: D2DTextFactory
    // borrows, so it has no destructor to Release the pointer a new wrapper is about
    // to hold, and the compare-first guard seatFormat needs would be dead weight here.
    void seatTextFactory() {
#ifdef _WIN32
        textFactoryStorage.seat<D2DTextFactory>(dwriteFactory);
        textFactory = textFactoryStorage.get();
#else
        textFactoryStorage.reset();
        textFactory = nullptr;
#endif
    }

    void seatBrush() {
#ifdef _WIN32
        brushStorage.seat<D2DBrush>(nativeBrush);
        brush = brushStorage.get();
#else
        brushStorage.reset();
        brush = nullptr;
#endif
    }

    void seatDashedStrokeStyle() {
#ifdef _WIN32
        dashedStrokeStyleStorage.seat<D2DStrokeStyle>(nativeDashedStrokeStyle);
        dashedStrokeStyle = dashedStrokeStyleStorage.get();
#else
        dashedStrokeStyleStorage.reset();
        dashedStrokeStyle = nullptr;
#endif
    }

    // THREE SEATS AND A FACTORY, down from four and a factory: the seatTextFormats()
    // line that stood between the stroke style and the factory is gone with the
    // function. Everything still listed here wraps a native that DirectWrite really
    // does create -- the render target, the brush, the dashed stroke style and the
    // DWrite factory itself -- which is the residue this port's anti-claim A2 said
    // would stay. Text formats are the family that finished leaving.
    void seatSeamWrappers() {
        seatRenderTarget();
        seatBrush();
        seatDashedStrokeStyle();
        seatTextFactory();
    }

    // ONE scalar bitmap is still reached through an ACCESSOR, rather than through a
    // seated pointer field like the render target and the formats. The reason is its
    // lifetime, not its type: the link peek arms and disarms while the app runs, so a
    // seated field would need reseating at every one of its mutation sites, and missing
    // one would leave a stale pointer that still draws the PREVIOUS image. Reseating
    // on read cannot be forgotten. It is affordable only because D2DBitmap borrows
    // (see render_d2d.h): rebuilding the wrapper is one pointer store and no
    // refcount traffic. Deliberately NOT the shape to copy for an owning wrapper.
    //
    // THIS COMMENT SAID "FOUR", THEN THE LIGHTBOX LEFT, AND NOW BOTH ICONS HAVE.
    // Each one went the same way and for a better reason than the accessor: a
    // shared_ptr<tinta::Bitmap> member has no native pointer behind it, so the
    // staleness the accessor defends against cannot arise, AND the member stops naming
    // a Direct2D type -- which is what let the two icons' `#ifdef` go rather than move.
    // The accessor shape is what a BORROWED native needs, not what the seam prefers.
    //
    // const, with mutable storage, because lightboxImageRect takes a const App&.
    //
    // THE GUARD IS TINTA_D2D_BACKEND, NOT _WIN32, for the reason CMakeLists.txt spells out
    // where that macro is defined: an `#ifdef _WIN32` in a header that BOTH stacks compile
    // is asking about the OS when it means to ask about the backend. These accessors were
    // the second instance of that mistake, after src/paint_document.cpp, and they were
    // found the same way -- by a link failure rather than by reading. Under
    // -DTINTA_LEGACY_D2D=OFF, qinta is a WINDOWS target with no D2D backend, so _WIN32 was
    // defined, `seat<D2DBitmap>` instantiated, and four objects that have nothing to do with
    // Direct2D (tabs, overlays, startpage, editrail) each came back with
    //
    //   error LNK2001: unresolved external symbol D2DBitmap::GetSize
    //
    // out of src/render_d2d.cpp, which qinta correctly does not compile. Those four are pure
    // CALLERS -- none of them names a D2D type -- which is why the fix belongs here, in the
    // one header they share, and not in any of them.
    //
    // Returning nullptr when the backend is absent is not a new behaviour, it is the SAME
    // behaviour this file already had off Windows, and the callers already handle it (each
    // one passes the pointer to a draw helper that skips a null bitmap). That was written
    // when all three chrome icons drew on neither stack, pending a Qt path. TWO OF THE
    // THREE NOW HAVE ONE and are members above; the link peek is the one still waiting,
    // so the paragraph is still live for it and is no longer the whole story.
    // No titleIconBitmap() or startPageIconBitmap() accessor either, and no
    // lightboxBitmap() one: all three members of those names ARE tinta::Bitmap holders
    // now, so .get() is the whole accessor and their Seated storages went with them.
    // For the two icons that also deleted an `#ifdef TINTA_D2D_BACKEND` returning
    // nullptr off Windows -- the guard existed only because the thing behind it was an
    // ID2D1Bitmap*, so the icons now draw on both stacks instead of on neither.
    tinta::Bitmap* linkPeekBitmap() const {
#ifdef TINTA_D2D_BACKEND
        return linkPeekBitmapStorage.seat<D2DBitmap>(nativeLinkPeekBitmap);
#else
        linkPeekBitmapStorage.reset();
        return nullptr;
#endif
    }

private:
    mutable Seated<tinta::Bitmap, ID2D1Bitmap> linkPeekBitmapStorage;
};

inline float dpi(const App& app, float value) {
    return value * app.contentScale;
}

// Width of the editor pane in edit mode (full window when preview is hidden)
// Desk gap (design 10a): the sliver of desk between the source column
// and the floating render sheet; it doubles as the split drag handle.
// Its center sits at width * editorSplitRatio.
inline float editSeamWidth(const App& app) {
    return dpi(app, 16.0f);
}

inline float editorPaneWidth(const App& app) {
    return app.editorShowPreview
        ? app.width * app.editorSplitRatio - editSeamWidth(app) * 0.5f
        : static_cast<float>(app.width);
}

// Left tool rail (design t8/t11): slides in with edit mode, carries the
// formatting controls
inline float editRailWidth(const App& app) {
    if (!app.editMode) return 0.0f;
    return dpi(app, 48.0f) * app.editRailAnim;
}

// Slim in-editor line-number column right of the rail (design t11)
inline float editorGutterWidth(const App& app) {
    return dpi(app, 34.0f);
}

inline bool editorWrapOn(const App& app) {
    return app.editorWordWrap;
}

// Folder browser panel width — shared by input hit-testing, the panel
// renderer, and the viewport shift
// Title-bar strip height: the custom caption hosting the tab strip. Zero
// in zen mode (borderless fullscreen hides all chrome).
inline float chromeTopHeight(const App& app) {
    if (app.zenMode) return 0.0f;
    return dpi(app, 40.0f);
}

inline float folderBrowserPanelWidth(const App& app) {
    float cap = dpi(app, 300.0f);
    float floor_ = dpi(app, 250.0f);
    float w = app.width * 0.2f;
    if (w < floor_) w = floor_;
    if (w > cap) w = cap;
    return w;
}

// TOC panel width — shared by input hit-testing, the panel renderer, and
// the viewport shift
inline float tocPanelWidth(const App& app) {
    float cap = dpi(app, 280.0f);
    float floor_ = dpi(app, 220.0f);
    float w = app.width * 0.2f;
    if (w < floor_) w = floor_;
    if (w > cap) w = cap;
    return w;
}

inline float documentViewportX(const App& app) {
    if (!app.editMode) {
        // Side panels push the content aside instead of covering it; the
        // shift follows the panel's slide-in animation. A right-docked TOC
        // leaves x at 0 and only narrows the viewport. Both panels can be
        // up at once (#156): left-side occupants add up.
        float x = 0.0f;
        if (app.showFolderBrowser) {
            x += folderBrowserPanelWidth(app) * app.folderBrowserAnimation;
        }
        if (app.showToc && app.tocOnLeft) {
            x += tocPanelWidth(app) * app.tocAnimation;
        }
        return x;
    }
    // Preview hidden: zero-width viewport at the right edge — document
    // rendering flows through unchanged and clips to nothing
    if (!app.editorShowPreview) {
        return static_cast<float>(app.width);
    }
    return app.width * app.editorSplitRatio + editSeamWidth(app) * 0.5f;
}

inline bool editorPreviewVisible(const App& app) {
    return app.editMode && app.editorShowPreview;
}

// Floating render sheet (design 10a): the page lies on the editor's
// desk and rises past the tab strip to the window's top edge — the
// caption buttons float over it as an island. Shadow is the only
// separator.
inline tinta::RectF editSheetRect(const App& app) {
    return tinta::rectF(documentViewportX(app), dpi(app, 10.0f),
                       (float)app.width - dpi(app, 16.0f),
                       (float)app.height - dpi(app, 14.0f));
}

inline tinta::Color editSurfaceMix(tinta::Color c, float to, float t) {
    c.r += (to - c.r) * t;
    c.g += (to - c.g) * t;
    c.b += (to - c.b) * t;
    c.a = 1.0f;
    return c;
}

// The desk both panes sit on: lifted a shade off the window in the dark,
// dimmed a touch in the light so the sheet reads as paper on top
inline tinta::Color editDeskColor(const App& app) {
    return app.theme.isDark
               ? editSurfaceMix(app.theme.background, 1.0f, 0.03f)
               : editSurfaceMix(app.theme.background, 0.0f, 0.045f);
}

inline tinta::Color editSheetColor(const App& app) {
    return app.theme.isDark
               ? editSurfaceMix(app.theme.background, 1.0f, 0.065f)
               : editSurfaceMix(app.theme.background, 1.0f, 0.35f);
}

inline float documentViewportWidth(const App& app) {
    float width;
    if (app.editMode) {
        width = static_cast<float>(app.width) - documentViewportX(app);
        // The floating sheet is inset from the window's right edge
        if (editorPreviewVisible(app)) width -= dpi(app, 16.0f);
    } else {
        width = static_cast<float>(app.width);
        // Snap to the panel's final width (not the animated position) so
        // the reading column relayouts once per toggle, not per frame.
        // Both panels can be up together (#156).
        if (app.showFolderBrowser) width -= folderBrowserPanelWidth(app);
        if (app.showToc) width -= tocPanelWidth(app);
    }
    return width > 0.0f ? width : 0.0f;
}

// Cursor blink runs on a timer instead of per-frame InvalidateRect so the
// app is fully idle between blinks. Call after editMode/search state changes.
inline void updateBlinkTimer(App& app) {
    if (!app.hwnd) return;
    if (app.editMode || (app.showSearch && app.searchActive) ||
        (app.showFolderBrowser &&
         (app.folderBrowserEditingPath || app.folderBrowserNaming != 0))) {
        app.cursorBlinkOn = true;
        armTimer(app.hwnd, TIMER_CURSOR_BLINK, 500);
    } else {
        disarmTimer(app.hwnd, TIMER_CURSOR_BLINK);
        app.cursorBlinkOn = true;
    }
}

// Restart the blink phase so the cursor stays visible while typing
inline void resetCursorBlink(App& app) {
    app.cursorBlinkOn = true;
    if (app.hwnd) armTimer(app.hwnd, TIMER_CURSOR_BLINK, 500);
}

// Notification fades repaint on this timer; the WM_TIMER handler kills it
// once no notification is active.
inline void startNotificationTimer(App& app) {
    if (app.hwnd) armTimer(app.hwnd, TIMER_NOTIFICATION, 33);
}

#endif // TINTA_APP_H
