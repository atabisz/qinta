#include "print.h"
#include "render.h"
#include "d2d_init.h"
#include "document.h"
#include "editor.h"
#include "utils.h"

// Named rather than left to arrive through app.h, which embeds the D2D wrappers by
// value and so has always pulled render_d2d.h in transitively. drawDocumentRange now
// takes a tinta::RenderTarget& and the three call sites construct D2DRenderTarget and
// D2DBrush, so this file uses both headers directly and says so.
#include "image_api.h"   // tinta::rasterizeToImage -- the link peek's offscreen raster
#include "render_api.h"
#include "render_d2d.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include <commdlg.h>
#include <documenttarget.h>
#include <shlwapi.h>
#include <wincodec.h>
// <winspool.h> was here and had no caller: swept 2026-09-20, and of the THREE
// dead includes that sweep confirmed it is the one that changes a DECISION
// rather than a line count. (Three of five candidates: the screen proposed five
// and the Windows build refused two of them, which is written up where those two
// live.) This file does not talk to the print spooler API at all --
// no OpenPrinter, no DeviceCapabilities, no DocumentProperties, no EnumPrinters
// -- so anybody sizing the QPrinter port off "print.cpp includes winspool.h"
// was sizing it off nothing. DEVMODEW and DEVNAMES below are real and come from
// windows.h, not from here.
//
// WHAT THIS FILE'S WINDOWS SURFACE ACTUALLY IS, measured the same day rather
// than inferred from the include list: ONE PrintDlgW (plus its PRINTDLGW), SIX
// IPrintDocumentPackage* symbols (the XPS package target), FIVE WIC symbols,
// and ONE SHCreateStreamOnFileEx. The census names commdlg.h as this file's
// cause because it stops at the FIRST unresolvable header -- so the row has
// never said anything about the four headers underneath it, and reading it as
// "print.cpp needs a print dialog" understates the work by the whole XPS half.

#include <algorithm>
#include <cstring>

#include <cmath>
#include <string>
#include <vector>

// --- Printing (#74) ---
//
// No PDF library: the document is re-laid-out at page width in a light
// print palette and each page is replayed into a D2D command list handed
// to ID2D1PrintControl. The Windows print pipeline does the rest — pick
// "Microsoft Print to PDF" and the OS produces the PDF, with font
// embedding (including CJK) handled by the driver.

namespace {

constexpr float kPageMarginDips = 72.0f;  // 0.75 in

// Paper-derived palette with a true white page
D2DTheme printTheme() {
    D2DTheme t = THEMES[0];  // Paper (light)
    t.background = tinta::colorF(1.0f, 1.0f, 1.0f);
    t.codeBackground = hexColor(0xF4F4F4);
    return t;
}

using SavedView = App::PrintSavedView;

// Saves the screen view state and switches to the print palette at 96 DPI,
// zoom 1. layoutAtPrintWidth does the actual re-layout; leavePrintLayout
// restores everything.
void enterPrintLayout(App& app, SavedView& saved) {
    saved.width = app.width;
    saved.height = app.height;
    saved.contentScale = app.contentScale;
    saved.zoomFactor = app.zoomFactor;
    saved.appliedZoomFactor = app.appliedZoomFactor;
    saved.scrollX = app.scrollX;
    saved.scrollY = app.scrollY;
    saved.targetScrollX = app.targetScrollX;
    saved.targetScrollY = app.targetScrollY;
    saved.theme = app.theme;
    saved.editMode = app.editMode;

    app.editMode = false;  // layout width must be the page, not a pane (#81)
    app.contentScale = 1.0f;
    app.zoomFactor = 1.0f;
    app.theme = printTheme();
    updateTextFormats(app);
}

// Blocks wider than the printable area cannot reflow (mermaid diagrams and
// tables have fixed layouts), so they print shrunk to fit the margins. Each
// overflowing primitive seeds a vertical band that grows to swallow every
// primitive it overlaps — the whole diagram or table scales together about
// its own top-left, leaving whitespace below rather than moving later
// content (pagination keeps using the unscaled bounds).
void computeShrinkBands(App& app, float contentW) {
    app.printShrinkBands.clear();
    const float eps = 0.5f;

    struct Item { float top, bottom, right; };
    std::vector<Item> items;
    items.reserve(app.layoutRects.size() + app.layoutLines.size() +
                  app.layoutConnectors.size() + app.layoutShapes.size() +
                  app.layoutBitmaps.size() + app.layoutTextRuns.size());
    for (const auto& r : app.layoutRects)
        items.push_back({r.rect.top, r.rect.bottom, r.rect.right});
    for (const auto& l : app.layoutLines)
        items.push_back({std::min(l.p1.y, l.p2.y), std::max(l.p1.y, l.p2.y),
                         std::max(l.p1.x, l.p2.x)});
    for (const auto& c : app.layoutConnectors)
        items.push_back({c.bounds.top, c.bounds.bottom, c.bounds.right});
    for (const auto& s : app.layoutShapes)
        items.push_back({s.rect.top, s.rect.bottom, s.rect.right});
    for (const auto& b : app.layoutBitmaps)
        items.push_back({b.destRect.top, b.destRect.bottom, b.destRect.right});
    for (const auto& t : app.layoutTextRuns)
        items.push_back({t.bounds.top, t.bounds.bottom, t.bounds.right});

    // Seed bands from overflowing items
    struct Band { float top, bottom, right; };
    std::vector<Band> bands;
    for (const auto& it : items) {
        if (it.right <= contentW + eps) continue;
        bands.push_back({it.top, it.bottom, it.right});
    }
    if (bands.empty()) return;

    // Grow bands over everything they vertically overlap, then merge bands
    // that grew into each other; repeat until stable
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& b : bands) {
            for (const auto& it : items) {
                if (it.bottom <= b.top + eps || it.top >= b.bottom - eps) continue;
                if (it.top < b.top)       { b.top = it.top;       changed = true; }
                if (it.bottom > b.bottom) { b.bottom = it.bottom; changed = true; }
                if (it.right > b.right)   { b.right = it.right;   changed = true; }
            }
        }
        std::sort(bands.begin(), bands.end(),
                  [](const Band& a, const Band& b) { return a.top < b.top; });
        for (size_t i = 0; i + 1 < bands.size();) {
            if (bands[i + 1].top < bands[i].bottom - eps) {
                bands[i].bottom = std::max(bands[i].bottom, bands[i + 1].bottom);
                bands[i].right = std::max(bands[i].right, bands[i + 1].right);
                bands.erase(bands.begin() + i + 1);
                changed = true;
            } else {
                i++;
            }
        }
    }

    for (const auto& b : bands) {
        float scale = std::max(0.1f, contentW / b.right);
        app.printShrinkBands.push_back({b.top, b.bottom, scale});
    }
}

