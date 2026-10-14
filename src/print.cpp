#include "print.h"
#include "print_pages.h"
#include "render.h"
#include "theme_apply.h"
#include "document.h"
#include "editor.h"
#include "utils.h"

// tinta::rasterizeToImage and tinta::encodePng: the link peek's offscreen raster, the
// --printpages page dump and, since 2026-10-06, the preview page too.
#include "image_api.h"
#include "render_api.h"

// The PDF seam. exportPdfFile below is now a page-geometry decision and one call into
// it, and the SHCreateStreamOnFileEx / IPrintDocumentPackageTarget / CreatePrintControl
// half of what it used to be lives in src/pdf_export_win32.cpp. tinta::PagePainter also
// comes from here -- see the alias below.
#include "pdf_export.h"

// THIS FILE COMPILES OFF WINDOWS AS OF 2026-10-06, and these lines used to be a list of
// Windows headers. render_d2d.h, xps_page_emitter.h, <commdlg.h>, <documenttarget.h> and
// <shlwapi.h> are gone. So is d2d_init.h, which was here only for updateTextFormats and
// that function is declared in theme_apply.h now. The Windows surface they served moved
// to src/print_win32.cpp: printDocument's printer dialog and XPS print job, and the
// locale's paper question. rasterizePreviewPage, the only other Direct2D code here, went
// onto the image seam instead (its comment says what that cost).
//
// <winspool.h> was here once too, and had no caller: swept 2026-09-20. Nothing in the
// print code talks to the spooler API -- no OpenPrinter, no DeviceCapabilities, no
// DocumentProperties, no EnumPrinters -- so anybody sizing the QPrinter port off
// "print.cpp includes winspool.h" was sizing it off nothing.

#include <filesystem>
#include <fstream>
// <functional> is named here and not left to arrive through pdf_export.h, which is where
// the PagePainter alias now lives: this file's own std::function use is the alias below,
// and a file that would not compile if a header stopped including something should say so.
#include <functional>
#include <memory>
#include <sstream>

#include <algorithm>
#include <cstring>

#include <cmath>
#include <string>
#include <vector>

// --- Printing (#74) ---
//
// No PDF library: the document is re-laid-out at page width in a light
// print palette and each page is painted through the render seam. On every platform PDF
// export goes through QPdfWriter (src/pdf_export_qt.cpp) and printing goes through QPrinter
// (src/print_qt.cpp). The Windows painter used to reach ID2D1PrintControl in the retired
// src/print_win32.cpp, and the OS print pipeline did the rest, with font embedding
// (including CJK) handled by the driver.

