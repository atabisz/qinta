// Start page (design t7): the quiet launcher an empty Tinta shows
// instead of the tutorial sample document. Recents are the hero, the
// sample document and tutorials become three Learn cards, the whole
// window stays a drop target, and everything on the page carries its
// key so the launcher doubles as a shortcut primer.

#include "startpage.h"
#include "annotations.h"
#include "app_icon_data.h"
#include "document.h"
#include "i18n.h"
#include "image_api.h"
#include "platform_paths.h"
#include "portable_str.h"
#include "portable_time.h"
#include "settings.h"
#include "tabs.h"
#include "text_api.h"
#include "utils.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>

// --- Embedded documents -------------------------------------------------

// The old bare-launch tutorial, now behind the "Sample document" card
// (and still the fallback when a requested file cannot be loaded)
static const char* kSampleDocument = R"(# Welcome to Tinta

**Tinta** is a fast, lightweight Markdown and Mermaid viewer for Windows.

## Getting Started

- **Drag & drop** a `.md` or `.mmd` file onto this window
- Press **B** to browse and open files from a folder
- Or run `tinta.exe readme.md` from the command line
- Press **?** for all available keyboard shortcuts

## Features

- 10 beautiful themes — press **T** to choose
- Native Mermaid flowchart rendering for `.mmd` files
- Edit mode with live preview — press **:**
- Search — press **F**
- Table of contents — press **Tab**
- Text selection and copy
- Syntax highlighting in code blocks for C/C++, C#, Python, JavaScript, Rust, Go, and Bash

## Code Example

```cpp
int main() {
    printf("Hello, World!\n");
    return 0;
}
```

## Keyboard Shortcuts

Press **?** at any time to see all shortcuts.

### Navigation

- **J / K** - Scroll down / up
- **Space / PgDn** - Page down
- **PgUp** - Page up
- **Home / End** - Jump to start / end
- **Ctrl+Scroll** - Zoom in / out

### View

- **F** or **Ctrl+F** - Search
- **Enter** - Next search match
- **B** - Toggle folder browser
- **Tab** - Toggle table of contents
- **T** - Theme chooser
- **S** - Toggle stats

### Editing

- **:** - Enter edit mode
- **Ctrl+S** - Save (in edit mode)
- **ESC ESC** - Exit edit mode

### General

- **Ctrl+A** - Select all
- **Ctrl+C** - Copy selection
- **ESC** - Close overlay / Quit
- **Q** - Quit
)";

static const char* kMermaidTour = R"(# Mermaid diagrams

Tinta renders **22 diagram families natively** from plain text. A diagram
is just a fenced code block with the `mermaid` language tag — edit the
text, save, and the picture follows.

## Flowchart

```mermaid
flowchart LR
    A[Write text] --> B{Looks right?}
    B -->|Yes| C[Ship it]
    B -->|No| D[Tweak and save]
    D --> A
```

## Sequence

```mermaid
sequenceDiagram
    participant You
    participant Tinta
    You->>Tinta: Save the file
    Tinta-->>You: Re-renders instantly
    You->>Tinta: Press : to edit
    Tinta-->>You: Live preview beside the text
```

## Pie

```mermaid
pie title Where the bytes go
    "Rendering" : 62
    "Parsing" : 23
    "Everything else" : 15
```

## Gantt

```mermaid
gantt
    title A tiny plan
    dateFormat YYYY-MM-DD
    section Draft
    Write outline   :done, a, 2026-08-20, 2d
    Fill sections   :active, b, after a, 3d
    section Review
    Read-through    :c, after b, 1d
```

## And more

Class, state, ER, gitGraph, mindmap, timeline, journey, quadrant and XY
charts all render the same way. A file ending in `.mmd` opens straight
as a diagram — no fences needed.
)";

static const char* kMarkdownBasics = R"(# Markdown basics

Everything Tinta renders, in two minutes. Press **:** to open this file
in the editor and watch the preview follow your keystrokes.

## Emphasis

**Bold**, *italic*, ~~strikethrough~~, ==highlight==, `inline code`,
x^2^ superscript and H~2~O subscript. Emoji shortcodes work too: :rocket: :tada:

## Lists

- Bullets with `-`, `*` or `+`
- Press Enter and the list continues by itself
    - Indent a level with Tab

1. Ordered lists number themselves
2. The editor increments on Enter

- [x] Task lists render as real checkboxes
- [ ] Click one in the viewer to flip it on disk

## Table

| Syntax | Renders as |
|---|---|
| `**bold**` | **bold** |
| `*italic*` | *italic* |
| `==mark==` | ==mark== |

## Code

```python
def greet(name):
    return f"Hello, {name}!"
```

## Quotes and alerts

> A plain blockquote.

> [!TIP]
> GitHub-style alerts render with their icon and tint.

## Links

