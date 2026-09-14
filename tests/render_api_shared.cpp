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
// TEXT IS ASSERTED HERE TOO, AND RELATIONALLY.
//
// An earlier version of this comment said no text was drawn here and that the file
// therefore needed no font database. That stopped being true when the text seam
// landed: 132 of the app.renderTarget call sites are DrawText or DrawTextLayout, and
// leaving the largest item in the surface to a Qt-only test would have left it with
// exactly the property render_d2d_tests.cpp exists to reject -- one implementation,
// checked against itself.
//
// The text assertions are RELATIONAL, never absolute pixel counts. Segoe UI and
// DejaVu Sans are different typefaces, so "this string is 137 pixels wide" is a
// statement about a font and not about the seam. "Centred ink starts further right
// than leading-aligned ink", "a narrower box produces more lines", "width never
// exceeds widthIncludingTrailingWhitespace" hold for any font that exists -- and
// each one is breakable by a mutation in ONE backend, which is the test that a
// cross-backend assertion is worth writing.
//
// Both harnesses refuse rather than report when they have no font database, because
// tests/qt_text_tests.cpp measured what the alternative looks like: under
// QT_QPA_PLATFORM=minimal, zero font families, fabricated metrics, exit code 0.

#include "render_api_shared.h"

#include <cmath>
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

// Ink bounds. -1 means nothing was painted, which is a distinct answer from 0 and is
// what makes "text appeared at all" checkable separately from "it appeared here".
int leftmostPainted(RenderHarness& h) {
    for (int x = 0; x < kWidth; x++)
        for (int y = 0; y < kHeight; y++)
            if (h.pixel(x, y) != kWhite) return x;
    return -1;
}

int rightmostPainted(RenderHarness& h) {
    for (int x = kWidth - 1; x >= 0; x--)
        for (int y = 0; y < kHeight; y++)
            if (h.pixel(x, y) != kWhite) return x;
    return -1;
}

int topmostPainted(RenderHarness& h) {
    for (int y = 0; y < kHeight; y++)
        for (int x = 0; x < kWidth; x++)
            if (h.pixel(x, y) != kWhite) return y;
    return -1;
}

// The colour of an ACTUALLY PAINTED pixel, or kWhite if nothing was painted.
//
// This exists because the obvious thing was wrong and the second backend caught it:
// reading h.pixel(leftmostPainted(h), topmostPainted(h)) reads the CORNER OF THE INK
// BOUNDING BOX, which is only painted by coincidence. It happened to be red under
// Qt and was white under Direct2D, so the colour assertion failed on the backend
// that was behaving correctly. A cross-backend suite finds bugs in its own
// assertions as readily as in either implementation.
Rgba firstPainted(RenderHarness& h) {
    for (int y = 0; y < kHeight; y++)
        for (int x = 0; x < kWidth; x++) {
            const Rgba p = h.pixel(x, y);
            if (p != kWhite) return p;
        }
    return kWhite;
}

// Wide enough to overflow a 200px surface at 16 DIP in any typeface, so the wrapping
// and trimming assertions have something to work on. ASCII on purpose: the wchar_t
// element-width question is real and is text_api.h's subject, but it is not what
// these assertions are measuring, and a non-BMP character here would confound the two.
const wchar_t* const kLongText = L"The quick brown fox jumps over the lazy dog";
const uint32_t kLongLength = 43;

