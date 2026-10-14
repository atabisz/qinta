// Left tool rail (design t8/t11): a 48dp column that slides in with edit
// mode carrying the formatting controls grouped text / blocks / insert.
// Line numbers live in the editor's own slim gutter beside it, and the
// thread seam (also here) ties source blocks to their render.

#include "editrail.h"
#include "overlays.h"
#include "editor.h"
#include "i18n.h"
#include "pandoc.h"
#include "startpage.h"
#include "tabs.h"
#include "portable_str.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>

// --- Small helpers (same one-shot layout pattern as the start page) ----

static tinta::Color railMix(tinta::Color base, tinta::Color to, float t) {
    tinta::Color c;
    c.r = base.r + (to.r - base.r) * t;
    c.g = base.g + (to.g - base.g) * t;
    c.b = base.b + (to.b - base.b) * t;
    c.a = 1.0f;
    return c;
}

static tinta::Color railA(tinta::Color c, float a) {
    c.a = a;
    return c;
}

// This file was chosen for the layout-factory conversion on a measurement, not
// on how easy it looked: it named DWRITE_ 53 times while already running 34 of
// its 35 drawing calls through app.renderTarget, so nearly all of that
// remaining DirectWrite surface was ONE factory's signature leaking two enum
// types into a struct, four static tables and thirteen call sites.
//
// The return type carries the ownership convention now. It used to be a raw
// IDWriteTextLayout* that every caller had to remember to Release -- the same
// spelling the editor's CACHED factory uses for a pointer callers must NOT
// release, with only a comment telling the two apart. unique_ptr and a raw
// borrow are different types, so the distinction stops depending on a comment.
static std::unique_ptr<tinta::TextLayout> railLayout(
        App& app, const std::wstring& text, float sizeDip,
        tinta::FontWeight weight, const wchar_t* family = kDefaultFont,
        tinta::FontStyle style = tinta::FontStyle::Normal) {
    std::unique_ptr<tinta::TextFormat> fmt = app.textFactory->CreateTextFormat(
        family, weight, style, tinta::FontStretch::Normal, dpi(app, sizeDip),
        L"en-us");
    if (!fmt) return nullptr;
    // Still set on the FORMAT and not the layout, as before. Every rail label
    // is a single unwrapped line, so the property belongs to the format the
    // layout is about to be built from.
    fmt->SetWordWrapping(tinta::WordWrapping::NoWrap);
    return app.textFactory->CreateTextLayout(text.c_str(),
                                             (uint32_t)text.size(), fmt.get(),
                                             4096.0f, 200.0f);
}

static void railDraw(App& app, tinta::TextLayout* layout, float x, float y,
                     tinta::Color color) {
    if (!layout) return;
    app.brush->SetColor(color);
    app.renderTarget->DrawTextLayout(tinta::pointF(x, y), layout, app.brush);
}

// Centers a glyph layout in a rect. No longer releases it -- the caller's
// unique_ptr does, which is why this takes a borrowed pointer.
static void railGlyph(App& app, const std::wstring& text, float sizeDip,
                      tinta::FontWeight weight, tinta::FontStyle style,
                      const wchar_t* family, tinta::RectF r,
                      tinta::Color color, bool strike = false) {
    std::unique_ptr<tinta::TextLayout> l =
        railLayout(app, text, sizeDip, weight, family, style);
    if (!l) return;
    if (strike) {
        // This was this file's last native reach-through, on the grounds that
        // per-range styling had no seam method. It has one now, and this is the
        // site that paid for it: the census done to justify leaving it native is
        // what sized the seam, and it corrected its own first answer -- the
        // population of per-range styling calls in the tree is EIGHT, of which
        // SIX are simple value setters like this one and two (SetTypography,
        // SetCharacterSpacing) are not, and stay native by design. The earlier
        // note here said "SIX" of "the whole population", which was the right
        // number for the wrong set.
        l->SetStrikethrough(true, tinta::TextRange{0, (uint32_t)text.size()});
    }
    tinta::TextMetrics m{};
    l->GetMetrics(&m);
    railDraw(app, l.get(), r.left + (r.right - r.left - m.width) * 0.5f,
             r.top + (r.bottom - r.top - m.height) * 0.5f, color);
}

struct RailBtn {
    int id;
    const wchar_t* glyph;
    float size;
    tinta::FontWeight weight;
    tinta::FontStyle style;
    const wchar_t* family;
    bool strike;
    const char* tipKey;      // i18n label
    const wchar_t* tipKeys;  // shortcut shown in the flyout ("" = none)
};

