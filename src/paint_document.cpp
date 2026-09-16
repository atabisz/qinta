#include "paint_document.h"

#include "app.h"

#include <algorithm>
#include <cmath>
#include <memory>

// TINTA_D2D_BACKEND, NOT _WIN32, AND THAT DISTINCTION COST A LINK FAILURE. All three
// guards in this file said `#ifdef _WIN32` when they were written, which was true of
// every target that existed then. tinta_qt is a WINDOWS target with no Direct2D backend,
// so _WIN32 was defined, the colour-emoji branch below compiled, and the link came back
// `unresolved external symbol nativeTextLayout` out of src/text_d2d.cpp -- a file
// tinta_qt correctly does not compile. The dashed block further down was worse: it
// resolves through d2d1.lib, so it would have LINKED and then dereferenced a null
// app.d2dFactory at the first dashed mermaid connector.
//
// The macro is defined by CMakeLists.txt on the targets that actually link the backend,
// gated on TINTA_LEGACY_D2D, and it goes away when those two backend files are deleted.
// The question a guard in a file BOTH stacks compile has to ask is which backend it got,
// never which OS it is on.
#ifdef TINTA_D2D_BACKEND
// The colour-emoji branch needs the layout back as an IDWriteTextLayout*, and
// nativeTextLayout() is the asserted downcast that does it. DirectWrite-only by
// construction, so the include is too -- text_d2d.h names DirectWrite types.
#include "text_d2d.h"
#endif

namespace {

#ifndef TINTA_D2D_BACKEND
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
#endif

}  // namespace

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

    // Dashed stroke is a factory object: create once, reuse every frame
    if (!app.dashedStrokeStyle &&
        std::any_of(
            app.layoutConnectors.begin(), app.layoutConnectors.end(),
            [](const App::LayoutConnector& connector) { return connector.dashed; })) {
#ifdef TINTA_D2D_BACKEND
        D2D1_STROKE_STYLE_PROPERTIES properties = {
            D2D1_CAP_STYLE_FLAT,
            D2D1_CAP_STYLE_FLAT,
            D2D1_CAP_STYLE_FLAT,
            D2D1_LINE_JOIN_MITER,
            10.0f,
            D2D1_DASH_STYLE_DASH,
            0.0f,
        };
        app.d2dFactory->CreateStrokeStyle(
            properties, nullptr, 0, &app.nativeDashedStrokeStyle);
        // A DEFECT FIX, AND IT IS MINE. Commit fc9ceb0 ("Route the app's drawing
        // calls through the portable render seam") changed the out-parameter above
        // from app.dashedStrokeStyle to app.nativeDashedStrokeStyle and changed the
        // local below from ID2D1StrokeStyle* to tinta::StrokeStyle* -- and never
        // added the line that seats the wrapper over the new native. So since that
        // commit, on Windows: app.dashedStrokeStyle stayed null forever, every
        // dashed mermaid connector drew SOLID, and because the null check above is
        // the "create once" latch, CreateStrokeStyle ran again on every frame that
        // had a dashed connector, overwriting the previous pointer without a
        // Release. A silent behaviour regression plus a per-frame COM leak, from a
        // seam whose entire contract is that behaviour does not change.
        //
        // Two lessons, both cheap to state and both already true elsewhere in this
        // tree. The compiler cannot see this class of bug: both spellings exist,
        // both type-check, and the wrong one is merely never read. And no test
        // covers it -- the dashed path is a mermaid connector on a painted window,
        // which is exactly the region this slice is the first to put under test.
        // The fix is one call, and it belongs here rather than at init: at init
        // (src/d2d_init.cpp:673) the native is still null, and Seated::seat(nullptr)
        // resets rather than seats, which is why the init call could not do it.
        app.seatDashedStrokeStyle();
#else
        // No factory and no native pointer without the Direct2D backend, so the marker
        // stands in. THIS ARM RUNS ON WINDOWS TOO, under tinta_qt -- it is not the
        // "non-Windows" arm any more, and reading it as one is how the guard came to be
        // spelled _WIN32 in the first place.
        //
        // Through the storage rather than by assigning app.dashedStrokeStyle a
        // pointer to a static: App's storage is what OWNS a seam wrapper in this
        // tree, and install() is the door include/seated.h wrote for exactly this
        // case ("nothing off Windows can produce a native COM pointer, so seat() is
        // unreachable there"). Same call shape as src/qt_init.cpp:18. A file-local
        // static would work too and would leave App holding a pointer whose lifetime
        // no member records -- which is the shape the Seated refactor removed.
        app.dashedStrokeStyle = app.dashedStrokeStyleStorage.install(
            std::make_unique<MarkerStrokeStyle>());
#endif
    }
    tinta::StrokeStyle* dashedStrokeStyle = app.dashedStrokeStyle;

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
#ifdef TINTA_D2D_BACKEND
        if (app.deviceContext) {
            // Stays native, deliberately. app.deviceContext is the colour-emoji path
            // and has no seam equivalent (see its declaration in app.h), so this is
            // the one call that needs the layout back as an IDWriteTextLayout*.
            app.deviceContext->DrawTextLayout(drawPos, nativeTextLayout(run.layout.get()),
                app.nativeBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        } else
#endif
        {
            // The `} else` above closes inside the #ifdef on purpose, so that without
            // the Direct2D backend this block is simply the whole branch and there is no
            // second copy of the seam call to keep in step. app.deviceContext exists off
            // Windows as a forward-declared pointer that is always null, so the runtime
            // shape is identical either way; what the #ifdef buys is that an incomplete
            // ID2D1DeviceContext is never dereferenced at COMPILE time -- and, since the
            // guard became TINTA_D2D_BACKEND, that tinta_qt never references
            // nativeTextLayout, which is what its link failure was.
            //
            // Onto the seam, which is one of the 26 DrawTextLayout sites render_api.h
            // was sized for. It could not move before this commit: the layout was a
            // native pointer, and passing it here would have needed a wrapper built
            // per frame per run.
            app.renderTarget->DrawTextLayout(drawPos, run.layout.get(), app.brush);
        }
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
