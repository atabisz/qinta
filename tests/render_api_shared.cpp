// The drawing seam, asserted in PIXELS, against any backend.
//
// WHY PIXELS AND NOT A MOCK
//
// include/render_api.h claims that Direct2D's drawing calls translate onto another
// backend without a lossy step, and that is the claim the port rests on: if it
// holds, roughly 915 call sites move with no edits at all. A mock backend recording
// "FillRoundedRectangle was called" would pass whether or not the corners came out
// round, whether or not the rectangle landed in the right place, and whether or not
// the colour survived the 0..1-float to 0..255-int conversion. So this reads the
// framebuffer back.
//
// Every assertion below that could pass for a boring reason is paired with a
// control that must come out the OTHER way:
//
//   * "the corner of a rounded rect is empty" is worthless on its own -- an
//     unpainted surface satisfies it, and so does a rectangle drawn in the wrong
//     place. It is paired with a plain FillRectangle of the SAME rect, which must
//     fill that same corner pixel.
//   * "the rectangle landed correctly" is checked at a point that would be painted
//     ONLY IF tinta::RectF (left/top/right/bottom) had been misread as x/y/width/
//     height. That is the one conversion in a backend capable of corrupting every
//     drawing call in the tree while still producing a plausible picture.
//   * "a clip suppressed the right half" is paired with a paint AFTER the pop, which
//     must reach the right half -- otherwise a clip that was never released would
//     read as a clip that worked.
//   * the whole suite is preceded by a control that painting changes the surface at
//     all, which is this file's equivalent of the font-database guard in
//     qt_text_tests.cpp: the lesson there was that a Qt instrument can return
//     well-formed numbers while measuring nothing, so a silent no-op backend must not
//     be able to read as a pass.
//
// No text is drawn here, so this needs no font database and deliberately does not
// check for one -- a guard that guards nothing is worse than no guard, because it
// implies coverage it does not have. Text is qt_text_tests.cpp.

#include "render_api_shared.h"

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

int countNonWhite(RenderHarness& h) {
    int n = 0;
    for (int y = 0; y < kHeight; y++)
        for (int x = 0; x < kWidth; x++)
            if (h.pixel(x, y) != kWhite) n++;
    return n;
}

// A fresh white surface with no session open.
void whiteSurface(RenderHarness& h) {
    h.newSurface();
    h.begin().Clear(tinta::colorF(1.0f, 1.0f, 1.0f));
    h.end();
}

}  // namespace