// Re-layouts the document at page-content width and finds the oversized
// blocks that need shrinking
void layoutAtPrintWidth(App& app, float contentWidthDips) {
    app.width = (int)contentWidthDips;
    app.height = 4000;  // layout does not clip vertically; any tall value
    app.layoutDirty = true;
    ensureLayoutComplete(app);
    computeShrinkBands(app, contentWidthDips);
}

void leavePrintLayout(App& app, const SavedView& saved) {
    app.editMode = saved.editMode;
    app.width = saved.width;
    app.height = saved.height;
    app.contentScale = saved.contentScale;
    app.zoomFactor = saved.zoomFactor;
    app.theme = saved.theme;
    updateTextFormats(app);
    app.appliedZoomFactor = saved.appliedZoomFactor;
    app.layoutDirty = true;
    ensureLayoutComplete(app);
    app.scrollX = saved.scrollX;
    app.scrollY = saved.scrollY;
    app.targetScrollX = saved.targetScrollX;
    app.targetScrollY = saved.targetScrollY;
    if (app.hwnd) requestRepaint(app.hwnd);
}

// Page break positions in document Y. Breaks avoid slicing text lines,
// images, and diagram shapes by pulling the cut above any straddling atom;
// an atom taller than most of a page is sliced rather than looping.
std::vector<float> computePageBreaks(const App& app, float pageContentH) {
    std::vector<float> breaks;
    float docEnd = app.contentHeight;
    float y = 0.0f;

    auto pullAbove = [&](float proposed, float pageTop) {
        for (int pass = 0; pass < 4; pass++) {
            float adjusted = proposed;
            auto consider = [&](float top, float bottom) {
                if (top < adjusted && bottom > adjusted && top > pageTop) {
                    adjusted = top;
                }
            };
            for (const auto& line : app.lineBuckets) consider(line.top, line.bottom);
            for (const auto& bmp : app.layoutBitmaps) consider(bmp.destRect.top, bmp.destRect.bottom);
            for (const auto& shape : app.layoutShapes) consider(shape.rect.top, shape.rect.bottom);
            // Shrink bands page-break as a unit: a scaled diagram split
            // across pages would render discontinuously
            for (const auto& band : app.printShrinkBands) consider(band.top, band.bottom);
            if (adjusted == proposed) break;
            proposed = adjusted;
        }
        // Monster atom (taller than 70% of a page): slice it instead
        if (proposed - pageTop < pageContentH * 0.3f) {
            return pageTop + pageContentH;
        }
        return proposed;
    };

    while (y + pageContentH < docEnd) {
        float next = pullAbove(y + pageContentH, y);
        breaks.push_back(next);
        y = next;
    }
    return breaks;  // implicit final page runs to docEnd
}

