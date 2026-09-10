#ifndef TINTA_RENDER_QT_H
#define TINTA_RENDER_QT_H

#include "render_api.h"

#include <QColor>
#include <QPainter>

class QPaintDevice;

// The Qt implementation of the drawing seam.
//
// This is the first non-Direct2D backend in the tree, and it exists to answer one
// question that no amount of reading could settle: does the Direct2D CALL SHAPE
// survive translation? Not "can Qt draw" -- obviously it can -- but whether
// Direct2D's arguments map onto QPainter's without a lossy or ambiguous step,
// because if they do not, the plan of keeping 915 call sites unedited collapses.
//
// The mappings that turned out to be exact, and are asserted in pixels by
// tests/render_api_tests.cpp:
//
//   FillRoundedRectangle(rect, radiusX, radiusY)  -> drawRoundedRect(r, rx, ry)
//   DrawLine(a, b, brush, strokeWidth)            -> QPen width, centred on the line
//   FillEllipse(centre, radiusX, radiusY)         -> drawEllipse(centre, rx, ry)
//   PushAxisAlignedClip / Pop                     -> save() + setClipRect(IntersectClip) / restore()
//
// Two differences that are real and are handled here rather than left to callers:
//
//   * Rectangles. tinta::RectF is left/top/right/bottom (it inherited D2D's
//     shape); QRectF is x/y/width/height. Converting in one place is the whole
//     reason toQt() exists -- geometry.h warns that qmd::Rect was NOT reused for
//     RectF precisely because mixing the two conventions silently reinterprets
//     every rectangle it touches.
//   * Antialiasing. Direct2D antialiases primitives by default; QPainter does
//     not. Without the render hint below, every rounded corner and every diagonal
//     line would come out hard-edged -- which is a visual regression that no
//     compile and no exit code would report.
class QtRenderTarget final : public tinta::RenderTarget {
public:
    // Borrows the device: the caller owns it and must outlive this object. The
    // painter is begun in the constructor and ended in the destructor, so scope
    // is the drawing session. That is why BeginDraw/EndDraw are absent from the
    // portable interface -- one backend's explicit call pair is another's RAII.
    explicit QtRenderTarget(QPaintDevice& device);
    ~QtRenderTarget() override;

    void Clear(const tinta::Color& color) override;

    void FillRectangle(const tinta::RectF& rect, tinta::Brush* brush) override;
    void DrawRectangle(const tinta::RectF& rect, tinta::Brush* brush,
                       float strokeWidth = 1.0f) override;

    void FillRoundedRectangle(const tinta::RoundedRect& rect, tinta::Brush* brush) override;
    void DrawRoundedRectangle(const tinta::RoundedRect& rect, tinta::Brush* brush,
                              float strokeWidth = 1.0f) override;

    void FillEllipse(const tinta::Ellipse& ellipse, tinta::Brush* brush) override;
    void DrawEllipse(const tinta::Ellipse& ellipse, tinta::Brush* brush,
                     float strokeWidth = 1.0f) override;

    void DrawLine(tinta::PointF start, tinta::PointF end, tinta::Brush* brush,
                  float strokeWidth = 1.0f,
                  tinta::StrokeStyle* strokeStyle = nullptr) override;

    void PushAxisAlignedClip(const tinta::RectF& rect, tinta::AntialiasMode mode) override;
    void PopAxisAlignedClip() override;

private:
    void setFill(tinta::Brush* brush);
    void setStroke(tinta::Brush* brush, float strokeWidth,
                   tinta::StrokeStyle* strokeStyle);

    QPainter painter_;
};

// A solid colour and nothing else. Direct2D needs a device-owned object here
// because a brush is a GPU resource; Qt does not, so this deliberately holds no
// painter reference and can be created before any render target exists.
class QtBrush final : public tinta::Brush {
public:
    QtBrush() = default;
    explicit QtBrush(const tinta::Color& color) : color_(color) {}

    void SetColor(const tinta::Color& color) override { color_ = color; }
    tinta::Color GetColor() const override { return color_; }

private:
    tinta::Color color_{0.0f, 0.0f, 0.0f, 1.0f};
};

// Conversions, exposed because the test asserts on them directly and because a
// second Qt-side file will need the same ones. Free functions rather than member
// operators: tinta's value types must stay aggregates (geometry.h explains why),
// and these convert the other way in any case.
QColor toQt(const tinta::Color& color);
QRectF toQt(const tinta::RectF& rect);
QPointF toQt(const tinta::PointF& point);

#endif  // TINTA_RENDER_QT_H
