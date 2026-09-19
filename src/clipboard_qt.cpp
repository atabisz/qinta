// The Qt half of the clipboard seam (include/clipboard.h).
//
// QtGui is enough here, unlike file_dialog_qt.cpp: QClipboard is reached from
// QGuiApplication, so this seam costs no new Qt module. That is worth stating
// because the file-picker seam's Widgets requirement was presented as the price of
// having a real second implementation, and it would be easy to read that as the
// price of every seam. It is not; it was the price of that one.
//
// TWO WAYS THIS IS WEAKER THAN THE WINDOWS HALF, both measured rather than assumed.
//
// FIRST, QClipboard::setText RETURNS NOTHING. Windows reports whether the data was
// accepted and utils.cpp:160 propagated it; Qt offers no such signal. So `true` here
// means "we asked and there was an application to ask", not "the clipboard now holds
// this text" -- a weaker claim wearing the same bool. The two ways to return false
// are therefore the only ones that exist: empty input, and no QGuiApplication. On
// Windows a third exists (SetClipboardData refused), and code that branches on false
// must not assume it has seen all three.
//
// SECOND, IT WAS EXPECTED THAT QClipboard MIGHT NORMALIZE LINE ENDINGS before this
// function sees them, which would matter at exactly one of the three call sites -- see
// include/clipboard.h. MEASURED: it does not. tests/clipboard_tests.cpp writes a CRLF
// and reads back all 13 characters here, the same as on Windows.
//
// That was measured under the OFFSCREEN QPA plugin, whose clipboard never leaves the
// process, so it says nothing about xcb or wayland. The expectation is therefore
// downgraded rather than deleted, and this file still does not compensate in either
// direction: adding CRs back would be inventing data, and stripping them would move a
// transformation into the seam that the header keeps out of it on purpose.
//
// std::wstring crosses this boundary through QString's own conversions and not
// through a cast, because wchar_t is 16 bits on Windows and 32 on Linux. A
// reinterpret_cast would work on exactly one of the two platforms this file is for.
//
// THE IMAGE DATA HALF ARRIVED 2026-09-19, and on this side it is the translation the
// Windows side is not: QClipboard::setMimeData with an image and an image/png entry,
// and QMimeData::imageData back. There is no encoder here and no decoder -- Qt owns
// both -- which is why this file gains five includes and no Qt MODULE. Counted rather
// than estimated: three Qt headers (QByteArray, QVariant in Qt6::Core, QImage in
// Qt6::Gui, both already on this target) plus <cstring> and image_api.h. The module list
// in CMakeLists.txt is untouched, and that is the claim worth making -- an include is
// free, a module is a link line.
//
// WHAT IT DOES NOT CLAIM is the round trip. The Qt arm of tests/clipboard_tests runs
// under the OFFSCREEN QPA plugin, whose clipboard never leaves the process, exactly as
// the CRLF paragraph above says. So a green image assertion here is evidence about an
// in-process clipboard and not about xcb, wayland or the Windows QPA plugin, each of
// which crosses a real system boundary that can convert or drop a format.

#include "clipboard.h"

#include <QByteArray>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QString>
#include <QVariant>

#include <cstring>

#include "image_api.h"

// The same little-endian assumption src/image_qt.cpp asserts, for the same reason and
// stated separately rather than inherited: Qt's Format_ARGB32_Premultiplied and
// Format_RGB32 both name a 32-bit INTEGER, 0xAARRGGBB and 0xffRRGGBB, whose memory
// order is B,G,R,A only on a little-endian machine. tinta::DecodedImage names its bytes
// in memory order. They agree on one byte order and this file would silently swap
// channels on the other.
#if Q_BYTE_ORDER != Q_LITTLE_ENDIAN
#error "clipboard_qt.cpp assumes little-endian: Qt's 32-bit formats are BGRA in memory only there"
#endif

namespace platform {

std::wstring clipboardText() {
    // Null when no QGuiApplication exists -- which is a real state for a test binary
    // that links this file without constructing an application, not a defensive
    // check. It reaches the caller as an empty string, the same way a failed
    // OpenClipboard does on Windows.
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) return std::wstring();
    return clipboard->text().toStdWString();
}

bool setClipboardText(const std::wstring& text) {
    if (text.empty()) return false;
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) return false;
    clipboard->setText(QString::fromStdWString(text));
    return true;  // see the header note: Qt gives us nothing better than this
}