// Replays the cached layout for the document Y range [yStart, yEnd) into
// the given render target, translated so yStart lands at offsetY. A
// print-path sibling of render()'s document pass — selection, search, and
// hover chrome intentionally excluded.
//
// THIS TAKES A tinta::RenderTarget, AND THAT IS THE WHOLE OF THE CHANGE.
//
// It used to take an ID2D1DeviceContext* and an ID2D1SolidColorBrush*, and four
// comments in this function explained that its drawing had to stay native because
// "the seam has no print target". That was never a property of the seam -- it was a
// property of THIS SIGNATURE. tinta::RenderTarget is an interface, D2DRenderTarget
// wraps an ID2D1RenderTarget* by storing it and nothing else, and
// ID2D1DeviceContext derives from ID2D1RenderTarget -- so the three callers below
// wrap their own device context at zero cost and every call in here becomes portable
// without the drawing changing at all.
//
// The coupling that left was invisible to an identifier grep: the file's live
// Direct2D count barely moves, because the ~60 calls this function makes were spelled
// `dc->` and `brush->` and only the parameter types and the D2D1:: matrix helpers
// were ever countable. 220 lines went from unportable to portable for a delta of 16.
//
// The sibling pass in src/paint_document.cpp supplied every spelling used here, and
// deliberately was NOT called instead: it draws through app.renderTarget with
// app.scrollX/scrollY offsets, so sharing it would mean parameterising the on-screen
// paint pass in the same commit that claims the printed page is byte-identical.
void drawDocumentRange(App& app, tinta::RenderTarget& target,
                       tinta::Brush* brush,
                       float yStart, float yEnd,
                       float offsetX, float offsetY) {
    auto inRange = [&](float top, float bottom) {
        return bottom >= yStart && top < yEnd;
    };
    float dx = offsetX;
    float dy = offsetY - yStart;

    // Oversized blocks draw scaled about their band's top-left; the band
    // transform composes with whatever transform the caller set (the
    // preview rasterizer scales the whole page to display resolution)
    tinta::Matrix3x2F baseTransform;
    target.GetTransform(&baseTransform);
    auto applyBand = [&](float top, float bottom) {
        for (const auto& band : app.printShrinkBands) {
            if (top >= band.top - 0.5f && bottom <= band.bottom + 0.5f) {
                target.SetTransform(
                    tinta::matrixScale(band.scale, band.scale,
                        tinta::pointF(dx, band.top + dy)) * baseTransform);
                return true;
            }
        }
        return false;
    };
    auto resetBand = [&](bool wasBanded) {
        if (wasBanded) target.SetTransform(baseTransform);
    };

    for (const auto& rect : app.layoutRects) {
        if (!inRange(rect.rect.top, rect.rect.bottom)) continue;
        bool banded = applyBand(rect.rect.top, rect.rect.bottom);
        brush->SetColor(rect.color);
        target.FillRectangle(
            tinta::rectF(rect.rect.left + dx, rect.rect.top + dy,
                        rect.rect.right + dx, rect.rect.bottom + dy),
            brush);
        resetBand(banded);
    }

    for (const auto& line : app.layoutLines) {
        float top = std::min(line.p1.y, line.p2.y);
        float bottom = std::max(line.p1.y, line.p2.y);
        if (!inRange(top, bottom)) continue;
        bool banded = applyBand(top, bottom);
        brush->SetColor(line.color);
        target.DrawLine(tinta::pointF(line.p1.x + dx, line.p1.y + dy),
                        tinta::pointF(line.p2.x + dx, line.p2.y + dy),
                        brush, line.stroke);
        resetBand(banded);
    }

    // Mermaid connectors (with arrowheads and dashing)
    for (const auto& connector : app.layoutConnectors) {
        if (!inRange(connector.bounds.top, connector.bounds.bottom)) continue;
        if (connector.points.size() < 2) continue;
        bool banded = applyBand(connector.bounds.top, connector.bounds.bottom);
        brush->SetColor(connector.color);
        for (size_t i = 1; i < connector.points.size(); i++) {
            const auto& from = connector.points[i - 1];
            const auto& to = connector.points[i];
            // THERE IS ONLY ONE SPELLING TO READ NOW. This note used to say
            // "app.dashedStrokeStyle, not app.nativeDashedStrokeStyle, and the two are
            // null or non-null TOGETHER -- seatDashedStrokeStyle sets both, so this is the
            // same read rather than a new one." Both of those members are gone: App holds
            // one owning unique_ptr<tinta::StrokeStyle>, so there is no second member to
            // pick the wrong one of, and the sentence explaining which to pick has nothing
            // left to explain.
            //
            // WHAT IS DELIBERATELY NOT FIXED HERE: this reads the style without
            // calling ensureDashedStrokeStyle(app), so a document whose dashed
            // connectors have never been painted on screen prints them SOLID. That is
            // pre-existing, and creating the style here would change what comes out of
            // the printer in the commit that claims the pages are byte-identical.
            // Recorded rather than repaired.
            target.DrawLine(tinta::pointF(from.x + dx, from.y + dy),
                            tinta::pointF(to.x + dx, to.y + dy),
                            brush, connector.stroke,
                            connector.dashed ? app.dashedStrokeStyle.get() : nullptr);
        }
        if (connector.directed) {
            const auto& tip = connector.points.back();
            const auto& prev = connector.points[connector.points.size() - 2];
            float vx = tip.x - prev.x, vy = tip.y - prev.y;
            float len = std::sqrt(vx * vx + vy * vy);
            if (len > 0.001f) {
                vx /= len; vy /= len;
                float wing = connector.arrowSize * 0.5f;
                tinta::PointF left = tinta::pointF(
                    tip.x - vx * connector.arrowSize + vy * wing,
                    tip.y - vy * connector.arrowSize - vx * wing);
                tinta::PointF right = tinta::pointF(
                    tip.x - vx * connector.arrowSize - vy * wing,
                    tip.y - vy * connector.arrowSize + vx * wing);
                tinta::PointF screenTip = tinta::pointF(tip.x + dx, tip.y + dy);
                target.DrawLine(screenTip, tinta::pointF(left.x + dx, left.y + dy),
                                brush, connector.stroke);
                target.DrawLine(screenTip, tinta::pointF(right.x + dx, right.y + dy),
                                brush, connector.stroke);
            }
        }
        resetBand(banded);
    }

    // Mermaid node shapes
    //
    // ON THE PATH SEAM NOW. This used to build through app.d2dFactory directly, with a
    // comment calling that "a limit of the seam rather than an oversight" because
    // BeginPath lives on tinta::RenderTarget and this function had no target to call it
    // on. It has one, so the reason is gone and the hand-rolled builder with it.
    //
    // The two enum arguments the old code passed are absent because the seam does not
    // offer them: render_api.h measured BeginFigure as FILLED at 7 of 7 sites and
    // EndFigure as CLOSED at 7 of 7, so the values are fixed rather than defaulted.
    // These two calls were part of that census -- the shapes drawn are the same ones.
    //
    // AddLines keeps the array form rather than becoming a loop. It is not a style
    // choice: this site is one of the two the seam carries AddLines FOR, and passing
    // the run whole is what tinta::asD2DPointArray used to guarantee here.
    auto fillStrokePolygon = [&](const tinta::PointF* pts, size_t count,
                                 const App::LayoutShape& shape) {
        std::unique_ptr<tinta::PathSink> sink = target.BeginPath();
        if (!sink) return;
        sink->BeginFigure(pts[0]);
        sink->AddLines(pts + 1, static_cast<uint32_t>(count - 1));
        sink->EndFigure();
        std::unique_ptr<tinta::PathGeometry> geometry = sink->Close();
        if (!geometry) return;
        if (shape.fill.a > 0.0f) {
            brush->SetColor(shape.fill);
            target.FillGeometry(geometry.get(), brush);
        }
        if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
            brush->SetColor(shape.stroke);
            target.DrawGeometry(geometry.get(), brush, shape.strokeWidth);
        }
    };

    for (const auto& shape : app.layoutShapes) {
        if (!inRange(shape.rect.top, shape.rect.bottom)) continue;
        bool banded = applyBand(shape.rect.top, shape.rect.bottom);
        tinta::RectF r = tinta::rectF(shape.rect.left + dx, shape.rect.top + dy,
                                    shape.rect.right + dx, shape.rect.bottom + dy);
        if (shape.type == App::LayoutShapeType::Path) {
            if (shape.geometry) {
                // The cached path is a seam handle and it is now drawn AS ONE. This
                // used to call nativePathGeometry() to unwrap it, because a raw device
                // context cannot take a tinta::PathGeometry -- the unwrapper WAS exported
                // from render_d2d.h for exactly this call, and this was its last caller
                // outside the backend. It is no longer exported at all: being the last
                // caller is what let it go file-local, so this comment is the reason that
                // slice was possible rather than a note about a call site.
                tinta::Matrix3x2F previous;
                target.GetTransform(&previous);
                target.SetTransform(
                    tinta::matrixTranslation(r.left, r.top) * previous);
                if (shape.fill.a > 0.0f) {
                    brush->SetColor(shape.fill);
                    target.FillGeometry(shape.geometry.get(), brush);
                }
                if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                    brush->SetColor(shape.stroke);
                    target.DrawGeometry(shape.geometry.get(), brush, shape.strokeWidth);
                }
                target.SetTransform(previous);
            }
        } else if (shape.type == App::LayoutShapeType::Diamond) {
            float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
            tinta::PointF pts[] = {{cx, r.top}, {r.right, cy}, {cx, r.bottom}, {r.left, cy}};
            fillStrokePolygon(pts, 4, shape);
        } else if (shape.type == App::LayoutShapeType::Hexagon) {
            float inset = (r.right - r.left) * 0.18f;
            float cy = (r.top + r.bottom) * 0.5f;
            tinta::PointF pts[] = {{r.left + inset, r.top}, {r.right - inset, r.top},
                                   {r.right, cy}, {r.right - inset, r.bottom},
                                   {r.left + inset, r.bottom}, {r.left, cy}};
            fillStrokePolygon(pts, 6, shape);
        } else if (shape.type == App::LayoutShapeType::Ellipse) {
            tinta::Ellipse e = tinta::ellipse(
                tinta::pointF((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f),
                (r.right - r.left) * 0.5f, (r.bottom - r.top) * 0.5f);
            if (shape.fill.a > 0.0f) { brush->SetColor(shape.fill); target.FillEllipse(e, brush); }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                brush->SetColor(shape.stroke);
                target.DrawEllipse(e, brush, shape.strokeWidth);
            }
        } else if (shape.type == App::LayoutShapeType::RoundedRectangle ||
                   shape.type == App::LayoutShapeType::Stadium) {
            float radius = shape.type == App::LayoutShapeType::Stadium
                ? (r.bottom - r.top) * 0.5f : shape.radius;
            tinta::RoundedRect rr = tinta::roundedRect(r, radius, radius);
            if (shape.fill.a > 0.0f) { brush->SetColor(shape.fill); target.FillRoundedRectangle(rr, brush); }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                brush->SetColor(shape.stroke);
                target.DrawRoundedRectangle(rr, brush, shape.strokeWidth);
            }
        } else {
            if (shape.fill.a > 0.0f) { brush->SetColor(shape.fill); target.FillRectangle(r, brush); }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                brush->SetColor(shape.stroke);
                target.DrawRectangle(r, brush, shape.strokeWidth);
            }
        }
        resetBand(banded);
    }

    // Images
    for (const auto& bmp : app.layoutBitmaps) {
        if (!bmp.bitmap) continue;
        if (!inRange(bmp.destRect.top, bmp.destRect.bottom)) continue;
        bool banded = applyBand(bmp.destRect.top, bmp.destRect.bottom);
        // The seam handle goes straight in; this used to unwrap it with
        // nativeBitmap() because `dc` was a raw device context.
        //
        // TWO ARGUMENTS, WHERE THE OLD CALL PASSED FOUR, and the dropped pair is not a
        // behaviour change: the old call passed 1.0f and
        // D2D1_BITMAP_INTERPOLATION_MODE_LINEAR explicitly, and those are precisely the
        // seam's defaults -- chosen to be Direct2D's own so that a 2-argument call means
        // opacity 1 and linear filtering on either backend (render_api.h says so where
        // DrawBitmap is declared, and render_d2d.cpp asserts the enum values match).
        target.DrawBitmap(bmp.bitmap.get(),
            tinta::rectF(bmp.destRect.left + dx, bmp.destRect.top + dy,
                        bmp.destRect.right + dx, bmp.destRect.bottom + dy));
        resetBand(banded);
    }

    // Text
    for (const auto& run : app.layoutTextRuns) {
        if (!run.layout) continue;
        if (!inRange(run.bounds.top, run.bounds.bottom)) continue;
        bool banded = applyBand(run.bounds.top, run.bounds.bottom);
        brush->SetColor(run.color);
        // On the seam. The comment here used to say "the seam has no print target and
        // will not until this file moves to QPrinter" -- the target arrived without
        // QPrinter, because it was the parameter type and not the printing backend that
        // was in the way.
        //
        // NO options argument, matching the old call, which means the printed page has
        // no colour-emoji pass. src/paint_document.cpp's on-screen sibling DOES pass
        // EnableColorFont and this deliberately still does not: adding it would change what
        // prints. The sibling no longer does it "through app.deviceContext" as this said
        // until 2026-09-19 -- it passes the option through the seam like any other draw, so
        // the difference between the two sites is now one argument and nothing else, which
        // is the whole reason it is worth a comment.
        //
        // On Direct2D that argument is what decides it: measured spread 0 without the bit
        // against 214 with it (tests/render_api_shared.cpp). On Qt it would be no change at
        // all, because that backend paints colour glyphs either way -- so if printing ever
        // moves to QPrinter, this omission stops being load-bearing and the printed page
        // gains colour emoji whether or not anyone edits this line.
        target.DrawTextLayout(tinta::pointF(run.pos.x + dx, run.pos.y + dy),
                              run.layout.get(), brush);
        resetBand(banded);
    }
}