Plain URLs autolink, `[text](url)` works, and so do [[wiki links]] and
bare file references like notes.md — live ones open in a tab, missing
ones offer to create the file.

## Math

Inline $E = mc^2$ and block math render natively.
)";

const char* startPageSampleDoc() { return kSampleDocument; }

// --- Small drawing helpers ----------------------------------------------

static tinta::Color mixToward(tinta::Color base, tinta::Color to, float t) {
    tinta::Color c;
    c.r = base.r + (to.r - base.r) * t;
    c.g = base.g + (to.g - base.g) * t;
    c.b = base.b + (to.b - base.b) * t;
    c.a = 1.0f;
    return c;
}

static tinta::Color withA(tinta::Color c, float a) {
    c.a = a;
    return c;
}

// One-shot layout: exact size/weight, optional tracking (design uses
// letter-spacing on the wordmark and section labels) and ellipsis
// trimming for paths.
//
// The three helpers below are the whole DirectWrite surface of this file: every
// one of the ~19 text sites goes through them, which is why porting this page was
// a matter of converting three functions and then the type names at their callers.
// Ownership moves from "caller releases" to unique_ptr, so the `if (x) x->Release()`
// after every use is gone rather than translated.
//
// ONE CONSEQUENCE OF THAT, STATED RATHER THAN DISCOVERED: the original released the
// wordmark and version layouts before building the tagline, so at most three existed
// at once. Scope-bound ownership keeps the nine function-level ones alive until
// renderStartPage returns (the loop-bodies' still destruct per iteration). That is a
// peak-memory difference of six small text layouts on a page that is redrawn only
// when idle, and it is NOT a behaviour difference -- layouts here are independent,
// none of them holds a device resource the next one needs, and nothing reads a
// layout after its last spDraw. Written down because the deleted `->Release()` lines
// were early on purpose and a reader is entitled to know the earliness was weighed.
static std::unique_ptr<tinta::TextLayout> spLayout(App& app,
                                                   const std::wstring& text,
                                                   float sizeDip,
                                                   tinta::FontWeight weight,
                                                   float maxW, bool wrap = false,
                                                   bool ellipsis = false,
                                                   float trackingDip = 0.0f) {
    std::unique_ptr<tinta::TextFormat> fmt = app.textFactory->CreateTextFormat(
        L"Segoe UI", weight, tinta::FontStyle::Normal, tinta::FontStretch::Normal,
        dpi(app, sizeDip), L"en-us");
    if (!fmt) return nullptr;
    if (!wrap) fmt->SetWordWrapping(tinta::WordWrapping::NoWrap);
    if (ellipsis) {
        // The sign is DESTROYED HERE, immediately after SetTrimming, and that is
        // the original's `sign->Release()` rather than a shortening of it: the
        // Direct2D backend's format AddRefs the native sign inside SetTrimming, and
        // the Qt backend stores only a bool (`hasTrimmingSign`). Neither keeps this
        // pointer, so the scope is correct on both and not merely on the one this
        // was written against.
        std::unique_ptr<tinta::InlineObject> sign =
            app.textFactory->CreateEllipsisTrimmingSign(fmt.get());
        if (sign) {
            tinta::Trimming trim{tinta::TrimmingGranularity::Character, 0, 0};
            fmt->SetTrimming(trim, sign.get());
        }
    }
    std::unique_ptr<tinta::TextLayout> layout = app.textFactory->CreateTextLayout(
        text.c_str(), (uint32_t)text.size(), fmt.get(), std::max(1.0f, maxW),
        dpi(app, 400.0f));
    if (layout && trackingDip != 0.0f) {
        // This one line is why the seam grew a method this slice. It used to be a
        // QueryInterface for IDWriteTextLayout1 and a four-argument call with two
        // literal zeros in it; the QI now lives in text_d2d.cpp and the two zeros
        // are gone because no caller in the tree ever passed anything else. See
        // text_api.h, which had DECLINED this method and says why it changed.
        layout->SetCharacterSpacing(dpi(app, trackingDip),
                                    tinta::TextRange{0, (uint32_t)text.size()});
    }
    return layout;
}

static void spDraw(App& app, tinta::TextLayout* layout, float x, float y,
                   tinta::Color color) {
    if (!layout) return;
    app.brush->SetColor(color);
    // Was app.nativeRenderTarget + app.nativeBrush, for the reason overlays.cpp
    // records at its own converted site: a native layout cannot be handed to the
    // seam's DrawTextLayout, so the two travelled together. Converting the layout
    // converted the paint.
    app.renderTarget->DrawTextLayout(tinta::pointF(x, y), layout, app.brush);
}

static tinta::TextMetrics spMetrics(tinta::TextLayout* layout) {
    tinta::TextMetrics m{};
    if (layout) layout->GetMetrics(&m);
    return m;
}