int runRenderApiAssertions(RenderHarness& h) {
    failures = 0;
    checks = 0;

    const tinta::Color red = tinta::colorF(1.0f, 0.0f, 0.0f);
    const tinta::Color blue = tinta::colorF(0.0f, 0.0f, 1.0f);

    // --- CONTROL: can this instrument speak at all? ---------------------------
    {
        h.newSurface();
        check(h.pixel(10, 10).a == 0,
              "CONTROL: the surface starts transparent, so a later opaque pixel is a change");
        h.begin().Clear(tinta::colorF(1.0f, 1.0f, 1.0f));
        h.end();
        check(h.pixel(10, 10) == kWhite,
              "CONTROL: painting reaches the framebuffer -- a no-op backend fails here");
        check(h.pixel(kWidth - 2, kHeight - 2) == kWhite,
              "Clear covers the far corner, not just the origin");
    }

    // --- Colour fidelity, and that Clear REPLACES rather than blends ----------
    {
        h.newSurface();
        h.begin().Clear(red);
        h.end();
        const Rgba p = h.pixel(100, 60);
        check(p.r == 255 && p.g == 0 && p.b == 0 && p.a == 255,
              "a pure red survives the 0..1 float to 0..255 conversion exactly");
        h.begin().Clear(tinta::colorF(0.0f, 0.0f, 0.0f, 0.0f));
        h.end();
        check(h.pixel(100, 60).a == 0,
              "Clear to transparent REPLACES the red -- with source-over it would still be red");
    }

    // --- FillRectangle: placement, and the left/top/right/bottom convention ---
    {
        whiteSurface(h);
        std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
        h.begin().FillRectangle(tinta::rectF(20, 10, 60, 40), brush.get());
        h.end();
        check(h.pixel(40, 25) != kWhite, "the interior of a filled rectangle is painted");
        check(h.pixel(40, 25) == kRed, "and it is painted the brush colour");
        check(h.pixel(80, 25) == kWhite, "a point to the right of the rectangle is untouched");
        check(h.pixel(40, 60) == kWhite, "a point below the rectangle is untouched");
        check(h.pixel(21, 25) != kWhite, "the pixel just inside the left edge IS painted");
        check(h.pixel(17, 25) == kWhite, "the pixel just outside the left edge is not");
        // THE DISCRIMINATOR. rectF(20, 10, 60, 40) spans x 20..60 and y 10..40. If a
        // backend treated those four floats as x/y/width/height it would span
        // x 20..80 and y 10..50, which looks entirely plausible on screen.
        check(h.pixel(70, 45) == kWhite,
              "a point painted ONLY IF RectF were misread as x/y/width/height stays background");
    }

    // --- SetColor, the single largest item in the COM surface (474 sites) -----
    {
        whiteSurface(h);
        std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
        {
            tinta::RenderTarget& target = h.begin();
            target.FillRectangle(tinta::rectF(10, 10, 50, 50), brush.get());
            brush->SetColor(blue);
            target.FillRectangle(tinta::rectF(100, 10, 140, 50), brush.get());
            h.end();
        }
        const Rgba first = h.pixel(30, 30);
        const Rgba second = h.pixel(120, 30);
        check(first == kRed, "the first fill used the brush's original colour");
        check(second == kBlue, "the second fill used the colour set between them");
        check(first != second,
              "CONTROL: the two regions differ, so the brush is not stuck on one colour");
    }

    // --- Alpha is honoured, and is not silently dropped ----------------------
    {
        whiteSurface(h);
        std::unique_ptr<tinta::Brush> brush = h.makeBrush(tinta::colorF(1.0f, 0.0f, 0.0f, 0.5f));
        h.begin().FillRectangle(tinta::rectF(20, 20, 100, 100), brush.get());
        h.end();
        const Rgba p = h.pixel(60, 60);
        check(p.r == 255, "a half-alpha red over white keeps a saturated red channel");
        check(p.g > 110 && p.g < 145,
              "the green channel lands near halfway, so the alpha was applied");
        check(p != kRed, "CONTROL: the result is NOT opaque red -- alpha was not dropped");
    }

    // --- FillRoundedRectangle, with the control that makes it mean something --
    {
        const tinta::RectF box = tinta::rectF(20, 20, 100, 100);

        whiteSurface(h);
        {
            std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
            h.begin().FillRoundedRectangle(tinta::roundedRect(box, 20, 20), brush.get());
            h.end();
        }
        const Rgba roundedCentre = h.pixel(60, 60);
        const Rgba roundedCorner = h.pixel(22, 22);
        const int roundedCount = countNonWhite(h);

        whiteSurface(h);
        {
            std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
            h.begin().FillRectangle(box, brush.get());
            h.end();
        }
        const Rgba squareCorner = h.pixel(22, 22);
        const int squareCount = countNonWhite(h);

        check(roundedCentre != kWhite, "the centre of a rounded rectangle is filled");
        check(roundedCorner == kWhite, "its extreme corner is rounded away");
        // Without this, the assertion above would also pass if the rectangle had been
        // drawn in the wrong place, at the wrong size, or not at all.
        check(squareCorner != kWhite,
              "CONTROL: a SQUARE rect of the same bounds DOES fill that corner pixel");
        check(roundedCount < squareCount,
              "the rounded rectangle covers strictly fewer pixels than the square one");
    }

    // --- DrawRectangle is a stroke, not a fill, and strokeWidth is real -------
    {
        whiteSurface(h);
        {
            std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
            h.begin().DrawRectangle(tinta::rectF(20, 20, 120, 80), brush.get(), 1.0f);
            h.end();
        }
        const int thinCount = countNonWhite(h);

        whiteSurface(h);
        {
            std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
            h.begin().DrawRectangle(tinta::rectF(20, 20, 120, 80), brush.get(), 6.0f);
            h.end();
        }
        check(h.pixel(70, 20) != kWhite, "the top edge of a stroked rectangle is painted");
        check(h.pixel(70, 50) == kWhite,
              "its interior is NOT painted -- so this is a stroke and not a fill");
        check(countNonWhite(h) > thinCount,
              "a 6px stroke covers more pixels than a 1px one, so strokeWidth is honoured");
    }

    // --- DrawLine ------------------------------------------------------------
    {
        whiteSurface(h);
        std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
        h.begin().DrawLine(tinta::pointF(10, 30), tinta::pointF(190, 30), brush.get(), 3.0f,
                           nullptr);
        h.end();
        check(h.pixel(100, 30) != kWhite, "a point on the line is painted");
        check(h.pixel(100, 60) == kWhite, "a point well off the line is not");
    }

    // --- Line caps: a Qt default that disagrees with Direct2D -----------------
    //
    // Qt's default pen cap is SquareCap, which extends a line by HALF THE PEN WIDTH
    // past each endpoint; Direct2D's default stroke style is flat-capped and stops
    // where it was told. The Qt backend sets FlatCap to match. The assertion has to
    // use a thick line, because on a 1px line the two are indistinguishable -- which
    // is exactly why this would otherwise have shipped unnoticed. Direct2D running
    // the same assertion is what makes "to match" a measurement rather than a claim.
    {
        whiteSurface(h);
        std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
        h.begin().DrawLine(tinta::pointF(50, 30), tinta::pointF(150, 30), brush.get(), 20.0f,
                           nullptr);
        h.end();
        check(h.pixel(55, 30) != kWhite,
              "CONTROL: the thick line itself is painted, so the next check reads something");
        check(h.pixel(44, 30) == kWhite,
              "the line stops at its start point -- Qt's default SquareCap would paint here");
    }

    // --- FillEllipse / DrawEllipse: centre-plus-radii maps exactly ------------
    {
        const tinta::Ellipse e = tinta::ellipse(tinta::pointF(100, 60), 40, 30);

        whiteSurface(h);
        {
            std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
            h.begin().FillEllipse(e, brush.get());
            h.end();
        }
        check(h.pixel(100, 60) != kWhite, "the centre of a filled ellipse is painted");
        check(h.pixel(100, 34) != kWhite, "a point inside it near the top is painted");
        // The ellipse spans x 60..140 and y 30..90, so the corner of that bounding box
        // is outside the curve. This is what distinguishes an ellipse from a rectangle
        // drawn over the same bounds.
        check(h.pixel(63, 33) == kWhite,
              "the corner of its bounding box is NOT painted, so it is an ellipse");

        whiteSurface(h);
        {
            std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
            h.begin().DrawEllipse(e, brush.get(), 3.0f);
            h.end();
        }
        check(h.pixel(100, 60) == kWhite, "a stroked ellipse leaves its centre unpainted");
        check(h.pixel(100, 30) != kWhite, "but its rim is painted");
    }

    // --- Clip, and the control that it was actually released ------------------
    {
        whiteSurface(h);
        std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
        {
            tinta::RenderTarget& target = h.begin();
            target.PushAxisAlignedClip(tinta::rectF(0, 0, 100, static_cast<float>(kHeight)),
                                       tinta::AntialiasMode::Aliased);
            target.FillRectangle(
                tinta::rectF(0, 0, static_cast<float>(kWidth), static_cast<float>(kHeight)),
                brush.get());
            target.PopAxisAlignedClip();

            brush->SetColor(blue);
            target.FillRectangle(
                tinta::rectF(150, 0, static_cast<float>(kWidth), static_cast<float>(kHeight)),
                brush.get());
            h.end();
        }
        check(h.pixel(50, 60) == kRed, "inside the clip, a full-surface fill lands");
        check(h.pixel(120, 60) == kWhite, "outside the clip, the same fill was suppressed");
        // Without this the assertion above would also pass if the clip had never been
        // popped, which is the failure mode that matters: a leaked clip silently blanks
        // everything drawn later in the frame.
        check(h.pixel(170, 60) == kBlue,
              "CONTROL: after the pop, painting reaches the far side again");
    }

    std::printf("%s%d render-api assertions run, %d failed (backend=%s)\n",
                failures == 0 ? "all " : "", checks, failures, h.name());
    return failures;
}
