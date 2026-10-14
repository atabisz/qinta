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

void QtPathSink::BeginFigure(tinta::PointF start) {
    path_.moveTo(toQt(start));
    figureOpen_ = true;
}

void QtPathSink::AddLine(tinta::PointF point) {
    path_.lineTo(toQt(point));
}

void QtPathSink::AddLines(const tinta::PointF* points, uint32_t count) {
    // A loop, where the D2D backend reinterprets the whole run in one call. Both
    // are correct and the asymmetry is the point: D2D's AddLines takes an array of
    // a layout-compatible type, Qt's path has no array form at all, so the seam
    // keeping the array shape costs nothing on Windows and costs this loop here.
    if (!points) return;
    for (uint32_t i = 0; i < count; ++i) path_.lineTo(toQt(points[i]));
}

void QtPathSink::AddArc(const tinta::ArcSegment& arc) {
    const QPointF from = path_.currentPosition();

    tinta::PointF center{};
    float startAngle = 0.0f;
    float sweepAngle = 0.0f;
    const bool solved = tinta::arcCenterFromEndpoints(
        tinta::pointF(static_cast<float>(from.x()), static_cast<float>(from.y())),
        arc.endpoint, arc.radius, arc.largeArc, &center, &startAngle, &sweepAngle);

    // No such arc -- coincident endpoints. A straight line to the endpoint is the
    // closest thing to Direct2D's behaviour here (D2D draws nothing for a
    // degenerate segment and leaves the current point where it was; a zero-length
    // lineTo does the same), and geometry.h explains at length why this case is
    // preserved rather than reinterpreted as a full circle.
    if (!solved) {
        path_.lineTo(toQt(arc.endpoint));
        return;
    }

    // Degrees, and NEGATED. Qt measures angles counter-clockwise from 3 o'clock;
    // geometry.h returns a clockwise angle and a clockwise sweep, because that is
    // the only direction the tree's 4 arc sites use. Two sign flips, both here.
    const double toDegrees = 180.0 / 3.14159265358979323846;
    const QRectF box(center.x - arc.radius, center.y - arc.radius,
                     arc.radius * 2.0, arc.radius * 2.0);

    // arcTo implicitly lineTo's the arc's start point if the current position is
    // elsewhere. It should not be -- the arc was solved FROM the current position --
    // but float rounding can leave a sub-pixel gap, and Qt closing it with a
    // degenerate line is better than leaving the subpath broken.
    path_.arcTo(box, -static_cast<double>(startAngle) * toDegrees,
                -static_cast<double>(sweepAngle) * toDegrees);
}

void QtPathSink::EndFigure() {
    // Guarded, because closeSubpath() on a path with no open subpath is a silent
    // no-op in Qt -- and a silently ignored EndFigure is exactly the class of
    // failure this seam exists to make loud.
    if (!figureOpen_) return;
    path_.closeSubpath();
    figureOpen_ = false;
}

std::unique_ptr<tinta::PathGeometry> QtPathSink::Close() {
    // No failure path, which is why this never returns null: D2D's Close can fail
    // because it commits to a device object, and building a QPainterPath cannot.
    // The seam still declares the null case because the Windows backend needs it.
    figureOpen_ = false;
    return std::make_unique<QtPathGeometry>(std::move(path_));
}

namespace {

// Returns null rather than asserting on the wrong backend, which is the opposite of
// nativePathGeometry in the retired render_d2d.cpp. Deliberate: that one could assert because
// <cassert> and a Windows-only translation unit were both already there, and this
// file is compiled on three platforms where an abort in a paint call is a worse
// outcome than a missing shape. The dynamic_cast still catches the mistake -- it
// just declines to draw instead of terminating.
const QPainterPath* qtPath(tinta::PathGeometry* geometry) {
    auto* concrete = dynamic_cast<QtPathGeometry*>(geometry);
    return concrete ? &concrete->path() : nullptr;
}

}  // namespace

