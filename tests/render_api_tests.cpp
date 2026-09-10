// The drawing seam, asserted in PIXELS.
//
// WHY PIXELS AND NOT A MOCK
//
// include/render_api.h claims that Direct2D's drawing calls translate onto Qt's
// without a lossy step, and that is the claim the port rests on: if it holds,
// roughly 915 call sites move to a second backend with no edits at all. A mock
// backend recording "FillRoundedRectangle was called" would pass whether or not
// the corners came out round, whether or not the rectangle landed in the right
// place, and whether or not the colour survived the 0..1-float to 0..255-int
// conversion. So this reads the framebuffer back.
//
// Every assertion below that could pass for a boring reason is paired with a
// control that must come out the OTHER way:
//
//   * "the corner of a rounded rect is empty" is worthless on its own -- an
//     unpainted surface satisfies it, and so does a rectangle drawn in the wrong
//     place. It is paired with a plain FillRectangle of the SAME rect, which must
//     fill that same corner pixel.
//   * "the rectangle landed correctly" is checked at a point that would be
//     painted ONLY IF tinta::RectF (left/top/right/bottom) had been misread as
//     Qt's x/y/width/height. That is the one conversion in render_qt.cpp capable
//     of corrupting every drawing call in the tree while still producing a
//     plausible picture.
//   * "a clip suppressed the right half" is paired with a paint AFTER the pop,
//     which must reach the right half -- otherwise a clip that was never released
//     would read as a clip that worked.
//   * the whole suite is preceded by a control that painting changes the surface
//     at all, which is this file's equivalent of the font-database guard in
//     qt_text_tests.cpp: the lesson there was that a Qt instrument can return
//     well-formed numbers while measuring nothing, so a silent no-op backend must
//     not be able to read as a pass.
//
// No text is drawn here, so this test needs no font database and deliberately
// does not check for one -- a guard that guards nothing is worse than no guard,
// because it implies coverage it does not have. Text is qt_text_tests.cpp.

#include "render_qt.h"

#include <QGuiApplication>
#include <QImage>

#include <cstdio>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::printf("FAIL: %s\n", message);
    failures++;
}

constexpr int kWidth = 200;
constexpr int kHeight = 120;

const QRgb kWhite = qRgba(255, 255, 255, 255);