// Keycap chip ("Ctrl+O", "N", "1"). Draws right-aligned when rightEdge
// is set, returns its width.
static float spChip(App& app, const std::wstring& label, float x,
                    float centerY, tinta::Color bg, tinta::Color fg,
                    bool rightAligned = false,
                    tinta::RectF* outRect = nullptr) {
    std::unique_ptr<tinta::TextLayout> l =
        spLayout(app, label, 11.0f, tinta::FontWeight::Normal, 4096.0f);
    tinta::TextMetrics m = spMetrics(l.get());
    float padX = dpi(app, 7.0f);
    float padY = dpi(app, 2.0f);
    float w = m.width + padX * 2.0f;
    float h = m.height + padY * 2.0f;
    float left = rightAligned ? x - w : x;
    tinta::RectF r = tinta::rectF(left, centerY - h * 0.5f, left + w,
                                centerY + h * 0.5f);
    app.brush->SetColor(bg);
    app.renderTarget->FillRoundedRectangle(
        tinta::roundedRect(r, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush);
    spDraw(app, l.get(), left + padX, r.top + padY, fg);
    if (outRect) *outRect = r;
    return w;
}

// --- Relative timestamps -------------------------------------------------

// The [Recent] timestamps in settings.ini are Windows FILETIME ticks -- 100ns
// intervals since 1601-01-01 UTC -- and that stays true off Windows, because it is
// the FILE FORMAT and not a platform detail. settings.h says so at its own
// declaration of fileTimeTicksNow(), naming this function as the decoder.
static const unsigned long long kSecondTicks = 10000000ULL;

// FILETIME ticks -> time_t, for the two places below that need a calendar breakdown.
//
// SIGNED intermediate deliberately: `when` is unsigned and a settings.ini carrying a
// tick value below the 1970 epoch would wrap catastrophically in unsigned arithmetic
// and land in the year 2500-odd rather than reading as a long-ago date. The subtraction
// happens in long long, so a pre-1970 timestamp becomes the negative time_t it should
// be. Not a hypothetical the old code shared: FileTimeToLocalFileTime handled the whole
// FILETIME range, so an unsigned wrap here would have been a REGRESSION rather than an
// inherited quirk.
static std::time_t ticksToTimeT(unsigned long long ticks) {
    const long long secondsSince1601 =
        static_cast<long long>(ticks / kSecondTicks);
    const long long unixEpochSeconds = static_cast<long long>(
        platform::kUnixEpochInFileTimeTicks / kSecondTicks);
    return static_cast<std::time_t>(secondsSince1601 - unixEpochSeconds);
}

// Days since an arbitrary fixed epoch, from a local calendar date. Only DIFFERENCES of
// two of these are ever used, so the epoch itself does not matter.
//
// THIS REPLACES DIVIDING A LOCAL FILETIME BY 864000000000, and it is the same answer by
// a different route rather than an approximation of it. The old code converted both
// instants to local FILETIME and divided, which yields the local calendar day index;
// this computes that index from the local calendar fields directly. Days-from-civil
// (Howard Hinnant's algorithm, shifting the year to start in March so the leap day
// lands last) is exact for every proleptic Gregorian date, including across year and
// century boundaries -- which matters because tm_yday alone silently gets 31 December
// to 1 January wrong.
static long long civilDays(const std::tm& t) {
    long long y = 1900LL + t.tm_year;
    const unsigned m = static_cast<unsigned>(t.tm_mon) + 1u;
    const unsigned d = static_cast<unsigned>(t.tm_mday);
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097LL + static_cast<long long>(doe) - 719468LL;
}

static std::wstring relativeWhen(App& app, unsigned long long when) {
    const unsigned long long now = fileTimeTicksNow();
    if (when == 0 || when >= now) return tr(app, "start.today");

    unsigned long long diff = now - when;
    if (diff < 90 * kSecondTicks) return tr(app, "start.just_now");
    if (diff < 60 * 60 * kSecondTicks) {
        wchar_t buf[48];
        tinta::swprintf(buf, tr(app, "start.min_ago"),
                        (int)(diff / (60 * kSecondTicks)));
        return buf;
    }

    // Calendar-day distance in local time decides today/yesterday/date
    const std::tm whenLocal = tinta::localTime(ticksToTimeT(when));
    const std::tm nowLocal = tinta::localTime(ticksToTimeT(now));
    const long long dayDiff = civilDays(nowLocal) - civilDays(whenLocal);
    if (dayDiff == 0) return tr(app, "start.today");
    if (dayDiff == 1) return tr(app, "start.yesterday");

    wchar_t buf[48];
#ifdef _WIN32
    // THE WINDOWS ARM IS UNCHANGED, CHARACTER FOR CHARACTER, and that is the point of
    // the #ifdef rather than a shared formatter: GetDateFormatEx renders "MMM d" in the
    // USER'S locale, so a German user sees "Sept. 14" and a Japanese user "9月14日".
    // Nothing in the C standard library reproduces that -- strftime formats in the
    // C/current locale, and this process never calls setlocale.
    SYSTEMTIME st{};
    st.wYear = (WORD)(whenLocal.tm_year + 1900);
    st.wMonth = (WORD)(whenLocal.tm_mon + 1);
    st.wDay = (WORD)whenLocal.tm_mday;
    st.wDayOfWeek = (WORD)whenLocal.tm_wday;
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"MMM d", buf,
                        _countof(buf), nullptr) > 0) {
        return buf;
    }