std::unique_ptr<tinta::TextFormat> makeFormat(RenderHarness& h, float size = 16.0f) {
    return h.textFactory().CreateTextFormat(h.fontFamily(), tinta::FontWeight::Normal,
                                            tinta::FontStyle::Normal,
                                            tinta::FontStretch::Normal, size, L"en-us");
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

    // === TEXT ================================================================
    //
    // 132 of the app.renderTarget call sites are text, so this is the largest single
    // item in the seam. Everything below is a relation, not a measurement of a
    // typeface.

    // --- CONTROL: is there a font at all, and is the size the seam's own unit? -
    {
        std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
        check(format != nullptr, "CONTROL: the backend can create a text format");
        if (format) {
            // The claim behind storing the size rather than reading it back off the
            // native font object. DirectWrite is in DIPs and QFont is in points, so a
            // round trip through pointSizeF * 96 / 72 returns 15.999999 for a 16 that
            // 24 call sites compare and divide.
            check(format->GetFontSize() == 16.0f,
                  "GetFontSize returns the DIP value it was given, exactly");

            std::unique_ptr<tinta::TextLayout> layout =
                h.textFactory().CreateTextLayout(L"Hello", 5, format.get(), 1000.0f, 100.0f);
            check(layout != nullptr, "CONTROL: the backend can create a text layout");
            if (layout) {
                tinta::TextMetrics m{};
                layout->GetMetrics(&m);
                // A font database that is present but empty resolves every family to
                // nothing and still returns numbers. A zero width is what that looks
                // like, so this is the guard that makes every metric below mean
                // something.
                check(m.width > 0.0f,
                      "CONTROL: laid-out text has a non-zero width -- an empty font "
                      "database fails here");
                check(m.height > 0.0f, "CONTROL: and a non-zero height");
                check(m.lineCount == 1u, "five characters in a 1000px box are one line");
                check(m.layoutWidth == 1000.0f && m.layoutHeight == 100.0f,
                      "the metrics echo back the box the layout was created with");
                check(layout->GetFontSize() == 16.0f,
                      "a layout carries the font size its format was created with");
            }
        }
    }

    // --- Wrapping: the relation, not a line count for a particular font --------
    {
        std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
        tinta::TextMetrics wide{};
        tinta::TextMetrics narrow{};
        if (format) {
            std::unique_ptr<tinta::TextLayout> a = h.textFactory().CreateTextLayout(
                kLongText, kLongLength, format.get(), 1000.0f, 100.0f);
            std::unique_ptr<tinta::TextLayout> b = h.textFactory().CreateTextLayout(
                kLongText, kLongLength, format.get(), 90.0f, 100.0f);
            if (a) a->GetMetrics(&wide);
            if (b) b->GetMetrics(&narrow);
        }
        check(wide.lineCount == 1u, "a long string in a 1000px box fits on one line");
        check(narrow.lineCount > wide.lineCount,
              "the SAME string in a 90px box takes more lines -- so wrapping happens");
        check(narrow.height > wide.height,
              "and the wrapped box is taller, so height tracks line count");
        // THE DISCRIMINATOR for wrapping. A backend that ignored maxWidth entirely
        // would satisfy nothing above except by accident, but a backend that clamped
        // the reported width to the box while still laying out one long line would
        // pass the line-count checks. This is what catches that.
        check(narrow.width <= 90.0f,
              "the wrapped text's ink fits inside the 90px box it was given");
        check(wide.width > 90.0f,
              "CONTROL: the unwrapped ink is wider than 90px, so the check above is not "
              "vacuous");
    }

    // --- NoWrap overrides the box, at any width -------------------------------
    {
        std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
        tinta::TextMetrics m{};
        if (format) {
            format->SetWordWrapping(tinta::WordWrapping::NoWrap);
            std::unique_ptr<tinta::TextLayout> layout = h.textFactory().CreateTextLayout(
                kLongText, kLongLength, format.get(), 40.0f, 100.0f);
            if (layout) layout->GetMetrics(&m);
        }
        check(m.lineCount == 1u,
              "NoWrap gives one line however narrow the box -- 40px here");
        check(m.width > 40.0f,
              "and its ink overflows the box, which is what NoWrap means");
    }

    // --- Trailing whitespace: two widths that must not be one ------------------
    {
        std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
        tinta::TextMetrics m{};
        if (format) {
            format->SetWordWrapping(tinta::WordWrapping::NoWrap);
            std::unique_ptr<tinta::TextLayout> layout = h.textFactory().CreateTextLayout(
                L"ab      ", 8, format.get(), 1000.0f, 100.0f);
            if (layout) layout->GetMetrics(&m);
        }
        check(m.width <= m.widthIncludingTrailingWhitespace,
              "width never exceeds widthIncludingTrailingWhitespace");
        check(m.widthIncludingTrailingWhitespace > m.width,
              "and for a string ending in six spaces the two DIFFER -- a backend "
              "reporting one number for both is not measuring trailing whitespace");
    }

    // --- TextAlignment moves the ink, in the metrics AND in the pixels ---------
    //
    // Two readings of the same property, because either alone can be satisfied
    // wrongly: a backend could report an offset it never applied, or apply one it
    // never reports. DirectWrite defines alignment as an ink offset within the box,
    // and QtTextRun computes that offset explicitly instead of delegating to
    // QTextOption for exactly this reason.
    {
        const tinta::RectF box = tinta::rectF(0, 20, 180, 60);
        float lefts[3] = {0.0f, 0.0f, 0.0f};
        int inkStarts[3] = {-1, -1, -1};
        const tinta::TextAlignment order[3] = {tinta::TextAlignment::Leading,
                                               tinta::TextAlignment::Center,
                                               tinta::TextAlignment::Trailing};
        for (int i = 0; i < 3; i++) {
            std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
            if (!format) continue;
            format->SetTextAlignment(order[i]);
            std::unique_ptr<tinta::TextLayout> layout = h.textFactory().CreateTextLayout(
                L"Hi", 2, format.get(), box.right - box.left, box.bottom - box.top);
            if (!layout) continue;
            tinta::TextMetrics m{};
            layout->GetMetrics(&m);
            lefts[i] = m.left;

            whiteSurface(h);
            std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
            h.begin().DrawTextLayout(tinta::pointF(box.left, box.top), layout.get(),
                                     brush.get());
            h.end();
            inkStarts[i] = leftmostPainted(h);
        }
        check(lefts[1] > lefts[0], "Center reports its ink further right than Leading");
        check(lefts[2] > lefts[1], "and Trailing further right again");
        check(inkStarts[0] >= 0,
              "CONTROL: leading-aligned text actually painted, so the columns below are "
              "real");
        check(inkStarts[1] > inkStarts[0],
              "the PIXELS agree: centred text starts in a later column than leading");
        check(inkStarts[2] > inkStarts[1], "and trailing later still");
    }

    // --- ParagraphAlignment: the vertical axis QTextOption does not cover ------
    {
        float tops[2] = {0.0f, 0.0f};
        int inkTops[2] = {-1, -1};
        const tinta::ParagraphAlignment order[2] = {tinta::ParagraphAlignment::Near,
                                                    tinta::ParagraphAlignment::Far};
        for (int i = 0; i < 2; i++) {
            std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
            if (!format) continue;
            format->SetParagraphAlignment(order[i]);
            std::unique_ptr<tinta::TextLayout> layout =
                h.textFactory().CreateTextLayout(L"Hi", 2, format.get(), 180.0f, 90.0f);
            if (!layout) continue;
            tinta::TextMetrics m{};
            layout->GetMetrics(&m);
            tops[i] = m.top;

            whiteSurface(h);
            std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
            h.begin().DrawTextLayout(tinta::pointF(5, 5), layout.get(), brush.get());
            h.end();
            inkTops[i] = topmostPainted(h);
        }
        check(tops[1] > tops[0], "ParagraphAlignment::Far reports its ink below Near");
        check(inkTops[0] >= 0, "CONTROL: the Near case painted");
        check(inkTops[1] > inkTops[0], "and the pixels agree -- Far paints lower down");
    }

    // --- DrawText, the string overload: 106 of the 132 text call sites ---------
    //
    // The layoutRect's WIDTH and HEIGHT become the layout box, which is what
    // Direct2D's layoutRect means -- so this must land in the same place as a layout
    // created with those dimensions and drawn at the rect's top-left. Checking the two
    // against each other is stronger than checking either against a coordinate,
    // because it is the property the 106 call sites actually rely on.
    {
        const tinta::RectF box = tinta::rectF(30, 20, 190, 90);

        whiteSurface(h);
        {
            std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
            std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
            if (format)
                h.begin().DrawText(L"Hi", 2, format.get(), box, brush.get());
            else
                h.begin();
            h.end();
        }
        const int stringLeft = leftmostPainted(h);
        const int stringTop = topmostPainted(h);
        const Rgba stringInk = firstPainted(h);

        whiteSurface(h);
        {
            std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
            std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
            std::unique_ptr<tinta::TextLayout> layout =
                format ? h.textFactory().CreateTextLayout(L"Hi", 2, format.get(),
                                                          box.right - box.left,
                                                          box.bottom - box.top)
                       : nullptr;
            if (layout)
                h.begin().DrawTextLayout(tinta::pointF(box.left, box.top), layout.get(),
                                         brush.get());
            else
                h.begin();
            h.end();
        }
        const int layoutLeft = leftmostPainted(h);
        const int layoutTop = topmostPainted(h);

        check(stringLeft >= 0, "CONTROL: DrawText painted something");
        check(stringLeft == layoutLeft,
              "DrawText and DrawTextLayout put the ink in the same column, so "
              "layoutRect's width/height ARE the layout box");
        check(stringTop == layoutTop, "and in the same row");
        check(stringLeft >= 30,
              "the ink starts at or after the rect's left edge -- it is not drawn at "
              "the origin");
        // Text is the one call in the Qt backend that paints with the PEN rather than
        // the brush. Getting that wrong draws nothing at all, which the CONTROL above
        // catches; getting the colour wrong is what this catches.
        check(stringInk.r > stringInk.g && stringInk.r > stringInk.b,
              "the glyphs are painted in the brush's colour, not the pen's default black");
    }

    // --- DrawTextOptions::Clip -------------------------------------------------
    //
    // No call site in the tree passes Clip today, which is precisely the reason to
    // assert it now: an unimplemented flag that is never exercised is indistinguishable
    // from an implemented one until the first caller needs it.
    {
        int rights[2] = {-1, -1};
        const tinta::DrawTextOptions order[2] = {tinta::DrawTextOptions::None,
                                                 tinta::DrawTextOptions::Clip};
        for (int i = 0; i < 2; i++) {
            std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
            if (!format) continue;
            format->SetWordWrapping(tinta::WordWrapping::NoWrap);
            std::unique_ptr<tinta::TextLayout> layout = h.textFactory().CreateTextLayout(
                kLongText, kLongLength, format.get(), 60.0f, 40.0f);
            if (!layout) continue;

            whiteSurface(h);
            std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
            h.begin().DrawTextLayout(tinta::pointF(10, 20), layout.get(), brush.get(),
                                     order[i]);
            h.end();
            rights[i] = rightmostPainted(h);
        }
        check(rights[0] > 70,
              "CONTROL: unclipped NoWrap text overflows its 60px box, so there is "
              "something for the clip to remove");
        check(rights[1] >= 0, "CONTROL: the clipped case still painted something");
        check(rights[1] < rights[0], "Clip removes the overflow");
        check(rights[1] <= 71,
              "and what survives stays inside the layout box -- x 10 plus 60, plus a "
              "pixel of antialiasing");
    }

    // --- Trimming: the ellipsis sign, and the 4 call sites that pass none ------
    {
        std::unique_ptr<tinta::InlineObject> sign;
        tinta::TextMetrics trimmed{};
        tinta::TextMetrics untrimmed{};
        {
            std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
            if (format) {
                format->SetWordWrapping(tinta::WordWrapping::NoWrap);
                std::unique_ptr<tinta::TextLayout> layout = h.textFactory().CreateTextLayout(
                    kLongText, kLongLength, format.get(), 80.0f, 40.0f);
                if (layout) layout->GetMetrics(&untrimmed);

                sign = h.textFactory().CreateEllipsisTrimmingSign(format.get());
                check(sign != nullptr, "the backend can create an ellipsis trimming sign");
                const tinta::Trimming trimming{tinta::TrimmingGranularity::Character, 0, 0};
                format->SetTrimming(trimming, sign.get());
                std::unique_ptr<tinta::TextLayout> clipped = h.textFactory().CreateTextLayout(
                    kLongText, kLongLength, format.get(), 80.0f, 40.0f);
                if (clipped) clipped->GetMetrics(&trimmed);
            }
        }
        check(untrimmed.width > 80.0f,
              "CONTROL: without trimming the ink overflows the 80px box");
        check(trimmed.width < untrimmed.width,
              "trimming shortens the laid-out text");
        check(trimmed.width <= 81.0f,
              "and brings it inside the box it was given");
    }

    // --- Hit testing: the offset contract, in BOTH directions ------------------
    //
    // text_api.h's TextLayout states the contract these assertions exist to hold the
    // backends to: every offset crossing the seam indexes the CALLER'S wchar_t
    // buffer. On Windows that is an identity and unfalsifiable by construction --
    // DirectWrite's units and a 16-bit wchar_t are the same thing -- which is exactly
    // why the assertions live here rather than in the Windows test. The Qt backend
    // has to convert, and the only thing that can catch a wrong conversion is a
    // suite that runs the same round trip against a backend that does not need one.
    //
    // Every check below is RELATIONAL. "The caret for offset 2 is right of offset 1",
    // "a point inside a glyph maps back to that glyph's offset", "a wrapped range
    // yields more rectangles than an unwrapped one" hold in Segoe UI and DejaVu Sans
    // alike. A literal x would be a statement about a typeface, and -- worse -- would
    // pass on whichever backend it was written against and redden on the other, which
    // is the failure mode that makes a cross-backend literal actively misleading.
    {
        std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
        std::unique_ptr<tinta::TextLayout> layout =
            format ? h.textFactory().CreateTextLayout(L"Hi there", 8, format.get(),
                                                      1000.0f, 100.0f)
                   : nullptr;
        check(layout != nullptr, "CONTROL: a layout exists to hit-test at all");

        if (layout) {
            // --- direction OUT: offset -> point ---
            float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
            tinta::HitTestMetrics m0{};
            tinta::HitTestMetrics m1{};
            tinta::HitTestMetrics m2{};
            const bool ok0 = layout->HitTestTextPosition(0, false, &x0, &y0, &m0);
            const bool ok1 = layout->HitTestTextPosition(1, false, &x1, &y1, &m1);
            const bool ok2 = layout->HitTestTextPosition(2, false, &x2, &y2, &m2);

            check(ok0 && ok1 && ok2,
                  "CONTROL: HitTestTextPosition succeeds, so the numbers below are answers "
                  "and not zeroed outputs");
            check(x1 > x0, "the caret for offset 1 is right of offset 0");
            check(x2 > x1, "and offset 2 right of offset 1");
            check(y0 == y1 && y1 == y2,
                  "all three are on one line, so they report one y");

            // THE FIELD THE CONTRACT IS ABOUT. A backend that answered in its own
            // internal units would still be monotonic and would still look right on
            // screen; textPosition coming back as the offset that went in is the only
            // thing that says the two coordinate systems are the same one.
            check(m0.textPosition == 0 && m1.textPosition == 1 && m2.textPosition == 2,
                  "the metrics report back the caller's own offsets, not a re-indexing");
            check(m0.length >= 1 && m1.length >= 1,
                  "and a length in those same units, at least one unit per ASCII character");
            check(m0.width > 0.0f && m0.height > 0.0f,
                  "a real character has a real box");

            // Adjacent clusters ABUT. This is the relation that catches a width taken
            // from the wrong cluster or a leading edge measured from the line start:
            // both stay monotonic, and only this fails.
            check(std::fabs((x0 + m0.width) - x1) <= 1.0f,
                  "offset 0's trailing edge meets offset 1's leading edge");

            // A trailing hit is the far side of the SAME cluster, which is what all 3
            // of the tree's out-offset sites branch on when they add m.length.
            float xTrail = 0.0f;
            float yTrail = 0.0f;
            layout->HitTestTextPosition(0, true, &xTrail, &yTrail, nullptr);
            check(xTrail > x0, "a trailing hit reports the far edge of the cluster");
            check(std::fabs(xTrail - x1) <= 1.0f,
                  "which is where the next cluster starts");

            // --- direction IN: point -> offset, closing the loop ---
            //
            // This is the assertion the whole slice is for. It is also the only one
            // that would catch a conversion that is self-consistent but wrong -- a
            // backend mapping offsets through the same wrong table in both directions
            // passes every monotonicity check above and fails here.
            bool trailing = false;
            bool inside = false;
            tinta::HitTestMetrics back{};
            const bool okIn = layout->HitTestPoint(x1 + m1.width * 0.25f,
                                                   y1 + m1.height * 0.5f, &trailing,
                                                   &inside, &back);
            check(okIn, "CONTROL: HitTestPoint succeeds");
            check(back.textPosition == 1,
                  "a point inside offset 1's glyph maps back to offset 1 -- the offset "
                  "contract round-trips");
            check(inside, "and reports the point as inside the text");
            check(!trailing,
                  "a point in the LEADING quarter of a glyph is not a trailing hit");

            bool trailing2 = false;
            layout->HitTestPoint(x1 + m1.width * 0.9f, y1 + m1.height * 0.5f, &trailing2,
                                 nullptr, nullptr);
            check(trailing2,
                  "a point in the TRAILING tenth of the same glyph is -- the flag is "
                  "derived from the point, not fixed");

            // Past the end of the text, which is what a drag-select does. The call must
            // SUCCEED and report not-inside; refusing would make selection.cpp's 5
            // sites lose the drag.
            bool insideFar = true;
            const bool okFar = layout->HitTestPoint(900.0f, y0 + m0.height * 0.5f, nullptr,
                                                    &insideFar, nullptr);
            check(okFar, "a point past the end of the line still answers");
            check(!insideFar, "and says it is outside the text");
        }
    }

    // --- A CHARACTER OUTSIDE THE BMP, which is the only case the map exists for --
    //
    // Every assertion above uses ASCII, and I nearly shipped the slice that way. Asking
    // what a mutation to the Qt index map WOULD redden is what caught it: nothing. The
    // map is live on Linux -- wchar_t is 32 bits there, so it is built and consulted --
    // but for ASCII it is numerically the identity, so breaking it changes no number
    // any assertion above reads. The contract's whole subject was untested by the suite
    // written to test it.
    //
    // U+1F600 is TWO wchar_t units on Windows and ONE on Linux, so the offsets a caller
    // uses genuinely differ by platform. That is not a problem for the assertions, it is
    // the point of them: the contract says an offset indexes the CALLER'S buffer, so the
    // expected values are derived from the literal's own element count rather than
    // written down. `b` is the last element on both platforms whatever its index is.
    //
    // No glyph coverage is assumed. Whether the font can draw U+1F600 affects how the
    // cluster LOOKS and not how it is indexed, and a .notdef box still has a width.
    {
        const wchar_t kAstral[] = L"a\U0001F600b";
        const uint32_t kAstralLen = (uint32_t)(sizeof(kAstral) / sizeof(wchar_t)) - 1;
        // The units the astral character occupies in the caller's buffer: 2 where
        // wchar_t is UTF-16, 1 where it is UTF-32.
        const uint32_t kAstralUnits = (sizeof(wchar_t) == 2) ? 2u : 1u;
        const uint32_t kOffsetOfB = 1u + kAstralUnits;

        // CONTROL, and it is not ceremony: if the compiler encoded the literal some
        // third way, every expectation below would be measuring the wrong thing while
        // still looking self-consistent.
        check(kAstralLen == 1u + kAstralUnits + 1u,
              "CONTROL: the astral literal has the element count this platform's wchar_t "
              "implies, so the offsets below are the caller's real ones");

        std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
        std::unique_ptr<tinta::TextLayout> layout =
            format ? h.textFactory().CreateTextLayout(kAstral, kAstralLen, format.get(),
                                                      1000.0f, 100.0f)
                   : nullptr;
        check(layout != nullptr, "CONTROL: a layout over non-BMP text exists");

        if (layout) {
            float xa = 0.0f, xAstral = 0.0f, xb = 0.0f, ignored = 0.0f;
            tinta::HitTestMetrics ma{};
            tinta::HitTestMetrics mAstral{};
            tinta::HitTestMetrics mb{};
            layout->HitTestTextPosition(0, false, &xa, &ignored, &ma);
            layout->HitTestTextPosition(1, false, &xAstral, &ignored, &mAstral);
            layout->HitTestTextPosition(kOffsetOfB, false, &xb, &ignored, &mb);

            check(ma.length == 1, "'a' occupies one caller unit");

            // THE ASSERTION THE MAP EXISTS FOR. A Qt backend that reported QString
            // units would answer 2 here on Linux, where the correct answer is 1 --
            // and that is the number that flows straight into a document offset at
            // 2 of the 3 out-offset call sites.
            check(mAstral.length == kAstralUnits,
                  "the astral character's length is reported in the CALLER's units, "
                  "which is 2 on a UTF-16 wchar_t and 1 on a UTF-32 one");
            check(mAstral.textPosition == 1, "and it starts at offset 1 on both");

            // THE OTHER HALF, and the one that catches an index map broken in the OUT
            // direction rather than the length: 'b' must be laid out entirely after the
            // astral cluster. An offset mapped without the conversion lands mid
            // surrogate pair, where both backends resolve to the cluster's own start --
            // so 'b' would report the astral character's x and this fails while every
            // monotonicity check above still passes.
            check(mAstral.width > 0.0f,
                  "CONTROL: the astral cluster has a real width, glyph or .notdef box");
            check(xb > xAstral,
                  "'b' begins to the right of the astral character, so its offset was "
                  "converted and not passed through");
            check(mb.textPosition == kOffsetOfB,
                  "and 'b' reports the caller's own offset for itself");

            // Round-trip through the astral cluster: a point inside it must come back
            // as offset 1 with the caller's length, on both platforms.
            tinta::HitTestMetrics back{};
            layout->HitTestPoint(xAstral + mAstral.width * 0.5f, mAstral.height * 0.5f,
                                 nullptr, nullptr, &back);
            check(back.textPosition == 1 && back.length == kAstralUnits,
                  "a point inside the astral character round-trips to offset 1 with the "
                  "caller's own length");
        }
    }

    // --- Which coordinate space a hit-test x is in -----------------------------
    //
    // THIS ONE SETTLES A QUESTION RATHER THAN GUARDING AN ANSWER, and text_qt.cpp
    // says so at the point it depends on it. QtTextRun does its own alignment via
    // QTextLine::setPosition instead of QTextOption, and whether Qt's cursorToX
    // includes that offset is invisible for a Leading-aligned string -- where the
    // offset is 0 and both readings agree -- and wrong by half the box for a Centered
    // one. Direct2D has no such ambiguity, so this is a genuine cross-backend claim:
    // the two must put the first caret in the same place relative to the ink.
    //
    // The relation is to GetMetrics().left, which is the ink's own left edge in the
    // same space. Tying the two readings together is what makes neither of them free
    // to drift, and it needs no literal and no font.
    {
        float caretLefts[2] = {0.0f, 0.0f};
        float inkLefts[2] = {0.0f, 0.0f};
        bool got[2] = {false, false};
        const tinta::TextAlignment order[2] = {tinta::TextAlignment::Leading,
                                               tinta::TextAlignment::Center};
        for (int i = 0; i < 2; i++) {
            std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
            if (!format) continue;
            format->SetTextAlignment(order[i]);
            std::unique_ptr<tinta::TextLayout> layout =
                h.textFactory().CreateTextLayout(L"Hi", 2, format.get(), 180.0f, 60.0f);
            if (!layout) continue;
            tinta::TextMetrics tm{};
            layout->GetMetrics(&tm);
            inkLefts[i] = tm.left;
            float x = 0.0f;
            float y = 0.0f;
            got[i] = layout->HitTestTextPosition(0, false, &x, &y, nullptr);
            caretLefts[i] = x;
        }
        check(got[0] && got[1], "CONTROL: both alignments produced a hit-test answer");
        // The control that stops the pair below from being vacuous: if alignment were
        // ignored altogether, both columns would be zero and both equalities would
        // hold for the wrong reason.
        check(inkLefts[1] > inkLefts[0],
              "CONTROL: Center really does move the ink right, so the two rows differ");
        check(std::fabs(caretLefts[0] - inkLefts[0]) <= 1.0f,
              "leading-aligned: the first caret sits at the ink's left edge");
        check(std::fabs(caretLefts[1] - inkLefts[1]) <= 1.0f,
              "centred: it sits at the ink's left edge THERE too -- a hit-test x is in "
              "layout space, alignment offset included");
        check(caretLefts[1] > caretLefts[0],
              "so the centred caret is right of the leading one, by the same offset the "
              "ink moved");
    }

    // --- HitTestTextRange: one rectangle per line, and the two-pass protocol ----
    //
    // The two-pass shape is asserted as a PROTOCOL and not just on its happy path,
    // because editor.cpp:2792 sizes a vector from the count-probe pass and would draw
    // no selection at all if a backend answered that pass with zero. The two backends
    // reach the same answer by different routes -- DirectWrite fails the call with
    // E_NOT_SUFFICIENT_BUFFER while filling the count in, Qt counts the lines it
    // walked -- so agreement here is a real cross-backend claim rather than a
    // restatement of one implementation.
    {
        std::unique_ptr<tinta::TextFormat> wideFormat = makeFormat(h);
        std::unique_ptr<tinta::TextFormat> narrowFormat = makeFormat(h);
        std::unique_ptr<tinta::TextLayout> wide =
            wideFormat ? h.textFactory().CreateTextLayout(kLongText, kLongLength,
                                                          wideFormat.get(), 1000.0f, 200.0f)
                       : nullptr;
        std::unique_ptr<tinta::TextLayout> narrow =
            narrowFormat ? h.textFactory().CreateTextLayout(kLongText, kLongLength,
                                                            narrowFormat.get(), 80.0f, 200.0f)
                         : nullptr;
        check(wide != nullptr && narrow != nullptr,
              "CONTROL: both a one-line and a wrapped layout exist to range-test");

        if (wide && narrow) {
            uint32_t wideCount = 0;
            uint32_t narrowCount = 0;
            const bool wideProbe =
                wide->HitTestTextRange(0, kLongLength, 0.0f, 0.0f, nullptr, 0, &wideCount);
            const bool narrowProbe =
                narrow->HitTestTextRange(0, kLongLength, 0.0f, 0.0f, nullptr, 0, &narrowCount);

            check(wideCount >= 1,
                  "CONTROL: the count-probe pass reports a count, so a zero below would "
                  "be a refusal and not an empty range");
            check(!wideProbe && !narrowProbe,
                  "the count-probe pass reports FAILURE -- it could not fill a buffer it "
                  "was not given -- while still yielding the count");
            check(narrowCount > wideCount,
                  "a wrapped range spans more lines than an unwrapped one, so it yields "
                  "more rectangles");

            // The real pass. Same range, a buffer big enough, and the count must not
            // change: a backend that recounted differently on the second pass would
            // overrun the buffer the first pass sized.
            std::vector<tinta::HitTestMetrics> boxes(narrowCount + 4);
            uint32_t actual = 0;
            const bool real = narrow->HitTestTextRange(0, kLongLength, 0.0f, 0.0f,
                                                       boxes.data(),
                                                       (uint32_t)boxes.size(), &actual);
            check(real, "the pass WITH a buffer succeeds");
            check(actual == narrowCount,
                  "and returns the same count the probe pass promised");

            bool allReal = actual > 0;
            for (uint32_t i = 0; i < actual; i++) {
                if (boxes[i].width <= 0.0f || boxes[i].height <= 0.0f) allReal = false;
            }
            check(allReal, "every rectangle returned has a real width and height");

            bool descending = false;
            for (uint32_t i = 1; i < actual; i++) {
                if (boxes[i].top <= boxes[i - 1].top) descending = true;
            }
            check(!descending,
                  "successive rectangles move DOWN the layout, one line at a time");

            // originX/originY are an offset the caller supplies and the metrics carry,
            // which is easy to accept and silently ignore.
            std::vector<tinta::HitTestMetrics> shifted(narrowCount + 4);
            uint32_t shiftedCount = 0;
            narrow->HitTestTextRange(0, kLongLength, 10.0f, 20.0f, shifted.data(),
                                     (uint32_t)shifted.size(), &shiftedCount);
            check(shiftedCount == actual, "the origin does not change how many there are");
            if (shiftedCount == actual && actual > 0) {
                check(std::fabs((shifted[0].left - boxes[0].left) - 10.0f) <= 0.5f &&
                          std::fabs((shifted[0].top - boxes[0].top) - 20.0f) <= 0.5f,
                      "but it does shift them, by exactly what was passed");
            }

            // AN EMPTY RANGE IS A CARET, AND THIS ASSERTION IS THE ONE THAT WAS WRONG.
            //
            // It first said "an empty range succeeds with a count of 0", which is what
            // the Qt backend did and what I believed the reasonable contract was. It
            // passed on Qt and failed on Direct2D, and the measurement settled it:
            // DirectWrite returns ONE metric, zero width, full line height, at the
            // requested offset. So the assertion now states DirectWrite's contract and
            // the Qt backend was changed to meet it -- because that is the behaviour
            // the editor's selection drawing already sees, and preserving it is the
            // entire premise of the seam.
            //
            // The relation to HitTestTextPosition is what keeps this from being a
            // transcription of one implementation: an empty selection's rectangle must
            // sit exactly where that offset's caret sits, which is a claim about the
            // two methods agreeing and needs no literal.
            const uint32_t caretAt = 2;
            uint32_t emptyProbeCount = 7;
            const bool emptyProbe = narrow->HitTestTextRange(caretAt, 0, 0.0f, 0.0f,
                                                            nullptr, 0, &emptyProbeCount);
            check(emptyProbeCount == 1,
                  "an empty range yields ONE rectangle -- the caret -- and not zero");
            check(!emptyProbe,
                  "and its count-probe pass fails like any other, so the two-pass "
                  "protocol has no special case");

            tinta::HitTestMetrics caretBox[2]{};
            uint32_t caretCount = 0;
            const bool caretOk = narrow->HitTestTextRange(caretAt, 0, 0.0f, 0.0f, caretBox,
                                                          2, &caretCount);
            check(caretOk && caretCount == 1, "the pass with a buffer returns that one");
            check(caretBox[0].width == 0.0f,
                  "the caret rectangle has ZERO width -- it is a position, not a span");
            check(caretBox[0].height > 0.0f,
                  "and a real height, so it is drawable as a caret");
            check(caretBox[0].textPosition == caretAt && caretBox[0].length == 0,
                  "reported at the offset asked for, with a length of 0");

            float caretX = 0.0f;
            float caretY = 0.0f;
            const bool caretPosOk =
                narrow->HitTestTextPosition(caretAt, false, &caretX, &caretY, nullptr);
            check(caretPosOk, "CONTROL: the same offset also answers HitTestTextPosition");
            check(std::fabs(caretBox[0].left - caretX) <= 1.0f &&
                      std::fabs(caretBox[0].top - caretY) <= 1.0f,
                  "and the empty range's rectangle sits exactly where that caret is -- "
                  "the two hit-test directions agree about one offset");
        }
    }

    // --- Transforms: round-trip, then the composition ORDER -------------------
    //
    // The order claim is the one worth the most here. QTransform and
    // D2D1_MATRIX_3X2_F name their members identically and compose identically, and
    // that similarity is exactly what would make an operand swap in one backend
    // invisible: the picture stays plausible, just wrong. So it is asserted as a
    // RELATION between two compositions of the same two matrices, which needs no
    // absolute pixel value and holds for either backend.
    {
        whiteSurface(h);
        tinta::Matrix3x2F read{};
        tinta::RenderTarget& rt = h.begin();
        rt.GetTransform(&read);
        h.end();
        check(read.m11 == 1.0f && read.m22 == 1.0f && read.m12 == 0.0f &&
                  read.m21 == 0.0f && read.dx == 0.0f && read.dy == 0.0f,
              "CONTROL: a fresh target starts at the identity transform");
    }

    {
        whiteSurface(h);
        // Deliberately not a translation or a scale: an asymmetric matrix catches a
        // backend that transposes m12 and m21, which every symmetric test misses.
        const tinta::Matrix3x2F sent{2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};
        tinta::Matrix3x2F read{};
        tinta::RenderTarget& rt = h.begin();
        rt.SetTransform(sent);
        rt.GetTransform(&read);
        rt.SetTransform(tinta::matrixIdentity());
        h.end();
        check(read.m11 == sent.m11 && read.m12 == sent.m12 && read.m21 == sent.m21 &&
                  read.m22 == sent.m22 && read.dx == sent.dx && read.dy == sent.dy,
              "GetTransform returns exactly what SetTransform was given, m12/m21 not transposed");
    }

    {
        // Identity must leave ink where an untransformed draw put it, and a
        // translation must move it by the amount translated. Both measured in
        // PIXELS, because a backend could store a transform it never applies.
        std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);

        whiteSurface(h);
        h.begin().FillRectangle(tinta::rectF(20, 10, 40, 30), brush.get());
        h.end();
        const int plainLeft = leftmostPainted(h);
        const int plainTop = topmostPainted(h);
        check(plainLeft >= 0, "CONTROL: the untransformed rectangle painted something");

        whiteSurface(h);
        {
            tinta::RenderTarget& rt = h.begin();
            rt.SetTransform(tinta::matrixIdentity());
            rt.FillRectangle(tinta::rectF(20, 10, 40, 30), brush.get());
            rt.SetTransform(tinta::matrixIdentity());
            h.end();
        }
        check(leftmostPainted(h) == plainLeft && topmostPainted(h) == plainTop,
              "an explicit identity transform changes nothing");

        whiteSurface(h);
        {
            tinta::RenderTarget& rt = h.begin();
            rt.SetTransform(tinta::matrixTranslation(30.0f, 20.0f));
            rt.FillRectangle(tinta::rectF(20, 10, 40, 30), brush.get());
            rt.SetTransform(tinta::matrixIdentity());
            h.end();
        }
        check(leftmostPainted(h) == plainLeft + 30,
              "a translation moves ink right by exactly the amount translated");
        check(topmostPainted(h) == plainTop + 20,
              "and down by exactly the amount translated");

        // REPLACE, NOT COMPOSE -- and this pair exists because writing the
        // falsifier first showed the rest of the block could not see the
        // difference. Qt's setWorldTransform takes a combine flag; the seam
        // specifies replace, because all 10 call sites in the tree compose for
        // themselves and a composing seam would double-apply. But every other
        // block here resets to identity inside the same session and then opens a
        // FRESH painter, which starts at identity regardless -- so a backend
        // passing combine=true would have gone green everywhere above. The second
        // SetTransform below is what makes the flag observable: under compose it
        // would leave the translation standing.
        whiteSurface(h);
        {
            tinta::RenderTarget& rt = h.begin();
            rt.SetTransform(tinta::matrixTranslation(30.0f, 20.0f));
            rt.SetTransform(tinta::matrixIdentity());
            rt.FillRectangle(tinta::rectF(20, 10, 40, 30), brush.get());
            h.end();
        }
        check(leftmostPainted(h) == plainLeft,
              "SetTransform REPLACES: a following identity undoes the translation rather than composing with it");
        check(topmostPainted(h) == plainTop,
              "and it does so on both axes, which a compose would leave shifted down");
    }

    {
        // THE ORDER ASSERTION. translation * scale applies the translation first, so
        // the scale multiplies the already-translated offset; the other order scales
        // first and then adds an unscaled offset. Both put ink further right than
        // nothing does, and they must NOT agree with each other -- which is what
        // makes this a test of composition rather than of either operation.
        std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
        const tinta::Matrix3x2F t = tinta::matrixTranslation(10.0f, 0.0f);
        const tinta::Matrix3x2F s = tinta::matrixScale(2.0f, 2.0f);

        whiteSurface(h);
        {
            tinta::RenderTarget& rt = h.begin();
            rt.SetTransform(t * s);
            rt.FillRectangle(tinta::rectF(1, 1, 6, 6), brush.get());
            rt.SetTransform(tinta::matrixIdentity());
            h.end();
        }
        const int translateThenScale = leftmostPainted(h);

        whiteSurface(h);
        {
            tinta::RenderTarget& rt = h.begin();
            rt.SetTransform(s * t);
            rt.FillRectangle(tinta::rectF(1, 1, 6, 6), brush.get());
            rt.SetTransform(tinta::matrixIdentity());
            h.end();
        }
        const int scaleThenTranslate = leftmostPainted(h);

        check(translateThenScale > 0 && scaleThenTranslate > 0,
              "CONTROL: both compositions painted something");
        check(translateThenScale != scaleThenTranslate,
              "CONTROL: the two matrices do not commute, so the order is observable");
        check(translateThenScale > scaleThenTranslate,
              "translation * scale scales the offset too, so its ink lands further right");

        // And the seam's own arithmetic agrees with what the pixels just showed,
        // which is what rules out both backends applying the same wrong order.
        const tinta::Matrix3x2F composed = t * s;
        check(composed.dx == 20.0f && composed.m11 == 2.0f,
              "tinta::operator* composes in row-vector order: dx is scaled to 20");
    }

    // --- Bitmaps: placement, orientation, sub-rect, opacity, interpolation -----
    //
    // THE BITMAP IS TWO-COLOURED ON PURPOSE, for the same reason the transform block
    // above uses an asymmetric matrix. A single-colour bitmap makes almost every
    // interesting mistake invisible: a horizontal flip, a transposed source
    // rectangle, a srcRect ignored entirely, and a destRect misread as x/y/width/
    // height all paint the same solid block and all pass. Left half red, right half
    // blue turns each of those into a wrong COLOUR at a named pixel.
    //
    // Nothing here asserts an absolute pixel count or a blend value. The dest rects
    // are integer-aligned and the blit is confined to them on both backends, so the
    // ink BOUNDS are exact and safe to assert; the interpolated interior is only ever
    // compared against itself under a different mode.
    {
        const int bw = 10;
        const int bh = 10;
        std::vector<Rgba> source(static_cast<size_t>(bw) * bh);
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++)
                source[static_cast<size_t>(y) * bw + x] = (x < bw / 2) ? kRed : kBlue;

        std::unique_ptr<tinta::Bitmap> bitmap = h.makeBitmap(bw, bh, source);
        // A harness that cannot build a bitmap has failed to supply an INSTRUMENT,
        // which is a different thing from the seam being broken -- so this is checked
        // rather than dereferenced, and every assertion below is skipped rather than
        // silently reading an unpainted surface as agreement.
        check(bitmap != nullptr,
              "CONTROL: the harness built a bitmap -- every bitmap assertion below is "
              "void without it");

        if (bitmap) {
            const tinta::SizeF size = bitmap->GetSize();
            check(size.width == static_cast<float>(bw) && size.height == static_cast<float>(bh),
                  "Bitmap::GetSize reports the dimensions the bitmap was created with");

            // 1:1, through the TWO-ARGUMENT call -- the arity 3 of the 7 sites use, so
            // this also checks that the defaults bind to opacity 1 and linear.
            whiteSurface(h);
            {
                tinta::RenderTarget& rt = h.begin();
                rt.DrawBitmap(bitmap.get(), tinta::rectF(20, 10, 30, 20));
                h.end();
            }
            check(leftmostPainted(h) == 20,
                  "DrawBitmap puts the bitmap's left edge at destRect.left");
            check(topmostPainted(h) == 10,
                  "and its top edge at destRect.top, so destRect is not read as x/y/w/h");
            check(h.pixel(21, 15) == kRed,
                  "the bitmap's left half lands on the left of the destination -- not mirrored");
            check(h.pixel(28, 15) == kBlue,
                  "and its right half on the right, which a flip or a transpose would swap");

            // 4x upscale. The bounds stay exact because neither backend paints outside
            // destRect; the colours are sampled well clear of the interpolation band
            // around the middle.
            whiteSurface(h);
            {
                tinta::RenderTarget& rt = h.begin();
                rt.DrawBitmap(bitmap.get(), tinta::rectF(20, 10, 60, 50));
                h.end();
            }
            check(leftmostPainted(h) == 20 && rightmostPainted(h) == 59,
                  "a destRect four times the bitmap's size scales it to fill exactly that rect");
            check(h.pixel(25, 30) == kRed && h.pixel(55, 30) == kBlue,
                  "scaling preserves orientation: red still left, blue still right");

            // srcRect, and its COUNTER-CASE. Taking only the left half must fill the
            // whole destination with red, and taking only the right half must fill the
            // same destination with blue. Either assertion alone would pass against a
            // backend that ignored srcRect entirely, because half of a bitmap drawn
            // whole is still red on the left; it is the PAIR that proves the argument
            // is read, and that its coordinates are in the SOURCE's space rather than
            // the destination's.
            whiteSurface(h);
            {
                const tinta::RectF leftHalf = tinta::rectF(0, 0, 5, 10);
                tinta::RenderTarget& rt = h.begin();
                rt.DrawBitmap(bitmap.get(), tinta::rectF(20, 10, 60, 50), 1.0f,
                              tinta::BitmapInterpolationMode::Linear, &leftHalf);
                h.end();
            }
            const bool leftHalfPainted = leftmostPainted(h) == 20;
            const bool leftHalfAllRed = h.pixel(25, 30) == kRed && h.pixel(55, 30) == kRed;

            whiteSurface(h);
            {
                const tinta::RectF rightHalf = tinta::rectF(5, 0, 10, 10);
                tinta::RenderTarget& rt = h.begin();
                rt.DrawBitmap(bitmap.get(), tinta::rectF(20, 10, 60, 50), 1.0f,
                              tinta::BitmapInterpolationMode::Linear, &rightHalf);
                h.end();
            }
            check(leftHalfPainted && leftmostPainted(h) == 20,
                  "CONTROL: both sub-rect draws filled the destination rect");
            check(leftHalfAllRed,
                  "srcRect selects a sub-region: the source's left half fills the whole "
                  "destination with red");
            check(h.pixel(25, 30) == kBlue && h.pixel(55, 30) == kBlue,
                  "COUNTER-CASE: the source's right half fills the same destination with "
                  "blue, which a backend ignoring srcRect could not do");

            // Opacity. Asserted as a RELATION against the same draw at full opacity
            // rather than as a blend value, because the exact blend is a property of
            // each backend's compositor and not of the seam.
            whiteSurface(h);
            {
                tinta::RenderTarget& rt = h.begin();
                rt.DrawBitmap(bitmap.get(), tinta::rectF(20, 10, 30, 20), 1.0f,
                              tinta::BitmapInterpolationMode::Linear);
                h.end();
            }
            const Rgba opaque = h.pixel(21, 15);
            check(opaque == kRed, "CONTROL: at opacity 1 the bitmap's own colour reaches the "
                                  "surface unblended");

            whiteSurface(h);
            {
                tinta::RenderTarget& rt = h.begin();
                rt.DrawBitmap(bitmap.get(), tinta::rectF(20, 10, 30, 20), 0.5f,
                              tinta::BitmapInterpolationMode::Linear);
                h.end();
            }
            const Rgba half = h.pixel(21, 15);
            check(half != opaque, "the opacity argument changes the result -- a backend "
                                  "dropping it would paint these identically");
            check(half.g > opaque.g && half.g < kWhite.g,
                  "and it blends toward the white background rather than replacing it, so "
                  "the value lies strictly between the bitmap's colour and the surface's");

            // Opacity 0. A REFUSAL path, and the only assertion here whose expected
            // answer is an empty surface -- so it is paired with the full-opacity draw
            // above, which must have painted, or "nothing was drawn" would be
            // indistinguishable from a DrawBitmap that never draws anything at all.
            whiteSurface(h);
            {
                tinta::RenderTarget& rt = h.begin();
                rt.DrawBitmap(bitmap.get(), tinta::rectF(20, 10, 30, 20), 0.0f,
                              tinta::BitmapInterpolationMode::Linear);
                h.end();
            }
            check(countNonWhite(h) == 0,
                  "at opacity 0 the bitmap is not drawn at all, which is only meaningful "
                  "because the opacity-1 control above painted");

            // INTERPOLATION MODE, which is the argument most likely to be accepted and
            // then thrown away: Direct2D's default is linear and Qt's is nearest, so a
            // backend that simply never touched its own setting would still produce a
            // plausible picture, and every assertion above would pass. It is made
            // observable by counting pixels along a row that are NEITHER of the
            // bitmap's two colours -- a 4x upscale under nearest-neighbour has none,
            // and under linear the red/blue boundary must blend.
            int blendedUnderNearest = 0;
            int blendedUnderLinear = 0;
            int paintedUnderNearest = 0;
            int paintedUnderLinear = 0;
            for (int pass = 0; pass < 2; pass++) {
                const tinta::BitmapInterpolationMode mode =
                    pass == 0 ? tinta::BitmapInterpolationMode::NearestNeighbor
                              : tinta::BitmapInterpolationMode::Linear;
                whiteSurface(h);
                {
                    tinta::RenderTarget& rt = h.begin();
                    rt.DrawBitmap(bitmap.get(), tinta::rectF(20, 10, 60, 50), 1.0f, mode);
                    h.end();
                }
                // The scan is the destination's INTERIOR, x in [22,58), not the whole
                // 40-pixel row. The outer two columns are where the dest rect's own
                // edge is antialiased against the white surface, and a blended pixel
                // there is a property of edge coverage under BOTH modes -- counting it
                // would redden the nearest-neighbour assertion for a reason that has
                // nothing to do with how the bitmap was sampled. The red/blue boundary
                // sits at dest x=40 (source x=5 at 4x), so the band this is looking for
                // is nowhere near the excluded columns.
                int blended = 0;
                int painted = 0;
                for (int x = 22; x < 58; x++) {
                    const Rgba p = h.pixel(x, 30);
                    if (p == kWhite) continue;
                    painted++;
                    if (p != kRed && p != kBlue) blended++;
                }
                if (pass == 0) {
                    blendedUnderNearest = blended;
                    paintedUnderNearest = painted;
                } else {
                    blendedUnderLinear = blended;
                    paintedUnderLinear = painted;
                }
            }
            check(paintedUnderNearest == 36 && paintedUnderLinear == 36,
                  "CONTROL: both interpolation modes filled the whole scanned destination "
                  "row, so the comparison below is between two real draws");
            check(blendedUnderNearest == 0,
                  "nearest-neighbour sampling produces only the bitmap's own two colours");
            check(blendedUnderLinear > 0,
                  "linear sampling blends across the red/blue boundary, so the "
                  "interpolation argument is read rather than left at the backend's default");
        }
    }

    // --- NEWLINES: a line is newline-delimited, not wrapped-only -----------------
    //
    // This family is named apart from the two below on purpose. It is asserted
    // entirely through GetMetrics, which SHIPPED five spans ago, because the
    // divergence it catches was in shipped code and not in the methods added
    // beside it -- Qt's QTextLayout does not break on '\n' at all, giving "ab\ncd"
    // one line whose natural width equals "abcd"'s. 155 assertions never caught it
    // because not one of them contained a newline. Keeping it on GetMetrics is what
    // makes a red here mean "the line model is wrong" rather than "the new struct
    // is wrong".
    //
    // The strings are real: mermaid.cpp decodes a literal \n in a node label into a
    // newline, and export_html.cpp lays those labels out to place SVG <text> rows.
    {
        static const wchar_t kOneLine[] = L"ab cd";
        static const wchar_t kTwoLines[] = L"ab\ncd";
        static const wchar_t kThreeLines[] = L"a\nb\nc";
        // The newline REMOVED rather than replaced. This is the discriminator, and
        // it exists because the obvious comparison is not one: a first version of
        // this block asserted width("ab\ncd") < width("ab cd") and called that the
        // test for a real break. The M1 mutation -- deleting the newline
        // substitution outright -- left it GREEN, because a swallowed newline
        // measures width("abcd"), which is narrower than a string containing a
        // space whether or not any line broke. Against "abcd" the two outcomes are
        // distinguishable: a broken layout is as wide as its widest HALF, while a
        // swallowed newline is exactly as wide as the whole.
        static const wchar_t kNoBreak[] = L"abcd";

        std::unique_ptr<tinta::TextFormat> f1 = makeFormat(h);
        std::unique_ptr<tinta::TextFormat> f2 = makeFormat(h);
        std::unique_ptr<tinta::TextFormat> f3 = makeFormat(h);
        std::unique_ptr<tinta::TextFormat> f4 = makeFormat(h);
        // Boxes wide enough that nothing can wrap, so every extra line below is a
        // newline's doing and not the box's.
        std::unique_ptr<tinta::TextLayout> one =
            f1 ? h.textFactory().CreateTextLayout(kOneLine, 5, f1.get(), 1000.0f, 400.0f)
               : nullptr;
        std::unique_ptr<tinta::TextLayout> two =
            f2 ? h.textFactory().CreateTextLayout(kTwoLines, 5, f2.get(), 1000.0f, 400.0f)
               : nullptr;
        std::unique_ptr<tinta::TextLayout> three =
            f3 ? h.textFactory().CreateTextLayout(kThreeLines, 5, f3.get(), 1000.0f, 400.0f)
               : nullptr;
        std::unique_ptr<tinta::TextLayout> noBreak =
            f4 ? h.textFactory().CreateTextLayout(kNoBreak, 4, f4.get(), 1000.0f, 400.0f)
               : nullptr;
        check(one && two && three && noBreak,
              "CONTROL: four layouts exist -- differing only in how many newlines they "
              "contain, and one with the newline removed rather than replaced");

        if (one && two && three && noBreak) {
            tinta::TextMetrics m1{}, m2{}, m3{}, mNoBreak{};
            one->GetMetrics(&m1);
            two->GetMetrics(&m2);
            three->GetMetrics(&m3);
            noBreak->GetMetrics(&mNoBreak);

            check(m1.lineCount == 1,
                  "CONTROL: the newline-free string of the same length is ONE line, so a "
                  "count above 1 below is a newline breaking and not the box wrapping");
            check(m2.lineCount == 2,
                  "an embedded newline BREAKS A LINE -- the same five characters with one "
                  "\\n in them lay out as two lines");
            check(m3.lineCount == 3,
                  "two embedded newlines break two lines, so the break is per-newline and "
                  "not a one-shot");
            check(m2.height > m1.height,
                  "a newline costs real vertical space: two lines are taller than one, "
                  "which is what export_html.cpp's baselines are spaced by");
            check(m3.height > m2.height,
                  "three lines are taller than two, so height tracks the newline count "
                  "rather than saturating");
            check(mNoBreak.width > 0.0f && mNoBreak.lineCount == 1,
                  "CONTROL: \"abcd\" with the newline REMOVED is one line of real width, "
                  "so the comparison below is against a measured whole-string width");
            check(m2.width < mNoBreak.width,
                  "the widest line of \"ab\\ncd\" is NARROWER than all of \"abcd\" on one "
                  "line -- the assertion that distinguishes a real break from a newline "
                  "SWALLOWED as a zero-width glyph, since a swallowed one measures "
                  "exactly the whole string. Comparing against \"ab cd\" instead does "
                  "NOT distinguish them, measured: the M1 mutation left that version "
                  "green");
        }
    }

    // --- GetLineMetrics: per-line lengths, and what `newlineLength` subtracts -----
    //
    // The consumer that makes this load-bearing is export_html.cpp, which computes
    // `visible = length - newlineLength` and slices the ORIGINAL string by it to
    // emit one SVG <text> row per line. So the assertions are about the arithmetic
    // that site does, not about any pixel: the lengths partition the string, and
    // newlineLength is what has to come off to get the visible text.
    {
        static const wchar_t kTwoLines[] = L"ab\ncd";
        std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
        std::unique_ptr<tinta::TextLayout> layout =
            format ? h.textFactory().CreateTextLayout(kTwoLines, 5, format.get(),
                                                      1000.0f, 400.0f)
                   : nullptr;
        check(layout != nullptr, "CONTROL: a two-line layout exists to measure");

        if (layout) {
            uint32_t probeCount = 0;
            const bool probe = layout->GetLineMetrics(nullptr, 0, &probeCount);
            check(probeCount == 2,
                  "CONTROL: the count-probe pass reports 2 lines, so a zero in the fill "
                  "pass below would be a refusal rather than an empty layout");
            check(!probe,
                  "GetLineMetrics's count-probe pass reports FAILURE while yielding the "
                  "count, the same protocol HitTestTextRange uses and export_html.cpp "
                  "depends on to size its vector");

            tinta::LineMetrics lines[8]{};
            const bool filled = layout->GetLineMetrics(lines, 8, &probeCount);
            check(filled && probeCount == 2,
                  "the fill pass succeeds and reports the same count the probe did");

            if (filled && probeCount == 2) {
                check(lines[0].length + lines[1].length == 5,
                      "the per-line lengths PARTITION the string -- they sum to its "
                      "length, which is what lets export_html.cpp walk `pos` through it "
                      "line by line without losing a character");
                check(lines[0].newlineLength == 1,
                      "the first line's newlineLength is 1: it was ended by one newline "
                      "character");
                check(lines[1].newlineLength == 0,
                      "the last line was ended by the end of the string, not a newline, "
                      "so nothing comes off it");
                check(lines[0].length - lines[0].newlineLength == 2,
                      "length minus newlineLength is the VISIBLE text of the line -- "
                      "\"ab\" -- which is the exact expression export_html.cpp substrings "
                      "with");
                check(lines[0].length > lines[0].newlineLength,
                      "length INCLUDES the newline rather than excluding it, so the "
                      "subtraction above cannot underflow a uint32_t");
                check(lines[0].baseline > 0.0f && lines[0].baseline <= lines[0].height,
                      "the baseline sits inside the line box -- positive, and no lower "
                      "than its height");
                check(lines[0].height > 0.0f && lines[1].height > 0.0f,
                      "both lines have real height, so the second is a laid-out line and "
                      "not a zeroed struct");
                check(lines[0].trailingWhitespaceLength >= lines[0].newlineLength,
                      "the newline sequence counts as trailing whitespace, as DirectWrite "
                      "defines it, so this can never be the smaller of the two");
            }

            // The astral case, because span 27's closing lesson was that an ASCII
            // corpus makes the offset conversion the numeric identity and leaves it
            // untested while the suite reads green.
            const wchar_t kNonBmp[] = L"a\U0001F600b";
            const uint32_t kNonBmpLen =
                (uint32_t)(sizeof(kNonBmp) / sizeof(wchar_t)) - 1;
            const uint32_t kNonBmpUnits = (sizeof(wchar_t) == 2) ? 2u : 1u;
            check(kNonBmpLen == 1u + kNonBmpUnits + 1u,
                  "CONTROL: the non-BMP literal has the element count this platform's "
                  "wchar_t implies, so the length below is the caller's real one and not "
                  "a coincidence of encoding");

            std::unique_ptr<tinta::TextFormat> aFormat = makeFormat(h);
            std::unique_ptr<tinta::TextLayout> aLayout =
                aFormat ? h.textFactory().CreateTextLayout(kNonBmp, kNonBmpLen,
                                                           aFormat.get(), 1000.0f, 400.0f)
                        : nullptr;
            check(aLayout != nullptr,
                  "CONTROL: a layout over non-BMP text exists, so the length below is a "
                  "converted offset and not an ASCII identity");
            if (aLayout) {
                uint32_t aCount = 0;
                tinta::LineMetrics aLines[4]{};
                const bool aFilled = aLayout->GetLineMetrics(aLines, 4, &aCount);
                check(aFilled && aCount == 1,
                      "CONTROL: the non-BMP string is one line, so its single length is "
                      "the whole string");
                if (aFilled && aCount == 1) {
                    check(aLines[0].length == kNonBmpLen,
                          "a line's length is in the CALLER's wchar_t units -- it equals "
                          "the stringLength handed to CreateTextLayout, which is the only "
                          "assertion here that a backend with 32-bit wchar_t can fail");
                }
            }
        }
    }

    // --- GetClusterMetrics: the two properties its one consumer depends on --------
    //
    // render.cpp builds a cumulative-width prefix table from these -- `cumW[cpos]`
    // keyed by accumulating `length` and `width` -- so what has to hold is
    // relational and not absolute: the lengths must partition the string, and the
    // widths must sum to the advance the layout reports. Anything else would be a
    // statement about Segoe UI versus DejaVu Sans.
    {
        static const wchar_t kSpaced[] = L"ab cd";
        std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
        std::unique_ptr<tinta::TextLayout> layout =
            format ? h.textFactory().CreateTextLayout(kSpaced, 5, format.get(),
                                                      1000.0f, 400.0f)
                   : nullptr;
        check(layout != nullptr, "CONTROL: a layout exists to take cluster metrics from");

        if (layout) {
            uint32_t probeCount = 0;
            const bool probe = layout->GetClusterMetrics(nullptr, 0, &probeCount);
            check(probeCount == 5,
                  "CONTROL: the probe pass counts one cluster per character for plain "
                  "Latin text, so the sums below are over five real clusters");
            check(!probe,
                  "GetClusterMetrics's count-probe pass reports FAILURE while yielding "
                  "the count, matching GetLineMetrics and HitTestTextRange -- the two "
                  "backends reach it by different routes and must still agree");

            tinta::ClusterMetrics clusters[16]{};
            const bool filled = layout->GetClusterMetrics(clusters, 16, &probeCount);
            check(filled && probeCount == 5,
                  "the fill pass succeeds and reports the same count the probe did");

            if (filled && probeCount == 5) {
                uint32_t lengthSum = 0;
                float widthSum = 0.0f;
                for (uint32_t i = 0; i < probeCount; i++) {
                    lengthSum += clusters[i].length;
                    widthSum += clusters[i].width;
                }
                tinta::TextMetrics tm{};
                layout->GetMetrics(&tm);

                check(lengthSum == 5,
                      "the cluster lengths PARTITION the string -- render.cpp's `cpos` "
                      "walks to the end by accumulating them, and a short sum leaves the "
                      "tail of its prefix table filled with a stale width");
                check(tm.widthIncludingTrailingWhitespace > 0.0f,
                      "CONTROL: the layout reports a real advance, so the comparison "
                      "below is against a measured width and not zero");
                check(std::fabs(widthSum - tm.widthIncludingTrailingWhitespace) < 0.75f,
                      "the cluster widths SUM to the layout's advance including trailing "
                      "whitespace, which is the property that makes cumW[b] - cumW[a] a "
                      "substring width");
                for (uint32_t i = 0; i < probeCount; i++)
                    check(clusters[i].width >= 0.0f,
                          "no cluster has negative width, so the prefix table is "
                          "monotonic and widthOf(a,b) is never negative");
                check(clusters[2].isWhitespace == 1,
                      "the cluster over the space is flagged whitespace -- a live read "
                      "THROUGH the Direct2D side's bit-field cast, which is the only "
                      "runtime check that cast has, since offsetof cannot assert a bit "
                      "field");
                check(clusters[0].isWhitespace == 0 && clusters[1].isWhitespace == 0,
                      "CONTROL: the letter clusters are NOT flagged whitespace, so the "
                      "assertion above is reading a real flag and not a struct filled "
                      "with ones");
            }
        }
    }

    std::printf("%s%d render-api assertions run, %d failed (backend=%s)\n",
                failures == 0 ? "all " : "", checks, failures, h.name());
    return failures;
}
