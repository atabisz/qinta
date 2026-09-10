#include "render_qt.h"

#include <QPaintDevice>
#include <QPen>

// See include/render_qt.h for why this backend exists and which Direct2D-to-Qt
// mappings it is claiming. Everything here is a translation; nothing decides
// policy.

QColor toQt(const tinta::Color& color) {
    // tinta::Color carries 0..1 floats because D2D1_COLOR_F did. fromRgbF keeps
    // that domain, so a pure red round-trips to exactly 255 rather than to 254
    // through an int multiply.
    return QColor::fromRgbF(color.r, color.g, color.b, color.a);
}

QRectF toQt(const tinta::RectF& rect) {
    // left/top/right/bottom -> x/y/width/height. The one conversion in this file
    // that would corrupt every drawing call if it were wrong, which is why the
    // test asserts a filled region's edges and not merely that something was
    // painted.
    return QRectF(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

QPointF toQt(const tinta::PointF& point) {
    return QPointF(point.x, point.y);
}

QtRenderTarget::QtRenderTarget(QPaintDevice& device) : painter_(&device) {
    // Direct2D antialiases primitives unless told otherwise; QPainter does the
    // opposite. Matching D2D's default here is what makes a rounded corner
    // actually round rather than stepped.
    painter_.setRenderHint(QPainter::Antialiasing, true);
}

QtRenderTarget::~QtRenderTarget() = default;

void QtRenderTarget::setFill(tinta::Brush* brush) {
    const QColor color = brush ? toQt(brush->GetColor()) : QColor(Qt::transparent);
    painter_.setPen(Qt::NoPen);
    painter_.setBrush(color);
}

void QtRenderTarget::setStroke(tinta::Brush* brush, float strokeWidth,
                               tinta::StrokeStyle* strokeStyle) {
    const QColor color = brush ? toQt(brush->GetColor()) : QColor(Qt::transparent);
    QPen pen(color);
    pen.setWidthF(strokeWidth);
    // TWO QT DEFAULTS THAT DISAGREE WITH DIRECT2D, and both are silent.
    //
    // Qt's default cap is SquareCap, which EXTENDS a line by half the pen width
    // beyond each endpoint; Direct2D's default stroke style is flat-capped, so it
    // stops exactly where it was told to. On a 1px hairline the difference is
    // invisible, which is what makes it dangerous -- tinta draws plenty of thicker
    // rules, and there every one of them would grow at both ends.
    //
    // Qt's default join is BevelJoin, Direct2D's is miter. That one shows up as
    // subtly clipped corners on stroked rectangles.
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    // Dash patterns are not read back from a StrokeStyle (it is an opaque handle
    // by design -- nothing in the tree inspects one), so a non-null style means
    // "dashed" and nothing more. When the D2D side is ported, the two dashed
    // connector call sites are the population to check this against.
    if (strokeStyle) pen.setStyle(Qt::DashLine);
    painter_.setPen(pen);
    painter_.setBrush(Qt::NoBrush);
}

void QtRenderTarget::Clear(const tinta::Color& color) {
    // D2D's Clear REPLACES the target contents, including alpha; Qt's default
    // composition would blend instead, so a Clear to a transparent colour would
    // leave whatever was underneath. Source mode makes the two agree.
    const QPainter::CompositionMode previous = painter_.compositionMode();
    painter_.setCompositionMode(QPainter::CompositionMode_Source);
    painter_.fillRect(painter_.viewport(), toQt(color));
    painter_.setCompositionMode(previous);
}

void QtRenderTarget::FillRectangle(const tinta::RectF& rect, tinta::Brush* brush) {
    setFill(brush);
    painter_.drawRect(toQt(rect));
}

void QtRenderTarget::DrawRectangle(const tinta::RectF& rect, tinta::Brush* brush,
                                   float strokeWidth) {
    setStroke(brush, strokeWidth, nullptr);
    painter_.drawRect(toQt(rect));
}

void QtRenderTarget::FillRoundedRectangle(const tinta::RoundedRect& rect,
                                          tinta::Brush* brush) {
    setFill(brush);
    painter_.drawRoundedRect(toQt(rect.rect), rect.radiusX, rect.radiusY);
}

void QtRenderTarget::DrawRoundedRectangle(const tinta::RoundedRect& rect,
                                          tinta::Brush* brush, float strokeWidth) {
    setStroke(brush, strokeWidth, nullptr);
    painter_.drawRoundedRect(toQt(rect.rect), rect.radiusX, rect.radiusY);
}

void QtRenderTarget::FillEllipse(const tinta::Ellipse& ellipse, tinta::Brush* brush) {
    setFill(brush);
    // Both APIs take a CENTRE plus two radii, so this needs no bounding-box
    // arithmetic -- the overload that takes radii is the exact match.
    painter_.drawEllipse(toQt(ellipse.point), ellipse.radiusX, ellipse.radiusY);
}

void QtRenderTarget::DrawEllipse(const tinta::Ellipse& ellipse, tinta::Brush* brush,
                                 float strokeWidth) {
    setStroke(brush, strokeWidth, nullptr);
    painter_.drawEllipse(toQt(ellipse.point), ellipse.radiusX, ellipse.radiusY);
}

void QtRenderTarget::DrawLine(tinta::PointF start, tinta::PointF end,
                              tinta::Brush* brush, float strokeWidth,
                              tinta::StrokeStyle* strokeStyle) {
    setStroke(brush, strokeWidth, strokeStyle);
    painter_.drawLine(toQt(start), toQt(end));
}

void QtRenderTarget::PushAxisAlignedClip(const tinta::RectF& rect,
                                         tinta::AntialiasMode mode) {
    // save() is what makes these nest: D2D allows a push inside a push, and every
    // push in the tree is matched by a pop in the same function.
    painter_.save();
    // D2D1_ANTIALIAS_MODE_ALIASED snaps the clip to pixel boundaries, which is
    // what all 9 call sites ask for. Qt clips aliased unless the antialiasing
    // hint is on, so the hint is turned off for the aliased case rather than
    // being left at the constructor's setting.
    painter_.setRenderHint(QPainter::Antialiasing,
                           mode != tinta::AntialiasMode::Aliased);
    painter_.setClipRect(toQt(rect), Qt::IntersectClip);
}

void QtRenderTarget::PopAxisAlignedClip() {
    painter_.restore();
}