#else
    // A DIVERGENCE THAT IS REAL AND IS NOT HIDDEN: this is the C locale, so the month
    // abbreviation is always English regardless of the user's system language, where
    // the Windows arm above is localised. Stated rather than papered over, because a
    // reader comparing screenshots deserves to know which half is the limitation.
    // "%b %e" is strftime's abbreviated month plus space-padded day; the padding is
    // then stripped so "Sep  4" reads "Sep 4" as GetDateFormatEx's "MMM d" does.
    char narrow[48];
    if (std::strftime(narrow, sizeof(narrow), "%b %e", &whenLocal) > 0) {
        std::wstring out;
        for (const char* p = narrow; *p; ++p) {
            // ASCII by construction in the C locale, so a widening cast is the whole
            // conversion; a multibyte decoder here would be machinery for a case that
            // cannot arise.
            if (*p == ' ' && !out.empty() && out.back() == L' ') continue;
            out.push_back((wchar_t)(unsigned char)*p);
        }
        return out;
    }
#endif
    return tr(app, "start.today");
}

// --- State ---------------------------------------------------------------

bool startPageActive(const App& app) {
    return app.currentFile.empty() && !app.editMode &&
           !app.startPageEmbeddedOpen;
}

void startPageRefreshRecents(App& app) {
    app.startPageRecents.clear();
    Settings stored = loadSettings();
    for (const auto& recent : stored.recentFiles) {
        app.startPageRecents.push_back({recent.path, recent.when});
    }
}

int startPageHitAt(const App& app, float x, float y) {
    for (const auto& hit : app.startPageHits) {
        if (x >= hit.first.left && x <= hit.first.right &&
            y >= hit.first.top && y <= hit.first.bottom) {
            return hit.second;
        }
    }
    return 0;
}

void startPageOpenEmbedded(App& app, HWND hwnd, int card) {
    const char* content = card == 1   ? kMermaidTour
                          : card == 2 ? kMarkdownBasics
                                      : kSampleDocument;
    const char* titleKey = card == 1   ? "start.learn_mermaid"
                           : card == 2 ? "start.learn_md"
                                       : "start.learn_sample";
    auto result = parseDocument(app.parser, content, std::string_view{});
    if (!result.success) return;
    app.root = result.root;
    app.parseTimeUs = result.parseTimeUs;
    app.sourceText = content;
    annotationsParseSource(app);
    app.currentFile.clear();
    app.scrollY = 0;
    app.scrollX = 0;
    app.targetScrollY = 0;
    app.targetScrollX = 0;
    app.contentHeight = 0;
    app.verticalScrollbarVisible = false;
    app.scrollbarContentHeight = 0.0f;
    app.pendingScrollRestore = -1.0f;
    app.fitBlocks.clear();
    app.layoutDirty = true;
    // The launcher hands over to the document; a later switch back to an
    // empty tab brings it right back
    app.startPageEmbeddedOpen = true;
    tabsInit(app);
    app.tabs[app.activeTab].title = tr(app, titleKey);
    updateWindowTitle(app);
    requestRepaint(hwnd);
}

// --- Rendering -----------------------------------------------------------

// THIS FUNCTION WAS THE LAST WINDOWS-ONLY ARM OF THIS FILE AND IT NO LONGER HAS ONE.
// The paragraph that used to sit here explained why the deferral was honest -- the icon
// was an embedded WIN32 resource reached through LoadImageW, and image_api.h's only
// bitmap-producing entry point took a FILE PATH, so there was nothing portable to call.
// That premise has expired: the bytes are embedded once for both backends
// (app_icon_data.h) and decoded through one entry point that owns the frame-selection
// policy (image_api.h). What is left is four lines with no conditional compilation in
// them at all, which is why the explanation is a past tense rather than a rationale.
//
// THE ONE THING WORTH KNOWING AT THIS CALL SITE: 64 is a size resources/tinta.ico has no
// frame for. The scaling and the frame choice are deliberately NOT decided here -- doing
// it at the call site would put the policy in two places, since tabs.cpp asks the same
// question at 32 where the frame IS exact.
void startPageEnsureIcon(App& app) {
    if (app.startPageIconBitmap || !app.renderTarget) return;
    app.startPageIconBitmap = tinta::decodeIconBytesToBitmap(
        *app.renderTarget, tinta::kAppIconIco, tinta::kAppIconIcoSize, 64);
}

