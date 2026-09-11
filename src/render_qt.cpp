#include "render_qt.h"

#include <QPaintDevice>
#include <QPen>
#include <QTransform>

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

void QtRenderTarget::DrawText(const wchar_t* string, uint32_t stringLength,
                              tinta::TextFormat* format, const tinta::RectF& layoutRect,
                              tinta::Brush* brush, tinta::DrawTextOptions options) {
    auto* qtFormat = dynamic_cast<QtTextFormat*>(format);
    if (!qtFormat) return;

    const QRectF box = toQt(layoutRect);
    const QtTextRun run(toQt(string, stringLength), qtFormat->font(), qtFormat->style(),
                        static_cast<float>(box.width()), static_cast<float>(box.height()));

    drawRun(run, box.topLeft(), brush, options);
}

void QtRenderTarget::DrawTextLayout(tinta::PointF origin, tinta::TextLayout* layout,
                                    tinta::Brush* brush, tinta::DrawTextOptions options) {
    auto* qtLayout = dynamic_cast<QtTextLayout*>(layout);
    if (!qtLayout) return;

    // The layout's own box is the clip rect when Clip is asked for, which is what
    // Direct2D does with this flag -- so the same helper serves both calls.
    drawRun(qtLayout->run(), toQt(origin), brush, options);
}

void QtRenderTarget::drawRun(const QtTextRun& run, const QPointF& origin,
                             tinta::Brush* brush, tinta::DrawTextOptions options) {
    const int flags = static_cast<int>(options);

    // Direct2D's Clip flag confines the glyphs to the layout rect. QPainter has no
    // per-call equivalent, so it becomes a scoped clip. Only two call sites in the
    // tree pass any option at all and neither passes Clip, so this path is
    // currently exercised only by the shared assertions -- which is the reason to
    // implement it now rather than to leave it silently ignored.
    const bool clip = (flags & static_cast<int>(tinta::DrawTextOptions::Clip)) != 0;
    if (clip) {
        const tinta::TextMetrics& metrics = run.metrics();
        painter_.save();
        painter_.setClipRect(QRectF(origin, QSizeF(metrics.layoutWidth, metrics.layoutHeight)),
                             Qt::IntersectClip);
    }

    // QTextLayout::draw paints with the PEN, not the brush -- the opposite of every
    // other call in this file, where setFill puts the colour in the brush and sets
    // NoPen. Getting this wrong draws nothing at all rather than drawing wrongly.
    painter_.setPen(brush ? toQt(brush->GetColor()) : QColor(Qt::transparent));
    run.draw(painter_, origin);

    if (clip) painter_.restore();
}

tinta::SizeF QtBitmap::GetSize() const {
    // deviceIndependentSize() rather than width()/height(), because the seam's
    // GetSize is documented as DIPs: an image carrying a devicePixelRatio must
    // report its logical size the way ID2D1Bitmap::GetSize does, or a 192-dpi
    // bitmap would come back twice the size Direct2D reports for the same image.
    const QSizeF size = image_.deviceIndependentSize();
    return tinta::SizeF{static_cast<float>(size.width()),
                        static_cast<float>(size.height())};
}

