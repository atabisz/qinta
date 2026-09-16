#include "image_api.h"

// For QtBitmap and QtRenderTarget, which decodeImageFileToBitmap needs. image_api.h
// forward-declares Bitmap and RenderTarget instead of including render_api.h, so each
// backend pulls in its own -- which is the arrangement that note in the header describes.
#include "render_qt.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QString>
#include <QSvgRenderer>
#include <QTemporaryFile>
#include <QUrl>
#include <QtGlobal>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

// The Qt half of include/image_api.h. A real implementation of the same three ideas,
// not a stand-in that returns false so the build proceeds.
//
// The byte-order assumption is spelled out because it is silent otherwise. WIC's
// GUID_WICPixelFormat32bppPBGRA names its bytes in memory order; Qt's
// Format_ARGB32_Premultiplied names a 32-bit integer, 0xAARRGGBB, whose memory order
// is therefore B,G,R,A on a little-endian machine and A,R,G,B on a big-endian one.
// They agree only on little-endian, which every platform this port targets is -- so
// the assumption is asserted here rather than left to produce channel-swapped images
// on a machine nobody tested.
#if Q_BYTE_ORDER != Q_LITTLE_ENDIAN
#error "image_qt.cpp assumes little-endian: Format_ARGB32_Premultiplied is BGRA in memory only there"
#endif

namespace tinta {

namespace {

// Copy a QImage's rows out as tightly packed premultiplied BGRA.
//
// Row by row, and not one memcpy of the whole buffer, because QImage pads each
// scanline to a 4-byte boundary and bytesPerLine() is therefore not always
// width * 4. For a 32-bit format the two happen to coincide, which is exactly why
// this is worth writing correctly once: a later format change would turn a whole-
// buffer copy into a slanted image rather than a compile error.
bool copyOutPixels(const QImage& src, DecodedImage& out) {
    const QImage img = src.format() == QImage::Format_ARGB32_Premultiplied
                           ? src
                           : src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (img.isNull()) return false;

    const unsigned w = static_cast<unsigned>(img.width());
    const unsigned h = static_cast<unsigned>(img.height());
    if (w == 0 || h == 0) return false;

    out.pixels.resize(static_cast<size_t>(w) * h * 4);
    for (unsigned y = 0; y < h; ++y) {
        std::memcpy(out.pixels.data() + static_cast<size_t>(y) * w * 4,
                    img.constScanLine(static_cast<int>(y)),
                    static_cast<size_t>(w) * 4);
    }
    out.width = w;
    out.height = h;
    out.ok = true;
    return true;
}

bool dimensionsAcceptable(int w, int h) {
    return w > 0 && h > 0 && static_cast<unsigned>(w) < kMaxImageDimension &&
           static_cast<unsigned>(h) < kMaxImageDimension;
}

} // namespace

std::wstring fetchToTempFile(const std::string& url) {
    // QNetworkAccessManager delivers through the event loop, so it needs a
    // QCoreApplication to exist. Returning empty rather than crashing when there
    // isn't one is what lets a test binary link this file without a network: the
    // failure is the same shape as an unreachable host, which every caller already
    // handles.
    if (!QCoreApplication::instance()) return std::wstring();

    const QUrl parsed = QUrl::fromUserInput(QString::fromStdString(url));
    if (!parsed.isValid()) return std::wstring();

    QNetworkAccessManager manager;
    QNetworkRequest request(parsed);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = manager.get(request);
    if (!reply) return std::wstring();

    // Blocking by contract -- the seam's callers are already off the UI thread, and
    // the Windows side blocks in URLDownloadToCacheFileW for the same reason.
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    std::wstring result;
    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray body = reply->readAll();
        if (!body.isEmpty()) {
            // Fetch-to-FILE, matching URLDownloadToCacheFileW rather than improving
            // on it: the caller decodes from a path. setAutoRemove(false) is what
            // makes the file outlive this scope, which is the whole point.
            QTemporaryFile temp;
            temp.setAutoRemove(false);
            if (temp.open() && temp.write(body) == body.size()) {
                temp.close();
                result = temp.fileName().toStdWString();
            }
        }
    }
    reply->deleteLater();
    return result;
}

DecodedImage decodeImageFile(const std::wstring& path) {
    DecodedImage out;
    QImage img;
    if (!img.load(QString::fromStdWString(path))) return out;
    if (!dimensionsAcceptable(img.width(), img.height())) return out;
    if (!copyOutPixels(img, out)) {
        out = DecodedImage();
    }
    return out;
}