void renderStartPage(App& app) {
    app.startPageHits.clear();
    if (!app.renderTarget || !app.brush || !app.textFactory) return;
    startPageEnsureIcon(app);

    const D2DTheme& th = app.theme;
    tinta::Color white = tinta::colorF(1.0f, 1.0f, 1.0f, 1.0f);
    tinta::Color text = th.text;
    tinta::Color heading = th.heading;
    tinta::Color accent = th.accent;

    // Theme-derived surfaces: dark themes lift toward the (light) text,
    // light themes toward paper white, mirroring the settings cards
    tinta::Color surface = th.isDark ? mixToward(th.background, text, 0.06f)
                                     : mixToward(th.background, white, 0.60f);
    tinta::Color surfaceBorder = withA(text, th.isDark ? 0.12f : 0.14f);
    tinta::Color card = th.isDark ? mixToward(th.background, text, 0.05f)
                                  : mixToward(th.background, white, 0.55f);
    tinta::Color cardBorder = withA(text, 0.10f);
    tinta::Color chipBg = mixToward(th.background, text, 0.07f);
    tinta::Color chipFg = withA(text, 0.60f);
    tinta::Color hoverBg = mixToward(th.background, text, 0.06f);
    float accentLum =
        0.299f * accent.r + 0.587f * accent.g + 0.114f * accent.b;
    tinta::Color onAccent = accentLum > 0.45f
                                ? tinta::colorF(0.05f, 0.08f, 0.15f, 1.0f)
                                : tinta::colorF(1.0f, 0.97f, 0.95f, 1.0f);

    float viewX = documentViewportX(app);
    float viewW = documentViewportWidth(app);
    float chrome = chromeTopHeight(app);
    float H = (float)app.height;

    float contW = std::min(dpi(app, 820.0f), viewW - dpi(app, 48.0f));
    bool twoCols = contW >= dpi(app, 640.0f);
    float rightW = twoCols ? dpi(app, 280.0f) : 0.0f;
    float colGap = twoCols ? dpi(app, 36.0f) : 0.0f;
    float leftW = contW - rightW - colGap;
    float x0 = (viewW - contW) * 0.5f;
    float y0 = chrome + dpi(app, 64.0f);

    auto addHit = [&](float l, float t, float r, float b, int id) {
        app.startPageHits.push_back(
            {tinta::rectF(l + viewX, t, r + viewX, b), id});
    };

    // --- Hero: icon, wordmark, version, tagline
    std::unique_ptr<tinta::TextLayout> wm =
        spLayout(app, L"Tinta", 38.0f, tinta::FontWeight::ExtraBold, 4096.0f,
                 false, false, -1.0f);
    tinta::TextMetrics wmM = spMetrics(wm.get());
    float iconSize = dpi(app, 34.0f);
    float heroH = std::max(wmM.height, iconSize);
    // The guard is on the handle itself now -- it was the native pointer, then a seam
    // accessor whose #else returned nullptr off Windows, and the condition never changed
    // through either move. What DID change is that this block is now reachable on both
    // stacks rather than only one, so a missing icon here means the decode failed, not
    // that the platform has no path to one.
    if (tinta::Bitmap* heroIcon = app.startPageIconBitmap.get()) {
        float iy = y0 + (heroH - iconSize) * 0.5f;
        app.renderTarget->DrawBitmap(
            heroIcon, tinta::rectF(x0, iy, x0 + iconSize, iy + iconSize));
    }
    float wmX = x0 + iconSize + dpi(app, 14.0f);
    spDraw(app, wm.get(), wmX, y0 + (heroH - wmM.height) * 0.5f, heading);
    wchar_t ver[32];
    tinta::swprintf(ver, L"v%d.%d.%d", TINTA_VERSION_MAJOR,
                    TINTA_VERSION_MINOR, TINTA_VERSION_PATCH);
    std::unique_ptr<tinta::TextLayout> verL =
        spLayout(app, ver, 12.0f, tinta::FontWeight::Normal, 4096.0f);
    tinta::TextMetrics verM = spMetrics(verL.get());
    // Sits on the wordmark baseline, a step to the right
    spDraw(app, verL.get(), wmX + wmM.width + dpi(app, 14.0f),
           y0 + (heroH - wmM.height) * 0.5f + wmM.height - verM.height -
               dpi(app, 6.0f),
           withA(text, 0.42f));

    std::unique_ptr<tinta::TextLayout> tag =
        spLayout(app, tr(app, "start.tagline"), 14.0f,
                 tinta::FontWeight::Normal, leftW);
    tinta::TextMetrics tagM = spMetrics(tag.get());
    float tagY = y0 + heroH + dpi(app, 6.0f);
    spDraw(app, tag.get(), x0, tagY, withA(text, 0.56f));

    // --- Action buttons: Open a file (accent), New document, Browse
    float btnY = tagY + tagM.height + dpi(app, 26.0f);
    float btnH = dpi(app, 38.0f);
    struct Btn {
        const char* label;
        std::wstring key;
        bool primary;
        int id;
    };
    // The chips show the user's actual bindings, not hardcoded defaults
    Btn btns[3] = {
        {"start.open", L"Ctrl+O", true, 1},
        {"start.new", keyLabel(app.keymap[KA_NEWFILE]), false, 2},
        {"start.browse", keyLabel(app.keymap[KA_BROWSE]), false, 3},
    };
    float bx = x0;
    for (const Btn& b : btns) {
        std::unique_ptr<tinta::TextLayout> label = spLayout(
            app, tr(app, b.label), 13.0f,
            b.primary ? tinta::FontWeight::SemiBold : tinta::FontWeight::Normal,
            4096.0f);
        tinta::TextMetrics lm = spMetrics(label.get());
        tinta::TextMetrics km{};
        {
            // Still scoped tightly, because this one was MEASURED AND DISCARDED
            // rather than measured and drawn -- the chip is drawn by spChip below
            // off its own layout. The original's early Release() is the reason the
            // braces are here instead of letting it live to the loop's end.
            std::unique_ptr<tinta::TextLayout> keyL =
                spLayout(app, b.key, 11.0f, tinta::FontWeight::Normal, 4096.0f);
            km = spMetrics(keyL.get());
        }
        float chipW = km.width + dpi(app, 14.0f);
        float w = dpi(app, 14.0f) + lm.width + dpi(app, 9.0f) + chipW +
                  dpi(app, 14.0f);
        tinta::RectF r = tinta::rectF(bx, btnY, bx + w, btnY + btnH);
        tinta::RoundedRect rr =
            tinta::roundedRect(r, dpi(app, 8.0f), dpi(app, 8.0f));
        bool hov = app.startPageHover == b.id;
        if (b.primary) {
            // Soft one-pixel drop like the mock's box-shadow
            tinta::RoundedRect shadow = tinta::roundedRect(
                tinta::rectF(r.left, r.top + dpi(app, 1.5f), r.right,
                            r.bottom + dpi(app, 1.5f)),
                dpi(app, 8.0f), dpi(app, 8.0f));
            app.brush->SetColor(tinta::colorF(0, 0, 0, 0.20f));
            app.renderTarget->FillRoundedRectangle(shadow, app.brush);
            tinta::Color fill = accent;
            if (hov) fill = mixToward(accent, white, 0.08f);
            app.brush->SetColor(fill);
            app.renderTarget->FillRoundedRectangle(rr, app.brush);
        } else {
            tinta::Color fill = surface;
            if (hov) fill = mixToward(surface, text, 0.04f);
            app.brush->SetColor(fill);
            app.renderTarget->FillRoundedRectangle(rr, app.brush);
            app.brush->SetColor(surfaceBorder);
            app.renderTarget->DrawRoundedRectangle(rr, app.brush, 1.0f);
        }
        float tx = bx + dpi(app, 14.0f);
        spDraw(app, label.get(), tx, btnY + (btnH - lm.height) * 0.5f,
               b.primary ? onAccent : text);
        tinta::Color kBg = b.primary ? withA(onAccent, 0.14f) : chipBg;
        tinta::Color kFg = b.primary ? withA(onAccent, 0.75f) : chipFg;
        spChip(app, b.key, tx + lm.width + dpi(app, 9.0f),
               btnY + btnH * 0.5f, kBg, kFg);
        addHit(r.left, r.top, r.right, r.bottom, b.id);
        bx += w + dpi(app, 8.0f);
    }

    // --- RECENT
    float recY = btnY + btnH + dpi(app, 34.0f);
    std::unique_ptr<tinta::TextLayout> recH =
        spLayout(app, tr(app, "start.recent"), 11.0f,
                 tinta::FontWeight::SemiBold, 4096.0f, false, false, 1.5f);
    tinta::TextMetrics recM = spMetrics(recH.get());
    spDraw(app, recH.get(), x0, recY, withA(text, 0.47f));
    if (!app.startPageRecents.empty()) {
        std::unique_ptr<tinta::TextLayout> clearL =
            spLayout(app, tr(app, "start.clear"), 11.5f,
                     tinta::FontWeight::Normal, 4096.0f);
        tinta::TextMetrics cm = spMetrics(clearL.get());
        float cx = x0 + leftW - cm.width;
        bool hov = app.startPageHover == 4;
        spDraw(app, clearL.get(), cx, recY + (recM.height - cm.height) * 0.5f,
               withA(text, hov ? 0.75f : 0.42f));
        addHit(cx - dpi(app, 6.0f), recY - dpi(app, 4.0f), x0 + leftW,
               recY + cm.height + dpi(app, 4.0f), 4);
    }

    float rowY = recY + recM.height + dpi(app, 8.0f);
    float rowH = dpi(app, 40.0f);
    float inset = dpi(app, 12.0f);
    int shown = std::min<int>(5, (int)app.startPageRecents.size());
    if (shown == 0) {
        std::unique_ptr<tinta::TextLayout> empty =
            spLayout(app, tr(app, "start.empty"), 12.5f,
                     tinta::FontWeight::Normal, leftW);
        spDraw(app, empty.get(), x0, rowY + dpi(app, 10.0f),
               withA(text, 0.42f));
    }
    for (int i = 0; i < shown; i++) {
        const App::RecentDoc& rd = app.startPageRecents[i];
        float top = rowY + i * rowH;
        tinta::RectF row =
            tinta::rectF(x0 - inset, top, x0 + leftW + inset, top + rowH);
        bool hov = app.startPageHover == 10 + i;
        if (hov) {
            app.brush->SetColor(hoverBg);
            app.renderTarget->FillRoundedRectangle(
                tinta::roundedRect(row, dpi(app, 7.0f), dpi(app, 7.0f)),
                app.brush);
            app.brush->SetColor(accent);
            app.renderTarget->FillRoundedRectangle(
                tinta::roundedRect(
                    tinta::rectF(row.left, top + dpi(app, 12.0f),
                                row.left + dpi(app, 3.0f),
                                top + dpi(app, 28.0f)),
                    dpi(app, 2.0f), dpi(app, 2.0f)),
                app.brush);
        }
        // Little page glyph
        float gx = x0;
        float gy = top + (rowH - dpi(app, 15.0f)) * 0.5f;
        app.brush->SetColor(withA(text, 0.5f));
        app.renderTarget->DrawRoundedRectangle(
            tinta::roundedRect(tinta::rectF(gx, gy, gx + dpi(app, 12.0f),
                                          gy + dpi(app, 15.0f)),
                              dpi(app, 2.0f), dpi(app, 2.0f)),
            app.brush, 1.0f);

        std::string pathUtf8 = rd.path;
        size_t slash = pathUtf8.find_last_of("/\\");
        std::wstring name = toWide(
            slash == std::string::npos ? pathUtf8
                                       : pathUtf8.substr(slash + 1));
        std::wstring dir = toWide(
            slash == std::string::npos ? std::string()
                                       : pathUtf8.substr(0, slash));

        // Right side first: the digit chip, then the timestamp
        tinta::RectF chipRect{};
        wchar_t digit[2] = {(wchar_t)(L'1' + i), 0};
        float chipW = spChip(app, digit, x0 + leftW, top + rowH * 0.5f,
                             chipBg, chipFg, true, &chipRect);
        std::unique_ptr<tinta::TextLayout> whenL =
            spLayout(app, relativeWhen(app, rd.when), 11.5f,
                     tinta::FontWeight::Normal, 4096.0f);
        tinta::TextMetrics whenM = spMetrics(whenL.get());
        float whenX = x0 + leftW - chipW - dpi(app, 12.0f) - whenM.width;
        spDraw(app, whenL.get(), whenX, top + (rowH - whenM.height) * 0.5f,
               withA(text, 0.42f));

        float nameX = gx + dpi(app, 12.0f) + dpi(app, 12.0f);
        std::unique_ptr<tinta::TextLayout> nameL =
            spLayout(app, name, 13.5f, tinta::FontWeight::Normal,
                     leftW * 0.55f, false, true);
        tinta::TextMetrics nameM = spMetrics(nameL.get());
        spDraw(app, nameL.get(), nameX, top + (rowH - nameM.height) * 0.5f,
               text);

        float pathX = nameX + nameM.width + dpi(app, 12.0f);
        float pathMax = whenX - dpi(app, 12.0f) - pathX;
        if (pathMax > dpi(app, 30.0f) && !dir.empty()) {
            std::unique_ptr<tinta::TextLayout> pathL =
                spLayout(app, dir, 12.0f, tinta::FontWeight::Normal, pathMax,
                         false, true);
            tinta::TextMetrics pm = spMetrics(pathL.get());
            spDraw(app, pathL.get(), pathX, top + (rowH - pm.height) * 0.5f,
                   withA(text, 0.38f));
        }
        addHit(row.left, row.top, row.right, row.bottom, 10 + i);
    }

    // --- LEARN column
    if (twoCols) {
        float x1 = x0 + leftW + colGap;
        float ly = y0 + dpi(app, 118.0f);
        std::unique_ptr<tinta::TextLayout> learnH = spLayout(
            app, tr(app, "start.learn"), 11.0f, tinta::FontWeight::SemiBold,
            4096.0f, false, false, 1.5f);
        tinta::TextMetrics lhM = spMetrics(learnH.get());
        spDraw(app, learnH.get(), x1, ly, withA(text, 0.47f));
        ly += lhM.height + dpi(app, 10.0f);

        struct Card {
            const wchar_t* ic;
            const char* title;
            const char* desc;
        };
        Card cards[3] = {
            {L"\u270E", "start.learn_sample", "start.learn_sample_d"},
            {L"\u25C8", "start.learn_mermaid", "start.learn_mermaid_d"},
            {L"\u2318", "start.learn_md", "start.learn_md_d"},
        };
        for (int i = 0; i < 3; i++) {
            float padX = dpi(app, 16.0f);
            float padY = dpi(app, 14.0f);
            std::unique_ptr<tinta::TextLayout> title =
                spLayout(app, tr(app, cards[i].title), 13.0f,
                         tinta::FontWeight::SemiBold, rightW - padX * 2.0f);
            std::unique_ptr<tinta::TextLayout> desc =
                spLayout(app, tr(app, cards[i].desc), 12.0f,
                         tinta::FontWeight::Normal, rightW - padX * 2.0f, true);
            tinta::TextMetrics tm = spMetrics(title.get());
            tinta::TextMetrics dm = spMetrics(desc.get());
            float cardH = padY + tm.height + dpi(app, 5.0f) + dm.height +
                          padY;
            tinta::RectF r = tinta::rectF(x1, ly, x1 + rightW, ly + cardH);
            tinta::RoundedRect rr =
                tinta::roundedRect(r, dpi(app, 9.0f), dpi(app, 9.0f));
            bool hov = app.startPageHover == 20 + i;
            app.brush->SetColor(hov ? mixToward(card, text, 0.03f) : card);
            app.renderTarget->FillRoundedRectangle(rr, app.brush);
            app.brush->SetColor(hov ? withA(text, 0.20f) : cardBorder);
            app.renderTarget->DrawRoundedRectangle(rr, app.brush, 1.0f);

            std::unique_ptr<tinta::TextLayout> ic =
                spLayout(app, cards[i].ic, 14.0f, tinta::FontWeight::Normal,
                         4096.0f);
            tinta::TextMetrics im = spMetrics(ic.get());
            spDraw(app, ic.get(), x1 + padX,
                   ly + padY + (tm.height - im.height) * 0.5f, accent);
            spDraw(app, title.get(), x1 + padX + im.width + dpi(app, 9.0f),
                   ly + padY, heading);
            spDraw(app, desc.get(), x1 + padX,
                   ly + padY + tm.height + dpi(app, 5.0f), withA(text, 0.52f));
            addHit(r.left, r.top, r.right, r.bottom, 20 + i);
            ly += cardH + dpi(app, 10.0f);
        }

        // All shortcuts ?
        ly += dpi(app, 4.0f);
        std::unique_ptr<tinta::TextLayout> sc =
            spLayout(app, tr(app, "start.shortcuts"), 12.0f,
                     tinta::FontWeight::Normal, 4096.0f);
        tinta::TextMetrics scM = spMetrics(sc.get());
        bool hov = app.startPageHover == 5;
        spDraw(app, sc.get(), x1, ly, withA(text, hov ? 0.75f : 0.45f));
        spChip(app, keyLabel(app.keymap[KA_HELP]),
               x1 + scM.width + dpi(app, 8.0f), ly + scM.height * 0.5f,
               chipBg, chipFg);
        addHit(x1, ly - dpi(app, 4.0f),
               x1 + scM.width + dpi(app, 8.0f) + dpi(app, 24.0f),
               ly + scM.height + dpi(app, 4.0f), 5);
    }

    // --- Drop hint, centered at the bottom
    std::unique_ptr<tinta::TextLayout> drop =
        spLayout(app, tr(app, "start.drop"), 12.0f, tinta::FontWeight::Normal,
                 4096.0f);
    tinta::TextMetrics dm = spMetrics(drop.get());
    float dropY = H - dpi(app, 22.0f) - dm.height;
    float dashW = dpi(app, 26.0f);
    float gap = dpi(app, 10.0f);
    float total = dashW + gap + dm.width + gap + dashW;
    float dx = (viewW - total) * 0.5f;
    float lineY = dropY + dm.height * 0.55f;
    app.brush->SetColor(withA(text, 0.28f));
    for (float sx = 0.0f; sx < dashW; sx += dpi(app, 6.5f)) {
        float e1 = std::min(sx + dpi(app, 3.5f), dashW);
        app.renderTarget->DrawLine(tinta::pointF(dx + sx, lineY),
                                   tinta::pointF(dx + e1, lineY), app.brush,
                                   1.0f);
        float rx = dx + dashW + gap + dm.width + gap;
        app.renderTarget->DrawLine(tinta::pointF(rx + sx, lineY),
                                   tinta::pointF(rx + e1, lineY), app.brush,
                                   1.0f);
    }
    spDraw(app, drop.get(), dx + dashW + gap, dropY, withA(text, 0.42f));
}