void QtRenderTarget::DrawBitmap(tinta::Bitmap* bitmap, const tinta::RectF& destRect,
                                float opacity,
                                tinta::BitmapInterpolationMode interpolationMode,
                                const tinta::RectF* srcRect) {
    auto* qtBitmap = dynamic_cast<QtBitmap*>(bitmap);
    if (!qtBitmap) return;

    const QImage& image = qtBitmap->image();

    // Null srcRect means the whole bitmap, and Qt has no null-rect overload that
    // also takes a target rect -- drawImage(QRectF, QImage) exists but ignores any
    // source region, so the two cases have to be spelled separately. The source rect
    // is in PIXELS (QImage::rect), not DIPs, which is why this does not go through
    // GetSize.
    const QRectF source = srcRect ? toQt(*srcRect) : QRectF(image.rect());

    // Qt's default is smooth-off for image scaling; Direct2D's is linear. Setting the
    // hint per call rather than in the constructor is what makes the mode argument
    // observable -- the constructor's Antialiasing hint does not govern image
    // sampling, so without this line NearestNeighbor and Linear would draw
    // identically and a backend ignoring the argument would pass every assertion.
    const bool previousSmooth = painter_.testRenderHint(QPainter::SmoothPixmapTransform);
    painter_.setRenderHint(QPainter::SmoothPixmapTransform,
                           interpolationMode == tinta::BitmapInterpolationMode::Linear);

    // MULTIPLY, do not replace. D2D's opacity argument scales whatever the target is
    // already compositing with, and QPainter::setOpacity replaces the painter's
    // value outright -- so an unconditional set would silently cancel any opacity a
    // caller had established around the call. No site in the tree does that today,
    // which is exactly why it would go unnoticed.
    const qreal previousOpacity = painter_.opacity();
    painter_.setOpacity(previousOpacity * opacity);

    // Argument order is (target, image, source). Getting it backwards still draws
    // something -- a plausible picture of the wrong region at the wrong size -- which
    // is why the shared assertions pin the sub-rect with a two-colour bitmap and a
    // counter-case rather than merely checking that ink appeared.
    painter_.drawImage(toQt(destRect), image, source);

    painter_.setOpacity(previousOpacity);
    painter_.setRenderHint(QPainter::SmoothPixmapTransform, previousSmooth);
}

void QtRenderTarget::PushAxisAlignedClip(const tinta::RectF& rect,
                                         tinta::AntialiasMode mode) {
    // save() is what makes these nest: D2D allows a push inside a push, and every
    // push in the tree is matched by a pop in the same function.
    painter_.save();
    // D2D1_ANTIALIAS_MODE_ALIASED snaps the clip to pixel boundaries. Qt clips
    // aliased unless the antialiasing hint is on, so the hint is turned off for the
    // aliased case rather than being left at the constructor's setting.
    //
    // THE BRANCH IS LOAD-BEARING and an earlier version of this comment said it was
    // not, claiming "all 9 call sites ask for" aliased. Measured: 5 of the 9 do, and
    // the other 4 pass D2D1_ANTIALIAS_MODE_PER_PRIMITIVE (src/overlays.cpp x3,
    // src/tabs.cpp). Collapsing this to an unconditional setRenderHint(false) --
    // which the old comment invited -- would harden those four clips with no compile
    // error and no failing assertion outside this suite.
    painter_.setRenderHint(QPainter::Antialiasing,
                           mode != tinta::AntialiasMode::Aliased);
    painter_.setClipRect(toQt(rect), Qt::IntersectClip);
}

void QtRenderTarget::PopAxisAlignedClip() {
    painter_.restore();
}

void QtRenderTarget::SetTransform(const tinta::Matrix3x2F& transform) {
    // setWorldTransform's second argument defaults to false, meaning REPLACE rather
    // than compose, which is what the seam specifies -- every call site in the tree
    // composes for itself. Passing true here would double-apply at all 10
    // app.renderTarget sites, and the picture would still look plausible.
    //
    // The argument order of QTransform's six-float constructor is m11, m12, m21,
    // m22, dx, dy, matching D2D1_MATRIX_3X2_F name for name. That was measured, not
    // read off the names: built this way, T * S maps (1,1) to (22,2), identical to
    // building the same two matrices with translate() and scale().
    painter_.setWorldTransform(QTransform(transform.m11, transform.m12, transform.m21,
                                          transform.m22, transform.dx, transform.dy));
}

void QtRenderTarget::GetTransform(tinta::Matrix3x2F* transform) {
    if (!transform) return;
    const QTransform current = painter_.worldTransform();
    *transform = tinta::Matrix3x2F{static_cast<float>(current.m11()),
                                   static_cast<float>(current.m12()),
                                   static_cast<float>(current.m21()),
                                   static_cast<float>(current.m22()),
                                   static_cast<float>(current.dx()),
                                   static_cast<float>(current.dy())};
}