namespace {

// Local names for src/print_pages.h's, so the sites below read as they always have.
using PageGeometry = PrintPageGeometry;
constexpr float kPageMarginDips = kPrintPageMarginDips;

// Paper-derived palette with a true white page
D2DTheme printTheme() {
    D2DTheme t = THEMES[0];  // Paper (light)
    t.background = tinta::colorF(1.0f, 1.0f, 1.0f);
    t.codeBackground = hexColor(0xF4F4F4);
    return t;
}

using SavedView = App::PrintSavedView;

// A PrintTargetScope stood here from 2026-10-06 to the next slice the same day. It seated an
// offscreen target as app.renderTarget for each print entry point, because on Qt App had a
// render target only inside paint. src/qt_init.cpp now keeps a factory target seated outside
// paint for every caller, which is what tinta has always had, so the scope had nothing to do.

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
    if (canRepaint()) requestRepaint();
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

// Paper size for previewing before the dialog runs, decided by locale only.
// Probing the default printer here (CreateDC) initializes third-party
// drivers and can pop their dialogs (Acrobat Distiller's broken-temp-folder
// error, for one) just to read a paper size — the real printer is consulted
// after the user confirms the print dialog, and re-pagination handles any
// difference. The locale question is a backend hook (src/print_pages.h).
PageGeometry defaultPageGeometry() {
    PageGeometry geo{794.0f, 1123.0f, 0.0f, 0.0f};  // A4
    if (printLocalePrefersLetter()) {
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
//
// ON THE IMAGE SEAM SINCE 2026-10-06, AND THE EXCLUSION IT ENDS WAS PRICED, NOT ARGUED.
// This was the last Direct2D target+staging pair in the file. It built a device context off
// app.deviceContext's device and read the page back with CPU_READ | CANNOT_DRAW.
// include/render_api.h kept it native because it is a LIVE-UI readback: it re-rasterises
// on every page flip, paper change and orientation toggle, and the seam's pixel vector
// costs one more full-buffer copy than the staging map did. The link peek met the same
// argument and the measurement ended it: 2.68 ms on Direct2D and 4.19 ms on Qt at
// 1040x920 px, both copies included, against a 25 ms budget. A preview page is smaller
// than that panel. It is A4 fitted between the bars, about 470x670 px in a 1280x800 window.
// Timed on its own as well: one page flip, raster plus copy, read 0.76 ms on the Qt arm for a
// 403x570 page (tests/qt_entry_tests.cpp prints it as flip_ms, Linux, 2026-10-06). The
// Direct2D arm's figure for this page is not measured. The peek put that arm ahead of Qt.
//
// What the move buys is the Qt stack's preview, which had no device context to build one
// from. The pixels arrive in the format app.printPreviewPixels has always held: premultiplied
// BGRA, top-down, stride width*4, the contract tinta::rasterizeToImage states.
//
// A BEHAVIOUR CHANGE ON WINDOWS, STATED: the old pair rasterised on whatever device
// app.deviceContext belonged to, which is the GPU once WM_APP_GPU_READY has landed. The
// seam's WIC target is software unconditionally. That is the same trade printDebugPages
// made, and for the preview it means the on-screen page is drawn by the same rasteriser as
// --printpages, which is the instrument this tree checks printing against.
bool rasterizePreviewPage(App& app, int page) {
    int pageCount = (int)app.printPreviewBounds.size() - 1;
    if (page < 0 || page >= pageCount || !app.renderTarget) return false;
    const unsigned pxW = app.printPreviewPxW, pxH = app.printPreviewPxH;
    if (pxW == 0 || pxH == 0) return false;

    const float fit = app.printPreviewFit;
    const float yStart = app.printPreviewBounds[page];
    const float yEnd = app.printPreviewBounds[page + 1];
    tinta::DecodedImage image = tinta::rasterizeToImage(
        *app.renderTarget, pxW, pxH, tinta::colorF(1.0f, 1.0f, 1.0f),
        [&](tinta::RenderTarget& target, tinta::Brush* brush) {
            if (!brush) return;
            // A scale transform rather than a DPI, for the reason renderPeekBitmap gives:
            // neither backend's offscreen target takes a DPI, and everything drawn here is
            // in DIPs. drawDocumentRange composes its shrink bands with this transform
            // rather than replacing it, which is what its own comment says it does for
            // "the preview rasterizer".
            target.SetTransform(tinta::matrixScale(fit, fit));
            drawDocumentRange(app, target, brush, yStart, yEnd,
                              kPageMarginDips, kPageMarginDips);
            target.SetTransform(tinta::matrixIdentity());
        });
    if (!image.ok) return false;
    app.printPreviewPixels = std::move(image.pixels);
    return true;
}

// What renderPages below hands an emitter INSTEAD of a page: a callable that
// paints one page's range into whatever target the emitter supplies.
//
// The type is `tinta::rasterizeToImage`'s `draw` parameter, spelled out rather than
// approximated, and that identity is not a coincidence to be admired later -- it is why
// printDebugPages can pass a painter straight through to the seam with no adapter at
// all. If this alias and image_api.h ever disagree, the seam call stops compiling, which
// is the cheapest possible guard on the agreement.
//
// THE SPELLING MOVED TO include/pdf_export.h AND THIS IS NOW AN ALIAS OF AN ALIAS,
// deliberately. A third consumer appeared -- the PDF seam takes this exact callback --
// and three independent `std::function<void(RenderTarget&, Brush*)>` spellings would
// have made the "stops compiling" guard above depend on nobody having fixed up two of
// them. The local name stays because the sites in this file use it unqualified.
using PagePainter = tinta::PagePainter;

// Runs layout + pagination and hands each page to emitPage as a PAINTER.
// `emitPage(int index, const PagePainter& paint) -> bool` chooses the surface,
// calls paint once, and reports success. Returns page count, or -1 on failure.
//
// THIS USED TO RECORD AN ID2D1CommandList PER PAGE AND HAND THAT OVER, AND THE
// INVERSION IS WHY THE FILE READS DIFFERENTLY NOW. The indirection bought nothing
// measurable: every command list was created, closed, emitted EXACTLY ONCE and
// released inside one loop iteration, so "record, then replay" was record-once,
// replay-once with a Direct2D type in the middle of the one function in this file
// that is pure layout. Inverting it moves the command list down into the two XPS
// emitters that genuinely need one (see XpsPageEmitter below), leaves this loop
// naming no backend type at all, and costs nothing -- the drawing already went
// through the seam, because drawDocumentRange has taken a tinta::RenderTarget&
// since the print seam landed.
//
// What this function may touch is therefore narrow on purpose: App's layout state
// and the page geometry. No device, no device context, no ID2D1 anything. The
// app.deviceContext null-check that used to guard the top is gone with the device
// it was guarding -- an emitter that needs one checks for itself, which is also
// what makes this loop reusable from a Qt path that will never have one.
template <typename EmitFn>
int renderPages(App& app, const PageGeometry& geo, EmitFn emitPage) {
    if (!app.root) return -1;

    SavedView saved{};
    enterPrintLayout(app, saved);
    layoutAtPrintWidth(app, geo.contentW);

    std::vector<float> breaks = computePageBreaks(app, geo.contentH);
    breaks.push_back(app.contentHeight + 1.0f);  // final page sentinel

    int pages = 0;
    bool ok = true;
    float yStart = 0.0f;
    for (size_t i = 0; ok && i < breaks.size(); i++) {
        const float yEnd = breaks[i];
        // By reference to app, by VALUE for the two bounds: the painter is called
        // inside emitPage, which is inside this iteration, so a reference would be
        // valid -- but yStart is reassigned at the bottom of the loop and an emitter
        // that ever holds the painter past its call would then paint the wrong band
        // silently. Copying two floats removes the question.
        const PagePainter paint = [&app, yStart, yEnd](tinta::RenderTarget& target,
                                                      tinta::Brush* brush) {
            if (!brush) return;
            drawDocumentRange(app, target, brush, yStart, yEnd,
                              kPageMarginDips, kPageMarginDips);
        };
        ok = emitPage(pages, paint);
        pages++;
        yStart = yEnd;
    }

    leavePrintLayout(app, saved);
    return ok ? pages : -1;
}

// XpsPageEmitter WAS DEFINED HERE, and then in src/xps_page_emitter.h, a header rather than
// a copy because it had two consumers: printDocument, which moved to src/print_win32.cpp on
// 2026-10-06, and src/pdf_export_win32.cpp. The header and both consumers were deleted with
// the Direct2D stack in 0b39ae6.
//
// ALL OF THIS FILE'S DIRECT2D CODE IS GONE NOW. The renderPages inversion moved the command
// list into the emitters, the PDF seam moved one emitter out, and the 2026-10-06 split
// moved the printer plumbing out and put the preview raster on the image seam.

} // namespace

// The page loop, for src/print_qt.cpp's printDocumentTo. A non-template entry point,
// because the template above cannot be called from another translation unit. The
// std::function costs one indirect call per PAGE, not per draw.
int renderPrintPages(App& app, const PrintPageGeometry& geo,
                     const std::function<bool(int, const tinta::PagePainter&)>& emitPage) {
    return renderPages(app, geo, emitPage);
}

// printDocument IS A BACKEND HOOK NOW, declared in include/print.h as it always was. Its one
// arm is src/print_qt.cpp, which prints on every platform; the Windows arm (printer dialog,
// XPS print job) was the retired src/print_win32.cpp.

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

void openPrintPreview(App& app) {
    // app.renderTarget, not app.deviceContext: the raster goes through the image seam,
    // which needs a live tinta::RenderTarget and nothing native. The Qt stack has no
    // device context at all, so the old guard was what kept its preview shut.
    if (app.showPrintPreview || !app.root || !app.renderTarget) return;
    if (app.editMode) {
        editorReparse(app);  // include unsaved edits in the preview and printout
    }
    if (app.printPreviewPaper < 0) detectDefaultFormat(app);

    enterPrintLayout(app, app.printSaved);
    app.printPreviewPage = 0;
    refreshPreviewLayout(app);
    app.showPrintPreview = true;
    requestRepaint();
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
    if (canRepaint()) requestRepaint();
}

void closePrintPreview(App& app) {
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
    requestRepaint();
}

void printPreviewSetPage(App& app, int page) {
    if (!app.showPrintPreview) return;
    int pageCount = (int)app.printPreviewBounds.size() - 1;
    page = std::max(0, std::min(page, pageCount - 1));
    if (page == app.printPreviewPage) return;
    app.printPreviewPage = page;
    rasterizePreviewPage(app, page);
    if (canRepaint()) requestRepaint();
}

void printPreviewConfirm(App& app) {
    if (!app.showPrintPreview) return;
    closePrintPreview(app);
    printDocument(app);
}

// PORTABLE BODY, AND THE SECOND ONE IN THIS FILE AFTER renderPeekBitmap. Nothing
// below names a Direct2D or WIC type: the page is painted through
// tinta::rasterizeToImage, encoded through tinta::encodePng, and written with
// std::ofstream. What made this possible is the renderPages inversion above -- the
// old body had to accept a closed ID2D1CommandList and replay it, which is a
// Direct2D operation no seam offers and none should.
//
// IT WAS EXCLUDED FOR A STATED REASON AND THE REASON DID NOT SURVIVE, so the
// exchange is recorded here the way the peek's was. include/render_api.h listed
// this site among four "CreateBitmap with NULL pixels" sites and said of the two
// target+staging pairs: "They STAY -- a seam that draws offscreen and hands back a
// pixel vector is not what a printer readback wants, and routing them would add a
// third copy to the two they already make."
//
// The copy arithmetic is right and the conclusion does not follow HERE, because it
// was reasoning about a printer readback and this is not one. Per page: the old
// body drew into an OPTIONS_TARGET bitmap, CopyFromBitmap'd into a CPU_READ staging
// bitmap, and let WIC read the mapped bits -- one full-buffer copy of ours. The new
// one draws into a WIC target, copies out to a DecodedImage vector and again into
// encodePng's source bitmap -- two. So it is one extra 3.5 MB copy per A4 page,
// paid by a harness switch that runs from a test script and that no user is ever
// waiting on. That is a different trade from the one the exclusion weighed.
//
// THE COUNT IS 1 -> 2 AND THAT IS ALL IT IS. A draft of this paragraph called the
// first bitmap a GPU bitmap and claimed the new body removes a GPU->CPU copy
// outright; see the software note below for why that was wrong. Both bodies copy
// within CPU memory on this path, so the extra copy is the whole delta.
//
// THE OTHER PAIR (rasterizePreviewPage) STAYED, AND ITS REASON WAS THE ONE THAT DID
// SURVIVE: that one re-rasterises on every page flip, paper change and orientation
// toggle while the preview is open, and it needs the pixels in
// app.printPreviewPixels to display rather than to encode. Same code shape, live
// UI, and the exclusion's argument applied to it unweakened. The lesson is not "the
// exclusion was wrong" -- it is that it covered two sites with one sentence, and
// only one of them was a printer readback. (It moved onto the seam on 2026-10-06 anyway,
// once the live-UI cost was timed rather than argued. Its own comment has the figure.)
//
// SOFTWARE RASTERISATION IS A PROPERTY WORTH HAVING HERE, not merely a cost
// tolerated -- but the version of this paragraph that shipped first got the reason
// wrong, and the hashes are what caught it. It said "the old body rasterised on the
// GPU, which makes this harness's output a function of the machine's driver." The
// old body rasterised wherever app.useHardwareRT pointed, that member starts FALSE
// (include/app.h), and the retired src/main_d2d.cpp returned from the --printpages branch
// about forty lines before it called startGpuWarmup -- so this harness was ALREADY software
// and 140 README pages plus a 3-page fixture came back byte-identical across the
// port. Holding an ID2D1DeviceContext is not evidence of hardware rasterisation.
//
// The claim that survives is narrower and still worth the lines: CreateWicBitmapRenderTarget
// is software UNCONDITIONALLY, where the old body inherited a flag that flips at
// runtime. No hash moved because the harness never reached the hardware case -- but
// a caller invoking this from a warm GUI would have, and now cannot. Several slices
// in this tree use --printpages page hashes as their falsifier, and an instrument
// whose readings depend on which GPU took them is a worse instrument.
int printDebugPages(App& app, const std::wstring& outDir) {
    if (!app.renderTarget) return -1;
    PageGeometry geo{794.0f, 1123.0f, 794.0f - kPageMarginDips * 2,
                     1123.0f - kPageMarginDips * 2};  // A4
    const unsigned pw = (unsigned)geo.pageW, ph = (unsigned)geo.pageH;

    const std::filesystem::path dir(outDir);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);  // already-exists is not an error

    return renderPages(app, geo, [&](int index, const PagePainter& paint) {
        // paint goes straight through: PagePainter IS rasterizeToImage's draw type,
        // so there is no adapter lambda here and no place for one to drift.
        // White rather than app.theme.background -- a printed page is white on every
        // theme, which is what the old body's two Clear(1,1,1) calls also said.
        const tinta::DecodedImage image = tinta::rasterizeToImage(
            *app.renderTarget, pw, ph, tinta::colorF(1.0f, 1.0f, 1.0f), paint);
        if (!image.ok) return false;
        const std::string png = tinta::encodePng(image);
        if (png.empty()) return false;

        std::ofstream out(dir / (L"page-" + std::to_wstring(index + 1) + L".png"),
                          std::ios::binary);
        if (!out) return false;
        out.write(png.data(), (std::streamsize)png.size());
        out.close();  // closed and checked here: ~ofstream flushes and swallows
        return out.good();
    });
}

// --- Export as PDF (#export_as) ---
//
// Pages use the last print-preview paper choice, or the locale default before any
// preview ran. Everything after that geometry decision is the seam's: how a PDF gets
// written is include/pdf_export.h's question, and on this platform the answer is still
// the print pipeline aimed at a file stream (src/pdf_export_win32.cpp says how and why).
//
// THE SIGNATURE DOES NOT MOVE, AND THAT IS A CONSTRAINT RATHER THAN A PREFERENCE.
// tests/export_html_tests.cpp links its own stub definition of this function, so a
// changed signature would break that target at LINK time -- an unresolved symbol far
// from here -- instead of at the call site.
bool exportPdfFile(App& app, const std::wstring& path) {
    // app.renderTarget is in the guard because the seam is handed it and would otherwise
    // be dereferenced null.
    //
    // app.deviceContext LEFT THE GUARD ON 2026-10-06, and the paragraph that kept it said
    // dropping it "would have relaxed the guard on a platform assumption". The assumption
    // can be read in the code instead. src/pdf_export_win32.cpp's beginPdfDocument
    // QueryInterfaces the same native for an ID2D1DeviceContext and returns nullptr when
    // that fails, before it opens a stream. So a Windows build with no device context still
    // gets no PDF and no file, and the check was a duplicate of the seam's own. On Qt the
    // check was not a duplicate: app.deviceContext is always null there, so it was the
    // whole reason this function refused.
    if (!app.root || !app.renderTarget) return false;
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

    // The document title is the print JOB name on this platform and would be the PDF
    // /Title on a QPdfWriter arm -- the same string either way, which is why it is a seam
    // parameter and not something the backend invents.
    std::unique_ptr<tinta::PdfPageSink> pdf = tinta::beginPdfDocument(
        *app.renderTarget, path, L"Qinta - " + toWide(app.currentFile));
    if (!pdf) return false;

    int pages = renderPages(app, geo, [&](int, const PagePainter& paint) {
        return pdf->addPage(geo.pageW, geo.pageH, paint);
    });
    // Short-circuited on purpose: a document with no pages is not finished, which is what
    // `pages > 0 && SUCCEEDED(printControl->Close())` said inline. The sink's destructor
    // still commits the stream, so the failure mode is the same unreadable file it was.
    return pages > 0 && pdf->finish();
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
    // file; this one is behind a FATAL include in the census, so it was classified
    // from renderPeekBitmap's own `const std::wstring& path` rather than from g++.
    // WHICH header is fatal is not worth writing down and this comment used to say
    // commdlg.h: the seam includes put <d2d1.h> above it and the census names only
    // the first one it cannot resolve. What survives either way is that the census
    // never reads this function at all, so nothing in here was g++-derived.
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
    // there is no arm that fails if this guard is deleted. THE REASON GIVEN HERE WAS WRONG
    // UNTIL 2026-09-20 and it was wrong about the build, not about the coverage. It read:
    // "src/print.cpp appears once in CMakeLists.txt, in the application target, so no test
    // binary compiles this function". SIX targets compiled this file then. It sat in
    // set(SOURCES), which CMakeLists.txt re-used as INPUT_TEST_SOURCES minus the retired
    // src/main_d2d.cpp alone -- so the tinta app plus input_tests, file_path_tests,
    // code_block_layout_tests, blockquote_layout_tests and theme_tests all compiled and LINKED
    // it. What survived was the narrower claim, which is the one the paragraph below actually
    // needs: none of those six CALLED this function, and the two targets that held the seam's
    // assertions (render_api_tests, render_d2d_tests) did not compile this file at all. Compiled is not
    // covered, and "appears once in CMakeLists" was a claim about the TEXT read as a claim
    // about the TARGETS. What replaces the test is a shape a reader can check, and the check has to
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
    // WHAT THIS DID NOT MAKE TRUE, because the tempting sentence was "the last Direct2D
    // rasteriser in the tree is gone" and it was still false: this file held TWO
    // CreateBitmap-with-NULL-pixels sites, ONE target+staging pair, in
    // rasterizePreviewPage above. That pair is gone too since 2026-10-06, so this file
    // holds none, which is a claim about this FILE. The tree still has offscreen Direct2D
    // targets in src/image_win32.cpp, which is the backend of the seam everything here
    // calls.
    //
    // IT SAID FOUR AND TWO PAIRS UNTIL 2026-09-20, and the other pair's departure is why
    // the count moved: the page rasteriser below ALSO drew offscreen into a staging bitmap,
    // and it is on the seam now. This paragraph's reason for excluding both -- "the seam
    // hands back a pixel vector, which is the copy they already make once and would then
    // make twice" -- was one sentence covering two sites, and it only ever described one of
    // them. printDebugPages encodes its pixels to a file and nothing waits for it;
    // rasterizePreviewPage re-rasterises on every page flip, paper change and orientation
    // toggle with the preview open, and wants the pixels to DISPLAY. Same code shape, and
    // only the second one is a live-UI readback. The full arithmetic is at printDebugPages.
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
