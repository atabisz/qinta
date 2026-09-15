#ifndef TINTA_RENDER_QT_H
#define TINTA_RENDER_QT_H

#include "render_api.h"
#include "text_qt.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

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
//   SetTransform / GetTransform                   -> setWorldTransform(t, /*combine=*/false) / worldTransform()
//
// That last one is exact in a way worth stating, because it is the mapping most
// likely to be assumed rather than checked: QTransform and D2D1_MATRIX_3X2_F both
// name their members m11/m12/m21/m22/dx/dy AND both compose in row-vector order,
// so `a * b` means "apply a, then b" in each. Measured on a non-commutative pair
// (Qt: translate(10,0) * scale(2,2) maps (1,1) to (22,2), the control the other way
// round gives (12,2)), not inferred from the naming.
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

    // Default arguments are stated ONLY in render_api.h. Repeating them here would
    // create a second source of truth that a call through a QtRenderTarget* would
    // silently prefer -- and if the two ever disagreed, the same call would mean
    // different things depending on the static type of the pointer it went through.
    void FillRectangle(const tinta::RectF& rect, tinta::Brush* brush) override;
    void DrawRectangle(const tinta::RectF& rect, tinta::Brush* brush,
                       float strokeWidth) override;

    void FillRoundedRectangle(const tinta::RoundedRect& rect, tinta::Brush* brush) override;
    void DrawRoundedRectangle(const tinta::RoundedRect& rect, tinta::Brush* brush,
                              float strokeWidth) override;

    void FillEllipse(const tinta::Ellipse& ellipse, tinta::Brush* brush) override;
    void DrawEllipse(const tinta::Ellipse& ellipse, tinta::Brush* brush,
                     float strokeWidth) override;

    void DrawLine(tinta::PointF start, tinta::PointF end, tinta::Brush* brush,
                  float strokeWidth, tinta::StrokeStyle* strokeStyle) override;

    std::unique_ptr<tinta::PathSink> BeginPath() override;
    void FillGeometry(tinta::PathGeometry* geometry, tinta::Brush* brush) override;
    void DrawGeometry(tinta::PathGeometry* geometry, tinta::Brush* brush,
                      float strokeWidth) override;

    // Text goes through QtTextRun, the same engine QtTextLayout::GetMetrics uses,
    // so a box that was measured and a box that was drawn cannot disagree. The
    // rect's width and height become the layout box, which is what Direct2D's
    // layoutRect means.
    void DrawText(const wchar_t* string, uint32_t stringLength, tinta::TextFormat* format,
                  const tinta::RectF& layoutRect, tinta::Brush* brush,
                  tinta::DrawTextOptions options) override;
    void DrawTextLayout(tinta::PointF origin, tinta::TextLayout* layout,
                        tinta::Brush* brush, tinta::DrawTextOptions options) override;

    std::unique_ptr<tinta::Bitmap> CreateBitmap(uint32_t pixelWidth, uint32_t pixelHeight,
                                               const uint8_t* premultipliedBgra,
                                               uint32_t stride, float dpi) override;

    void DrawBitmap(tinta::Bitmap* bitmap, const tinta::RectF& destRect, float opacity,
                    tinta::BitmapInterpolationMode interpolationMode,
                    const tinta::RectF* srcRect) override;

    void PushAxisAlignedClip(const tinta::RectF& rect, tinta::AntialiasMode mode) override;
    void PopAxisAlignedClip() override;

    void SetTransform(const tinta::Matrix3x2F& transform) override;
    void GetTransform(tinta::Matrix3x2F* transform) override;

private:
    void setFill(tinta::Brush* brush);
    void setStroke(tinta::Brush* brush, float strokeWidth,
                   tinta::StrokeStyle* strokeStyle);

    // BOTH text calls end here. DrawText builds a run from its layoutRect and
    // DrawTextLayout borrows the one its layout already measured, but from this
    // point on the two are identical -- and they must stay identical, because the
    // pen-versus-brush rule and the Clip translation are each a one-line mistake
    // away from drawing nothing at all.
    void drawRun(const QtTextRun& run, const QPointF& origin, tinta::Brush* brush,
                 tinta::DrawTextOptions options);

    QPainter painter_;
};

// OWNS its image, which is the opposite of D2DBitmap and the same choice QtBrush
// makes: a QImage is not a device resource, so there is no device for it to belong
// to and nothing for it to outlive. QImage is copy-on-write, so holding one by value
// costs a refcount rather than a pixel copy.
//
// Format is deliberately NOT converted on construction. The caller decides, because
// a Format_ARGB32 (straight alpha) and a Format_ARGB32_Premultiplied image are both
// legitimate here and silently converting would make the seam's opacity argument
// mean something different depending on which one was handed in.
class QtBitmap final : public tinta::Bitmap {
public:
    explicit QtBitmap(QImage image) : image_(std::move(image)) {}

    tinta::SizeF GetSize() const override;

    const QImage& image() const { return image_; }

private:
    QImage image_;
};

// OWNS its QPainterPath, the same way QtBitmap owns its QImage and for the same
// reason: a QPainterPath is a value, not a device resource, so there is nothing for
// it to belong to. That makes this the SIMPLE half of the path pair -- D2DPathGeometry
// had to be given a destructor and deleted copies because ID2D1PathGeometry is
// refcounted; here the compiler-generated members are correct.
//
// No accessors beyond path(), because render_api.h::PathGeometry deliberately has
// none: nothing in the tree reads a finished path back.
class QtPathGeometry final : public tinta::PathGeometry {
public:
    explicit QtPathGeometry(QPainterPath path) : path_(std::move(path)) {}

    const QPainterPath& path() const { return path_; }

private:
    QPainterPath path_;
};

// Builds a QPainterPath through the D2D sink call sequence.
//
// THE ONE PLACE THE TWO ARC PARAMETERISATIONS MEET. Direct2D gives an endpoint and
// a large/small flag; Qt's arcTo wants a bounding box, a start angle and a sweep
// length in DEGREES, positive being COUNTER-clockwise on screen. So the conversion
// is a solve for the centre (tinta::arcCenterFromEndpoints, in geometry.h so both
// compilers assert it) plus two sign flips, and the signs are the part that would
// otherwise produce a plausible arc bulging the wrong way.
//
// Qt has no BeginFigure/EndFigure pair -- moveTo starts a subpath and closeSubpath
// ends it -- so the seam's two calls map onto those. EndFigure is unconditionally
// CLOSED because the census found 7 of 7 sites closing.
class QtPathSink final : public tinta::PathSink {
public:
    void BeginFigure(tinta::PointF start) override;
    void AddLine(tinta::PointF point) override;
    void AddLines(const tinta::PointF* points, uint32_t count) override;
    void AddArc(const tinta::ArcSegment& arc) override;
    void EndFigure() override;
    std::unique_ptr<tinta::PathGeometry> Close() override;

    // Exposed for the test, which asserts on the built path's element sequence
    // rather than on painted pixels: an arc's element list is what shows the sign
    // convention came out right, and a screenshot comparison of two similar arcs
    // would not.
    const QPainterPath& path() const { return path_; }

private:
    QPainterPath path_;
    // Where the current subpath started, so EndFigure can close and AddArc can
    // report a degenerate arc. QPainterPath::currentPosition() covers the second
    // need; this covers neither on its own and is kept for the first because
    // closeSubpath is a no-op on an empty path and silence there is the failure
    // mode this seam is meant to remove.
    bool figureOpen_ = false;
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