static const RailBtn kTextBtns[] = {
    {1, L"B", 13.0f, tinta::FontWeight::Bold, tinta::FontStyle::Normal,
     kDefaultFont, false, "rail.bold", L"Ctrl+B"},
    {2, L"I", 13.0f, tinta::FontWeight::Normal, tinta::FontStyle::Italic,
     L"Georgia", false, "rail.italic", L"Ctrl+I"},
    {3, L"S", 13.0f, tinta::FontWeight::Normal, tinta::FontStyle::Normal,
     kDefaultFont, true, "rail.strike", L""},
    {4, L"</>", 10.5f, tinta::FontWeight::Normal, tinta::FontStyle::Normal,
     kDefaultCodeFont, false, "rail.code", L""},
    {5, L"\U0001F517", 12.0f, tinta::FontWeight::Normal,
     tinta::FontStyle::Normal, kDefaultFont, false, "rail.link", L"Ctrl+K"},
};
static const RailBtn kBlockBtns[] = {
    {10, L"\u2022\u2261", 12.0f, tinta::FontWeight::Normal,
     tinta::FontStyle::Normal, kDefaultFont, false, "rail.bullets", L""},
    {11, L"\u2611", 13.0f, tinta::FontWeight::Normal,
     tinta::FontStyle::Normal, kDefaultFont, false, "rail.tasks", L""},
    {12, L"\u275D", 13.0f, tinta::FontWeight::Normal,
     tinta::FontStyle::Normal, kDefaultFont, false, "rail.quote", L""},
};
static const RailBtn kInsertBtns[] = {
    {20, L"\u229E", 13.0f, tinta::FontWeight::Normal,
     tinta::FontStyle::Normal, kDefaultFont, false, "rail.table", L""},
    {21, L"\u25C8", 13.0f, tinta::FontWeight::Normal,
     tinta::FontStyle::Normal, kDefaultFont, false, "rail.diagram", L""},
    {22, L"\U0001F5BC", 12.0f, tinta::FontWeight::Normal,
     tinta::FontStyle::Normal, kDefaultFont, false, "rail.image", L""},
};
// Foot: pandoc export, shown only when an executable was found
static const RailBtn kFootBtns[] = {
    {23, L"\u2912", 13.0f, tinta::FontWeight::Normal,
     tinta::FontStyle::Normal, kDefaultFont, false, "rail.export", L""},
};