struct PageGeometry {
    float pageW, pageH;          // full page in DIPs
    float contentW, contentH;    // inside the margins
};

// Paper size for previewing before the dialog runs, decided by locale only.
// Probing the default printer here (CreateDC) initializes third-party
// drivers and can pop their dialogs (Acrobat Distiller's broken-temp-folder
// error, for one) just to read a paper size — the real printer is consulted
// after the user confirms the print dialog, and re-pagination handles any
// difference.
PageGeometry defaultPageGeometry() {
    PageGeometry geo{794.0f, 1123.0f, 0.0f, 0.0f};  // A4
    wchar_t measure[2] = L"0";
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IMEASURE, measure, 2) &&
        measure[0] == L'1') {
        geo.pageW = 816.0f;   // US customary: Letter
        geo.pageH = 1056.0f;
    }
    geo.contentW = geo.pageW - kPageMarginDips * 2;
    geo.contentH = geo.pageH - kPageMarginDips * 2;
    return geo;
}

// Rasterizes one page of the active print layout into the preview pixel
// buffer at display resolution (page DIPs scaled by printPreviewFit), so
// the preview stays crisp instead of downscaling a 96-DPI rendering.
bool rasterizePreviewPage(App& app, int page) {
    int pageCount = (int)app.printPreviewBounds.size() - 1;
    if (page < 0 || page >= pageCount || !app.deviceContext) return false;
    UINT pxW = app.printPreviewPxW, pxH = app.printPreviewPxH;
    if (pxW == 0 || pxH == 0) return false;

    ID2D1Device* device = nullptr;
    app.deviceContext->GetDevice(&device);
    if (!device) return false;
    ID2D1DeviceContext* dc = nullptr;
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
    device->Release();
    if (!dc) return false;

    D2D1_BITMAP_PROPERTIES1 targetProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    D2D1_BITMAP_PROPERTIES1 stagingProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ID2D1Bitmap1* target = nullptr;
    ID2D1Bitmap1* staging = nullptr;
    dc->CreateBitmap(D2D1::SizeU(pxW, pxH), nullptr, 0, &targetProps, &target);
    dc->CreateBitmap(D2D1::SizeU(pxW, pxH), nullptr, 0, &stagingProps, &staging);

    bool ok = false;
    if (target && staging) {
        dc->SetTarget(target);
        dc->BeginDraw();
        dc->SetTransform(D2D1::Matrix3x2F::Scale(app.printPreviewFit, app.printPreviewFit));
        dc->Clear(tinta::colorF(1.0f, 1.0f, 1.0f));
        ID2D1SolidColorBrush* brush = nullptr;
        dc->CreateSolidColorBrush(tinta::colorF(0, 0, 0), &brush);
        if (brush) {
            // THE WRAP IS FREE AND IT BORROWS. D2DRenderTarget's constructor stores an
            // ID2D1RenderTarget* and does nothing else -- no AddRef, no BeginDraw -- and
            // ID2D1DeviceContext derives from ID2D1RenderTarget, so the print-specific
            // context goes in as-is. D2DBrush is the same shape. Both must not outlive
            // what they wrap, which is why they are locals inside the scope that owns
            // the brush, and the Release below is still this function's job.
            D2DRenderTarget seamTarget(dc);
            D2DBrush seamBrush(brush);
            drawDocumentRange(app, seamTarget, &seamBrush,
                              app.printPreviewBounds[page],
                              app.printPreviewBounds[page + 1],
                              kPageMarginDips, kPageMarginDips);
            brush->Release();
        }
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
        if (SUCCEEDED(dc->EndDraw())) {
            D2D1_POINT_2U zero{0, 0};
            D2D1_RECT_U all{0, 0, pxW, pxH};
            D2D1_MAPPED_RECT mapped{};
            if (SUCCEEDED(staging->CopyFromBitmap(&zero, target, &all)) &&
                SUCCEEDED(staging->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
                app.printPreviewPixels.resize((size_t)pxW * pxH * 4);
                for (UINT row = 0; row < pxH; row++) {
                    memcpy(app.printPreviewPixels.data() + (size_t)row * pxW * 4,
                           mapped.bits + (size_t)row * mapped.pitch,
                           (size_t)pxW * 4);
                }
                staging->Unmap();
                ok = true;
            }
        }
    }
    if (staging) staging->Release();
    if (target) target->Release();
    dc->Release();
    return ok;
}

// Runs layout + pagination + per-page drawing. emitPage receives a closed
// command list per page. Returns page count, or -1 on failure.
template <typename EmitFn>
int renderPages(App& app, const PageGeometry& geo, EmitFn emitPage) {
    if (!app.deviceContext || !app.root) return -1;

    ID2D1Device* device = nullptr;
    app.deviceContext->GetDevice(&device);
    if (!device) return -1;

    SavedView saved{};
    enterPrintLayout(app, saved);
    layoutAtPrintWidth(app, geo.contentW);

    std::vector<float> breaks = computePageBreaks(app, geo.contentH);
    breaks.push_back(app.contentHeight + 1.0f);  // final page sentinel

    ID2D1DeviceContext* pageDc = nullptr;
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &pageDc);
    int pages = 0;
    bool ok = pageDc != nullptr;

    float yStart = 0.0f;
    for (size_t i = 0; ok && i < breaks.size(); i++) {
        float yEnd = breaks[i];
        ID2D1CommandList* commandList = nullptr;
        if (FAILED(pageDc->CreateCommandList(&commandList)) || !commandList) {
            ok = false;
            break;
        }
        pageDc->SetTarget(commandList);
        pageDc->BeginDraw();
        pageDc->Clear(tinta::colorF(1.0f, 1.0f, 1.0f));
        ID2D1SolidColorBrush* brush = nullptr;
        pageDc->CreateSolidColorBrush(tinta::colorF(0, 0, 0), &brush);
        if (brush) {
            // Borrowed wraps over the page context and its scratch brush; see the
            // preview rasterizer above. Note what is being wrapped: pageDc's target is
            // an ID2D1CommandList, so the drawing recorded through the seam here is
            // replayed later by ID2D1PrintControl. The seam neither knows nor cares --
            // it holds an ID2D1RenderTarget* and forwards.
            D2DRenderTarget seamTarget(pageDc);
            D2DBrush seamBrush(brush);
            drawDocumentRange(app, seamTarget, &seamBrush, yStart, yEnd,
                              kPageMarginDips, kPageMarginDips);
            brush->Release();
        }
        ok = SUCCEEDED(pageDc->EndDraw()) && SUCCEEDED(commandList->Close());
        if (ok) {
            ok = emitPage(commandList, pages);
            pages++;
        }
        commandList->Release();
        pageDc->SetTarget(nullptr);
        yStart = yEnd;
    }

    if (pageDc) pageDc->Release();
    device->Release();
    leavePrintLayout(app, saved);
    return ok ? pages : -1;
}

} // namespace