void QtRenderTarget::FillGeometry(tinta::PathGeometry* geometry, tinta::Brush* brush) {
    const QPainterPath* path = qtPath(geometry);
    if (!path) return;
    setFill(brush);
    // drawPath rather than fillPath, so that the pen/brush state setFill and
    // setStroke establish is the single place either operation is decided. fillPath
    // takes its own brush argument and would make this the one drawing call in the
    // file whose colour did not come from the same two helpers.
    painter_.drawPath(*path);
}

void QtRenderTarget::DrawGeometry(tinta::PathGeometry* geometry, tinta::Brush* brush,
                                  float strokeWidth) {
    const QPainterPath* path = qtPath(geometry);
    if (!path) return;
    setStroke(brush, strokeWidth, nullptr);
    painter_.drawPath(*path);
}

std::unique_ptr<tinta::PathSink> QtRenderTarget::BeginPath() {
    // Needs nothing from the painter, unlike the D2D backend which has to reach
    // ID2D1Factory through GetFactory(). render_api.h put BeginPath on RenderTarget
    // for Direct2D's sake and noted this asymmetry; this is it.
    return std::make_unique<QtPathSink>();
}

void QtRenderTarget::DrawText(const wchar_t* string, uint32_t stringLength,
                              tinta::TextFormat* format, const tinta::RectF& layoutRect,
                              tinta::Brush* brush, tinta::DrawTextOptions options) {
    auto* qtFormat = dynamic_cast<QtTextFormat*>(format);
    if (!qtFormat) return;

    const QRectF box = toQt(layoutRect);
    // No ranged formats, and that is a property of the seam rather than an omission
    // here: ranged styling lives on TextLayout, and this path takes a TextFormat --
    // a template with no text to have styled ranges of. Passed explicitly instead of
    // through a default argument so the reasoning has somewhere to sit.
    const QtTextRun run(toQt(string, stringLength), qtFormat->font(), qtFormat->style(),
                        static_cast<float>(box.width()), static_cast<float>(box.height()),
                        {});

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

std::unique_ptr<tinta::Bitmap> QtRenderTarget::CreateBitmap(uint32_t pixelWidth,
                                                           uint32_t pixelHeight,
                                                           const uint8_t* premultipliedBgra,
                                                           uint32_t stride, float dpi) {
    if (!pixelWidth || !pixelHeight || !premultipliedBgra || !stride) return nullptr;
    if (dpi <= 0.0f) return nullptr;

    // Format_ARGB32_Premultiplied, and the byte order is why it matches BGRA without
    // a swizzle: Qt's ARGB32 formats are 0xAARRGGBB in a 32-bit word, so on a
    // little-endian machine the bytes in memory read B, G, R, A -- exactly what
    // DXGI_FORMAT_B8G8R8A8_UNORM means. That is a real dependency on endianness, not
    // an accident, so it is asserted rather than assumed.
    static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN,
                  "QImage's ARGB32 word order only matches BGRA byte order on a "
                  "little-endian host; a big-endian port must swizzle here");

    // The QImage(uchar*, ...) constructor does NOT copy, and the caller's buffer is a
    // local vector at two of the three sites -- so this copies deliberately, via a
    // non-owning view and copy(). Holding the view would give a bitmap that outlives
    // its pixels and draws whatever later occupies that memory.
    const QImage view(premultipliedBgra, static_cast<int>(pixelWidth),
                      static_cast<int>(pixelHeight), static_cast<qsizetype>(stride),
                      QImage::Format_ARGB32_Premultiplied);
    if (view.isNull()) return nullptr;
    QImage image = view.copy();
    if (image.isNull()) return nullptr;

    // devicePixelRatio is how a QImage carries what dpi carries in Direct2D:
    // QtBitmap::GetSize reports deviceIndependentSize(), so a ratio of 2 makes a
    // 2x raster report its intrinsic size, which is what the SVG path needs. 96 is
    // the identity here for the same reason it is Direct2D's default.
    image.setDevicePixelRatio(static_cast<qreal>(dpi) / 96.0);
    return std::make_unique<QtBitmap>(std::move(image));
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