static const RailBtn* railBtnById(int id) {
    for (const RailBtn& b : kTextBtns) {
        if (b.id == id) return &b;
    }
    for (const RailBtn& b : kBlockBtns) {
        if (b.id == id) return &b;
    }
    for (const RailBtn& b : kInsertBtns) {
        if (b.id == id) return &b;
    }
    for (const RailBtn& b : kFootBtns) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

// --- Rendering ----------------------------------------------------------

void renderEditRail(App& app) {
    app.editRailHits.clear();
    if (!app.editMode || !app.renderTarget || !app.brush ||
        !app.textFactory) {
        return;
    }

    // Slide-in: advance toward 1 and keep painting until it lands
    if (app.editRailAnim < 1.0f) {
        app.editRailAnim = std::min(1.0f, app.editRailAnim + 0.25f);
        requestRepaint();
    }

    const D2DTheme& th = app.theme;
    float railW = dpi(app, 48.0f);
    float slide = railW * (1.0f - app.editRailAnim);
    float H = (float)app.height;

    tinta::Color railBg =
        th.isDark ? railMix(th.background, tinta::colorF(0, 0, 0, 1), 0.45f)
                  : railMix(th.background, tinta::colorF(0, 0, 0, 1), 0.06f);
    tinta::Color text = th.text;
    tinta::Color accent = th.accent;
    float accentLum =
        0.299f * accent.r + 0.587f * accent.g + 0.114f * accent.b;
    tinta::Color onAccent = accentLum > 0.45f
                                ? tinta::colorF(0.05f, 0.08f, 0.15f, 1.0f)
                                : tinta::colorF(1.0f, 0.97f, 0.95f, 1.0f);

    app.renderTarget->PushAxisAlignedClip(
        tinta::rectF(0, 0, railW, H), tinta::AntialiasMode::Aliased);
    tinta::Matrix3x2F prev;
    app.renderTarget->GetTransform(&prev);
    app.renderTarget->SetTransform(tinta::matrixTranslation(-slide, 0) *
                                   prev);

    app.brush->SetColor(railBg);
    app.renderTarget->FillRectangle(tinta::rectF(0, 0, railW, H), app.brush);
    app.brush->SetColor(railA(text, 0.07f));
    app.renderTarget->FillRectangle(
        tinta::rectF(railW - 1.0f, 0, railW, H), app.brush);

    auto addHit = [&](tinta::RectF r, int id) {
        r.left -= slide;
        r.right -= slide;
        app.editRailHits.push_back({r, id});
    };
    auto sep = [&](float y) {
        app.brush->SetColor(railA(text, 0.09f));
        app.renderTarget->FillRectangle(
            tinta::rectF((railW - dpi(app, 20.0f)) * 0.5f, y,
                        (railW + dpi(app, 20.0f)) * 0.5f, y + 1.0f),
            app.brush);
    };

    // Logo also opens the application menu in edit mode (#194).
    renderAppMenuButtonBackground(app);
    float y = dpi(app, 11.0f);
    // THE THIRD CONSUMER, and the one the plan for this slice missed: the rail draws the
    // hero icon at 20 DIP from the same 64 px bitmap startpage.cpp produces. It routes
    // through the one entry point by calling the one producer -- startPageEnsureIcon --
    // rather than by decoding its own, which is what stops a third copy of the ladder
    // appearing at a third size.
    startPageEnsureIcon(app);
    if (app.startPageIconBitmap) {
        float iconSize = dpi(app, 20.0f);
        app.renderTarget->DrawBitmap(
            app.startPageIconBitmap.get(),
            tinta::rectF((railW - iconSize) * 0.5f, y,
                        (railW + iconSize) * 0.5f, y + iconSize));
    }
    y += dpi(app, 20.0f) + dpi(app, 12.0f);
    sep(y);
    y += dpi(app, 9.0f);

    // Tool groups: text style, blocks, insert
    float btn = dpi(app, 32.0f);
    float bx = (railW - btn) * 0.5f;
    auto group = [&](const RailBtn* btns, int count) {
        for (int i = 0; i < count; i++) {
            const RailBtn& b = btns[i];
            tinta::RectF r = tinta::rectF(bx, y, bx + btn, y + btn);
            bool hov = app.editRailHover == b.id;
            if (hov) {
                app.brush->SetColor(railA(text, 0.07f));
                app.renderTarget->FillRoundedRectangle(
                    tinta::roundedRect(r, dpi(app, 7.0f), dpi(app, 7.0f)),
                    app.brush);
            }
            railGlyph(app, b.glyph, b.size, b.weight, b.style, b.family, r,
                      hov ? railA(text, 0.95f) : railA(text, 0.8f),
                      b.strike);
            addHit(r, b.id);
            y += btn + dpi(app, 2.0f);
        }
    };
    group(kTextBtns, 5);
    y += dpi(app, 7.0f);
    sep(y);
    y += dpi(app, 8.0f);
    group(kBlockBtns, 3);
    y += dpi(app, 7.0f);
    sep(y);
    y += dpi(app, 8.0f);
    group(kInsertBtns, 3);

    // Foot: the pandoc export button, pinned to the rail's bottom and
    // present only when an executable is available
    if (pandocAvailable(app)) {
        float fy = H - dpi(app, 12.0f) - btn;
        sep(fy - dpi(app, 8.0f));
        const RailBtn& b = kFootBtns[0];
        tinta::RectF r = tinta::rectF(bx, fy, bx + btn, fy + btn);
        bool hov = app.editRailHover == b.id;
        if (hov) {
            app.brush->SetColor(railA(text, 0.07f));
            app.renderTarget->FillRoundedRectangle(
                tinta::roundedRect(r, dpi(app, 7.0f), dpi(app, 7.0f)),
                app.brush);
        }
        railGlyph(app, b.glyph, b.size, b.weight, b.style, b.family, r,
                  hov ? railA(text, 0.95f) : railA(text, 0.8f), b.strike);
        addHit(r, b.id);
    }
    (void)onAccent;
    (void)accent;

    app.renderTarget->SetTransform(prev);
    app.renderTarget->PopAxisAlignedClip();

    // Flyout label beside the hovered tool (design: "Bold  Ctrl+B");
    // suppressed while an insert flyout panel is open beside the rail
    const RailBtn* hovBtn = railBtnById(app.editRailHover);
    if (hovBtn && !app.editCtxOpen && app.folderBrowserFormat) {
        tinta::RectF src{};
        for (const auto& hit : app.editRailHits) {
            if (hit.second == hovBtn->id) {
                src = hit.first;
                break;
            }
        }
        std::wstring label = tr(app, hovBtn->tipKey);
        std::unique_ptr<tinta::TextLayout> ll =
            railLayout(app, label, 12.0f, tinta::FontWeight::Normal);
        std::unique_ptr<tinta::TextLayout> kl =
            hovBtn->tipKeys[0]
                ? railLayout(app, hovBtn->tipKeys, 10.5f,
                             tinta::FontWeight::Normal)
                : nullptr;
        if (ll) {
            tinta::TextMetrics lm{}, km{};
            ll->GetMetrics(&lm);
            if (kl) kl->GetMetrics(&km);
            float pad = dpi(app, 12.0f);
            float gap = kl ? dpi(app, 8.0f) : 0.0f;
            float w = pad + lm.width + gap + km.width + pad;
            float h = dpi(app, 30.0f);
            float fx = railW + dpi(app, 10.0f);
            float fy = (src.top + src.bottom - h) * 0.5f;
            tinta::RectF fr = tinta::rectF(fx, fy, fx + w, fy + h);
            tinta::Color fbg = th.isDark
                                   ? railMix(th.background,
                                             tinta::colorF(0, 0, 0, 1), 0.3f)
                                   : railMix(th.background,
                                             tinta::colorF(1, 1, 1, 1), 0.5f);
            fbg.a = 0.96f;
            app.brush->SetColor(fbg);
            app.renderTarget->FillRoundedRectangle(
                tinta::roundedRect(fr, dpi(app, 7.0f), dpi(app, 7.0f)),
                app.brush);
            app.brush->SetColor(railA(text, 0.14f));
            app.renderTarget->DrawRoundedRectangle(
                tinta::roundedRect(fr, dpi(app, 7.0f), dpi(app, 7.0f)),
                app.brush, 1.0f);
            railDraw(app, ll.get(), fx + pad, fy + (h - lm.height) * 0.5f,
                     text);
            if (kl) {
                railDraw(app, kl.get(), fx + pad + lm.width + gap,
                         fy + (h - km.height) * 0.5f, railA(text, 0.45f));
            }
        }
    }
}

// --- Input --------------------------------------------------------------

int editRailHitAt(const App& app, float x, float y) {
    for (const auto& hit : app.editRailHits) {
        if (x >= hit.first.left && x <= hit.first.right &&
            y >= hit.first.top && y <= hit.first.bottom) {
            return hit.second;
        }
    }
    return 0;
}

bool editRailMouseDown(App& app, int x, int y) {
    if (!app.editMode || (float)x >= editRailWidth(app)) return false;
    int hit = editRailHitAt(app, (float)x, (float)y);
    if (hit != 0) {
        editRailInvoke(app, hit);
    }
    return true;  // the rail swallows its column's clicks either way
}

bool editRailMouseMove(App& app, int x, int y) {
    if (!app.editMode) return false;
    int hit = (float)x < editRailWidth(app)
                  ? editRailHitAt(app, (float)x, (float)y)
                  : 0;
    if (hit != app.editRailHover) {
        app.editRailHover = hit;
        requestRepaint();
    }
    return (float)x < editRailWidth(app);
}

// --- Raw editor insert menu (design t9) ----------------------------------
//
// Clipboard verbs on top, then an INSERT section whose right column shows
// the markdown each item drops at the caret. Table and Diagram open
// flyout submenus: a size grid and the mermaid templates.

struct ECtxItem {
    int id;           // 0 = header, -1 = separator-after marker on prev
    const wchar_t* icon;
    const char* label;    // i18n key ("" for header text below)
    const wchar_t* hint;  // right column (mono)
    bool needsSel;
    bool sub;
    bool sepAfter;
};

static const ECtxItem kECtx[] = {
    {100, L"", "ectx.cut", L"Ctrl+X", true, false, false},
    {101, L"", "ectx.copy", L"Ctrl+C", true, false, false},
    {102, L"", "ectx.paste", L"Ctrl+V", false, false, true},
    {0, L"", "ectx.insert", L"", false, false, false},
    {110, L"\u229E", "ectx.table", L"\u25B8", false, true, false},
    {111, L"\u25C8", "ectx.diagram", L"\u25B8", false, true, false},
    {112, L"\U0001F5BC", "ectx.image", L"![](\u2026)", false, false, false},
    {113, L"</>", "ectx.codeblock", L"```", false, false, false},
    {114, L"\U0001F517", "ectx.link", L"[](\u2026)", false, false, false},
    {115, L"\u2015", "ectx.hr", L"---", false, false, false},
};

struct EDiagItem {
    const wchar_t* icon;
    const char* label;
    const wchar_t* hint;
};
static const EDiagItem kEDiag[] = {
    {L"\u2192", "ectx.d.flow", L"flowchart"},
    {L"\u21C4", "ectx.d.seq", L"sequenceDiagram"},
    {L"\u25AD", "ectx.d.class", L"classDiagram"},
    {L"\u25D0", "ectx.d.state", L"stateDiagram"},
    {L"\u25A4", "ectx.d.gantt", L"gantt"},
    {L"\u25D4", "ectx.d.pie", L"pie"},
    {L"\u270E", "ectx.d.empty", L"```mermaid"},
};

void openEditCtxMenu(App& app, float x, float y) {
    editorMoveCaretToPoint(app, (int)x, (int)y);
    app.editCtxOpen = true;
    app.editCtxRailOnly = false;
    app.editCtxX = x;
    app.editCtxY = y;
    app.editCtxHover = 0;
    app.editCtxSub = 0;
    app.editCtxSubHover = 0;
    app.editCtxGridC = 3;
    app.editCtxGridR = 4;
    requestRepaint();
}

// The rail's table / diagram buttons open the insert menu's submenu
// standalone, anchored beside the button; the insert lands at the caret
void openEditRailFlyout(App& app, int sub) {
    if (app.editCtxOpen && app.editCtxRailOnly && app.editCtxSub == sub) {
        closeEditCtxMenu(app);  // same button again toggles it away
        return;
    }
    float anchorY = chromeTopHeight(app) + dpi(app, 60.0f);
    int wantId = sub == 1 ? 20 : sub == 2 ? 21 : 23;
    for (const auto& hit : app.editRailHits) {
        if (hit.second == wantId) {
            anchorY = hit.first.top;
            break;
        }
    }
    app.editCtxOpen = true;
    app.editCtxRailOnly = true;
    app.editCtxX = editRailWidth(app) + dpi(app, 10.0f);
    app.editCtxY = anchorY;
    app.editCtxHover = 0;
    app.editCtxSub = sub;
    app.editCtxSubHover = 0;
    app.editCtxGridC = 3;
    app.editCtxGridR = 4;
    requestRepaint();
}

void closeEditCtxMenu(App& app) {
    if (!app.editCtxOpen) return;
    app.editCtxOpen = false;
    app.editCtxRailOnly = false;
    app.editCtxSub = 0;
    requestRepaint();
}

void renderEditCtxMenu(App& app) {
    app.editCtxHits.clear();
    app.editCtxSubHits.clear();
    // The SECOND guard in this file, and I missed it on the first sweep because I
    // matched one spelling's exact indentation instead of counting the population.
    // Both gates now test app.textFactory, the wrapper these functions actually
    // call through: app.dwriteFactory being alive says nothing about whether the
    // seam was seated, and the whole point of the conversion is that the seam is
    // the only text factory this file names.
    if (!app.editCtxOpen || !app.renderTarget || !app.textFactory) return;

    const D2DTheme& th = app.theme;
    tinta::Color text = th.text;
    tinta::Color accent = th.accent;
    tinta::Color menuBg =
        th.isDark ? railMix(th.background, tinta::colorF(0, 0, 0, 1), 0.25f)
                  : railMix(th.background, tinta::colorF(1, 1, 1, 1), 0.55f);
    menuBg.a = 0.97f;

    float w = dpi(app, 232.0f);
    float rowH = dpi(app, 34.0f);
    float hdH = dpi(app, 24.0f);
    float sepH = dpi(app, 9.0f);
    float padV = dpi(app, 4.0f);

    float totalH = padV * 2.0f;
    for (const ECtxItem& it : kECtx) {
        totalH += it.id == 0 ? hdH : rowH;
        if (it.sepAfter) totalH += sepH;
    }
    float mx, my;
    if (app.editCtxRailOnly) {
        // Rail flyout: no main panel - position so the submenu (which
        // hangs off the main panel's right edge) lands beside the rail
        mx = app.editCtxX - w - dpi(app, 4.0f);
        my = app.editCtxY;
    } else {
        mx = std::min(app.editCtxX, (float)app.width - w - dpi(app, 8.0f));
        my = std::min(app.editCtxY,
                      (float)app.height - totalH - dpi(app, 8.0f));
        my = std::max(my, chromeTopHeight(app) + dpi(app, 4.0f));
    }
    float subAnchorY = my;

    if (!app.editCtxRailOnly) {
    tinta::RectF panel = tinta::rectF(mx, my, mx + w, my + totalH);
    // Soft shadow, then the acrylic-ish panel
    app.brush->SetColor(tinta::colorF(0, 0, 0, 0.30f));
    app.renderTarget->FillRoundedRectangle(
        tinta::roundedRect(tinta::rectF(panel.left + dpi(app, 2.0f),
                                      panel.top + dpi(app, 4.0f),
                                      panel.right + dpi(app, 2.0f),
                                      panel.bottom + dpi(app, 4.0f)),
                          dpi(app, 8.0f), dpi(app, 8.0f)),
        app.brush);
    app.brush->SetColor(menuBg);
    app.renderTarget->FillRoundedRectangle(
        tinta::roundedRect(panel, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush);
    app.brush->SetColor(railA(text, 0.12f));
    app.renderTarget->DrawRoundedRectangle(
        tinta::roundedRect(panel, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush,
        1.0f);

    float cy = my + padV;
    for (const ECtxItem& it : kECtx) {
        if (it.id == 0) {
            std::unique_ptr<tinta::TextLayout> hl =
                railLayout(app, tr(app, it.label), 10.0f,
                           tinta::FontWeight::SemiBold);
            if (hl) {
                tinta::TextMetrics m{};
                hl->GetMetrics(&m);
                railDraw(app, hl.get(), mx + dpi(app, 14.0f),
                         cy + hdH - m.height - dpi(app, 2.0f),
                         railA(text, 0.45f));
            }
            cy += hdH;
            continue;
        }
        bool disabled = it.needsSel && !app.editorHasSelection;
        bool hov = app.editCtxHover == it.id && !disabled;
        bool subOpen = (it.id == 110 && app.editCtxSub == 1) ||
                       (it.id == 111 && app.editCtxSub == 2);
        tinta::RectF r = tinta::rectF(mx + dpi(app, 4.0f), cy,
                                    mx + w - dpi(app, 4.0f), cy + rowH);
        if (hov || subOpen) {
            app.brush->SetColor(subOpen ? railA(accent, 0.16f)
                                        : railA(text, 0.08f));
            app.renderTarget->FillRoundedRectangle(
                tinta::roundedRect(r, dpi(app, 5.0f), dpi(app, 5.0f)),
                app.brush);
        }
        float alpha = disabled ? 0.38f : 1.0f;
        if (it.icon[0]) {
            railGlyph(app, it.icon, it.id == 113 ? 9.5f : 12.0f,
                      tinta::FontWeight::Normal, tinta::FontStyle::Normal,
                      it.id == 113 ? kDefaultCodeFont : kDefaultFont,
                      tinta::rectF(r.left + dpi(app, 6.0f), r.top,
                                  r.left + dpi(app, 28.0f), r.bottom),
                      railA(accent, alpha));
        }
        std::unique_ptr<tinta::TextLayout> ll =
            railLayout(app, tr(app, it.label), 13.0f,
                       tinta::FontWeight::Normal);
        if (ll) {
            tinta::TextMetrics m{};
            ll->GetMetrics(&m);
            railDraw(app, ll.get(), r.left + dpi(app, 34.0f),
                     cy + (rowH - m.height) * 0.5f,
                     subOpen ? railA(accent, 0.95f)
                             : railA(text, 0.92f * alpha));
        }
        if (it.hint[0]) {
            std::unique_ptr<tinta::TextLayout> hl =
                railLayout(app, it.hint, 11.0f, tinta::FontWeight::Normal,
                           kDefaultCodeFont);
            if (hl) {
                tinta::TextMetrics m{};
                hl->GetMetrics(&m);
                railDraw(app, hl.get(), r.right - dpi(app, 10.0f) - m.width,
                         cy + (rowH - m.height) * 0.5f,
                         railA(text, 0.45f * alpha));
            }
        }
        if (!disabled) app.editCtxHits.push_back({r, it.id});
        if (it.id == 110 && app.editCtxSub == 1) subAnchorY = cy;
        if (it.id == 111 && app.editCtxSub == 2) subAnchorY = cy;
        cy += rowH;
        if (it.sepAfter) {
            app.brush->SetColor(railA(text, 0.10f));
            app.renderTarget->FillRectangle(
                tinta::rectF(mx + dpi(app, 12.0f), cy + sepH * 0.5f,
                            mx + w - dpi(app, 12.0f),
                            cy + sepH * 0.5f + 1.0f),
                app.brush);
            cy += sepH;
        }
    }
    }  // !editCtxRailOnly

    // Submenus, anchored beside their parent row
    if (app.editCtxSub == 2) {
        float sw = dpi(app, 240.0f);
        float srowH = dpi(app, 34.0f);
        float sh = dpi(app, 30.0f) + 7 * (srowH + dpi(app, 2.0f)) +
                   dpi(app, 8.0f);
        float sx = std::min(mx + w + dpi(app, 4.0f),
                            (float)app.width - sw - dpi(app, 8.0f));
        float sy = std::min(subAnchorY,
                            (float)app.height - sh - dpi(app, 8.0f));
        tinta::RectF sp = tinta::rectF(sx, sy, sx + sw, sy + sh);
        app.brush->SetColor(menuBg);
        app.renderTarget->FillRoundedRectangle(
            tinta::roundedRect(sp, dpi(app, 8.0f), dpi(app, 8.0f)),
            app.brush);
        app.brush->SetColor(railA(text, 0.12f));
        app.renderTarget->DrawRoundedRectangle(
            tinta::roundedRect(sp, dpi(app, 8.0f), dpi(app, 8.0f)),
            app.brush, 1.0f);
        std::unique_ptr<tinta::TextLayout> hl =
            railLayout(app, tr(app, "ectx.templates"), 10.5f,
                       tinta::FontWeight::SemiBold);
        if (hl) {
            railDraw(app, hl.get(), sx + dpi(app, 14.0f), sy + dpi(app, 8.0f),
                     railA(text, 0.5f));
        }
        float ry = sy + dpi(app, 30.0f);
        for (int i = 0; i < 7; i++) {
            tinta::RectF r = tinta::rectF(sx + dpi(app, 4.0f), ry,
                                        sx + sw - dpi(app, 4.0f),
                                        ry + srowH);
            bool hov = app.editCtxSubHover == 130 + i;
            if (hov) {
                app.brush->SetColor(railA(text, 0.08f));
                app.renderTarget->FillRoundedRectangle(
                    tinta::roundedRect(r, dpi(app, 5.0f), dpi(app, 5.0f)),
                    app.brush);
            }
            railGlyph(app, kEDiag[i].icon, 12.0f,
                      tinta::FontWeight::Normal, tinta::FontStyle::Normal,
                      kDefaultFont,
                      tinta::rectF(r.left + dpi(app, 6.0f), r.top,
                                  r.left + dpi(app, 28.0f), r.bottom),
                      accent);
            std::unique_ptr<tinta::TextLayout> ll =
                railLayout(app, tr(app, kEDiag[i].label), 13.0f,
                           tinta::FontWeight::Normal);
            if (ll) {
                tinta::TextMetrics m{};
                ll->GetMetrics(&m);
                railDraw(app, ll.get(), r.left + dpi(app, 34.0f),
                         ry + (srowH - m.height) * 0.5f,
                         railA(text, 0.92f));
            }
            std::unique_ptr<tinta::TextLayout> hint =
                railLayout(app, kEDiag[i].hint, 10.5f,
                           tinta::FontWeight::Normal, kDefaultCodeFont);
            if (hint) {
                tinta::TextMetrics m{};
                hint->GetMetrics(&m);
                railDraw(app, hint.get(), r.right - dpi(app, 10.0f) - m.width,
                         ry + (srowH - m.height) * 0.5f, railA(text, 0.4f));
            }
            app.editCtxSubHits.push_back({r, 130 + i});
            ry += srowH + dpi(app, 2.0f);
        }
    } else if (app.editCtxSub == 3) {
        // Pandoc export formats (rail flyout only)
        float sw = dpi(app, 220.0f);
        float srowH = dpi(app, 34.0f);
        float sh = dpi(app, 30.0f) +
                   PANDOC_FORMAT_COUNT * (srowH + dpi(app, 2.0f)) +
                   dpi(app, 8.0f);
        float sx = std::min(mx + w + dpi(app, 4.0f),
                            (float)app.width - sw - dpi(app, 8.0f));
        float sy = std::min(subAnchorY,
                            (float)app.height - sh - dpi(app, 8.0f));
        tinta::RectF sp = tinta::rectF(sx, sy, sx + sw, sy + sh);
        app.brush->SetColor(menuBg);
        app.renderTarget->FillRoundedRectangle(
            tinta::roundedRect(sp, dpi(app, 8.0f), dpi(app, 8.0f)),
            app.brush);
        app.brush->SetColor(railA(text, 0.12f));
        app.renderTarget->DrawRoundedRectangle(
            tinta::roundedRect(sp, dpi(app, 8.0f), dpi(app, 8.0f)),
            app.brush, 1.0f);
        std::unique_ptr<tinta::TextLayout> hl =
            railLayout(app, tr(app, "rail.export"), 10.5f,
                       tinta::FontWeight::SemiBold);
        if (hl) {
            railDraw(app, hl.get(), sx + dpi(app, 14.0f), sy + dpi(app, 8.0f),
                     railA(text, 0.5f));
        }
        float ry = sy + dpi(app, 30.0f);
        for (int i = 0; i < PANDOC_FORMAT_COUNT; i++) {
            tinta::RectF r = tinta::rectF(sx + dpi(app, 4.0f), ry,
                                        sx + sw - dpi(app, 4.0f),
                                        ry + srowH);
            bool hov = app.editCtxSubHover == 150 + i;
            if (hov) {
                app.brush->SetColor(railA(text, 0.08f));
                app.renderTarget->FillRoundedRectangle(
                    tinta::roundedRect(r, dpi(app, 5.0f), dpi(app, 5.0f)),
                    app.brush);
            }
            std::unique_ptr<tinta::TextLayout> ll =
                railLayout(app, pandocFormatLabel(i), 13.0f,
                           tinta::FontWeight::Normal);
            if (ll) {
                tinta::TextMetrics m{};
                ll->GetMetrics(&m);
                railDraw(app, ll.get(), r.left + dpi(app, 12.0f),
                         ry + (srowH - m.height) * 0.5f,
                         railA(text, 0.92f));
            }
            std::unique_ptr<tinta::TextLayout> hint =
                railLayout(app, pandocFormatExt(i), 10.5f,
                           tinta::FontWeight::Normal, kDefaultCodeFont);
            if (hint) {
                tinta::TextMetrics m{};
                hint->GetMetrics(&m);
                railDraw(app, hint.get(), r.right - dpi(app, 10.0f) - m.width,
                         ry + (srowH - m.height) * 0.5f, railA(text, 0.4f));
            }
            app.editCtxSubHits.push_back({r, 150 + i});
            ry += srowH + dpi(app, 2.0f);
        }
    } else if (app.editCtxSub == 1) {
        float cell = dpi(app, 20.0f);
        float gap = dpi(app, 3.0f);
        float pad = dpi(app, 12.0f);
        float sw = pad * 2.0f + 8.0f * cell + 7.0f * gap;
        float sh = pad * 2.0f + 6.0f * cell + 5.0f * gap + dpi(app, 24.0f);
        float sx = std::min(mx + w + dpi(app, 4.0f),
                            (float)app.width - sw - dpi(app, 8.0f));
        float sy = std::min(subAnchorY,
                            (float)app.height - sh - dpi(app, 8.0f));
        tinta::RectF sp = tinta::rectF(sx, sy, sx + sw, sy + sh);
        app.brush->SetColor(menuBg);
        app.renderTarget->FillRoundedRectangle(
            tinta::roundedRect(sp, dpi(app, 8.0f), dpi(app, 8.0f)),
            app.brush);
        app.brush->SetColor(railA(text, 0.12f));
        app.renderTarget->DrawRoundedRectangle(
            tinta::roundedRect(sp, dpi(app, 8.0f), dpi(app, 8.0f)),
            app.brush, 1.0f);
        for (int r = 0; r < 6; r++) {
            for (int c = 0; c < 8; c++) {
                bool in = r < app.editCtxGridR && c < app.editCtxGridC;
                tinta::RectF cr = tinta::rectF(
                    sx + pad + c * (cell + gap), sy + pad + r * (cell + gap),
                    sx + pad + c * (cell + gap) + cell,
                    sy + pad + r * (cell + gap) + cell);
                app.brush->SetColor(in ? railA(accent, 0.35f)
                                       : railA(text, 0.05f));
                app.renderTarget->FillRoundedRectangle(
                    tinta::roundedRect(cr, 2.0f, 2.0f), app.brush);
                app.brush->SetColor(in ? railA(accent, 0.6f)
                                       : railA(text, 0.12f));
                app.renderTarget->DrawRoundedRectangle(
                    tinta::roundedRect(cr, 2.0f, 2.0f), app.brush, 1.0f);
            }
        }
        wchar_t sizeLabel[48];
        tinta::swprintf(sizeLabel, std::size(sizeLabel), L"%d \u00D7 %d %ls",
                        app.editCtxGridC, app.editCtxGridR,
                        tr(app, "ectx.table_word"));
        std::unique_ptr<tinta::TextLayout> sl =
            railLayout(app, sizeLabel, 12.0f, tinta::FontWeight::Normal);
        if (sl) {
            tinta::TextMetrics m{};
            sl->GetMetrics(&m);
            railDraw(app, sl.get(), sx + (sw - m.width) * 0.5f,
                     sy + sh - dpi(app, 22.0f), railA(text, 0.9f));
        }
        // The whole grid area is one hit; cell resolution happens on move
        app.editCtxSubHits.push_back(
            {tinta::rectF(sx, sy, sx + sw, sy + sh), 140});
    }
}

bool editCtxMouseDown(App& app, int x, int y) {
    if (!app.editCtxOpen) return false;
    float fx = (float)x, fy = (float)y;
    for (const auto& hit : app.editCtxSubHits) {
        if (fx < hit.first.left || fx > hit.first.right ||
            fy < hit.first.top || fy > hit.first.bottom) {
            continue;
        }
        if (hit.second == 140) {
            editorInsertTableGrid(app, app.editCtxGridC,
                                  app.editCtxGridR);
        } else if (hit.second >= 150) {
            // Close first: the export flow opens a modal save dialog
            int fmt = hit.second - 150;
            closeEditCtxMenu(app);
            pandocExportFlow(app, fmt);
            return true;
        } else if (hit.second >= 130) {
            editorInsertDiagramTemplate(app, hit.second - 130);
        }
        closeEditCtxMenu(app);
        return true;
    }
    for (const auto& hit : app.editCtxHits) {
        if (fx < hit.first.left || fx > hit.first.right ||
            fy < hit.first.top || fy > hit.first.bottom) {
            continue;
        }
        switch (hit.second) {
            case 100: editorClipboardCut(app); break;
            case 101: editorClipboardCopy(app); break;
            case 102: editorClipboardPaste(app); break;
            case 110: app.editCtxSub = 1; requestRepaint(); return true;
            case 111: app.editCtxSub = 2; requestRepaint(); return true;
            case 112: editRailInvoke(app, 22); break;
            case 113:
                editorInsertSnippetPublic(app, L"```\n\n```\n", 4);
                break;
            case 114: editRailInvoke(app, 5); break;
            case 115:
                editorInsertSnippetPublic(app, L"---\n", 4);
                break;
        }
        closeEditCtxMenu(app);
        return true;
    }
    closeEditCtxMenu(app);
    return true;  // the closing click is consumed
}

bool editCtxMouseMove(App& app, int x, int y) {
    if (!app.editCtxOpen) return false;
    float fx = (float)x, fy = (float)y;
    int hover = 0, subHover = 0;
    for (const auto& hit : app.editCtxHits) {
        if (fx >= hit.first.left && fx <= hit.first.right &&
            fy >= hit.first.top && fy <= hit.first.bottom) {
            hover = hit.second;
            break;
        }
    }
    for (const auto& hit : app.editCtxSubHits) {
        if (fx >= hit.first.left && fx <= hit.first.right &&
            fy >= hit.first.top && fy <= hit.first.bottom) {
            subHover = hit.second;
            if (hit.second == 140) {
                // Resolve the hovered grid size
                float cell = dpi(app, 20.0f);
                float gap = dpi(app, 3.0f);
                float pad = dpi(app, 12.0f);
                int c = (int)((fx - hit.first.left - pad) / (cell + gap)) + 1;
                int r = (int)((fy - hit.first.top - pad) / (cell + gap)) + 1;
                app.editCtxGridC = std::max(1, std::min(c, 8));
                app.editCtxGridR = std::max(1, std::min(r, 6));
            }
            break;
        }
    }
    // Hovering a parent row opens its submenu; hovering another row
    // closes it
    if (hover == 110) app.editCtxSub = 1;
    else if (hover == 111) app.editCtxSub = 2;
    else if (hover != 0 && subHover == 0) app.editCtxSub = 0;
    if (hover != app.editCtxHover || subHover != app.editCtxSubHover) {
        app.editCtxHover = hover;
        app.editCtxSubHover = subHover;
        requestRepaint();
    } else if (subHover == 140) {
        requestRepaint();  // live grid highlight
    }
    return true;
}

// --- Floating sheet (design 10a) -----------------------------------------
//
// The render is a sheet of paper lying on the editor's desk: both panes
// share the desk surface, the sheet floats inset from the chrome with a
// soft shadow thrown below it, and the caret's block keeps its wash on
// the page. The desk gap left of the sheet is the split drag handle.

// Index of the scroll anchor (top-level block) containing the caret,
// or -1 when the tables aren't ready
static int seamCaretAnchor(const App& app) {
    if (app.scrollAnchors.empty() || app.editorLineByteOffsets.empty() ||
        app.editorLineStarts.empty()) {
        return -1;
    }
    // Caret position -> line (wchar indices), then the byte offset of the
    // line's END: anchors point at a block's first text node, which sits
    // past the "## " marker, so keying on the line start would resolve a
    // heading's caret to the previous block
    size_t lo = 0, hi = app.editorLineStarts.size();
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (app.editorLineStarts[mid] <= app.editorCursorPos) lo = mid;
        else hi = mid;
    }
    size_t caretLine = std::min(lo, app.editorLineByteOffsets.size() - 1);
    size_t caretByte =
        caretLine + 1 < app.editorLineByteOffsets.size()
            ? app.editorLineByteOffsets[caretLine + 1] - 1
            : SIZE_MAX;
    // Greatest anchor at or before the caret's line start
    size_t alo = 0, ahi = app.scrollAnchors.size();
    while (alo + 1 < ahi) {
        size_t mid = (alo + ahi) / 2;
        if (app.scrollAnchors[mid].sourceOffset <= caretByte) alo = mid;
        else ahi = mid;
    }
    return (int)alo;
}

// Desk fill, sheet shadow, sheet surface and edge - drawn before the
// document content clips into the sheet
void renderEditSheetChrome(App& app) {
    if (!editorPreviewVisible(app) || !app.renderTarget || !app.brush) {
        return;
    }
    float W = (float)app.width;
    float H = (float)app.height;
    tinta::RectF sheet = editSheetRect(app);
    float radius = dpi(app, 10.0f);

    // Desk right of the source column (the editor fills its own side)
    app.brush->SetColor(editDeskColor(app));
    app.renderTarget->FillRectangle(
        tinta::rectF(editorPaneWidth(app), 0, W, H), app.brush);
    // The tab strip's band continues across the top behind the sheet so
    // the chrome reads as one surface - without this the strip stops
    // dead at the source column's edge, which on dark themes (strip far
    // darker than desk) looks cut off
    app.brush->SetColor(tabStripBackground(app));
    app.renderTarget->FillRectangle(
        tinta::rectF(editorPaneWidth(app), 0, W, chromeTopHeight(app)),
        app.brush);

    // Shadow: stacked translucent fills growing outward and sliding down,
    // so the sheet throws its shade below itself
    float grow = dpi(app, 2.2f);
    float drop = dpi(app, 1.1f);
    float ringAlpha = app.theme.isDark ? 0.07f : 0.035f;
    for (int i = 8; i >= 1; i--) {
        tinta::RectF halo = tinta::rectF(
            sheet.left - grow * i, sheet.top - grow * i * 0.3f + drop * i,
            sheet.right + grow * i, sheet.bottom + grow * i + drop * i);
        app.brush->SetColor(tinta::colorF(0, 0, 0, ringAlpha));
        app.renderTarget->FillRoundedRectangle(
            tinta::roundedRect(halo, radius + grow * i, radius + grow * i),
            app.brush);
    }

    // The sheet itself, closed by a hairline edge
    app.brush->SetColor(editSheetColor(app));
    app.renderTarget->FillRoundedRectangle(
        tinta::roundedRect(sheet, radius, radius), app.brush);
    app.brush->SetColor(railA(app.theme.text, 0.08f));
    app.renderTarget->DrawRoundedRectangle(
        tinta::roundedRect(sheet, radius, radius), app.brush, 1.0f);
}

// Soft accent wash behind the caret block's rendered output; drawn inside
// the preview's transform, before the document content
void renderPreviewCaretBlock(App& app, float previewWidth) {
    if (!editorPreviewVisible(app) || !app.renderTarget || !app.brush) {
        return;
    }
    int k = seamCaretAnchor(app);
    if (k < 0) return;
    float blockTop = app.scrollAnchors[k].renderedY - app.scrollY -
                     dpi(app, 4.0f);
    float nextY = k + 1 < (int)app.scrollAnchors.size()
                      ? app.scrollAnchors[k + 1].renderedY
                      : app.contentHeight;
    float blockBottom = nextY - app.scrollY - dpi(app, 8.0f);
    if (blockBottom <= blockTop + dpi(app, 4.0f)) return;
    if (blockBottom < editSheetRect(app).top || blockTop > app.height) return;
    tinta::Color c = app.theme.accent;
    c.a = 0.08f;
    app.brush->SetColor(c);
    // Hug the content column with a small overhang (design t11's wash)
    // rather than spanning the sheet - a full-width band makes indented
    // blocks beside plain ones read as misaligned
    float inset = std::max(dpi(app, 10.0f),
                           app.layoutIndent - dpi(app, 12.0f));
    app.renderTarget->FillRoundedRectangle(
        tinta::roundedRect(tinta::rectF(inset, blockTop,
                                      previewWidth - inset, blockBottom),
                          dpi(app, 5.0f), dpi(app, 5.0f)),
        app.brush);
}