bool printDocument(App& app) {
    // Standard printer picker; PD_RETURNDC so the paper size can be read
    PRINTDLGW pd{};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = app.hwnd;
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;

    // Preset the preview's paper and orientation; whatever the user picks
    // in the dialog still wins (the geometry is re-read from the DC)
    if (app.printPreviewPaper >= 0) {
        pd.hDevMode = GlobalAlloc(GHND, sizeof(DEVMODEW));
        if (pd.hDevMode) {
            auto* dm = (DEVMODEW*)GlobalLock(pd.hDevMode);
            if (dm) {
                dm->dmSize = sizeof(DEVMODEW);
                dm->dmSpecVersion = DM_SPECVERSION;
                dm->dmFields = DM_ORIENTATION | DM_PAPERSIZE;
                dm->dmOrientation = app.printPreviewLandscape
                    ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
                dm->dmPaperSize = PRINT_PAPERS[app.printPreviewPaper].dmPaper;
                GlobalUnlock(pd.hDevMode);
            }
        }
    }

    if (!PrintDlgW(&pd) || !pd.hDC) {
        if (pd.hDevMode) GlobalFree(pd.hDevMode);
        if (pd.hDevNames) GlobalFree(pd.hDevNames);
        return false;  // cancelled
    }

    float dpiX = (float)GetDeviceCaps(pd.hDC, LOGPIXELSX);
    float dpiY = (float)GetDeviceCaps(pd.hDC, LOGPIXELSY);
    PageGeometry geo{};
    geo.pageW = GetDeviceCaps(pd.hDC, PHYSICALWIDTH) * 96.0f / dpiX;
    geo.pageH = GetDeviceCaps(pd.hDC, PHYSICALHEIGHT) * 96.0f / dpiY;
    if (geo.pageW < 200 || geo.pageH < 200) {  // driver returned nonsense
        geo.pageW = 794.0f;   // A4
        geo.pageH = 1123.0f;
    }
    geo.contentW = geo.pageW - kPageMarginDips * 2;
    geo.contentH = geo.pageH - kPageMarginDips * 2;

    // Printer name from the DEVNAMES block
    std::wstring printerName;
    if (pd.hDevNames) {
        auto* names = (DEVNAMES*)GlobalLock(pd.hDevNames);
        if (names) {
            printerName = (wchar_t*)names + names->wDeviceOffset;
            GlobalUnlock(pd.hDevNames);
        }
    }
    DeleteDC(pd.hDC);
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    if (printerName.empty()) return false;

    IPrintDocumentPackageTargetFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(PrintDocumentPackageTargetFactory), nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        return false;
    }
    std::wstring jobName = L"Tinta - " + toWide(app.currentFile);
    IPrintDocumentPackageTarget* target = nullptr;
    HRESULT hr = factory->CreateDocumentPackageTargetForPrintJob(
        printerName.c_str(), jobName.c_str(), nullptr, nullptr, &target);
    factory->Release();
    if (FAILED(hr) || !target) return false;

    ID2D1Device* device = nullptr;
    app.deviceContext->GetDevice(&device);
    ID2D1PrintControl* printControl = nullptr;
    if (device) {
        device->CreatePrintControl(app.wicFactory, target, nullptr, &printControl);
        device->Release();
    }

    bool ok = false;
    if (printControl) {
        int pages = renderPages(app, geo, [&](ID2D1CommandList* list, int) {
            return SUCCEEDED(printControl->AddPage(
                list, tinta::sizeF(geo.pageW, geo.pageH), nullptr));
        });
        ok = pages > 0 && SUCCEEDED(printControl->Close());
        printControl->Release();
    }
    target->Release();
    return ok;
}

