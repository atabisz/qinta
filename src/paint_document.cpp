#include "paint_document.h"

#include "app.h"

#include <algorithm>
#include <cmath>
#include <memory>

// TINTA_D2D_BACKEND, NOT _WIN32, AND THAT DISTINCTION COST A LINK FAILURE. Kept as a
// record because the reasoning still governs the dashed block below, and because the
// failure it describes is the one a future guard here would repeat.
//
// All three guards in this file said `#ifdef _WIN32` when they were written, which was
// true of every target that existed then. qinta is a WINDOWS target with no Direct2D
// backend, so _WIN32 was defined, the colour-emoji branch then in the run loop compiled,
// and the link came back `unresolved external symbol nativeTextLayout` out of
// src/text_d2d.cpp -- a file qinta correctly does not compile. The dashed block further
// down was worse: it resolves through d2d1.lib, so it would have LINKED and then
// dereferenced a null app.d2dFactory at the first dashed mermaid connector.
//
// THAT COLOUR-EMOJI BRANCH IS GONE as of 2026-09-19 -- it duplicated a seam path that
// already carried DrawTextOptions::EnableColorFont, see the run loop for the evidence --
// and with it went the `#ifdef TINTA_D2D_BACKEND` include of text_d2d.h that served it.
// So this file no longer names DirectWrite at all, and nothing here downcasts a layout.
// The lesson outlives its example: the question a guard in a file BOTH stacks compile has
// to ask is which backend it got, never which OS it is on.
//
// The macro is defined by CMakeLists.txt on the targets that actually link the backend,
// gated on TINTA_LEGACY_D2D, and it goes away when those two backend files are deleted.

namespace {

// THE PORTABLE DASHED STROKE STYLE, and it is a marker object rather than a dash
// pattern.
//
// tinta::StrokeStyle (include/render_api.h:180) is an opaque handle: an abstract
// class whose only member is a virtual destructor. It carries no dash array, no
// phase and no cap style, because the one thing the tree ever asks of it is
// "dashed or not" -- src/render_qt.cpp's setStroke() reads it as
// `if (strokeStyle) pen.setStyle(Qt::DashLine)` and nothing more.
//
// So the portable side does not need a Qt object at all; it needs a non-null
// pointer with the right static type. This is that, with static storage duration so
// the lifetime question does not arise: it outlives every App.
//
// THE LIMIT, stated because the next person to need a SECOND stroke style will hit
// it: this cannot express one. The moment two distinct dash patterns exist, the
// marker has to become a real object carrying the pattern, and render_qt.cpp's
// "non-null means dashed" reading has to be replaced along with it. That is a
// deliberate deferral of work nothing in the tree needs yet, not an oversight --
// Direct2D's side builds exactly one stroke style too.
struct MarkerStrokeStyle final : tinta::StrokeStyle {};

}  // namespace

// ONE OWNER FOR THE DASHED STROKE STYLE, and the move out of paintDocumentLayout is a
// defect fix rather than tidying.
//
// The creation used to sit inline in the paint pass, so app.dashedStrokeStyle came into
// existence only when a document that had already been PAINTED contained a dashed
// connector. The DOCX exporter needs the same object -- src/export_docx.cpp built its
// own local one with byte-identical properties (FLAT/FLAT/FLAT, MITER, 10.0f, DASH,
// 0.0f), verified field by field against the block below before reusing this one -- and
// an export taken before any dashed connector was ever painted would have read NULL.
// DrawLine with a null stroke style draws SOLID and reports nothing, which is precisely
// the failure mode the fc9ceb0 note below records; reusing app.dashedStrokeStyle without
// this function would have reintroduced it through a different door.
//
// Create-if-null, returns nothing: every caller reads app.dashedStrokeStyle afterwards,
// which is what the paint pass already did. It lives in THIS file and not in a header
// because MarkerStrokeStyle is in the anonymous namespace above, and because the guard
// has to be TINTA_D2D_BACKEND -- see this file's opening note for what _WIN32 cost.
void ensureDashedStrokeStyle(App& app) {
    if (app.dashedStrokeStyle) return;
    // No factory and no native pointer without the Direct2D backend, so the marker
    // stands in. THIS ARM RUNS ON WINDOWS TOO, under qinta -- it is not the
    // "non-Windows" arm any more, and reading it as one is how the guard came to be
    // spelled _WIN32 in the first place.
    //
    // THE TWO ARMS NOW END IN THE SAME STATEMENT, which they did not before: this one
    // called dashedStrokeStyleStorage.install() while the Direct2D arm called
    // app.seatDashedStrokeStyle(). Both are assignments into one unique_ptr member now,
    // and Seated<> is gone from this member entirely -- install() was the door
    // include/seated.h wrote for the case where "nothing off Windows can produce a native
    // COM pointer, so seat() is unreachable there", and a member that never seats does not
    // need the template that exists to make seating idempotent. That header predicted this
    // exact collapse: reset(), get() and operator bool are std::unique_ptr's names
    // "taken deliberately so that a storage which stops having a native to seat can be
    // COLLAPSED to a plain unique_ptr without touching one caller".
    //
    // Still not a pointer to a file-local static, for the reason that choice was rejected
    // the first time: it would leave App holding a pointer whose lifetime no member
    // records, which is the shape the Seated refactor removed.
    app.dashedStrokeStyle = std::make_unique<MarkerStrokeStyle>();
}