// The Qt half of the local-raster branch. Five lines against the Windows side's sixty,
// and the asymmetry is the point rather than an accident: Qt's decoder already produces
// the platform's own image type, so there is no format converter to drive and no device
// upload to perform. QtBitmap holds its QImage by value and QImage is copy-on-write, so
// the move below transfers a refcount and copies no pixels -- which is what makes this
// entry point worth having on Qt too, not only on Direct2D. Routing the same image
// through decodeImageFile plus RenderTarget::CreateBitmap would cost two copies: one
// into DecodedImage::pixels, and QImage::copy at render_qt.cpp:323 on the way back out.
//
// `target` is checked and then unused, and that is deliberate on both counts. Unused
// because a QImage needs nothing from a render target; checked because the seam's
// contract says a mismatched target is a configuration error, and a backend that
// silently accepted one would let a Qt build limp along holding a Direct2D target until
// something else noticed. The cast goes to QtRenderTarget rather than away from
// D2DRenderTarget for a mechanical reason as well as a symmetry one: D2DRenderTarget's
// typeinfo lives in render_d2d.cpp, which does not compile off Windows, so naming it
// here would be a link error rather than a check.
std::shared_ptr<Bitmap> decodeImageFileToBitmap(RenderTarget& target,
                                                const std::wstring& path) {
    assert(dynamic_cast<QtRenderTarget*>(&target) != nullptr &&
           "a non-Qt RenderTarget reached the Qt image backend");
    if (!dynamic_cast<QtRenderTarget*>(&target)) return nullptr;

    QImage img;
    if (!img.load(QString::fromStdWString(path))) return nullptr;
    if (img.isNull()) return nullptr;
    return std::make_shared<QtBitmap>(std::move(img));
}

DecodedImage rasterizeSvg(const void* data, size_t size, float scale,
                          float& intrinsicW, float& intrinsicH) {
    DecodedImage out;
    intrinsicW = 0.0f;
    intrinsicH = 0.0f;
    if (!data || size == 0 || scale <= 0.0f) return out;

    const QByteArray bytes(static_cast<const char*>(data),
                           static_cast<qsizetype>(size));
    QSvgRenderer renderer(bytes);
    if (!renderer.isValid()) return out;

    // defaultSize() resolves width/height first and falls back to the viewBox, which
    // is the same order the Direct2D path walks. One genuine divergence, stated
    // because it is unfixable rather than overlooked: this returns a QSize, so a
    // fractional intrinsic size is truncated to whole DIPs, where D2D reads the
    // attributes as floats. A 100.5-DIP-wide document therefore lays out one DIP
    // narrower here.
    const QSize def = renderer.defaultSize();
    float svgW = static_cast<float>(def.width());
    float svgH = static_cast<float>(def.height());
    if (svgW <= 0.0f || svgH <= 0.0f) {
        svgW = 300.0f;  // the SVG spec's replaced-element default
        svgH = 150.0f;
    }
    svgW = std::min(svgW, 4096.0f);
    svgH = std::min(svgH, 4096.0f);

    const int pxW = static_cast<int>(std::ceil(svgW * scale));
    const int pxH = static_cast<int>(std::ceil(svgH * scale));
    if (!dimensionsAcceptable(pxW, pxH)) return out;

    QImage canvas(pxW, pxH, QImage::Format_ARGB32_Premultiplied);
    if (canvas.isNull()) return out;
    canvas.fill(Qt::transparent);
    {
        QPainter painter(&canvas);
        if (!painter.isActive()) return out;
        renderer.render(&painter, QRectF(0.0, 0.0, static_cast<double>(svgW) * scale,
                                        static_cast<double>(svgH) * scale));
    }

    if (!copyOutPixels(canvas, out)) return DecodedImage();
    intrinsicW = svgW;
    intrinsicH = svgH;
    return out;
}

DecodedImage rasterizeSvgFile(const std::wstring& path, float scale,
                              float& intrinsicW, float& intrinsicH) {
    intrinsicW = 0.0f;
    intrinsicH = 0.0f;
    QFile file(QString::fromStdWString(path));
    if (!file.open(QIODevice::ReadOnly)) return DecodedImage();
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) return DecodedImage();
    return rasterizeSvg(bytes.constData(), static_cast<size_t>(bytes.size()), scale,
                        intrinsicW, intrinsicH);
}

} // namespace tinta