// ARGB32 rather than ARGB32_Premultiplied on purpose: a premultiplied surface
// would make every colour assertion below depend on the alpha it was blended
// with, so a wrong colour and a wrong alpha could cancel out.
QImage freshImage() {
    QImage image(kWidth, kHeight, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    return image;
}

QRgb px(const QImage& image, int x, int y) {
    return image.pixel(x, y);
}

int countNonWhite(const QImage& image) {
    int n = 0;
    for (int y = 0; y < kHeight; y++)
        for (int x = 0; x < kWidth; x++)
            if (px(image, x, y) != kWhite) n++;
    return n;
}

// A white surface with a drawing session already open. Returned by value into a
// caller-owned QImage, so the painter has to be constructed by the caller; this
// helper only prepares the image.
QImage whiteImage() {
    QImage image = freshImage();
    QtRenderTarget target(image);
    target.Clear(tinta::colorF(1.0f, 1.0f, 1.0f));
    return image;
}

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    const tinta::Color red = tinta::colorF(1.0f, 0.0f, 0.0f);
    const tinta::Color blue = tinta::colorF(0.0f, 0.0f, 1.0f);

    // --- CONTROL: can this instrument speak at all? ---------------------------
    {
        QImage image = freshImage();
        check(qAlpha(px(image, 10, 10)) == 0,
              "CONTROL: the surface starts transparent, so a later opaque pixel is a change");
        {
            QtRenderTarget target(image);
            target.Clear(tinta::colorF(1.0f, 1.0f, 1.0f));
        }
        check(px(image, 10, 10) == kWhite,
              "CONTROL: painting reaches the framebuffer -- a no-op backend fails here");
        check(px(image, kWidth - 2, kHeight - 2) == kWhite,
              "Clear covers the far corner, not just the origin");
    }

    // --- Colour fidelity, and that Clear REPLACES rather than blends ----------
    {
        QImage image = freshImage();
        {
            QtRenderTarget target(image);
            target.Clear(red);
        }
        const QRgb p = px(image, 100, 60);
        check(qRed(p) == 255 && qGreen(p) == 0 && qBlue(p) == 0 && qAlpha(p) == 255,
              "a pure red survives the 0..1 float to 0..255 conversion exactly");
        {
            QtRenderTarget target(image);
            target.Clear(tinta::colorF(0.0f, 0.0f, 0.0f, 0.0f));
        }
        check(qAlpha(px(image, 100, 60)) == 0,
              "Clear to transparent REPLACES the red -- with source-over it would still be red");
    }

    // --- FillRectangle: placement, and the left/top/right/bottom convention ---
    {
        QImage image = whiteImage();
        QtBrush brush(red);
        {
            QtRenderTarget target(image);
            target.FillRectangle(tinta::rectF(20, 10, 60, 40), &brush);
        }
        check(px(image, 40, 25) != kWhite, "the interior of a filled rectangle is painted");
        check(px(image, 40, 25) == qRgba(255, 0, 0, 255), "and it is painted the brush colour");
        check(px(image, 80, 25) == kWhite, "a point to the right of the rectangle is untouched");
        check(px(image, 40, 60) == kWhite, "a point below the rectangle is untouched");
        check(px(image, 21, 25) != kWhite, "the pixel just inside the left edge IS painted");
        check(px(image, 17, 25) == kWhite, "the pixel just outside the left edge is not");
        // THE DISCRIMINATOR. rectF(20, 10, 60, 40) spans x 20..60 and y 10..40. If
        // toQt() had treated those four floats as x/y/width/height it would span
        // x 20..80 and y 10..50, which looks entirely plausible on screen.
        check(px(image, 70, 45) == kWhite,
              "a point painted ONLY IF RectF were misread as x/y/width/height stays background");
    }

    // --- SetColor, the single largest item in the COM surface (474 sites) -----
    {
        QImage image = whiteImage();
        QtBrush brush(red);
        {
            QtRenderTarget target(image);
            target.FillRectangle(tinta::rectF(10, 10, 50, 50), &brush);
            brush.SetColor(blue);
            target.FillRectangle(tinta::rectF(100, 10, 140, 50), &brush);
        }
        const QRgb first = px(image, 30, 30);
        const QRgb second = px(image, 120, 30);
        check(first == qRgba(255, 0, 0, 255), "the first fill used the brush's original colour");
        check(second == qRgba(0, 0, 255, 255), "the second fill used the colour set between them");
        check(first != second,
              "CONTROL: the two regions differ, so the brush is not stuck on one colour");
    }

    // --- Alpha is honoured, and is not silently dropped ----------------------
    {
        QImage image = whiteImage();
        QtBrush brush(tinta::colorF(1.0f, 0.0f, 0.0f, 0.5f));
        {
            QtRenderTarget target(image);
            target.FillRectangle(tinta::rectF(20, 20, 100, 100), &brush);
        }
        const QRgb p = px(image, 60, 60);
        check(qRed(p) == 255, "a half-alpha red over white keeps a saturated red channel");
        check(qGreen(p) > 110 && qGreen(p) < 145,
              "the green channel lands near halfway, so the alpha was applied");
        check(p != qRgba(255, 0, 0, 255),
              "CONTROL: the result is NOT opaque red -- alpha was not dropped");
    }

    // --- FillRoundedRectangle, with the control that makes it mean something --
    {
        const tinta::RectF box = tinta::rectF(20, 20, 100, 100);
        QtBrush brush(red);

        QImage rounded = whiteImage();
        {
            QtRenderTarget target(rounded);
            target.FillRoundedRectangle(tinta::roundedRect(box, 20, 20), &brush);
        }
        QImage square = whiteImage();
        {
            QtRenderTarget target(square);
            target.FillRectangle(box, &brush);
        }

        check(px(rounded, 60, 60) != kWhite, "the centre of a rounded rectangle is filled");
        check(px(rounded, 22, 22) == kWhite, "its extreme corner is rounded away");
        // Without this, the assertion above would also pass if the rectangle had
        // been drawn in the wrong place, at the wrong size, or not at all.
        check(px(square, 22, 22) != kWhite,
              "CONTROL: a SQUARE rect of the same bounds DOES fill that corner pixel");
        check(countNonWhite(rounded) < countNonWhite(square),
              "the rounded rectangle covers strictly fewer pixels than the square one");
    }

    // --- DrawRectangle is a stroke, not a fill, and strokeWidth is real -------
    {
        QtBrush brush(red);
        QImage thin = whiteImage();
        {
            QtRenderTarget target(thin);
            target.DrawRectangle(tinta::rectF(20, 20, 120, 80), &brush, 1.0f);
        }
        QImage thick = whiteImage();
        {
            QtRenderTarget target(thick);
            target.DrawRectangle(tinta::rectF(20, 20, 120, 80), &brush, 6.0f);
        }
        check(px(thick, 70, 20) != kWhite, "the top edge of a stroked rectangle is painted");
        check(px(thick, 70, 50) == kWhite,
              "its interior is NOT painted -- so this is a stroke and not a fill");
        check(countNonWhite(thick) > countNonWhite(thin),
              "a 6px stroke covers more pixels than a 1px one, so strokeWidth is honoured");
    }

    // --- DrawLine ------------------------------------------------------------
    {
        QImage image = whiteImage();
        QtBrush brush(red);
        {
            QtRenderTarget target(image);
            target.DrawLine(tinta::pointF(10, 30), tinta::pointF(190, 30), &brush, 3.0f);
        }
        check(px(image, 100, 30) != kWhite, "a point on the line is painted");
        check(px(image, 100, 60) == kWhite, "a point well off the line is not");
    }

    // --- Line caps: a Qt default that disagrees with Direct2D -----------------
    //
    // Qt's default pen cap is SquareCap, which extends a line by HALF THE PEN
    // WIDTH past each endpoint; Direct2D's default stroke style is flat-capped and
    // stops where it was told. render_qt.cpp sets FlatCap to match. The assertion
    // has to use a thick line, because on a 1px line the two are indistinguishable
    // -- which is exactly why this would otherwise have shipped unnoticed.
    {
        QImage image = whiteImage();
        QtBrush brush(red);
        {
            QtRenderTarget target(image);
            target.DrawLine(tinta::pointF(50, 30), tinta::pointF(150, 30), &brush, 20.0f);
        }
        check(px(image, 55, 30) != kWhite,
              "CONTROL: the thick line itself is painted, so the next check reads something");
        check(px(image, 44, 30) == kWhite,
              "the line stops at its start point -- Qt's default SquareCap would paint here");
    }

    // --- FillEllipse / DrawEllipse: centre-plus-radii maps exactly ------------
    {
        const tinta::Ellipse e = tinta::ellipse(tinta::pointF(100, 60), 40, 30);
        QtBrush brush(red);

        QImage filled = whiteImage();
        {
            QtRenderTarget target(filled);
            target.FillEllipse(e, &brush);
        }
        check(px(filled, 100, 60) != kWhite, "the centre of a filled ellipse is painted");
        check(px(filled, 100, 34) != kWhite, "a point inside it near the top is painted");
        // The ellipse spans x 60..140 and y 30..90, so the corner of that bounding
        // box is outside the curve. This is what distinguishes an ellipse from a
        // rectangle drawn over the same bounds.
        check(px(filled, 63, 33) == kWhite,
              "the corner of its bounding box is NOT painted, so it is an ellipse");

        QImage outlined = whiteImage();
        {
            QtRenderTarget target(outlined);
            target.DrawEllipse(e, &brush, 3.0f);
        }
        check(px(outlined, 100, 60) == kWhite, "a stroked ellipse leaves its centre unpainted");
        check(px(outlined, 100, 30) != kWhite, "but its rim is painted");
    }

    // --- Clip, and the control that it was actually released ------------------
    {
        QImage image = whiteImage();
        QtBrush brush(red);
        {
            QtRenderTarget target(image);
            target.PushAxisAlignedClip(tinta::rectF(0, 0, 100, kHeight),
                                       tinta::AntialiasMode::Aliased);
            target.FillRectangle(tinta::rectF(0, 0, kWidth, kHeight), &brush);
            target.PopAxisAlignedClip();

            brush.SetColor(blue);
            target.FillRectangle(tinta::rectF(150, 0, kWidth, kHeight), &brush);
        }
        check(px(image, 50, 60) == qRgba(255, 0, 0, 255),
              "inside the clip, a full-surface fill lands");
        check(px(image, 120, 60) == kWhite,
              "outside the clip, the same fill was suppressed");
        // Without this the assertion above would also pass if the clip had never
        // been popped, which is the failure mode that matters: a leaked clip
        // silently blanks everything drawn later in the frame.
        check(px(image, 170, 60) == qRgba(0, 0, 255, 255),
              "CONTROL: after the pop, painting reaches the far side again");
    }

    std::printf("%s%d render-api assertions run, %d failed (Qt %s, platform=%s)\n",
                failures == 0 ? "all " : "", checks, failures, qVersion(),
                QGuiApplication::platformName().toUtf8().constData());
    return failures == 0 ? 0 : 1;
}