// Lays out, paginates, fits and rasterizes for the currently selected
// paper format. Assumes enterPrintLayout has already run.
static void refreshPreviewLayout(App& app) {
    const PrintPaper& paper = PRINT_PAPERS[app.printPreviewPaper];
    float pageW = app.printPreviewLandscape ? paper.h : paper.w;
    float pageH = app.printPreviewLandscape ? paper.w : paper.h;
    app.printPreviewPageW = pageW;
    app.printPreviewPageH = pageH;
    float contentW = pageW - kPageMarginDips * 2;
    float contentH = pageH - kPageMarginDips * 2;

    layoutAtPrintWidth(app, contentW);

    std::vector<float> breaks = computePageBreaks(app, contentH);
    app.printPreviewBounds.clear();
    app.printPreviewBounds.push_back(0.0f);
    for (float b : breaks) app.printPreviewBounds.push_back(b);
    app.printPreviewBounds.push_back(app.contentHeight + 1.0f);

    // Fit the page between the header and the button bar
    float ui = app.printSaved.contentScale;
    float availW = app.printSaved.width - 80.0f * ui;
    float availH = app.printSaved.height - 130.0f * ui;
    app.printPreviewFit = std::max(0.05f,
        std::min(availW / pageW, availH / pageH));
    app.printPreviewPxW = (unsigned)(pageW * app.printPreviewFit);
    app.printPreviewPxH = (unsigned)(pageH * app.printPreviewFit);

    int pageCount = (int)app.printPreviewBounds.size() - 1;
    app.printPreviewPage = std::max(0, std::min(app.printPreviewPage, pageCount - 1));
    rasterizePreviewPage(app, app.printPreviewPage);
}

// First open: start from the default printer's paper, matched to the
// closest offered format
static void detectDefaultFormat(App& app) {
    PageGeometry geo = defaultPageGeometry();
    bool landscape = geo.pageW > geo.pageH;
    float w = landscape ? geo.pageH : geo.pageW;
    float h = landscape ? geo.pageW : geo.pageH;
    int best = 0;
    float bestDiff = 1e9f;
    for (int i = 0; i < PRINT_PAPER_COUNT; i++) {
        float diff = std::fabs(w - PRINT_PAPERS[i].w) + std::fabs(h - PRINT_PAPERS[i].h);
        if (diff < bestDiff) { bestDiff = diff; best = i; }
    }
    app.printPreviewPaper = best;
    app.printPreviewLandscape = landscape;
}

void openPrintPreview(App& app, HWND hwnd) {
    if (app.showPrintPreview || !app.root || !app.deviceContext) return;
    if (app.editMode) {
        editorReparse(app);  // include unsaved edits in the preview and printout
    }
    if (app.printPreviewPaper < 0) detectDefaultFormat(app);

    enterPrintLayout(app, app.printSaved);
    app.printPreviewPage = 0;
    refreshPreviewLayout(app);
    app.showPrintPreview = true;
    requestRepaint(hwnd);
}

void printPreviewSetFormat(App& app, int paper, bool landscape) {
    paper = std::max(0, std::min(paper, PRINT_PAPER_COUNT - 1));
    if (!app.showPrintPreview) {
        app.printPreviewPaper = paper;
        app.printPreviewLandscape = landscape;
        return;
    }
    if (paper == app.printPreviewPaper && landscape == app.printPreviewLandscape) return;
    app.printPreviewPaper = paper;
    app.printPreviewLandscape = landscape;
    refreshPreviewLayout(app);
    if (app.hwnd) requestRepaint(app.hwnd);
}

void closePrintPreview(App& app, HWND hwnd) {
    if (!app.showPrintPreview) return;
    app.showPrintPreview = false;
    leavePrintLayout(app, app.printSaved);
    app.printPreviewPixels.clear();
    app.printPreviewPixels.shrink_to_fit();
    app.printPreviewBounds.clear();
    if (app.editMode) {
        // Editor line layouts were built against the print-layout formats
        app.clearEditorLineLayoutCache();
        app.editorRowMetricsWidth = -1.0f;
    }
    requestRepaint(hwnd);
}

void printPreviewSetPage(App& app, int page) {
    if (!app.showPrintPreview) return;
    int pageCount = (int)app.printPreviewBounds.size() - 1;
    page = std::max(0, std::min(page, pageCount - 1));
    if (page == app.printPreviewPage) return;
    app.printPreviewPage = page;
    rasterizePreviewPage(app, page);
    if (app.hwnd) requestRepaint(app.hwnd);
}

void printPreviewConfirm(App& app, HWND hwnd) {
    if (!app.showPrintPreview) return;
    closePrintPreview(app, hwnd);
    printDocument(app);
}