// THE ONE FUNCTION IN THIS FILE THAT IS NOT WEAKER THAN THE WINDOWS HALF, which is
// worth saying after the two paragraphs at the top of this file that are. Windows
// asks whether CF_BITMAP is available; QMimeData::hasImage asks whether the clipboard
// carries an image, and both answer the same question the one caller has -- "is there
// a picture to paste instead of text".
//
// The formats behind the two answers are NOT the same set, and that is the honest
// caveat rather than a defect: hasImage() is true for anything Qt can hand back as an
// image, which on X11 includes image/png where CF_BITMAP is a DIB handle. So a
// clipboard holding a PNG but no device-independent bitmap could answer differently on
// the two platforms. Nothing in this tree depends on the difference -- the caller's
// only use of a true answer is to attempt a conversion that reports its own failure.
bool clipboardHasImage() {
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) return false;
    const QMimeData* mime = clipboard->mimeData();
    return mime && mime->hasImage();
}

bool setClipboardImage(const tinta::DecodedImage& image, const std::string& pngBytes) {
    if (!image.ok || image.width == 0 || image.height == 0) return false;
    const size_t stride = static_cast<size_t>(image.width) * 4;
    if (image.pixels.size() != stride * image.height) return false;
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) return false;

    // The non-owning QImage constructor, then copy(). QImage over a borrowed buffer does
    // not take ownership and does not extend the buffer's lifetime, and `image` is a
    // const reference whose pixels belong to the caller -- so the copy is what makes
    // this safe rather than a cost to optimize away later.
    const QImage view(image.pixels.data(), static_cast<int>(image.width),
                      static_cast<int>(image.height), static_cast<int>(stride),
                      QImage::Format_ARGB32_Premultiplied);
    if (view.isNull()) return false;

    // setMimeData rather than setImage, because the PNG has to ride on the SAME
    // QMimeData as the image. Two calls would leave only the second one's data on the
    // clipboard: setImage replaces the mime data wholesale. Ownership of the QMimeData
    // passes to the clipboard, which is Qt's documented contract for this call and is
    // why there is no delete here.
    QMimeData* mime = new QMimeData();
    mime->setImageData(view.copy());
    if (!pngBytes.empty()) {
        mime->setData(QStringLiteral("image/png"),
                      QByteArray(pngBytes.data(), static_cast<int>(pngBytes.size())));
    }
    clipboard->setMimeData(mime);
    return true;  // see the header note on setClipboardText: Qt reports no acceptance
}

tinta::DecodedImage clipboardImage() {
    tinta::DecodedImage image;
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) return image;
    const QMimeData* mime = clipboard->mimeData();
    if (!mime || !mime->hasImage()) return image;

    const QImage src = qvariant_cast<QImage>(mime->imageData());
    if (src.isNull()) return image;
    // tinta::kMaxImageDimension's bound, EXCLUSIVE, matching src/image_qt.cpp's
    // dimensionsAcceptable and src/clipboard_win32.cpp's copy of the same test. The seam
    // enforces it in every backend so the two cannot drift.
    if (src.width() <= 0 || src.height() <= 0 ||
        static_cast<unsigned>(src.width()) >= tinta::kMaxImageDimension ||
        static_cast<unsigned>(src.height()) >= tinta::kMaxImageDimension) {
        return image;
    }

    // Format_RGB32 -- 0xffRRGGBB -- because this seam's contract is that the clipboard is
    // OPAQUE, and off Windows that has to be enforced rather than inherited: Qt can hand
    // back a genuinely transparent image where a CF_BITMAP simply has no alpha channel to
    // hand back. Converting to an opaque format is the only part of this that composites,
    // and this file does not claim what it composites AGAINST -- that is a Qt internal
    // nothing here has measured. What it does claim is the alpha byte, below.
    const QImage opaque = src.format() == QImage::Format_RGB32
                              ? src
                              : src.convertToFormat(QImage::Format_RGB32);
    if (opaque.isNull()) return image;

    const unsigned width = static_cast<unsigned>(opaque.width());
    const unsigned height = static_cast<unsigned>(opaque.height());
    // Row by row, for the reason src/image_qt.cpp gives at copyOutPixels: QImage pads
    // each scanline, so bytesPerLine() is not width * 4 by definition even though it is
    // for a 32-bit format. A whole-buffer memcpy would become a slanted image under a
    // later format change rather than a compile error.
    image.pixels.resize(static_cast<size_t>(width) * height * 4);
    for (unsigned y = 0; y < height; ++y) {
        std::memcpy(image.pixels.data() + static_cast<size_t>(y) * width * 4,
                    opaque.constScanLine(static_cast<int>(y)),
                    static_cast<size_t>(width) * 4);
    }
    // The alpha byte is written even though Format_RGB32's own definition already fixes
    // it at 0xff, because the invariant this function promises should not depend on what
    // a conversion does internally. Same line as the Windows half, where it is load
    // bearing instead of belt-and-braces -- see include/clipboard.h.
    for (size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = 255;

    image.width = width;
    image.height = height;
    image.ok = true;
    return image;
}

}  // namespace platform
