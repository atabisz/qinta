#include "image_api.h"

// For QtBitmap and QtRenderTarget, which decodeImageFileToBitmap needs. image_api.h
// forward-declares Bitmap and RenderTarget instead of including render_api.h, so each
// backend pulls in its own -- which is the arrangement that note in the header describes.
#include "render_qt.h"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QImageReader>
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
// typeinfo lived in render_d2d.cpp, which never compiled off Windows, so naming it
// here would have been a link error rather than a check (and since 0b39ae6 the type is gone).
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

std::shared_ptr<Bitmap> decodeIconBytesToBitmap(RenderTarget& target, const void* data,
                                                size_t size, int pixelSize) {
    assert(dynamic_cast<QtRenderTarget*>(&target) != nullptr &&
           "a non-Qt RenderTarget reached the Qt image backend");
    if (!dynamic_cast<QtRenderTarget*>(&target)) return nullptr;
    if (!data || size == 0 || pixelSize <= 0) return nullptr;

    // fromRawData, not a copy: the bytes outlive the call at every caller (a static array),
    // and the buffer is read and discarded inside this function.
    const QByteArray bytes = QByteArray::fromRawData(static_cast<const char*>(data),
                                                    static_cast<int>(size));
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return nullptr;

    QImageReader reader(&buffer);
    if (!reader.canRead()) return nullptr;

    // THE FRAME CHOICE IS image_api.h's POLICY, IMPLEMENTED, NOT Qt's DEFAULT.
    // Left alone, Qt's ICO handler hands back a frame chosen by its own rule and WIC's
    // decoder chooses by another, so the hero size -- 64, which resources/qinta.ico has no
    // frame for -- would render differently on the two platforms with nothing to reveal it.
    // jumpToImage/size walks the container so the stated rule decides instead.
    const int frameCount = reader.imageCount();
    int bestIndex = -1;
    int bestWidth = 0;
    bool haveAtOrAbove = false;
    for (int i = 0; i < frameCount; ++i) {
        if (!reader.jumpToImage(i)) continue;
        const QSize frameSize = reader.size();
        const int fw = frameSize.width();
        if (fw <= 0) continue;

        const bool atOrAbove = fw >= pixelSize;
        if (haveAtOrAbove) {
            if (atOrAbove && fw < bestWidth) {
                bestWidth = fw;
                bestIndex = i;
            }
        } else if (atOrAbove) {
            haveAtOrAbove = true;
            bestWidth = fw;
            bestIndex = i;
        } else if (fw > bestWidth) {
            bestWidth = fw;
            bestIndex = i;
        }
    }

    QImage frame;
    if (bestIndex >= 0 && reader.jumpToImage(bestIndex)) {
        frame = reader.read();
    }
    if (frame.isNull()) {
        // A container whose frames could not be enumerated (imageCount() == -1 is a
        // documented answer for some formats) still gets one honest attempt at the whole
        // stream, rather than being reported as undecodable. The policy is unaffected: a
        // single-frame stream has only one choice.
        buffer.seek(0);
        QImageReader retry(&buffer);
        frame = retry.read();
    }
    if (frame.isNull()) return nullptr;

    // Scale unconditionally, for the same reason the Direct2D side does: an exact-frame
    // request and a scaled one must not take different code paths, or a test at 32 passes
    // while 64 is broken. SmoothTransformation is the closest Qt analogue to WIC's Fant.
    QImage scaled = frame.scaled(pixelSize, pixelSize, Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation);
    if (scaled.isNull()) return nullptr;
    // The whole tree's bitmaps are premultiplied BGRA; see this file's endianness note.
    if (scaled.format() != QImage::Format_ARGB32_Premultiplied)
        scaled = scaled.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (scaled.isNull()) return nullptr;

    return std::make_shared<QtBitmap>(std::move(scaled));
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

DecodedImage rasterizeToImage(RenderTarget& target, unsigned width, unsigned height,
                              const Color& clearColor,
                              const std::function<void(RenderTarget&, Brush*)>& draw) {
    // `target` is UNUSED on this arm, deliberately, and the asymmetry is worth a line
    // rather than a cast-to-void on its own. The Direct2D arm needs it to reach the
    // ID2D1Factory that creates a WIC-backed target; Qt creates a QImage from nothing
    // and needs no device at all. The parameter stays in the shared signature because a
    // seam whose two arms take different arguments is not one seam, and because dropping
    // it would let a caller with no live target call this on Qt and fail only on Windows.
    (void)target;

    if (!draw) return DecodedImage();
    if (width == 0 || height == 0) return DecodedImage();
    if (width >= kMaxImageDimension || height >= kMaxImageDimension) return DecodedImage();

    QImage canvas(static_cast<int>(width), static_cast<int>(height),
                  QImage::Format_ARGB32_Premultiplied);
    if (canvas.isNull()) return DecodedImage();
    // The caller's clear colour, per the header's contract. QImage's buffer is
    // uninitialised otherwise, so this is not belt-and-braces -- without it the untouched
    // margins of a diagram would be whatever the allocator last left there.
    //
    // fill(QColor) and not a memset of the premultiplied bytes: Qt premultiplies the
    // colour itself for Format_ARGB32_Premultiplied, so the same tinta::Color lands on
    // this arm and the Direct2D one, where D2D's Clear takes straight alpha and
    // premultiplies on write. Going through toQt also keeps the one conversion this tree
    // has for the type rather than adding a second.
    canvas.fill(toQt(clearColor));

    {
        // Scoped, because QtRenderTarget begins its QPainter in the constructor and ends
        // it in the destructor. Reading the pixels while the painter is still open is
        // undefined, so the scope is load-bearing rather than stylistic.
        QtRenderTarget rt(canvas);
        QtBrush brush(tinta::colorF(0, 0, 0));
        draw(rt, &brush);
    }

    DecodedImage out;
    if (!copyOutPixels(canvas, out)) return DecodedImage();
    return out;
}

std::string encodePng(const DecodedImage& image) {
    if (!image.ok) return std::string();
    if (image.width == 0 || image.height == 0) return std::string();
    // Checked, not trusted: the buffer arrives from a caller, and a short one would be
    // read past by QImage's constructor below rather than diagnosed by it.
    if (image.pixels.size() != static_cast<size_t>(image.width) * image.height * 4) {
        return std::string();
    }

    // Wraps the caller's bytes without copying, so `image` must outlive the save call --
    // it does, being a const reference held for this whole function.
    const QImage wrapped(image.pixels.data(), static_cast<int>(image.width),
                         static_cast<int>(image.height),
                         static_cast<int>(image.width) * 4,
                         QImage::Format_ARGB32_Premultiplied);
    if (wrapped.isNull()) return std::string();

    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) return std::string();
    if (!wrapped.save(&buffer, "PNG")) return std::string();
    buffer.close();
    if (bytes.isEmpty()) return std::string();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

} // namespace tinta