int printDebugPages(App& app, const std::wstring& outDir) {
    PageGeometry geo{794.0f, 1123.0f, 794.0f - kPageMarginDips * 2,
                     1123.0f - kPageMarginDips * 2};  // A4

    ID2D1Device* device = nullptr;
    app.deviceContext->GetDevice(&device);
    if (!device) return -1;
    ID2D1DeviceContext* rasterDc = nullptr;
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &rasterDc);
    device->Release();
    if (!rasterDc) return -1;

    UINT pw = (UINT)geo.pageW, ph = (UINT)geo.pageH;
    D2D1_BITMAP_PROPERTIES1 targetProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    D2D1_BITMAP_PROPERTIES1 stagingProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ID2D1Bitmap1* pageBitmap = nullptr;
    ID2D1Bitmap1* staging = nullptr;
    rasterDc->CreateBitmap(D2D1::SizeU(pw, ph), nullptr, 0, &targetProps, &pageBitmap);
    rasterDc->CreateBitmap(D2D1::SizeU(pw, ph), nullptr, 0, &stagingProps, &staging);
    if (!pageBitmap || !staging) {
        if (pageBitmap) pageBitmap->Release();
        if (staging) staging->Release();
        rasterDc->Release();
        return -1;
    }

    CreateDirectoryW(outDir.c_str(), nullptr);

    int pages = renderPages(app, geo, [&](ID2D1CommandList* list, int index) {
        rasterDc->SetTarget(pageBitmap);
        rasterDc->BeginDraw();
        rasterDc->Clear(tinta::colorF(1.0f, 1.0f, 1.0f));
        rasterDc->DrawImage(list);
        if (FAILED(rasterDc->EndDraw())) return false;

        D2D1_POINT_2U zero{0, 0};
        D2D1_RECT_U all{0, 0, pw, ph};
        if (FAILED(staging->CopyFromBitmap(&zero, pageBitmap, &all))) return false;
        D2D1_MAPPED_RECT mapped{};
        if (FAILED(staging->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return false;

        bool written = false;
        IWICBitmapEncoder* encoder = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        std::wstring path = outDir + L"\\page-" + std::to_wstring(index + 1) + L".png";
        if (app.wicFactory &&
            SUCCEEDED(app.wicFactory->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(app.wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
            SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
            SUCCEEDED(frame->Initialize(nullptr))) {
            frame->SetSize(pw, ph);
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
            frame->SetPixelFormat(&fmt);
            if (SUCCEEDED(frame->WritePixels(ph, mapped.pitch,
                                             mapped.pitch * ph, mapped.bits)) &&
                SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
                written = true;
            }
        }
        if (frame) frame->Release();
        if (encoder) encoder->Release();
        if (stream) stream->Release();
        staging->Unmap();
        return written;
    });

    staging->Release();
    pageBitmap->Release();
    rasterDc->Release();
    return pages;
}

// --- Export as PDF (#export_as) ---
//
// The print pipeline already produces vector pages with driver-side font
// embedding; routing the same job to the Microsoft Print to PDF driver
// with the job output redirected into a file stream yields the PDF at the
// chosen path with no dialog. Pages use the last print-preview paper
// choice, or the locale default before any preview ran.
bool exportPdfFile(App& app, const std::wstring& path) {
    if (!app.root || !app.deviceContext) return false;
    if (app.editMode) {
        editorReparse(app);  // include unsaved edits, like the preview
    }

    PageGeometry geo{};
    if (app.printPreviewPaper >= 0 &&
        app.printPreviewPaper < PRINT_PAPER_COUNT) {
        const PrintPaper& paper = PRINT_PAPERS[app.printPreviewPaper];
        geo.pageW = app.printPreviewLandscape ? paper.h : paper.w;
        geo.pageH = app.printPreviewLandscape ? paper.w : paper.h;
        geo.contentW = geo.pageW - kPageMarginDips * 2;
        geo.contentH = geo.pageH - kPageMarginDips * 2;
    } else {
        geo = defaultPageGeometry();
    }

    IStream* fileStream = nullptr;
    if (FAILED(SHCreateStreamOnFileEx(
            path.c_str(), STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE,
            FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &fileStream))) {
        return false;
    }

    IPrintDocumentPackageTargetFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(
            __uuidof(PrintDocumentPackageTargetFactory), nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        fileStream->Release();
        return false;
    }
    std::wstring jobName = L"Tinta - " + toWide(app.currentFile);
    IPrintDocumentPackageTarget* target = nullptr;
    HRESULT hr = factory->CreateDocumentPackageTargetForPrintJob(
        L"Microsoft Print to PDF", jobName.c_str(), fileStream, nullptr,
        &target);
    factory->Release();
    if (FAILED(hr) || !target) {
        fileStream->Release();
        return false;
    }

    ID2D1Device* device = nullptr;
    app.deviceContext->GetDevice(&device);
    ID2D1PrintControl* printControl = nullptr;
    if (device) {
        device->CreatePrintControl(app.wicFactory, target, nullptr,
                                   &printControl);
        device->Release();
    }

    bool ok = false;
    if (printControl) {
        int pages = renderPages(app, geo, [&](ID2D1CommandList* list, int) {
            return SUCCEEDED(printControl->AddPage(
                list, tinta::sizeF(geo.pageW, geo.pageH), nullptr));
        });
        ok = pages > 0 && SUCCEEDED(printControl->Close());
        printControl->Release();
    }
    target->Release();
    fileStream->Commit(STGC_DEFAULT);
    fileStream->Release();
    return ok;
}

// Link peek: renders the top of another markdown file at the live theme
// into a 2x bitmap. The document swap rides the print-layout save/restore
// (the palette switch is undone so the peek matches the screen).
std::shared_ptr<tinta::Bitmap> renderPeekBitmap(App& app, const std::wstring& path,
                                                float widthDips, float& heightDips) {
    // app.renderTarget rather than app.deviceContext now: the raster goes through the seam,
    // which needs a live tinta::RenderTarget only to reach that backend's own factories.
    if (!app.renderTarget || !app.root) return nullptr;
    // Named, not inline -- most vexing parse. C58's last site in a portable-destined
    // file; this one is behind a FATAL commdlg.h in the census, so it was classified
    // from renderPeekBitmap's own `const std::wstring& path` rather than from g++.
    const std::filesystem::path peekPath(path);
    std::ifstream file(peekPath);
    if (!file) return nullptr;
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto parsed = parseDocument(app.parser, buffer.str(), path);
    if (!parsed.success || !parsed.root) return nullptr;

    // EVERYTHING THIS FUNCTION BORROWS FROM App IS RESTORED BY A DESTRUCTOR, NOT BY THE
    // LINES AT THE BOTTOM. The old shape restored four pieces of state on the normal path
    // and its own comment admitted the exception path was uncovered: a throw out of
    // drawDocumentRange -- or out of any allocation in here -- propagated past the restore
    // and left the application showing the PEEK's document root, the peek target's
    // currentFile, a 100% reading width and the print layout still in force. That is not a
    // leak, it is a visibly wrong window, and it was the worse half of the two the comment
    // named.
    //
    // NOT TESTED, AND SAID RATHER THAN IMPLIED, which is the mistake the old comment made:
    // src/print.cpp appears once in CMakeLists.txt, in the application target, so no test
    // binary compiles this function and there is no arm that fails if this guard is
    // deleted. What replaces the test is a shape a reader can check, and the check has to
    // be stated precisely because the body below DOES write all four -- borrowing them is
    // the whole point. The property is that each saved value is written BACK exactly once,
    // in this destructor, and no other statement in the function restores anything: there
    // is no cleanup block to fall out of and no early return that has to remember. Making
    // it testable costs a test target that links the whole app, which is larger than the
    // thing being fixed.
    struct PeekBorrowedState {
        App& app;
        qmd::ElementPtr savedRoot;
        std::string savedFile;
        int savedPct;
        SavedView saved{};

        explicit PeekBorrowedState(App& a)
            : app(a), savedRoot(a.root), savedFile(a.currentFile),
              savedPct(a.readingWidthPct) {
            enterPrintLayout(app, saved);
        }
        ~PeekBorrowedState() {
            app.root = savedRoot;
            app.currentFile = savedFile;
            app.readingWidthPct = savedPct;
            app.peekFrameless = false;
            leavePrintLayout(app, saved);
        }
        PeekBorrowedState(const PeekBorrowedState&) = delete;
        PeekBorrowedState& operator=(const PeekBorrowedState&) = delete;
    } borrowed(app);
    const SavedView& saved = borrowed.saved;
    // The peek must look exactly like the screen: keep the palette, the
    // DPI scale, and the zoom instead of the print defaults; the reading
    // column stays full width inside the small panel
    app.theme = saved.theme;
    app.contentScale = saved.contentScale;
    app.zoomFactor = saved.zoomFactor;
    app.readingWidthPct = 100;
    updateTextFormats(app);
    app.root = parsed.root;
    app.currentFile = toUtf8(path);  // relative images resolve at the target
    // Plain-text targets drop the whole-file code block's card: the peek
    // panel is already the frame
    app.peekFrameless = isPlainTextDocumentPath(std::wstring_view(path));
    layoutAtPrintWidth(app, widthDips);
    app.peekFrameless = false;
    // The layout starts below the (invisible) title strip; the peek slice
    // begins there and shrinks to short targets
    float topPad = chromeTopHeight(app);
    heightDips = std::max(dpi(app, 60.0f),
                          std::min(heightDips,
                                   app.contentHeight - topPad + dpi(app, 8.0f)));

    // THE EXCLUSION THAT KEPT THIS BODY Direct2D-TERMINAL WAS PRICED RATHER THAN ARGUED,
    // SO A MEASUREMENT COULD END IT -- which is what happened. include/render_api.h
    // listed this site under
    // "ID2D1DeviceContext::CreateBitmap with NULL pixels", noted that its shape is exactly
    // tinta::rasterizeToImage's (draw offscreen, keep the result), and set the price:
    // "the WIC software path plus two full-buffer copies per dwell (out to a DecodedImage
    // vector, back up through CreateBitmap) on a panel that reaches roughly 520x460 DIPs
    // at 2x", closing with "a measurement showing the copies are cheap at this size would
    // settle it."
    //
    // MEASURED AT THAT EXACT SIZE, 1040x920 px, best of 7 warm runs with both copies
    // inside the timing: 2.68 ms on Direct2D (the WIC software path itself), 4.19 ms on
    // Qt, against a 25 ms budget written down before the instrument existed. The panel is
    // drawn ONCE per dwell and cached until the peek disarms, and the same dwell already
    // pays a file read, a parse and a full layoutAtPrintWidth above -- costs both the old
    // body and this one share. So the trade the header refused is roughly ten times
    // cheaper than the threshold, and refusing it further would have been an estimate
    // outranking a reading.
    //
    // WHAT THIS DOES NOT MAKE TRUE, because the tempting sentence is "the last Direct2D
    // rasteriser in the tree is gone" and it is false: this file still holds FOUR
    // CreateBitmap-with-NULL-pixels sites, two target+staging pairs in
    // rasterizePreviewPage and the page rasteriser below, and both build their own device
    // context and read the drawing back with CPU_READ | CANNOT_DRAW. They are not this
    // shape and this seam does not serve them -- it hands back a pixel vector, which is
    // the copy they already make once and would then make twice. What left is ONE body:
    // the only one of the five that drew offscreen and KEPT the result.
    //
    // THE 2x IS A TRANSFORM HERE, NOT A DPI, AND THAT IS THE ONE REAL DIFFERENCE FROM THE
    // OLD BODY. The old code set 192 DPI on its own device context so 1 DIP became 2 px.
    // The seam's offscreen target is built by the backend and neither arm offers a DPI to
    // set: src/image_win32.cpp hardcodes its WIC render target at 96, and src/image_qt.cpp
    // paints into a bare QImage where one coordinate unit is one pixel by construction. So
    // the scale has to come from inside the callback, and a 2x scale transform is
    // equivalent for drawing that is all in DIPs -- which everything below is. Probed
    // on BOTH backends rather than assumed: a DIP rect of 0..10 covers device pixels
    // 0..20, ink at px 15 and background at px 25. If that had failed, this port needed a
    // DPI parameter on the seam and would not have shipped.
    //
    // The handle comes back at 192 DPI so GetSize() still reports DIPs, which is what the
    // call sites and app.h's peek member have always assumed; src/render.cpp's SVG path is
    // the precedent for that argument and render_api.h states it.
    std::shared_ptr<tinta::Bitmap> result;
    if (app.renderTarget) {
        const unsigned pxW = (unsigned)(widthDips * 2.0f);
        const unsigned pxH = (unsigned)(heightDips * 2.0f);
        tinta::DecodedImage raster = tinta::rasterizeToImage(
            *app.renderTarget, pxW, pxH, app.theme.background,
            [&](tinta::RenderTarget& target, tinta::Brush* brush) {
                if (!brush) return;
                target.SetTransform(tinta::matrixScale(2.0f, 2.0f));
                drawDocumentRange(app, target, brush, topPad, topPad + heightDips, 0.0f,
                                  0.0f);
                target.SetTransform(tinta::matrixIdentity());
            });
        if (raster.ok) {
            // unique_ptr in, shared_ptr out: the seam hands back ownership and the
            // conversion is the only place this function touches a lifetime. Nothing here
            // is a raw pointer any more, so the three hand-written Release() calls the old
            // body needed -- and the exception path they did not cover -- are gone by
            // construction rather than by a cleanup block that has to be got right.
            std::unique_ptr<tinta::Bitmap> handle = app.renderTarget->CreateBitmap(
                pxW, pxH, raster.pixels.data(), pxW * 4, 192.0f);
            result = std::move(handle);
        }
    }
    return result;
}