DocumentViewport prepareDocumentViewport(App& app) {
    // Apply a saved reading position once the layout can reach it (#77)
    if (app.pendingScrollRestore >= 0.0f &&
        (app.layoutComplete ||
         app.contentHeight >= app.pendingScrollRestore + app.height)) {
        app.scrollY = app.pendingScrollRestore;
        app.targetScrollY = app.pendingScrollRestore;
        app.pendingScrollRestore = -1.0f;
    }

    DocumentViewport viewport;

    // Clamp scroll values
    viewport.documentWidth = documentViewportWidth(app);
    viewport.maxScrollX = std::max(0.0f, app.contentWidth - viewport.documentWidth);
    viewport.maxScrollY = std::max(0.0f, app.contentHeight - app.height);
    app.scrollX = std::max(0.0f, std::min(app.scrollX, viewport.maxScrollX));
    app.scrollY = std::max(0.0f, std::min(app.scrollY, viewport.maxScrollY));

    // Render cached layout (document coordinates -> screen)
    viewport.top = app.scrollY;
    viewport.bottom = app.scrollY + app.height;
    viewport.left = app.scrollX;
    viewport.right = app.scrollX + viewport.documentWidth;
    viewport.cullMargin = 100.0f;

    return viewport;
}

void paintDocumentLayout(App& app, const DocumentViewport& viewport) {
    if (!app.renderTarget || !app.brush) return;

    // The struct's fields are unpacked back into the names the extracted code used.
    // That is deliberate and it is the reason this function's body is character-for-
    // character what src/main_d2d.cpp ran: an extraction that also renamed 60-odd
    // references would have mixed a relocation with an edit, and there would be no
    // way to tell a transcription slip from an intended change by reading the diff.
    const float viewportTop = viewport.top;
    const float viewportBottom = viewport.bottom;
    const float viewportLeft = viewport.left;
    const float viewportRight = viewport.right;
    const float cullMargin = viewport.cullMargin;

    for (const auto& rect : app.layoutRects) {
        if (rect.rect.bottom < viewportTop - cullMargin ||
            rect.rect.top > viewportBottom + cullMargin) {
            continue;
        }
        if (rect.rect.right < viewportLeft - cullMargin ||
            rect.rect.left > viewportRight + cullMargin) {
            continue;
        }
        app.brush->SetColor(rect.color);
        app.renderTarget->FillRectangle(
            tinta::rectF(rect.rect.left - app.scrollX, rect.rect.top - app.scrollY,
                       rect.rect.right - app.scrollX, rect.rect.bottom - app.scrollY),
            app.brush);
        app.drawCalls++;
    }

    // Dashed stroke is a factory object: create once, reuse every frame. The
    // std::any_of stays HERE and did not move into the helper, deliberately -- it is
    // this pass's policy ("do not build one for a document that has no dashed
    // connector"), not a property of the object. The DOCX exporter has no connector
    // list to ask and needs the style whenever a diagram prim is dashed, so it calls
    // the helper unconditionally; folding this test inside would have made that
    // impossible without inventing a second entry point.
    if (!app.dashedStrokeStyle &&
        std::any_of(
            app.layoutConnectors.begin(), app.layoutConnectors.end(),
            [](const App::LayoutConnector& connector) { return connector.dashed; })) {
        ensureDashedStrokeStyle(app);
    }
    tinta::StrokeStyle* dashedStrokeStyle = app.dashedStrokeStyle.get();

    for (const auto& connector : app.layoutConnectors) {
        if (connector.bounds.bottom < viewportTop - cullMargin ||
            connector.bounds.top > viewportBottom + cullMargin ||
            connector.bounds.right < viewportLeft - cullMargin ||
            connector.bounds.left > viewportRight + cullMargin ||
            connector.points.size() < 2) {
            continue;
        }

        app.brush->SetColor(connector.color);
        for (size_t i = 1; i < connector.points.size(); i++) {
            const auto& from = connector.points[i - 1];
            const auto& to = connector.points[i];
            app.renderTarget->DrawLine(
                tinta::pointF(from.x - app.scrollX, from.y - app.scrollY),
                tinta::pointF(to.x - app.scrollX, to.y - app.scrollY),
                app.brush, connector.stroke,
                connector.dashed ? dashedStrokeStyle : nullptr);
            app.drawCalls++;
        }

        if (connector.directed) {
            const auto& tip = connector.points.back();
            const auto& previous = connector.points[connector.points.size() - 2];
            float dx = tip.x - previous.x;
            float dy = tip.y - previous.y;
            float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.001f) {
                dx /= length;
                dy /= length;
                float wing = connector.arrowSize * 0.5f;
                tinta::PointF left = tinta::pointF(
                    tip.x - dx * connector.arrowSize + dy * wing,
                    tip.y - dy * connector.arrowSize - dx * wing);
                tinta::PointF right = tinta::pointF(
                    tip.x - dx * connector.arrowSize - dy * wing,
                    tip.y - dy * connector.arrowSize + dx * wing);
                tinta::PointF screenTip =
                    tinta::pointF(tip.x - app.scrollX, tip.y - app.scrollY);
                app.renderTarget->DrawLine(
                    screenTip,
                    tinta::pointF(left.x - app.scrollX, left.y - app.scrollY),
                    app.brush, connector.stroke);
                app.renderTarget->DrawLine(
                    screenTip,
                    tinta::pointF(right.x - app.scrollX, right.y - app.scrollY),
                    app.brush, connector.stroke);
                app.drawCalls += 2;
            }
        }
    }
    // Draws a cached polygon geometry (built lazily in local space) at the
    // shape's screen position via a translation transform
    auto drawPolygon = [&](const tinta::PointF* localPoints, size_t count,
                           App::LayoutShape& shape) {
        if (count < 3) return;

        // THE ONE LAZY BUILDER of the six. The other five run at layout time; this
        // one runs inside the paint loop, which is why the failure path returns
        // without drawing rather than falling through -- an empty geometry here
        // would be re-attempted every frame.
        //
        // AddLines is kept as the array call rather than unrolled into a loop: the
        // seam carries both because a shape census (not a name census) found this
        // exact form at 10 of 26 sites, and the Qt backend does the loop instead.
        if (!shape.geometry) {
            std::unique_ptr<tinta::PathSink> sink = app.renderTarget->BeginPath();
            if (!sink) return;
            sink->BeginFigure(localPoints[0]);
            sink->AddLines(localPoints + 1, static_cast<uint32_t>(count - 1));
            sink->EndFigure();
            shape.geometry = sink->Close();
            if (!shape.geometry) return;
        }

        tinta::Matrix3x2F previous;
        app.renderTarget->GetTransform(&previous);
        app.renderTarget->SetTransform(
            tinta::matrixTranslation(shape.rect.left - app.scrollX,
                                          shape.rect.top - app.scrollY) * previous);
        if (shape.fill.a > 0.0f) {
            app.brush->SetColor(shape.fill);
            app.renderTarget->FillGeometry(shape.geometry.get(), app.brush);
            app.drawCalls++;
        }
        if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
            app.brush->SetColor(shape.stroke);
            app.renderTarget->DrawGeometry(
                shape.geometry.get(), app.brush, shape.strokeWidth);
            app.drawCalls++;
        }
        app.renderTarget->SetTransform(previous);
    };

    for (auto& shape : app.layoutShapes) {
        if (shape.rect.bottom < viewportTop - cullMargin ||
            shape.rect.top > viewportBottom + cullMargin ||
            shape.rect.right < viewportLeft - cullMargin ||
            shape.rect.left > viewportRight + cullMargin) {
            continue;
        }

        tinta::RectF rect = tinta::rectF(
            shape.rect.left - app.scrollX,
            shape.rect.top - app.scrollY,
            shape.rect.right - app.scrollX,
            shape.rect.bottom - app.scrollY);

        if (shape.type == App::LayoutShapeType::Path) {
            // Geometry was built eagerly at layout time in local space
            if (!shape.geometry) continue;
            tinta::Matrix3x2F previous;
            app.renderTarget->GetTransform(&previous);
            app.renderTarget->SetTransform(
                tinta::matrixTranslation(shape.rect.left - app.scrollX,
                                              shape.rect.top - app.scrollY) *
                previous);
            if (shape.fill.a > 0.0f) {
                app.brush->SetColor(shape.fill);
                app.renderTarget->FillGeometry(shape.geometry.get(), app.brush);
                app.drawCalls++;
            }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                app.brush->SetColor(shape.stroke);
                app.renderTarget->DrawGeometry(
                    shape.geometry.get(), app.brush, shape.strokeWidth);
                app.drawCalls++;
            }
            app.renderTarget->SetTransform(previous);
            continue;
        }

        if (shape.type == App::LayoutShapeType::Diamond) {
            float w = shape.rect.right - shape.rect.left;
            float h = shape.rect.bottom - shape.rect.top;
            tinta::PointF points[] = {
                tinta::pointF(w * 0.5f, 0),
                tinta::pointF(w, h * 0.5f),
                tinta::pointF(w * 0.5f, h),
                tinta::pointF(0, h * 0.5f),
            };
            drawPolygon(points, 4, shape);
            continue;
        }

        if (shape.type == App::LayoutShapeType::Hexagon) {
            float w = shape.rect.right - shape.rect.left;
            float h = shape.rect.bottom - shape.rect.top;
            float inset = w * 0.18f;
            tinta::PointF points[] = {
                tinta::pointF(inset, 0),
                tinta::pointF(w - inset, 0),
                tinta::pointF(w, h * 0.5f),
                tinta::pointF(w - inset, h),
                tinta::pointF(inset, h),
                tinta::pointF(0, h * 0.5f),
            };
            drawPolygon(points, 6, shape);
            continue;
        }

        app.brush->SetColor(shape.fill);
        if (shape.type == App::LayoutShapeType::Ellipse) {
            tinta::Ellipse ellipse = tinta::ellipse(
                tinta::pointF(
                    (rect.left + rect.right) * 0.5f,
                    (rect.top + rect.bottom) * 0.5f),
                (rect.right - rect.left) * 0.5f,
                (rect.bottom - rect.top) * 0.5f);
            if (shape.fill.a > 0.0f) {
                app.renderTarget->FillEllipse(ellipse, app.brush);
                app.drawCalls++;
            }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                app.brush->SetColor(shape.stroke);
                app.renderTarget->DrawEllipse(
                    ellipse, app.brush, shape.strokeWidth);
                app.drawCalls++;
            }
            continue;
        }

        if (shape.type == App::LayoutShapeType::RoundedRectangle ||
            shape.type == App::LayoutShapeType::Stadium) {
            float radius = shape.type == App::LayoutShapeType::Stadium
                ? (rect.bottom - rect.top) * 0.5f
                : shape.radius;
            tinta::RoundedRect rounded = tinta::roundedRect(rect, radius, radius);
            if (shape.fill.a > 0.0f) {
                app.renderTarget->FillRoundedRectangle(rounded, app.brush);
                app.drawCalls++;
            }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                app.brush->SetColor(shape.stroke);
                app.renderTarget->DrawRoundedRectangle(
                    rounded, app.brush, shape.strokeWidth);
                app.drawCalls++;
            }
            continue;
        }

        if (shape.fill.a > 0.0f) {
            app.renderTarget->FillRectangle(rect, app.brush);
            app.drawCalls++;
        }
        if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
            app.brush->SetColor(shape.stroke);
            app.renderTarget->DrawRectangle(
                rect, app.brush, shape.strokeWidth);
            app.drawCalls++;
        }
    }

    // Render images (bitmaps)
    // const& is preserved, and it now costs nothing to preserve. This comment used
    // to explain that LayoutBitmap::seam() was "const over mutable storage precisely
    // so this loop does not have to give up its constness to draw" -- that accessor
    // and its mutable Seated storage are gone. The field is a
    // shared_ptr<tinta::Bitmap>, so .get() on a const element is const already.
    for (const auto& bmp : app.layoutBitmaps) {
        if (!bmp.bitmap) continue;
        if (bmp.destRect.bottom < viewportTop - cullMargin ||
            bmp.destRect.top > viewportBottom + cullMargin) continue;
        if (bmp.destRect.right < viewportLeft - cullMargin ||
            bmp.destRect.left > viewportRight + cullMargin) continue;
        app.renderTarget->DrawBitmap(bmp.bitmap.get(),
            tinta::rectF(bmp.destRect.left - app.scrollX,
                         bmp.destRect.top - app.scrollY,
                         bmp.destRect.right - app.scrollX,
                         bmp.destRect.bottom - app.scrollY));
        app.drawCalls++;
    }

    for (const auto& run : app.layoutTextRuns) {
        if (run.bounds.bottom < viewportTop - cullMargin ||
            run.bounds.top > viewportBottom + cullMargin) {
            continue;
        }
        if (run.bounds.right < viewportLeft - cullMargin ||
            run.bounds.left > viewportRight + cullMargin) {
            continue;
        }
        app.brush->SetColor(run.color);
        tinta::PointF drawPos = tinta::pointF(run.pos.x - app.scrollX, run.pos.y - app.scrollY);
        // ONE CALL, BOTH BACKENDS, AND THE COLOUR-EMOJI OPTION GOES THROUGH THE SEAM.
        //
        // This was two arms until 2026-09-19: a `#ifdef TINTA_D2D_BACKEND` branch calling
        // app.deviceContext->DrawTextLayout(..., D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT)
        // whenever the QueryInterface for ID2D1DeviceContext had succeeded, falling back to
        // this seam call when it had not. The native arm was a duplicate of a path the seam
        // has carried since it was built, and four comments in this tree -- here, in app.h,
        // in paint_document.h and in render_d2d.h -- each asserted colour emoji had "no seam
        // equivalent", citing one another. All four were wrong the whole time:
        // tinta::DrawTextOptions::EnableColorFont (the enumerator in include/text_api.h) is
        // static_assert'd equal to D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT near the top of
        // src/render_d2d.cpp and forwarded by D2DRenderTarget::DrawTextLayout in that same
        // file, and src/math_render.cpp was already passing it.
        //
        // Those three citations were LINE NUMBERS until 2026-09-19 and two of them were
        // already stale -- include/text_api.h:184 had become :205 and the forward is not on
        // the line named -- both moved by the very comments written to fix this class of rot.
        // Symbols do not move; grep for them.
        //
        // Measured before and after, not argued: tests/render_api_shared.cpp now asserts this
        // flag across both backends, and it reads channel spread 0 without the flag against
        // 214 with it on Direct2D -- a black brush cannot produce a saturated pixel any other
        // way. Qt reads 209 either way, because QTextLayout hands the run to QPainter, which
        // resolves a colour font itself; Qt has therefore been rendering colour emoji all
        // along through the plain call this line replaces, which is the clearest evidence the
        // native arm bought nothing.
        //
        // ONE DELIBERATE BEHAVIOUR CHANGE, AND IT HAS AN UNMEASURED TAIL. Stated plainly
        // because the first version of this comment called it "an improvement rather than a
        // wash", which was a claim about every run made from a measurement of one.
        //
        // The old code passed the option only when the QueryInterface for ID2D1DeviceContext
        // had succeeded, and fell through to a no-option seam call when it had not. The
        // option is now unconditional. Where the QI succeeds -- which is the case on every
        // host this has been run on, and the case the pixel figures above were taken under --
        // the two are identical. Where it FAILS, the old code was monochrome by construction
        // and the new code hands bit 4 to a target that D2DRenderTarget declares as
        // ID2D1RenderTarget*, not as a device context. What Direct2D does with the colour
        // bit on a plain render target is NOT measured here and cannot be settled from this
        // tree; the QI's own HRESULT is discarded in src/d2d_init.cpp, so nothing downstream
        // can even observe which case it is in.
        //
        // WHAT KEEPS THIS FROM BEING A NEW RISK: src/math_render.cpp has passed the same bit
        // through the same seam method to the same target unconditionally since a7bdb91, with
        // no QI check of any kind. So the hazard predates this change by a long way. What
        // this change does is widen its reach from documents containing maths to every
        // document. That is a real widening and worth knowing; it is not a path this commit
        // opened, and reinstating a guard here would not close it while math_render stands.
        app.renderTarget->DrawTextLayout(drawPos, run.layout.get(), app.brush,
                                         tinta::DrawTextOptions::EnableColorFont);
        app.drawCalls++;
    }

    for (const auto& line : app.layoutLines) {
        float minY = std::min(line.p1.y, line.p2.y);
        float maxY = std::max(line.p1.y, line.p2.y);
        if (maxY < viewportTop - cullMargin || minY > viewportBottom + cullMargin) {
            continue;
        }
        app.brush->SetColor(line.color);
        app.renderTarget->DrawLine(
            tinta::pointF(line.p1.x - app.scrollX, line.p1.y - app.scrollY),
            tinta::pointF(line.p2.x - app.scrollX, line.p2.y - app.scrollY),
            app.brush, line.stroke);
        app.drawCalls++;
    }
}
