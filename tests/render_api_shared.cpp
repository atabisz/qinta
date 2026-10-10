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

// The two image-seam families only -- the icon decode and the offscreen raster. Guarded
// rather than included unconditionally because a target built without the image backend
// must not merely fail to link the entry points -- it must not name them at all, and each
// #else arm below prints a skip line so an absent family cannot pass for a green one.
//
// <filesystem> and <fstream> come in under the same guard and for one assertion only: the
// offscreen-raster family round-trips its PNG back through tinta::decodeImageFile, which
// takes a PATH, so the bytes have to reach disk. std::filesystem::path is what carries a
// wide path portably here -- the same spelling tests/image_tests.cpp uses, and deliberately
// NOT an fstream constructed from a std::wstring, which compiles under MSVC as an extension
// and does not compile at all under libstdc++.
#ifdef TINTA_HAVE_IMAGE_SEAM
#include "app_icon_data.h"
#include "image_api.h"

#include <filesystem>
#include <fstream>
#include <string>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <vector>

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

// The widest gap between any single pixel's R, G and B. Zero across a whole surface
// means every pixel is a shade of grey, which is exactly what a MONOCHROME glyph
// painted with a black brush produces; a colour-font glyph carries its own palette and
// cannot come out neutral. So this is the one reading that distinguishes "the colour
// font was used" from "something was drawn".
//
// Alpha is deliberately not part of the spread. An antialiased edge varies alpha while
// staying neutral in hue, so including it would make every arm look colourful and the
// discriminator would stop discriminating.
int maxChannelSpread(RenderHarness& h) {
    int worst = 0;
    for (int y = 0; y < kHeight; y++) {
        for (int x = 0; x < kWidth; x++) {
            const Rgba p = h.pixel(x, y);
            const int hi = std::max(p.r, std::max(p.g, p.b));
            const int lo = std::min(p.r, std::min(p.g, p.b));
            worst = std::max(worst, hi - lo);
        }
    }
    return worst;
}

// A w x h block of the surface, read back row-major. Used only by the bitmap-creation
// family, which compares two whole blocks rather than named pixels: the claim there is
// that two INDEPENDENT construction paths produce the same image, and a handful of
// sampled points would let them differ everywhere else.
std::vector<Rgba> readBlock(RenderHarness& h, int left, int top, int w, int hgt) {
    std::vector<Rgba> out(static_cast<size_t>(w) * hgt);
    for (int y = 0; y < hgt; y++)
        for (int x = 0; x < w; x++)
            out[static_cast<size_t>(y) * w + x] = h.pixel(left + x, top + y);
    return out;
}

// Straight Rgba -> the premultiplied BGRA byte order RenderTarget::CreateBitmap takes,
// at a caller-chosen stride. `padding` fills the bytes past each row's end, so a
// backend that ignored `stride` and walked the buffer tightly would read it as image
// data and produce a visibly wrong picture rather than an identical one.
std::vector<uint8_t> premultipliedBgra(const std::vector<Rgba>& pixels, int w, int hgt,
                                      int stride, uint8_t padding) {
    std::vector<uint8_t> out(static_cast<size_t>(stride) * hgt, padding);
    for (int y = 0; y < hgt; y++) {
        uint8_t* row = out.data() + static_cast<size_t>(y) * stride;
        for (int x = 0; x < w; x++) {
            const Rgba p = pixels[static_cast<size_t>(y) * w + x];
            // (v * a + 127) / 255, the usual rounded premultiply. Every colour this
            // family uses is fully opaque, so this is the identity in practice -- it is
            // written correctly anyway rather than as a copy, because the next person
            // to add a translucent case should not have to discover that it was not.
            const int a = p.a;
            row[x * 4 + 0] = static_cast<uint8_t>((p.b * a + 127) / 255);
            row[x * 4 + 1] = static_cast<uint8_t>((p.g * a + 127) / 255);
            row[x * 4 + 2] = static_cast<uint8_t>((p.r * a + 127) / 255);
            row[x * 4 + 3] = static_cast<uint8_t>(a);
        }
    }
    return out;
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

// Every format in this suite was a Normal-weight one until now, and makeFormat is
// why: it hardcodes the weight, so 193 assertions passed FontWeight::Normal and
// nothing ever passed anything else. That was found by mutation -- replacing the
// D2D backend's weight argument with a hardcoded DWRITE_FONT_WEIGHT_NORMAL left
// all 193 green. Note what it does NOT mean: the enum VALUES are asserted from
// both sides at compile time (text_d2d.cpp against DWRITE_, text_qt.cpp against
// QFont::), thoroughly and correctly. The unasserted claim was the other one --
// that CreateTextFormat hands its weight argument to anything at all. A pairing
// assertion and a plumbing assertion are different claims, and a block of six
// static_asserts reads like coverage of the parameter while having no opinion on
// whether the parameter is used.
std::unique_ptr<tinta::TextFormat> makeWeightedFormat(RenderHarness& h,
                                                      tinta::FontWeight weight,
                                                      float size = 16.0f) {
    return h.textFactory().CreateTextFormat(h.fontFamily(), weight,
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

    // --- PATHS: the seam's only CREATION call, and the only one with a sequence --
    //
    // Every other drawing call on this interface is one call with its arguments.
    // BeginPath returns an object that has to be fed in order and then closed, and
    // the two backends implement that sequence completely differently: Direct2D
    // opens a refcounted ID2D1GeometrySink on a device-owned ID2D1PathGeometry and
    // has to Release both; Qt appends to a QPainterPath value. So this block is
    // where the seam is least likely to be a rename and most likely to be a
    // translation -- which is exactly why the assertions are pixels, and shared.
    {
        // A triangle, filled. The bounding-box corner is the discriminator, the same
        // way it is for the ellipse above: a backend that filled the bounds instead of
        // the path would pass every other check here.
        whiteSurface(h);
        {
            tinta::RenderTarget& target = h.begin();
            std::unique_ptr<tinta::PathSink> sink = target.BeginPath();
            check(sink != nullptr, "BeginPath returns a sink");
            if (sink) {
                sink->BeginFigure(tinta::pointF(20, 100));
                sink->AddLine(tinta::pointF(100, 20));
                sink->AddLine(tinta::pointF(180, 100));
                sink->EndFigure();
                std::unique_ptr<tinta::PathGeometry> geometry = sink->Close();
                check(geometry != nullptr, "Close hands back a geometry");
                if (geometry) {
                    std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
                    target.FillGeometry(geometry.get(), brush.get());
                }
            }
            h.end();
        }
        check(h.pixel(100, 80) != kWhite, "the interior of a filled triangle is painted");
        check(h.pixel(100, 80) == kRed, "and it is painted the brush colour");
        check(h.pixel(25, 25) == kWhite,
              "the corner of its bounding box is NOT painted, so a PATH was filled and not a box");
        check(h.pixel(100, 110) == kWhite, "a point below the triangle's base is untouched");
    }
    {
        // AddLines, the array form. MSVC deduces the count from the array at 10 of the
        // 26 sites in the tree, and on the Direct2D side this call reinterprets the run
        // as D2D1_POINT_2F* without copying -- geometry_tests.cpp asserts that stride
        // in isolation, and this asserts that the reinterpretation draws the same
        // picture the one-point-at-a-time form does. A stride error would paint a
        // plausible wrong triangle, not nothing.
        whiteSurface(h);
        {
            tinta::RenderTarget& target = h.begin();
            std::unique_ptr<tinta::PathSink> sink = target.BeginPath();
            if (sink) {
                const tinta::PointF rest[2] = {{100, 20}, {180, 100}};
                sink->BeginFigure(tinta::pointF(20, 100));
                sink->AddLines(rest, 2);
                sink->EndFigure();
                std::unique_ptr<tinta::PathGeometry> geometry = sink->Close();
                if (geometry) {
                    std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
                    target.FillGeometry(geometry.get(), brush.get());
                }
            }
            h.end();
        }
        check(h.pixel(100, 80) == kRed, "AddLines paints the same interior as repeated AddLine");
        check(h.pixel(25, 25) == kWhite, "and the same background outside it");
    }
    {
        // DrawGeometry is a stroke, and EndFigure CLOSES. Both are asserted by the same
        // shape: a V of two AddLine segments, stroked. The closing edge along y == 20 is
        // drawn ONLY because EndFigure closed the figure -- and unlike a fill, where
        // both backends close implicitly, a stroke shows the difference. render_api.h
        // dropped D2D1_FIGURE_END_OPEN from the seam on a 7-of-7 census; this is the
        // assertion that the constant it hardcoded instead is the closed one.
        whiteSurface(h);
        {
            tinta::RenderTarget& target = h.begin();
            std::unique_ptr<tinta::PathSink> sink = target.BeginPath();
            if (sink) {
                sink->BeginFigure(tinta::pointF(20, 20));
                sink->AddLine(tinta::pointF(100, 100));
                sink->AddLine(tinta::pointF(180, 20));
                sink->EndFigure();
                std::unique_ptr<tinta::PathGeometry> geometry = sink->Close();
                if (geometry) {
                    std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
                    target.DrawGeometry(geometry.get(), brush.get(), 3.0f);
                }
            }
            h.end();
        }
        check(h.pixel(60, 60) != kWhite,
              "CONTROL: a stroked path's own edge is painted, so the next checks read something");
        check(h.pixel(100, 60) == kWhite,
              "the V's interior is NOT filled, so DrawGeometry is a stroke");
        check(h.pixel(100, 21) != kWhite,
              "the CLOSING edge is stroked, so EndFigure closed the figure");
    }
    {
        // ADDARC, AND THE SIGN OF ITS SWEEP.
        //
        // This is the one call on the seam whose two backends do not merely spell the
        // same thing differently -- they use different PARAMETERISATIONS. Direct2D takes
        // an endpoint and a large/small flag; Qt takes a bounding box, a start angle and
        // a sweep in degrees measured the other way round. tinta::arcCenterFromEndpoints
        // solves for the centre and QtPathSink flips both signs, and the failure mode of
        // getting either wrong is an arc that bulges the wrong way: a picture, on one
        // platform only, that no exit code reports.
        //
        // Chord from (60,90) to (140,90) with radius 45, closed by EndFigure, filled.
        // The small clockwise arc bulges UP (apex near y == 66) because these coordinates
        // are y-down and increasing angle is clockwise on screen. The large arc is the
        // major arc of the OTHER circle of the same radius through the same two points,
        // whose centre sits above the chord, so it encloses a strictly bigger region.
        const tinta::PointF arcStart = tinta::pointF(60, 90);
        const tinta::PointF arcEnd = tinta::pointF(140, 90);

        auto fillArc = [&](bool largeArc) {
            whiteSurface(h);
            tinta::RenderTarget& target = h.begin();
            std::unique_ptr<tinta::PathSink> sink = target.BeginPath();
            if (sink) {
                sink->BeginFigure(arcStart);
                tinta::ArcSegment arc{};
                arc.endpoint = arcEnd;
                arc.radius = 45.0f;
                arc.largeArc = largeArc;
                sink->AddArc(arc);
                sink->EndFigure();
                std::unique_ptr<tinta::PathGeometry> geometry = sink->Close();
                if (geometry) {
                    std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
                    target.FillGeometry(geometry.get(), brush.get());
                }
            }
            h.end();
        };

        fillArc(false);
        const int smallCount = countNonWhite(h);
        check(h.pixel(100, 80) == kRed, "the small arc's segment is filled above the chord");
        // THE SIGN CHECK. A counter-clockwise sweep would put the segment BELOW the
        // chord instead, and every other assertion in this block would still pass.
        check(h.pixel(100, 100) == kWhite,
              "and NOT below it -- a reversed sweep direction fails exactly here");
        // 34px above the chord is inside the arc; the chord's own midpoint is 24px from
        // the apex, so this also says the arc reaches out and is not a flat chord.
        check(h.pixel(100, 70) == kRed, "the segment extends up to near the arc's apex");
        check(h.pixel(100, 60) == kWhite, "but not past it");
        check(h.pixel(65, 80) == kWhite,
              "CONTROL: a point outside the small arc's circle is background");

        fillArc(true);
        check(h.pixel(100, 80) == kRed,
              "the large arc fills the shared region too, so both variants drew");
        check(h.pixel(65, 80) == kRed,
              "AND the point the small arc left alone -- so largeArc selected the major arc");
        check(countNonWhite(h) > smallCount,
              "the large arc encloses strictly more pixels than the small one");
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

    // --- EnableColorFont: the flag whose "no seam equivalent" was never true ------
    //
    // src/paint_document.cpp carried a native `#ifdef TINTA_D2D_BACKEND` branch calling
    // app.deviceContext->DrawTextLayout with D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT,
    // and FOUR separate comments -- in that file, include/app.h, include/paint_document.h
    // and include/render_d2d.h -- each asserted colour emoji had "no seam equivalent".
    // The seam has carried DrawTextOptions::EnableColorFont since it was built, held
    // value-equal to the Direct2D constant by a static_assert in src/render_d2d.cpp, with
    // a live caller in src/math_render.cpp. The four comments agreed with each other and
    // were all wrong, and the reason the duplicate branch survived is the same reason the
    // Clip block above gives for itself: an unexercised flag is indistinguishable from an
    // unimplemented one. This is the exercise.
    //
    // THE LENGTH IS COMPUTED, NOT LITERAL, AND THAT IS NOT FUSSINESS. U+1F600 is ONE
    // wchar_t where wchar_t is 32-bit and a surrogate PAIR where it is 16-bit, so a
    // hardcoded 2 would read past the terminator off Windows and a hardcoded 1 would cut
    // the pair in half on it. The seam's offset contract is stated in caller wchar_t
    // units precisely so a cross-backend test cannot dodge this.
    //
    // THE BRUSH IS BLACK ON PURPOSE. A monochrome glyph takes the brush colour, so every
    // pixel it paints is neutral; a colour-font glyph carries its own palette and ignores
    // the brush. maxChannelSpread is therefore a reading of which kind of glyph arrived,
    // and it is the only thing that separates "the flag did nothing" from "there was no
    // colour glyph here to act on".
    {
        const wchar_t* const kEmoji = L"\U0001F600";
        const uint32_t kEmojiLength = static_cast<uint32_t>(std::wcslen(kEmoji));
        const tinta::Color black = tinta::colorF(0.0f, 0.0f, 0.0f);
        const tinta::DrawTextOptions order[2] = {tinta::DrawTextOptions::None,
                                                 tinta::DrawTextOptions::EnableColorFont};
        int painted[2] = {-1, -1};
        int spread[2] = {-1, -1};
        std::vector<Rgba> surface[2];

        for (int i = 0; i < 2; i++) {
            std::unique_ptr<tinta::TextFormat> format = makeFormat(h, 48.0f);
            if (!format) continue;
            std::unique_ptr<tinta::TextLayout> layout = h.textFactory().CreateTextLayout(
                kEmoji, kEmojiLength, format.get(), kWidth - 20.0f, kHeight - 20.0f);
            if (!layout) continue;

            whiteSurface(h);
            std::unique_ptr<tinta::Brush> brush = h.makeBrush(black);
            h.begin().DrawTextLayout(tinta::pointF(10, 10), layout.get(), brush.get(),
                                     order[i]);
            h.end();
            painted[i] = countNonWhite(h);
            spread[i] = maxChannelSpread(h);
            surface[i] = readBlock(h, 0, 0, kWidth, kHeight);
        }

        check(painted[0] > 0 && painted[1] > 0,
              "CONTROL: both EnableColorFont arms painted something, so neither reading "
              "below is of an empty surface");

        // Printed whatever the verdict, because a claim about this flag is worth nothing
        // without the numbers it was read from -- and because the two hosts this file runs
        // on do not have the same fonts, so a future reader needs the readings and not
        // just the pass.
        std::printf("       EnableColorFont: painted None=%d Colour=%d, channel spread "
                    "None=%d Colour=%d, surfaces %s (backend=%s)\n",
                    painted[0], painted[1], spread[0], spread[1],
                    (surface[0] == surface[1] ? "IDENTICAL" : "DIFFER"), h.name());

        // 8 rather than 0: an un-premultiplied readback and a subpixel-antialiased edge
        // can each introduce a channel or two of drift on a neutral glyph, and a
        // threshold of 0 would call that colour. Measured, the two backends sit nowhere
        // near it -- a neutral glyph reads 0 and a colour one reads 209-214.
        // NO ASSERTION OVER THE TWO CAPTURES ABOVE CAN FAIL, AND UNTIL 2026-09-19 THIS BLOCK
        // MADE TWO OF THEM. Both sat inside `if (painted[1] > 0 && spread[1] > 8)`:
        //   check(spread[1] > 8, ...)                          -- the branch guard itself.
        //   check(spread[0] > 8 || surface[0] != surface[1])    -- spread is a pure function
        //     of the captured surface (maxChannelSpread scans the region readBlock reads), so
        //     surfaces equal implies spread[0] == spread[1] > 8 and the left disjunct holds;
        //     surfaces unequal and the right one does. True either way.
        // The second one's comment claimed it rejected "colour without the flag, monochrome
        // with it" -- that shape gives spread[0] > 8, so it PASSES. A vacuous assertion is
        // worse than none: it reports a measurement to ctest that was never taken.
        //
        // THE STRUCTURAL REASON, worth keeping because it generalises: both readings derive
        // from one pair of captures, so any predicate over them is a statement about that
        // pair and not about the seam. Falsifiability needs a quantity the captures do not
        // determine. Two exist, and this block now uses both -- an INDEPENDENT layout
        // (below), and the Clip block further up, which passes its own option through the
        // identical `options` parameter of D2DRenderTarget::DrawTextLayout. That is what
        // actually pins "the option reaches the backend": drop the forward and
        // `check(rights[1] <= 71, ...)` fails. This block never guarded that, and its
        // UNDECIDABLE path would have stayed silent while it broke.
        const bool sawColourGlyph = painted[1] > 0 && spread[1] > 8;
        if (!sawColourGlyph) {
            // UNDECIDABLE, NOT NEGATIVE, and printed rather than checked. With no colour
            // glyph on this host there is nothing for the flag to change, so "the two
            // arms match" is true and says nothing whatever about the seam. A check()
            // here would record a pass for a measurement that never happened, which is
            // the failure this file's opening comment exists to reject.
            std::printf("       UNDECIDABLE on this host: the EnableColorFont arm produced "
                        "no saturated pixel (spread %d, needs > 8), so no colour glyph "
                        "reached the surface and the comparison above is not evidence in "
                        "either direction. Install a colour emoji font to decide it.\n",
                        spread[1]);
        } else {
            // REPORTED, NOT ASSERTED, for the reason given above. Measured: direct2d GATES
            // colour on the flag (spread 0 -> 214, surfaces differ), qt paints the colour
            // glyph unconditionally (209 -> 209, surfaces identical) because QTextLayout
            // hands the run to QPainter, which resolves a colour font on its own. Both are
            // correct, which is why an earlier draft asserting the direct2d shape for both
            // failed qt on a difference that was not a defect -- and why the draft after
            // that, reaching for something that would hold on both, reached a tautology.
            // The honest form of a per-host reading is a printed reading.
            std::printf("       EnableColorFont: %s (backend=%s)\n",
                        spread[0] > 8 ? "backend paints colour glyphs unconditionally and "
                                        "ignores the flag"
                                      : "backend gates colour on the flag",
                        h.name());
        }

        // THE FALSIFIABLE HALF, and it guards the change that actually shipped. Since
        // 2026-09-19 src/paint_document.cpp passes EnableColorFont on EVERY text run, not
        // just ones holding emoji, so the flag's effect on ordinary text stopped being
        // hypothetical. An independent layout makes this non-vacuous: nothing above
        // determines these captures, so the comparison can fail.
        //
        // It must be byte-identical. A backend that perturbed plain text when asked for
        // colour fonts -- different hinting, a different rasteriser path, a fallback font --
        // would change every document in the app on the strength of one enum value, and
        // would do it invisibly, since the emoji reading above would still look right.
        {
            const wchar_t* const kPlain = L"Hello";
            const uint32_t kPlainLength = static_cast<uint32_t>(std::wcslen(kPlain));
            const tinta::Color plainBrushColor = tinta::colorF(0.0f, 0.0f, 0.0f);
            const tinta::DrawTextOptions plainOrder[2] = {
                tinta::DrawTextOptions::None, tinta::DrawTextOptions::EnableColorFont};
            std::vector<Rgba> plain[2];
            int plainPainted[2] = {-1, -1};

            for (int i = 0; i < 2; i++) {
                std::unique_ptr<tinta::TextFormat> format = makeFormat(h, 48.0f);
                if (!format) continue;
                std::unique_ptr<tinta::TextLayout> layout = h.textFactory().CreateTextLayout(
                    kPlain, kPlainLength, format.get(), kWidth - 20.0f, kHeight - 20.0f);
                if (!layout) continue;

                whiteSurface(h);
                std::unique_ptr<tinta::Brush> brush = h.makeBrush(plainBrushColor);
                h.begin().DrawTextLayout(tinta::pointF(10, 10), layout.get(), brush.get(),
                                         plainOrder[i]);
                h.end();
                plainPainted[i] = countNonWhite(h);
                plain[i] = readBlock(h, 0, 0, kWidth, kHeight);
            }

            check(plainPainted[0] > 0 && plainPainted[1] > 0,
                  "CONTROL: both plain-text arms painted something, so the comparison below "
                  "is not between two blank surfaces");
            check(plain[0] == plain[1],
                  "EnableColorFont leaves NON-EMOJI text byte-identical -- it is passed on "
                  "every run now, so it must not perturb ordinary glyphs");
            std::printf("       EnableColorFont on plain text: painted None=%d Colour=%d, "
                        "surfaces %s (backend=%s)\n",
                        plainPainted[0], plainPainted[1],
                        (plain[0] == plain[1] ? "IDENTICAL" : "DIFFER"), h.name());
        }
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

    {
        // SCALE ABOUT A CENTRE. The print path's shrink band scales an oversized
        // block about its band's top-left corner, so the corner must be a FIXED
        // POINT -- the block grows or shrinks away from it rather than sliding.
        //
        // Asserted with the rectangle's left edge sitting ON the centre, which is
        // what makes the plain two-argument scale a discriminating control rather
        // than a second way of saying the same thing: at scale 2 about (8,8) the
        // edge stays at 8, and with no centre it lands at 16. A centre that was
        // silently ignored would therefore read as the control's answer, not as a
        // near miss.
        std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
        const float c = 8.0f;

        whiteSurface(h);
        {
            tinta::RenderTarget& rt = h.begin();
            rt.SetTransform(tinta::matrixScale(2.0f, 2.0f, tinta::pointF(c, c)));
            rt.FillRectangle(tinta::rectF(c, c, c + 4.0f, c + 4.0f), brush.get());
            rt.SetTransform(tinta::matrixIdentity());
            h.end();
        }
        const int aboutCentre = leftmostPainted(h);
        const int aboutCentreTop = topmostPainted(h);

        whiteSurface(h);
        {
            tinta::RenderTarget& rt = h.begin();
            rt.SetTransform(tinta::matrixScale(2.0f, 2.0f));
            rt.FillRectangle(tinta::rectF(c, c, c + 4.0f, c + 4.0f), brush.get());
            rt.SetTransform(tinta::matrixIdentity());
            h.end();
        }
        const int aboutOrigin = leftmostPainted(h);

        check(aboutCentre > 0 && aboutOrigin > 0,
              "CONTROL: both scales painted something");
        check(aboutCentre != aboutOrigin,
              "CONTROL: the centre is observable -- ignoring it would move the ink");
        check(aboutCentre == static_cast<int>(c) && aboutCentreTop == static_cast<int>(c),
              "matrixScale about a centre leaves that centre fixed, on both axes");
        check(aboutOrigin == static_cast<int>(c * 2.0f),
              "and the two-argument form scales the offset, which is why the band needs the centre");

        // The arithmetic, so a backend that happened to produce the right pixels
        // through a different matrix cannot pass this block alone.
        const tinta::Matrix3x2F about = tinta::matrixScale(2.0f, 2.0f, tinta::pointF(c, c));
        check(about.dx == -c && about.dy == -c,
              "matrixScale's centred form offsets by center - scale * center");
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

    // --- Bitmap CREATION through the seam ---------------------------------------
    //
    // RenderTarget::CreateBitmap is the second exception to render_api.h's "no
    // creation" rule, and it is checked against the harness's OWN makeBitmap rather
    // than against itself. That is deliberate and it is why makeBitmap was left
    // hand-rolled per backend instead of being reimplemented as a call to the seam:
    // two independent construction paths that must produce the same pixels is a
    // stronger instrument than one path compared with a copy of itself. Under Qt they
    // are QImage(Format_ARGB32) versus QImage(Format_ARGB32_Premultiplied) from a raw
    // buffer; under Direct2D they are two CreateBitmap calls with different
    // premultiply arithmetic in front of them.
    //
    // ALPHA IS OUT OF SCOPE HERE, and stated rather than quietly avoided: the two
    // paths disagree about where premultiplication happens, so a translucent pixel
    // would compare a value that went through premultiply-then-unpremultiply against
    // one that never did, and a rounding difference of one would redden this for a
    // reason that is not the seam's. The colours below are fully opaque.
    {
        const int bw = 10;
        const int bh = 10;
        std::vector<Rgba> source(static_cast<size_t>(bw) * bh);
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++)
                source[static_cast<size_t>(y) * bw + x] = (x < bw / 2) ? kRed : kBlue;

        // Tight stride, and a green pad byte value that cannot occur in the image, so
        // the padded-stride case below has something to be wrong WITH.
        const std::vector<uint8_t> tight = premultipliedBgra(source, bw, bh, bw * 4, 0);
        const int paddedStride = (bw + 3) * 4;
        const std::vector<uint8_t> padded =
            premultipliedBgra(source, bw, bh, paddedStride, 0x7F);

        // Created INSIDE the session it is drawn in, on purpose: a Direct2D bitmap
        // belongs to its render target, and newSurface() is entitled to build a new
        // one. Creating before the surface existed would be testing the harness's
        // teardown order rather than the seam.
        whiteSurface(h);
        std::unique_ptr<tinta::Bitmap> made;
        {
            tinta::RenderTarget& rt = h.begin();
            made = rt.CreateBitmap(static_cast<uint32_t>(bw), static_cast<uint32_t>(bh),
                                   tight.data(), static_cast<uint32_t>(bw * 4));
            if (made) rt.DrawBitmap(made.get(), tinta::rectF(20, 10, 30, 20));
            h.end();
        }
        check(made != nullptr,
              "CONTROL: RenderTarget::CreateBitmap returned a bitmap from a CPU pixel "
              "buffer -- every assertion in this family is void without it");

        if (made) {
            const std::vector<Rgba> viaSeam = readBlock(h, 20, 10, bw, bh);
            check(h.pixel(21, 15) == kRed && h.pixel(28, 15) == kBlue,
                  "a bitmap created through the seam draws its left half red and its "
                  "right half blue, so the pixel buffer is read in BGRA byte order and "
                  "not RGBA");

            // GetSize AT THE DEFAULT DPI. render_api.h documents GetSize as DIPs, and
            // at 96 dpi a DIP is a pixel -- so this is the case where the two
            // coincide, and it is worth its own line because it is the one every
            // caller but the SVG path relies on.
            const tinta::SizeF size = made->GetSize();
            check(size.width == static_cast<float>(bw) && size.height == static_cast<float>(bh),
                  "at the default dpi a created bitmap reports its pixel dimensions");

            // THE LOAD-BEARING ONE. src/render.cpp rasterizes SVGs at 2x and declares
            // 192 dpi so the result reports its INTRINSIC size; passing 96 there would
            // draw every SVG in a document at double size and nothing else would look
            // wrong. A backend that accepted the dpi argument and threw it away would
            // pass every other assertion in this file.
            whiteSurface(h);
            std::unique_ptr<tinta::Bitmap> at192;
            {
                tinta::RenderTarget& rt = h.begin();
                at192 = rt.CreateBitmap(static_cast<uint32_t>(bw), static_cast<uint32_t>(bh),
                                        tight.data(), static_cast<uint32_t>(bw * 4), 192.0f);
                h.end();
            }
            check(at192 != nullptr, "CONTROL: the 192-dpi bitmap was created at all");
            if (at192) {
                const tinta::SizeF dips = at192->GetSize();
                check(dips.width == static_cast<float>(bw) / 2.0f &&
                      dips.height == static_cast<float>(bh) / 2.0f,
                      "a bitmap declared at 192 dpi reports HALF its pixel dimensions, "
                      "because GetSize is in DIPs -- this is what lets render.cpp draw a "
                      "2x SVG raster at its intrinsic size");
            }

            // The independent second construction. Same source pixels, the harness's
            // own path, the same destination rect.
            whiteSurface(h);
            std::unique_ptr<tinta::Bitmap> byHarness = h.makeBitmap(bw, bh, source);
            check(byHarness != nullptr,
                  "CONTROL: the harness built the comparison bitmap by its own route");
            if (byHarness) {
                {
                    tinta::RenderTarget& rt = h.begin();
                    rt.DrawBitmap(byHarness.get(), tinta::rectF(20, 10, 30, 20));
                    h.end();
                }
                const std::vector<Rgba> viaHarness = readBlock(h, 20, 10, bw, bh);
                bool agree = viaSeam.size() == viaHarness.size();
                for (size_t i = 0; agree && i < viaSeam.size(); i++)
                    if (viaSeam[i] != viaHarness[i]) agree = false;
                check(agree,
                      "the seam's bitmap and the harness's independently-built one draw "
                      "the SAME 100 pixels -- two construction paths, one image");
                // Paired with a control that the block is not simply blank, which would
                // make the agreement above vacuous on a backend that drew nothing.
                bool anyInk = false;
                for (const Rgba& p : viaHarness)
                    if (p != kWhite) anyInk = true;
                check(anyInk, "CONTROL: that comparison was between two blocks with ink in "
                              "them, not two empty ones");
            }

            // STRIDE. Passed rather than derived, so a backend that derived it anyway
            // would read the pad bytes as image data. The pad value is mid-grey, which
            // is neither of the source's colours.
            whiteSurface(h);
            {
                tinta::RenderTarget& rt = h.begin();
                std::unique_ptr<tinta::Bitmap> strided =
                    rt.CreateBitmap(static_cast<uint32_t>(bw), static_cast<uint32_t>(bh),
                                    padded.data(), static_cast<uint32_t>(paddedStride));
                if (strided) rt.DrawBitmap(strided.get(), tinta::rectF(20, 10, 30, 20));
                h.end();
            }
            const std::vector<Rgba> viaStride = readBlock(h, 20, 10, bw, bh);
            bool strideAgrees = viaStride.size() == viaSeam.size();
            for (size_t i = 0; strideAgrees && i < viaStride.size(); i++)
                if (viaStride[i] != viaSeam[i]) strideAgrees = false;
            check(strideAgrees,
                  "a padded stride produces the same image as a tight one, so `stride` is "
                  "honoured rather than recomputed as width * 4");
        }

        // REFUSALS. Every call site in the tree branches on the result, so returning
        // null on bad input is part of the contract and not merely tidy. These are
        // meaningful only because the control above created one successfully from the
        // same buffer.
        {
            tinta::RenderTarget& rt = h.begin();
            check(rt.CreateBitmap(0, static_cast<uint32_t>(bh), tight.data(),
                                  static_cast<uint32_t>(bw * 4)) == nullptr,
                  "a zero width returns null rather than a zero-sized bitmap");
            check(rt.CreateBitmap(static_cast<uint32_t>(bw), 0, tight.data(),
                                  static_cast<uint32_t>(bw * 4)) == nullptr,
                  "and so does a zero height");
            check(rt.CreateBitmap(static_cast<uint32_t>(bw), static_cast<uint32_t>(bh),
                                  nullptr, static_cast<uint32_t>(bw * 4)) == nullptr,
                  "a null pixel buffer returns null rather than reading address zero");
            check(rt.CreateBitmap(static_cast<uint32_t>(bw), static_cast<uint32_t>(bh),
                                  tight.data(), 0) == nullptr,
                  "and a zero stride returns null rather than dividing by it");
            h.end();
        }
    }

    // --- The ICON decode: one container, two sizes, both backends ----------------
    //
    // WHY THIS FAMILY IS HERE AND NOT IN tests/image_tests.cpp, which is the image
    // seam's own both-backends target: decodeIconBytesToBitmap needs a live
    // RenderTarget, and image_tests constructs none -- it links the render backend for
    // typeinfo and asserts on pixel buffers. The harnesses here already own a target on
    // both backends, so the assertions cost one source file per target instead of a
    // second render-target bring-up.
    //
    // WHAT IT IS FOR. resources/qinta.ico holds frames at 256/48/32/16 and the start
    // page asks for 64. There IS no 64 frame, so at that size the answer depends
    // entirely on which frame is chosen and how it is scaled -- WIC's ICO decoder and
    // Qt's ICO handler choose differently, and two libraries left to their defaults
    // would render a visibly different hero icon per platform with nothing in the build
    // to say so. image_api.h states the rule (smallest frame >= requested, else the
    // largest, then scale to exactly the requested size) and both backends implement it.
    //
    // 32 AND 64 BOTH, because they exercise different branches of that rule: 32 is an
    // exact frame match and 64 is not. A test at 32 alone would be green while the
    // interesting size diverged, which is also why NEITHER backend skips the scaler when
    // the frame already matches -- see image_win32.cpp and image_qt.cpp.
    //
    // WHAT IT DELIBERATELY DOES NOT ASSERT: that the two backends produce the SAME
    // PIXELS at 64. They resample from the 256 frame with different filters (Fant versus
    // Qt's SmoothTransformation), so identical output is not claimed and a comparison
    // here would be a false one. The contract asserted is the geometry and the refusals;
    // the resampling difference is published rather than tested away.
    //
    // AND THE SECOND THING IT DOES NOT ASSERT IS THE ONE I PLANNED IT TO. The intent was
    // that the 64 px case would FAIL if a backend were left on its library's default
    // frame. It does not, and the arithmetic is the reason rather than the writing:
    // qinta.ico's frames are 256/48/32/16 and its FIRST entry is the 256, so at a request
    // of 64 the stated policy -- smallest frame at or above 64 -- SELECTS 256, which is
    // the same frame GetFrame(0) and QImageReader's default hand back. Policy and default
    // COINCIDE at the size that was supposed to separate them. At 32 they differ in which
    // frame is read but not in the output size, and the two backends resample with
    // different filters, so no pixel comparison here can tell a policy read from a default
    // read either.
    //
    // Stated rather than quietly downgraded: the frame CHOICE is covered by reading the
    // two implementations (C3 and C4), not by this arm. What this arm covers is geometry,
    // ink, the fallback clause, and the refusals. A control that cannot fire is not a
    // control, and calling this one satisfied because the assertions are green would be
    // exactly the reading it was written to prevent.
#ifdef TINTA_HAVE_IMAGE_SEAM
    {
        whiteSurface(h);
        std::shared_ptr<tinta::Bitmap> at32;
        std::shared_ptr<tinta::Bitmap> at64;
        {
            tinta::RenderTarget& rt = h.begin();
            at32 = tinta::decodeIconBytesToBitmap(rt, tinta::kAppIconIco,
                                                  tinta::kAppIconIcoSize, 32);
            at64 = tinta::decodeIconBytesToBitmap(rt, tinta::kAppIconIco,
                                                  tinta::kAppIconIcoSize, 64);
            if (at64) rt.DrawBitmap(at64.get(), tinta::rectF(20, 10, 84, 74));
            h.end();
        }

        check(at32 != nullptr,
              "CONTROL: the embedded app icon decoded at 32 at all -- every assertion in "
              "this family is void without it");
        check(at64 != nullptr,
              "CONTROL: and at 64, the size the .ico has no frame for");

        if (at32) {
            const tinta::SizeF s = at32->GetSize();
            check(s.width == 32.0f && s.height == 32.0f,
                  "the icon decoded at 32 reports 32x32 -- the exact-frame case");
        }
        if (at64) {
            const tinta::SizeF s = at64->GetSize();
            check(s.width == 64.0f && s.height == 64.0f,
                  "the icon decoded at 64 reports 64x64 even though no 64 frame exists, so "
                  "the frame was scaled to the request rather than returned as found");

            // NOT a dimensions-only pass. A backend that returned a correctly-sized but
            // EMPTY bitmap would satisfy every check above; this is what makes the size
            // assertions mean an image was decoded.
            const std::vector<Rgba> block = readBlock(h, 20, 10, 64, 64);
            bool anyInk = false;
            for (const Rgba& p : block)
                if (p != kWhite) anyInk = true;
            check(anyInk,
                  "the 64px icon actually draws ink over white, so the frame carried "
                  "pixels rather than being a correctly-sized empty bitmap");
        }

        // THE FALLBACK BRANCH OF THE STATED POLICY, which nothing else reaches. The rule
        // has three clauses -- smallest frame at or above the request, ELSE the largest
        // available, then scale to exactly the request -- and the app only ever asks for
        // 32 and 64, both of which take the first clause. So `else the largest` is dead
        // code in both backends as far as the product is concerned, and dead code that
        // has never run is not implemented, it is written. 512 is above every frame in
        // qinta.ico (the largest is 256), so this is the only assertion that executes it.
        //
        // It is also the only UPSCALE in this family, which is worth one line of its own:
        // a backend that refused to enlarge -- or that clamped to the source size and
        // returned 256 -- would satisfy every other check here.
        {
            std::shared_ptr<tinta::Bitmap> at512;
            {
                tinta::RenderTarget& rt = h.begin();
                at512 = tinta::decodeIconBytesToBitmap(rt, tinta::kAppIconIco,
                                                       tinta::kAppIconIcoSize, 512);
                h.end();
            }
            check(at512 != nullptr,
                  "a request above every frame still decodes, by falling back to the "
                  "largest frame rather than failing");
            if (at512) {
                const tinta::SizeF s = at512->GetSize();
                check(s.width == 512.0f && s.height == 512.0f,
                      "and it is scaled UP to the full 512 requested, not clamped to the "
                      "256 frame it came from");
            }
        }

        // REFUSALS. Every caller branches on null -- a failed decode has to render no
        // icon, not a garbage one. Meaningful only because the controls above succeeded
        // from the same array.
        {
            tinta::RenderTarget& rt = h.begin();
            check(tinta::decodeIconBytesToBitmap(rt, nullptr, tinta::kAppIconIcoSize,
                                                 32) == nullptr,
                  "a null byte pointer returns null rather than reading address zero");
            check(tinta::decodeIconBytesToBitmap(rt, tinta::kAppIconIco, 0, 32) == nullptr,
                  "a zero size returns null rather than decoding an empty stream");
            check(tinta::decodeIconBytesToBitmap(rt, tinta::kAppIconIco,
                                                 tinta::kAppIconIcoSize, 0) == nullptr,
                  "a zero pixel size returns null rather than a zero-sized bitmap");
            // TRUNCATED, not malformed: the first 8 bytes are a real ICONDIR header
            // claiming frames that are not there. A decoder that trusted the header would
            // read past the buffer, and one that returned a bitmap anyway would be
            // handing a caller pixels it never had.
            check(tinta::decodeIconBytesToBitmap(rt, tinta::kAppIconIco, 8, 32) == nullptr,
                  "a truncated container returns null rather than a garbage bitmap");
            h.end();
        }
    }
#else
    // A SKIPPED ARM SAYS SO. An arm that was never compiled and one that stopped
    // working read identically in a summary line, and this target is reachable without
    // the image backend (off Windows it needs Qt Svg and Network on top of Core and
    // Gui, which is a narrower guard than this target's own). Printing the skip is what
    // keeps a silently absent icon family from reading as a green one.
    std::printf("SKIPPED the icon-decode family: this target was built without the image "
                "backend (TINTA_HAVE_IMAGE_SEAM undefined)\n");
#endif

    // --- OFFSCREEN RASTERIZATION: drawing into pixels, and PNG back out ----------
    //
    // tinta::rasterizeToImage and tinta::encodePng, the two entry points that make
    // src/export_docx.cpp's three raster sites portable. Asserted HERE, in the shared
    // suite, rather than in tests/image_tests.cpp, for the reason that file states about
    // itself: rasterizeToImage takes a live RenderTarget and image_tests has none. This is
    // the only place in the tree where the same offscreen-raster assertions run against
    // Direct2D and Qt.
    //
    // THE CLEAR COLOUR IS ASSERTED AS THE CALLER'S OPAQUE RED, AND THAT IS THE WHOLE
    // POINT OF THE FAMILY. image_api.h's contract for this function first said the target
    // was cleared to FULLY TRANSPARENT, which is a reasonable sentence about a document
    // format and a false one about this program -- all three call sites in export_docx.cpp
    // clear to app.theme.background, and under a dark theme a transparent raster
    // composited onto a white page renders light text on white. A test that expected
    // transparency would have passed against exactly that defect. So the alpha assertion
    // below is not tidiness: `a == 255` is the discriminator, and it fails on any
    // implementation that ignores clearColor and clears to nothing.
    //
    // 40x24 rather than a square, because a backend that transposed width and height
    // would satisfy every pixels.size() check.
#ifdef TINTA_HAVE_IMAGE_SEAM
    {
        constexpr unsigned kRasterW = 40;
        constexpr unsigned kRasterH = 24;
        // Straight-alpha inputs; the seam hands back PREMULTIPLIED BGRA, and both of these
        // are fully opaque, so premultiplied and straight are the same bytes. That is why
        // this family can assert exact values where image_tests needs near1: there is
        // nothing to round at a == 255, and any rounding rule agrees.
        const tinta::Color kClearRed = tinta::colorF(1.0f, 0.0f, 0.0f, 1.0f);
        const tinta::Color kInkBlue = tinta::colorF(0.0f, 0.0f, 1.0f, 1.0f);

        int drawCalls = 0;
        bool brushWasNull = false;
        tinta::DecodedImage raster;
        {
            tinta::RenderTarget& rt = h.begin();
            raster = tinta::rasterizeToImage(
                rt, kRasterW, kRasterH, kClearRed,
                [&](tinta::RenderTarget& target, tinta::Brush* brush) {
                    drawCalls++;
                    if (!brush) {
                        brushWasNull = true;
                        return;
                    }
                    brush->SetColor(kInkBlue);
                    // rectF is LEFT/TOP/RIGHT/BOTTOM. This rect is chosen so that the
                    // two readings disagree: read as l/t/r/b it spans x 20..30, read as
                    // x/y/width/height it would span x 20..50. The assertion at x=35
                    // below is red under the correct reading and blue under the wrong
                    // one, which is the one conversion capable of corrupting every
                    // drawing call in the tree while still producing a plausible image.
                    target.FillRectangle(tinta::rectF(20.0f, 2.0f, 30.0f, 6.0f), brush);
                });
            h.end();
        }

        check(raster.ok,
              "CONTROL: rasterizeToImage produced an image at all -- every assertion in "
              "the offscreen-raster family is void without it");
        check(drawCalls == 1,
              "CONTROL: the draw callback ran EXACTLY once, so the pixels below came from "
              "it and not from a backend that cleared and returned");
        check(!brushWasNull,
              "CONTROL: the callback received a non-null scratch brush, which is the only "
              "brush a caller has inside this call -- a Direct2D brush belongs to its "
              "target and the caller's cannot be used here");

        check(raster.width == kRasterW && raster.height == kRasterH,
              "the raster is exactly the requested 40x24, not transposed and not rounded "
              "to the backend's preferred size");
        check(raster.pixels.size() ==
                  static_cast<size_t>(kRasterW) * kRasterH * 4,
              "pixels are tightly packed: size is exactly w * h * 4, so the stride is "
              "width*4 with no row padding, as DecodedImage means everywhere else");

        if (raster.ok && raster.pixels.size() ==
                             static_cast<size_t>(kRasterW) * kRasterH * 4) {
            // BGRA, top-down, stride width*4. A local rather than a helper because the
            // one other block that reads pixels this way (image_tests) is a different
            // binary, and a two-line index is clearer here than a shared abstraction.
            const auto at = [&](unsigned x, unsigned y) {
                const uint8_t* p =
                    raster.pixels.data() + (static_cast<size_t>(y) * kRasterW + x) * 4;
                return Rgba{p[2], p[1], p[0], p[3]};
            };

            // THE CLEAR. (0,0) is outside the filled rect on every reading of it.
            const Rgba corner = at(0, 0);
            check(corner.r == 255 && corner.g == 0 && corner.b == 0,
                  "the untouched corner is the caller's RED, so clearColor reached the "
                  "clear and the channels are in BGRA order rather than RGBA");
            check(corner.a == 255,
                  "and it is OPAQUE -- this is the assertion a 'cleared to transparent' "
                  "contract would have failed, and the defect it exists to catch");

            // THE DRAW. Inside the rect on the correct reading of RectF.
            const Rgba ink = at(25, 4);
            check(ink.b == 255 && ink.r == 0 && ink.g == 0 && ink.a == 255,
                  "the callback's fill landed: a pixel inside the rect is the BLUE the "
                  "scratch brush was set to, so SetColor on the supplied brush works and "
                  "the drawing reached the offscreen surface");

            // THE DISCRIMINATOR. Outside on the correct reading, inside on the wrong one.
            const Rgba beyond = at(35, 4);
            check(beyond.r == 255 && beyond.b == 0,
                  "a pixel at x=35 is still red, so rectF's right edge was read as an "
                  "EDGE and not as a width -- the l/t/r/b versus x/y/w/h confusion");

            // Below the rect: the clear survives where nothing drew, which is what makes
            // the ink assertion mean "drew there" rather than "filled everything".
            const Rgba below = at(25, 20);
            check(below.r == 255 && below.b == 0,
                  "a pixel below the rect is still red, so the fill was bounded rather "
                  "than covering the surface");
        }

        // REFUSALS. image_api.h promises `ok == false` AND AN EMPTY VECTOR for every
        // failure, never a partial buffer, so both halves are checked: a caller that
        // tests only `ok` must not be able to reach half-drawn pixels.
        {
            const auto noop = [](tinta::RenderTarget&, tinta::Brush*) {};
            tinta::RenderTarget& rt = h.begin();
            const tinta::DecodedImage zeroW =
                tinta::rasterizeToImage(rt, 0, kRasterH, kClearRed, noop);
            const tinta::DecodedImage zeroH =
                tinta::rasterizeToImage(rt, kRasterW, 0, kClearRed, noop);
            // The BOUND ITSELF, not a value comfortably past it. kMaxImageDimension is an
            // EXCLUSIVE upper bound -- image_api.h says a dimension EQUAL to it is
            // rejected -- so this is the assertion a `>` that should have been `>=` fails.
            const tinta::DecodedImage atBound = tinta::rasterizeToImage(
                rt, tinta::kMaxImageDimension, kRasterH, kClearRed, noop);
            const tinta::DecodedImage noDraw = tinta::rasterizeToImage(
                rt, kRasterW, kRasterH, kClearRed,
                std::function<void(tinta::RenderTarget&, tinta::Brush*)>());
            h.end();

            check(!zeroW.ok && zeroW.pixels.empty(),
                  "a zero width refuses with an empty buffer rather than a 0x24 image");
            check(!zeroH.ok && zeroH.pixels.empty(), "a zero height refuses likewise");
            check(!atBound.ok && atBound.pixels.empty(),
                  "a dimension EQUAL to kMaxImageDimension refuses, which is the "
                  "exclusive bound the header states rather than a friendlier <=");
            check(!noDraw.ok && noDraw.pixels.empty(),
                  "an empty draw callback refuses rather than calling through a null "
                  "std::function");
        }

        // --- THE LINK PEEK'S WHOLE PATH: a 2x transform out, a 192-dpi handle back ---
        //
        // `renderPeekBitmap` (src/print.cpp) draws the hover panel at 2x and hands back a
        // `shared_ptr<Bitmap>` that reports HALF its pixel size in DIPs. It used to do
        // that with a native `D2D1_BITMAP_OPTIONS_TARGET` bitmap created at 192 dpi, so
        // the 2x lived in the bitmap's own DPI. Through this seam it cannot: the WIC
        // target `src/image_win32.cpp:628-631` builds is HARDCODED at 96 dpi, so the
        // scale has to be a transform inside the callback and the 192 has to go on
        // `CreateBitmap` instead. Those are two different mechanisms reaching the same
        // result, and getting either wrong is silent -- the peek renders at half size in
        // the top-left corner of an otherwise blank panel, or at the right size and twice
        // the intended DIP extent. Neither throws, neither logs.
        //
        // NOTHING ELSE IN THE TREE ASSERTS THIS. `CreateBitmap` at 192 dpi is checked
        // above (line ~1921) over a hand-built buffer, and `SetTransform` is checked
        // against the harness's own surface -- but the surface is not the offscreen WIC
        // target, and a transform reaching one does not prove it reaches the other. The
        // composition is what the port rests on, so the composition is what is asserted.
        //
        // And `renderPeekBitmap` itself is not reached from any test. THE REASON GIVEN HERE
        // WAS WRONG UNTIL 2026-09-20: it read "`src/print.cpp` cannot be reached from any
        // test: it appears exactly once in CMakeLists.txt, in the application target, so no
        // test binary compiles `renderPeekBitmap` itself." SIX targets compile that file --
        // it is in `set(SOURCES)`, re-used as `INPUT_TEST_SOURCES` minus `src/main_d2d.cpp`
        // alone, so `input_tests`, `file_path_tests`, `code_block_layout_tests`,
        // `blockquote_layout_tests` and `theme_tests` all link it alongside the app. What is
        // true is that none of them CALLS it, and that THIS file's two targets
        // (`render_api_tests`, `render_d2d_tests`) are not among the six, so the assertion
        // below genuinely cannot reach the real body. Compiled is not covered. This is still
        // the closest arm that exists -- the same two seam calls in the same order with the
        // same arguments -- and the gap is stated rather than implied.
        {
            constexpr uint32_t kPeekW = 64, kPeekH = 64;
            constexpr float kDip = 10.0f;  // a DIP rect 0..10 must cover px 0..20 at 2x

            tinta::RenderTarget& rt = h.begin();
            const tinta::DecodedImage scaled = tinta::rasterizeToImage(
                rt, kPeekW, kPeekH, tinta::colorF(1.0f, 1.0f, 1.0f, 1.0f),
                [&](tinta::RenderTarget& target, tinta::Brush* brush) {
                    if (!brush) return;
                    target.SetTransform(tinta::matrixScale(2.0f, 2.0f));
                    brush->SetColor(tinta::colorF(0.0f, 0.0f, 0.0f, 1.0f));
                    target.FillRectangle(tinta::rectF(0.0f, 0.0f, kDip, kDip), brush);
                    target.SetTransform(tinta::matrixIdentity());
                });
            // THE CONTROL IS THE SAME DRAW WITHOUT THE TRANSFORM, and it is what makes
            // the three pixel reads above mean anything: if the offscreen target ignored
            // transforms entirely, or if some later change made the seam apply its own
            // 2x, this arm and the one above would read IDENTICALLY and the assertions
            // would be tautologies over one surface
            // ([[an-assertion-over-one-capture-cannot-fail]]).
            const tinta::DecodedImage plain = tinta::rasterizeToImage(
                rt, kPeekW, kPeekH, tinta::colorF(1.0f, 1.0f, 1.0f, 1.0f),
                [&](tinta::RenderTarget& target, tinta::Brush* brush) {
                    if (!brush) return;
                    brush->SetColor(tinta::colorF(0.0f, 0.0f, 0.0f, 1.0f));
                    target.FillRectangle(tinta::rectF(0.0f, 0.0f, kDip, kDip), brush);
                });
            // The handle the peek actually returns, built from the raster's own bytes at
            // 192 dpi -- the second half of the body, and the reason the caller can lay
            // the panel out in DIPs without knowing a raster happened.
            std::unique_ptr<tinta::Bitmap> handle;
            if (scaled.ok) {
                handle = rt.CreateBitmap(kPeekW, kPeekH, scaled.pixels.data(), kPeekW * 4,
                                         192.0f);
            }
            h.end();

            auto inkAt = [&](const tinta::DecodedImage& img, uint32_t x, uint32_t y) -> int {
                const size_t i = (static_cast<size_t>(y) * kPeekW + x) * 4;
                return i + 3 < img.pixels.size() ? img.pixels[i] : -1;
            };

            check(scaled.ok && plain.ok,
                  "CONTROL: both the scaled and unscaled rasters succeeded, so the pixel "
                  "comparisons below are between two real buffers");
            check(inkAt(scaled, 5, 5) == 0,
                  "under a 2x transform the offscreen raster is inked at device pixel "
                  "(5,5), inside the rect on either reading");
            check(inkAt(scaled, 15, 15) == 0,
                  "and inked at (15,15), which a DIP rect of 0..10 only covers if the 2x "
                  "transform reached the offscreen target -- this is the pixel that goes "
                  "light when the link peek renders at half size");
            check(inkAt(scaled, 25, 25) != 0,
                  "and NOT inked at (25,25), so the transform scaled the rect rather than "
                  "the whole buffer coming back dark");
            check(inkAt(plain, 15, 15) != 0 && inkAt(plain, 5, 5) == 0,
                  "CONTROL: the same rect WITHOUT the transform is inked at (5,5) and "
                  "light at (15,15) -- so the transform is what moved the boundary, and "
                  "the assertion above is not reading a constant");
            check(handle != nullptr,
                  "the raster's own bytes become a Bitmap handle, which is what "
                  "renderPeekBitmap returns to its caller");
            if (handle) {
                const tinta::SizeF dips = handle->GetSize();
                check(dips.width == static_cast<float>(kPeekW) / 2.0f &&
                          dips.height == static_cast<float>(kPeekH) / 2.0f,
                      "and at 192 dpi it reports HALF its pixel size in DIPs, so the "
                      "panel that lays the peek out in DIPs gets the extent it had "
                      "before the body stopped using a native 192-dpi target");
            }
        }

        // --- encodePng, and the round trip that makes a length check worthless -----
        //
        // A byte count would pass on any blob. So: the PNG SIGNATURE, then the bytes go to
        // disk and come back through tinta::decodeImageFile -- this tree's OWN decoder, on
        // whichever backend is running -- and the dimensions and the colours have to
        // survive. That is the assertion that distinguishes "encoded a PNG of this image"
        // from "produced something PNG-shaped".
        if (raster.ok) {
            const std::string png = tinta::encodePng(raster);
            check(!png.empty(),
                  "CONTROL: encodePng returned bytes for a good image -- the round trip "
                  "below is void without it");
            check(png.size() >= 8 && static_cast<unsigned char>(png[0]) == 0x89 &&
                      png[1] == 'P' && png[2] == 'N' && png[3] == 'G' && png[4] == '\r' &&
                      png[5] == '\n' && static_cast<unsigned char>(png[6]) == 0x1A &&
                      png[7] == '\n',
                  "the encoded bytes open with the eight-byte PNG signature, so the "
                  "container is PNG and not the backend's default format");

            if (!png.empty()) {
                const std::filesystem::path out =
                    std::filesystem::temp_directory_path() /
                    "tinta_render_api_raster_roundtrip.png";
                {
                    std::ofstream file(out, std::ios::binary);
                    file.write(png.data(), static_cast<std::streamsize>(png.size()));
                }
                check(std::filesystem::exists(out),
                      "CONTROL: the encoded PNG reached disk, so a failed decode below "
                      "means a bad encode and not a missing file");

                const tinta::DecodedImage back = tinta::decodeImageFile(out.wstring());
                check(back.ok,
                      "the encoded PNG decodes again through this tree's own decoder, "
                      "which a length check on the bytes could never establish");
                check(back.width == kRasterW && back.height == kRasterH,
                      "and it decodes back to the 40x24 that was requested, so the "
                      "dimensions survived the encode rather than being inferred");
                if (back.ok && back.pixels.size() ==
                                   static_cast<size_t>(kRasterW) * kRasterH * 4) {
                    const auto at = [&](unsigned x, unsigned y) {
                        const uint8_t* p =
                            back.pixels.data() + (static_cast<size_t>(y) * kRasterW + x) * 4;
                        return Rgba{p[2], p[1], p[0], p[3]};
                    };
                    const Rgba corner = at(0, 0);
                    const Rgba ink = at(25, 4);
                    // TOP-DOWN, and this pair is what pins it. The cleared corner and the
                    // drawn rect are at different rows, so a backend that wrote the rows
                    // bottom-up would put blue at neither of these points and red at both.
                    check(corner.r == 255 && corner.b == 0 && corner.a == 255,
                          "the decoded corner is still opaque red, so the clear colour "
                          "survived encode and decode");
                    check(ink.b == 255 && ink.r == 0,
                          "the decoded rect is still blue AT THE SAME ROW, so the rows "
                          "went out top-down rather than flipped");
                }
                std::error_code ignored;
                std::filesystem::remove(out, ignored);
            }

            // encodePng's REFUSALS. The short-buffer one is the interesting member: the
            // header says the size is CHECKED rather than trusted because the buffer
            // arrives from a caller, and both backends would read past a short one.
            check(tinta::encodePng(tinta::DecodedImage()).empty(),
                  "a DecodedImage with ok == false encodes to nothing rather than to a "
                  "PNG of unspecified fields");
            {
                tinta::DecodedImage lying = raster;
                lying.pixels.pop_back();
                check(tinta::encodePng(lying).empty(),
                      "a pixel buffer one byte short of w*h*4 is REFUSED rather than read "
                      "past, which is the check the header promises is not a trust");
                tinta::DecodedImage zero = raster;
                zero.width = 0;
                check(tinta::encodePng(zero).empty(),
                      "a zero dimension encodes to nothing rather than to an empty PNG");
            }
        }
    }
#else
    // A SKIPPED ARM SAYS SO -- the same reasoning as the icon family above, and the same
    // guard, because rasterizeToImage lives in the very same translation units.
    std::printf("SKIPPED the offscreen-raster family: this target was built without the "
                "image backend (TINTA_HAVE_IMAGE_SEAM undefined)\n");
#endif

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

    // --- WEIGHT IS ACTUALLY PLUMBED --------------------------------------------
    //
    // Relational and not absolute, for the reason every measurement family here is:
    // the harness picks the font family, so no width has a value this file can name.
    // Bold rather than SemiBold on purpose -- a family with no semibold face gets
    // whatever the backend falls back to, which may be the Normal face at an
    // identical width, and an assertion that reads equal-and-passes is unfalsifiable.
    // Synthetic embolden is universal, so "bold is wider" holds without knowing
    // which family the harness chose.
    {
        std::unique_ptr<tinta::TextFormat> normalWeight =
            makeWeightedFormat(h, tinta::FontWeight::Normal);
        std::unique_ptr<tinta::TextFormat> boldWeight =
            makeWeightedFormat(h, tinta::FontWeight::Bold);
        check(normalWeight != nullptr && boldWeight != nullptr,
              "the factory creates a format at each of two weights");
        if (normalWeight && boldWeight) {
            std::unique_ptr<tinta::TextLayout> normalLayout =
                h.textFactory().CreateTextLayout(kLongText, kLongLength,
                                                 normalWeight.get(), 10000.0f, 1000.0f);
            std::unique_ptr<tinta::TextLayout> boldLayout =
                h.textFactory().CreateTextLayout(kLongText, kLongLength,
                                                 boldWeight.get(), 10000.0f, 1000.0f);
            check(normalLayout != nullptr && boldLayout != nullptr,
                  "both weighted formats produce a layout");
            if (normalLayout && boldLayout) {
                tinta::TextMetrics normalMetrics{};
                tinta::TextMetrics boldMetrics{};
                normalLayout->GetMetrics(&normalMetrics);
                boldLayout->GetMetrics(&boldMetrics);
                check(normalMetrics.width > 0.0f,
                      "CONTROL: the Normal-weight layout has a nonzero width, so the "
                      "comparison below is between two real measurements and not two "
                      "empty surfaces");
                check(boldMetrics.width > normalMetrics.width,
                      "a Bold format measures WIDER than a Normal one at the same "
                      "family and size -- the only assertion in this file that fails "
                      "if a backend accepts the weight argument and ignores it, which "
                      "the Direct2D backend did silently under mutation while all of "
                      "the enum-pairing static_asserts stayed green");
                check(boldMetrics.height == normalMetrics.height,
                      "CONTROL: emboldening changes the WIDTH and not the line height, "
                      "so the assertion above is reading the weight and not some "
                      "wholesale difference between two unrelated layouts");
            }
        }
    }

    // --- RANGED STYLING: are the RANGE'S BOUNDS read, not just the value? --------
    //
    // Three methods, and the assertions are built around the one failure mode a
    // value-only test cannot see: a backend that applies the style to the WHOLE
    // layout and ignores the range would pass "styled is wider than unstyled" on
    // every single call. So each of the three compares a SUB-RANGE against a
    // WHOLE-RANGE application of the same value, and it is that inequality --
    // sub-range strictly between unstyled and whole-range -- that makes the range
    // argument falsifiable.
    //
    // Everything is relational for the reason the weight family above gives: the
    // harness picks the font, so no width here has a value this file may name. The
    // one exception is the size RATIO, and it is named because it catches a real
    // and otherwise-invisible unit bug -- see below.
    //
    // A local helper rather than makeFormat, because these need a fresh LAYOUT per
    // arm: the whole point is that styling mutates the layout, so two arms cannot
    // share one.
    {
        auto freshLayout = [&h]() -> std::unique_ptr<tinta::TextLayout> {
            std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
            if (!format) return nullptr;
            // A box far wider and taller than the text so nothing wraps: every width
            // difference below is then the styling's doing and not the box's.
            return h.textFactory().CreateTextLayout(kLongText, kLongLength, format.get(),
                                                    10000.0f, 1000.0f);
        };
        const tinta::TextRange kWhole{0, kLongLength};
        const tinta::TextRange kFirstHalf{0, kLongLength / 2};

        auto widthOf = [](tinta::TextLayout* layout) -> float {
            if (!layout) return 0.0f;
            tinta::TextMetrics m{};
            layout->GetMetrics(&m);
            return m.width;
        };
        auto heightOf = [](tinta::TextLayout* layout) -> float {
            if (!layout) return 0.0f;
            tinta::TextMetrics m{};
            layout->GetMetrics(&m);
            return m.height;
        };

        // --- SetFontWeight ---------------------------------------------------
        {
            std::unique_ptr<tinta::TextLayout> plain = freshLayout();
            std::unique_ptr<tinta::TextLayout> whole = freshLayout();
            std::unique_ptr<tinta::TextLayout> half = freshLayout();
            check(plain && whole && half,
                  "CONTROL: three identical layouts exist, so the only difference "
                  "between the arms below is the ranged styling applied to them");
            if (plain && whole && half) {
                // Bold and not SemiBold, for the reason the weight family above
                // states: a family with no semibold face falls back to something, and
                // an assertion that reads equal-and-passes is unfalsifiable.
                whole->SetFontWeight(tinta::FontWeight::Bold, kWhole);
                half->SetFontWeight(tinta::FontWeight::Bold, kFirstHalf);

                const float wPlain = widthOf(plain.get());
                const float wWhole = widthOf(whole.get());
                const float wHalf = widthOf(half.get());

                check(wPlain > 0.0f,
                      "CONTROL: the unstyled layout has a real width, so the three "
                      "comparisons below are between measurements and not zeros");
                check(wWhole > wPlain,
                      "SetFontWeight over the whole range measures WIDER -- the ranged "
                      "setter is plumbed to the layout at all");
                check(wHalf > wPlain,
                      "and over half the range it still measures wider than unstyled, so "
                      "a sub-range application is not silently dropped");
                check(wHalf < wWhole,
                      "THE RANGE'S BOUNDS ARE READ: emboldening half the text is "
                      "NARROWER than emboldening all of it. This is the only assertion "
                      "in the family that fails for a backend which honours the value "
                      "and applies it to the whole layout regardless of the range -- "
                      "every other comparison here passes for such a backend");
                check(heightOf(whole.get()) == heightOf(plain.get()),
                      "CONTROL: emboldening changes width and not line height, so the "
                      "widths above are reading the weight rather than two unrelated "
                      "layouts");
            }
        }

        // --- SetFontSize -----------------------------------------------------
        {
            std::unique_ptr<tinta::TextLayout> plain = freshLayout();
            std::unique_ptr<tinta::TextLayout> whole = freshLayout();
            std::unique_ptr<tinta::TextLayout> half = freshLayout();
            check(plain && whole && half,
                  "CONTROL: three more identical layouts for the size arm");
            if (plain && whole && half) {
                // Exactly double makeFormat's 16 DIP, which is what licenses the ratio
                // assertion below.
                whole->SetFontSize(32.0f, kWhole);
                half->SetFontSize(32.0f, kFirstHalf);

                const float wPlain = widthOf(plain.get());
                const float wWhole = widthOf(whole.get());
                const float wHalf = widthOf(half.get());

                check(wPlain > 0.0f,
                      "CONTROL: the unstyled layout at 16 DIP has a real width");
                check(wWhole > wPlain, "SetFontSize over the whole range measures wider");
                check(wHalf > wPlain && wHalf < wWhole,
                      "THE RANGE'S BOUNDS ARE READ for size too: enlarging half the text "
                      "lands strictly between unstyled and enlarging all of it");
                check(heightOf(whole.get()) > heightOf(plain.get()),
                      "a larger font size costs real vertical space, so this is a font "
                      "change and not a horizontal-only artefact");

                // THE ONE NAMED NUMBER IN THIS FAMILY, AND WHAT IT CATCHES.
                //
                // The seam's size unit is DIPs and QFont's is points, so the Qt
                // backend owes dipToPoints on this path exactly as it does on the base
                // font. Forgetting it is invisible to every assertion above: 32 points
                // where 24 were meant is still bigger than 16 DIP, still wider than
                // half-styled, still taller. It shows up only in the RATIO -- advance
                // width scales linearly with size for a given face, so doubling the
                // size doubles the width, while a missed unit conversion gives
                // 32/12 = 2.67x. The window excludes that with room to spare on both
                // sides for hinting and rounding.
                const float ratio = wPlain > 0.0f ? wWhole / wPlain : 0.0f;
                check(ratio > 1.8f && ratio < 2.2f,
                      "DOUBLING the ranged font size roughly DOUBLES the width -- the "
                      "assertion that catches a DIP-to-point conversion missing on this "
                      "path, which would read 2.67x and pass every other check here");
            }
        }

        // --- SetCharacterSpacing: the one with a PREDICTED MAGNITUDE ---------
        //
        // The newest method on this seam, added 2026-09-18 for startpage.cpp, and it
        // overturns a documented refusal to add it -- see text_api.h. So it arrives
        // with the strongest assertion in this family rather than the weakest: not
        // just "wider", but wider BY ROUGHLY THE RIGHT AMOUNT.
        //
        // The magnitude is what makes it strong. Tracking of t DIPs over n characters
        // adds about t*n to the advance, and asserting that rather than "wider" is
        // what pins the UNIT and not just the sign.
        //
        // WHAT THE MUTATION CONTROL ACTUALLY MEASURED, which is not what the first
        // draft of this comment predicted. Qt splits letter spacing into a TYPE and a
        // VALUE, and the type defaults to PercentageSpacing where 100 means
        // unchanged. Deleting the type line from the Qt backend was expected to make
        // the tracking too SMALL and to be caught by the magnitude window alone; it
        // instead reads 4.0 as four percent of each advance and collapses the run, so
        // THREE assertions fail including two direction ones. The block is stronger
        // than predicted rather than weaker, and the prediction is corrected here
        // rather than the result being described as what was expected.
        //
        // THE WINDOW IS 0.8x-1.2x AND THAT NUMBER WAS MEASURED, NOT CHOSEN. It started
        // at 0.5x-1.5x, which was wide enough to pass a DIP-versus-point confusion --
        // a factor of 0.75, and the single likeliest unit bug on this seam, since the
        // setter two functions above this one in the Qt backend legitimately needs
        // exactly that conversion. Both engines add the tracking once per cluster and
        // this text is ASCII with no ligature pairs, so the delta is t*n on both and
        // the tighter window holds with room to spare. 0.75 sits outside it.
        {
            std::unique_ptr<tinta::TextLayout> plain = freshLayout();
            std::unique_ptr<tinta::TextLayout> loose = freshLayout();
            std::unique_ptr<tinta::TextLayout> half = freshLayout();
            std::unique_ptr<tinta::TextLayout> tight = freshLayout();
            check(plain && loose && half && tight,
                  "CONTROL: four identical layouts, so every width difference below is "
                  "the tracking and not the box");
            if (plain && loose && half && tight) {
                // 4 DIP on a 16 DIP font -- deliberately coarse. A tracking small
                // enough to be realistic would sit inside the rounding noise between
                // two shapers and make the magnitude window meaningless.
                const float kTrack = 4.0f;
                loose->SetCharacterSpacing(kTrack, kWhole);
                half->SetCharacterSpacing(kTrack, kFirstHalf);
                // NEGATIVE, because that is the direction startpage.cpp's wordmark
                // actually uses (-1.0 DIP), and a backend that took an absolute value
                // or clamped at zero would pass every positive arm above.
                tight->SetCharacterSpacing(-2.0f, kWhole);

                const float wPlain = widthOf(plain.get());
                const float wLoose = widthOf(loose.get());
                const float wHalf = widthOf(half.get());
                const float wTight = widthOf(tight.get());

                check(wPlain > 0.0f,
                      "CONTROL: the untracked layout has a real width, so the "
                      "comparisons below are between measurements and not zeros");
                check(wLoose > wPlain,
                      "positive tracking measures WIDER -- the setter is plumbed to the "
                      "layout at all");
                check(wTight < wPlain,
                      "NEGATIVE tracking measures NARROWER, which is the direction the "
                      "start page's wordmark uses; a backend clamping at zero or taking "
                      "a magnitude passes every other arm in this block");
                check(wHalf > wPlain && wHalf < wLoose,
                      "THE RANGE'S BOUNDS ARE READ: tracking half the text is wider than "
                      "none and narrower than all of it");

                // THE MAGNITUDE. n is the character count and not the cluster count,
                // and the window is wide enough to absorb the difference: this text is
                // ASCII with no ligature pairs in it, so the two are equal here, but
                // the assertion is about an order of magnitude and not a pixel.
                const float delta = wLoose - wPlain;
                const float predicted = kTrack * static_cast<float>(kLongLength);
                check(delta > predicted * 0.8f && delta < predicted * 1.2f,
                      "THE ADDED WIDTH IS tracking x characters to within 20% -- the "
                      "assertion that pins the UNIT rather than the sign, and the only "
                      "one here that fails for a backend applying the right number of "
                      "the wrong kind of unit (DIPs read as points is 0.75x)");
                check(heightOf(loose.get()) == heightOf(plain.get()),
                      "CONTROL: tracking changes advance and not line height, so the "
                      "widths above are reading the spacing rather than a reflow");
            }
        }

        // --- The contract: out of bounds and empty ranges do nothing ---------
        //
        // text_api.h promises that styling past the end of the text has no
        // observable effect, and that an empty range styles nothing. Both are
        // asserted because both are reachable by ARITHMETIC rather than by intent --
        // a caller computing `{pos, len}` from a document offset can produce either
        // without meaning to, and neither backend's native call complains.
        {
            std::unique_ptr<tinta::TextLayout> plain = freshLayout();
            std::unique_ptr<tinta::TextLayout> past = freshLayout();
            std::unique_ptr<tinta::TextLayout> empty = freshLayout();
            std::unique_ptr<tinta::TextLayout> huge = freshLayout();
            std::unique_ptr<tinta::TextLayout> control = freshLayout();
            check(plain && past && empty && huge && control,
                  "CONTROL: five identical layouts for the boundary arm");
            if (plain && past && empty && huge && control) {
                past->SetFontWeight(tinta::FontWeight::Bold,
                                    tinta::TextRange{kLongLength, 10});
                empty->SetFontWeight(tinta::FontWeight::Bold, tinta::TextRange{0, 0});
                // "To the end", spelled the way DirectWrite permits. This is the arm
                // that fails if a backend computes startPosition + length in 32 bits
                // and wraps to a position before the start.
                huge->SetFontWeight(tinta::FontWeight::Bold,
                                    tinta::TextRange{0, 0xFFFFFFFFu});
                control->SetFontWeight(tinta::FontWeight::Bold, kWhole);

                const float wPlain = widthOf(plain.get());
                check(wPlain > 0.0f, "CONTROL: the unstyled reference has a real width");
                check(widthOf(control.get()) > wPlain,
                      "CONTROL: the SAME value over an IN-BOUNDS range does change the "
                      "width, so the three no-ops below are the ranges being rejected "
                      "and not the setter being dead");
                check(widthOf(past.get()) == wPlain,
                      "a range starting past the end of the text has NO observable "
                      "effect, which is the promise text_api.h makes and the clamp in "
                      "the Qt run keeps");
                check(widthOf(empty.get()) == wPlain,
                      "an empty range styles nothing -- so a defaulted TextRange{} "
                      "cannot silently embolden the first character");
                check(widthOf(huge.get()) == widthOf(control.get()),
                      "a length of 0xFFFFFFFF means TO THE END and is equivalent to the "
                      "exact whole-text range, rather than wrapping to an empty or "
                      "inverted range");
            }
        }

        // --- Two properties over one range MERGE, per property ---------------
        //
        // This asserts a decision rather than a mapping, which is why it is here at
        // all. The Qt backend carries ranged styling as an ordered list of
        // QTextCharFormats and Qt merges them in list order, so a format carrying
        // TWO properties would impose both on its range -- and a later SetFontSize
        // would reset an earlier SetFontWeight to the default it never asked for.
        // One property per entry is what makes the merge per-property, matching
        // DirectWrite, where the attributes are stored independently and this is
        // free. A cross-backend suite is what turns "free on one side" into a
        // checked claim on the other.
        {
            std::unique_ptr<tinta::TextLayout> sizeOnly = freshLayout();
            std::unique_ptr<tinta::TextLayout> both = freshLayout();
            check(sizeOnly && both, "CONTROL: two identical layouts for the merge arm");
            if (sizeOnly && both) {
                sizeOnly->SetFontSize(32.0f, kWhole);
                // Weight FIRST, then size over the same range -- so the later call is
                // the one that would clobber if the formats were not per-property.
                both->SetFontWeight(tinta::FontWeight::Bold, kWhole);
                both->SetFontSize(32.0f, kWhole);

                const float wSizeOnly = widthOf(sizeOnly.get());
                const float wBoth = widthOf(both.get());
                check(wSizeOnly > 0.0f,
                      "CONTROL: the size-only layout has a real width to compare against");
                check(wBoth > wSizeOnly,
                      "a LATER ranged property MERGES with an earlier one over the same "
                      "range instead of replacing it -- bold-then-32 is wider than 32 "
                      "alone, so the weight survived the size call");
            }
        }

        // --- SetStrikethrough: the one with no metric, asserted in PIXELS ----
        //
        // Strikethrough changes no TextMetrics field at all -- same advance, same
        // height -- so a metric assertion for it would be unfalsifiable, and the
        // 29a lesson was that an unfalsifiable assertion beside real ones reads as
        // coverage. It has to be pixels, and pixels are what the harness can read.
        //
        // Painted-pixel COUNTS rather than a probe at one coordinate: the strike
        // line's y depends on the font's strikeout position, which is a property of
        // whichever family the harness chose, and this file may not name it.
        {
            static const wchar_t kShort[] = L"Hello world";
            static const uint32_t kShortLen = 11;
            const tinta::TextRange kShortWhole{0, kShortLen};
            const tinta::TextRange kShortHalf{0, 5};  // "Hello"

            auto paintedWith = [&h, &red](tinta::TextRange range, bool strike) -> int {
                std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
                if (!format) return -1;
                std::unique_ptr<tinta::TextLayout> layout =
                    h.textFactory().CreateTextLayout(kShort, kShortLen, format.get(),
                                                     190.0f, 100.0f);
                if (!layout) return -1;
                if (strike) layout->SetStrikethrough(true, range);
                whiteSurface(h);
                std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
                h.begin().DrawTextLayout(tinta::pointF(5, 5), layout.get(), brush.get());
                h.end();
                return countNonWhite(h);
            };

            const int nPlain = paintedWith(kShortWhole, false);
            const int nWhole = paintedWith(kShortWhole, true);
            const int nHalf = paintedWith(kShortHalf, true);
            const int nOff = [&]() {
                // SetStrikethrough(FALSE) over the whole range, on a layout that was
                // never struck. The last-writer-wins rule means this must be a
                // no-op, and it is the arm that would fail if a backend treated any
                // call to the setter as "turn it on".
                std::unique_ptr<tinta::TextFormat> format = makeFormat(h);
                if (!format) return -1;
                std::unique_ptr<tinta::TextLayout> layout =
                    h.textFactory().CreateTextLayout(kShort, kShortLen, format.get(),
                                                     190.0f, 100.0f);
                if (!layout) return -1;
                layout->SetStrikethrough(false, kShortWhole);
                whiteSurface(h);
                std::unique_ptr<tinta::Brush> brush = h.makeBrush(red);
                h.begin().DrawTextLayout(tinta::pointF(5, 5), layout.get(), brush.get());
                h.end();
                return countNonWhite(h);
            }();

            check(nPlain > 0,
                  "CONTROL: the unstruck string painted pixels, so the three counts "
                  "below are differences between real draws");
            check(nWhole > nPlain,
                  "a struck layout paints MORE pixels than an unstruck one -- the only "
                  "evidence available for a property with no metric consequence");
            check(nHalf > nPlain && nHalf < nWhole,
                  "THE RANGE'S BOUNDS ARE READ for strikethrough: striking \"Hello\" "
                  "paints more than nothing and strictly less than striking \"Hello "
                  "world\"");
            check(nOff == nPlain,
                  "CONTROL: SetStrikethrough(false) paints exactly the unstruck count, "
                  "so the setter reads its BOOL and does not merely react to being "
                  "called");
        }
    }

    // --- AnalyzeLineBreaks: UAX#14, the one text question that is about the SCRIPT --
    //
    // The seam method with the largest gap between what was claimed and what was ever
    // executed. The reasoning it replaced lived only in a comment in render.cpp --
    // Chinese prose has no spaces, so a backend that cannot answer treats an entire
    // sentence as one unbreakable word -- and nothing ran it. Two unrelated engines
    // answer it now, IDWriteTextAnalyzer and QTextBoundaryFinder, so an agreement here
    // is evidence about the SEAM rather than about either implementation.
    //
    // Every non-ASCII character is written as a \u escape and never as a literal,
    // because MSVC without /utf-8 reads source in the active code page: a literal
    // ideograph here would make the compiler guess at the very bytes under test.
    //
    // Sized text.size() + 1 and indexed exactly as src/render.cpp's
    // analyzeBreakOpportunities does it. A test that sizes the vector differently from
    // the only caller is testing a contract nobody uses.
    {
        auto analyse = [&h](const wchar_t* text, uint32_t len,
                            std::vector<bool>& out) -> bool {
            out.assign(static_cast<size_t>(len) + 1, false);
            return h.textFactory().AnalyzeLineBreaks(text, len, out);
        };

        // ASCII first, because it is the case a reader can check by eye -- if this
        // fails, nothing below is interpretable.
        {
            static const wchar_t kAscii[] = L"hello world";
            std::vector<bool> b;
            const bool ok = analyse(kAscii, 11, b);
            check(ok,
                  "CONTROL: the backend analysed line breaks at all -- a false here "
                  "would leave every assertion below reading the vector's own zeroes");
            if (ok) {
                check(b[6], "a line may break before 'w', the character after the space");
                check(!b[3], "and not in the middle of \"hello\"");
                check(!b[5],
                      "nor before the space itself -- the opportunity falls AFTER a "
                      "space, which is the off-by-one the seam's loop is entirely about");
            }
        }

        {
            // U+65E5 U+672C U+8A9E -- three Han ideographs, no space anywhere. This is
            // the fact the old comment asserted in prose and nothing measured.
            static const wchar_t kHan[] = L"\u65E5\u672C\u8A9E";
            std::vector<bool> b;
            if (analyse(kHan, 3, b)) {
                check(b[1] && b[2],
                      "A LINE MAY BREAK BETWEEN IDEOGRAPHS, at every position -- the "
                      "space-only fallback returns false for all of them, so this is "
                      "the assertion that distinguishes a real analyzer from none");
            }
        }

        {
            // U+3002, the ideographic full stop: UAX#14 class CL. A break before it
            // would orphan the punctuation onto the head of the next line.
            static const wchar_t kClose[] = L"\u65E5\u672C\u3002";
            std::vector<bool> b;
            if (analyse(kClose, 3, b)) {
                check(b[1], "CONTROL: the ideograph pair in the same string still breaks");
                check(!b[2],
                      "NO BREAK BEFORE CLOSING PUNCTUATION -- the full stop stays with "
                      "the ideograph it follows, which space-only breaking gets right "
                      "only by never breaking at all");
            }
        }

        {
            // U+300C / U+300D, the corner brackets: class OP forbids a break after the
            // opener, class CL a break before the closer.
            static const wchar_t kBracket[] = L"\u300C\u65E5\u672C\u300D";
            std::vector<bool> b;
            if (analyse(kBracket, 4, b)) {
                check(!b[1], "NO BREAK AFTER AN OPENING BRACKET");
                check(!b[3], "and none before a closing one");
                check(b[2],
                      "CONTROL: the two ideographs BETWEEN the brackets do break, so "
                      "the two prohibitions above are not just an analyzer refusing to "
                      "answer for this string");
            }
        }

        {
            // Quotation marks are class QU, which forbids a break on either side --
            // asserted on ASCII because it is the one prohibition that shows up in
            // ordinary English text.
            static const wchar_t kQuoted[] = L"\"hello\"";
            std::vector<bool> b;
            if (analyse(kQuoted, 7, b)) {
                check(!b[1], "no break between an opening quote and the word it opens");
                check(!b[6], "and none between the word and its closing quote");
            }
        }

        // --- THE NON-BMP CASE, which is the only reason the index walk exists -------
        //
        // The same omission the astral hit-testing block above was written to fix, and
        // I nearly repeated it: every assertion so far is BMP, and for BMP text the
        // Qt backend's caller-unit-to-QString-unit step is numerically 1 on both
        // platforms, so breaking it reddens nothing. The mapping's entire subject would
        // have been untested by the block written to test it.
        //
        // Offsets are derived from the literal's own element count rather than written
        // down, because U+1F600 is TWO caller units on Windows and ONE on Linux --
        // exactly as the astral hit-test block does it.
        {
            static const wchar_t kMixed[] = L"\u65E5\U0001F600\u672C";
            const uint32_t kMixedLen = (uint32_t)(sizeof(kMixed) / sizeof(wchar_t)) - 1;
            const uint32_t kAstralUnits = (sizeof(wchar_t) == 2) ? 2u : 1u;
            check(kMixedLen == 1u + kAstralUnits + 1u,
                  "CONTROL: the mixed BMP/astral literal has this platform's element "
                  "count, so the index below is the caller's real one");

            std::vector<bool> b;
            if (analyse(kMixed, kMixedLen, b)) {
                check(b[1],
                      "CONTROL: a break is offered between the first ideograph and the "
                      "astral character");
                // THE ASSERTION THE WALK EXISTS FOR. On Linux a backend that stepped
                // its QString cursor by 1 per caller unit would ask the boundary finder
                // about a position INSIDE the surrogate pair, which is not a boundary,
                // and this would come back false. On Windows the step is 1 either way,
                // so this assertion is live off Windows and free on it -- which is the
                // honest description of the mapping itself.
                check(b[1u + kAstralUnits],
                      "AND ONE AFTER IT, at the caller's index -- the QString cursor "
                      "stayed aligned across a surrogate pair");
            }
        }

        // --- The contract in text_api.h, clause by clause -------------------------
        {
            static const wchar_t kAscii[] = L"hello world";
            const uint32_t len = 11;

            // Clause 2, POISONED BOTH WAYS. Filling with false and asserting index 0
            // and index length come back false would pass for a backend that writes
            // false into them -- and writing them is the likely mistake, because Qt's
            // boundary finder reports a boundary at BOTH ends, so an implementation
            // that copies its answer straight across writes true at 0 and at length.
            // Each end is therefore run once poisoned true and once poisoned false and
            // must survive unchanged in both.
            std::vector<bool> t(static_cast<size_t>(len) + 1, true);
            const bool okT = h.textFactory().AnalyzeLineBreaks(kAscii, len, t);
            std::vector<bool> f(static_cast<size_t>(len) + 1, false);
            const bool okF = h.textFactory().AnalyzeLineBreaks(kAscii, len, f);
            check(okT && okF, "CONTROL: both poisoned runs analysed");
            if (okT && okF) {
                check(t[0] && !f[0],
                      "INDEX 0 IS UNTOUCHED: true when poisoned true, false when "
                      "poisoned false, so the backend demonstrably did not write it");
                check(t[len] && !f[len],
                      "AND SO IS INDEX length -- the end-of-string break belongs to the "
                      "caller, which sets it unconditionally");
                check(t[6] && f[6],
                      "CONTROL: an interior index IS written, to the same value from "
                      "both poisons -- without this, the two checks above would be "
                      "satisfied by a backend that writes nothing whatsoever");
            }
        }

        {
            // Clause 3: a false return leaves canBreak alone. The caller's space-only
            // fallback reads the vector it assigned itself, so a backend that filled
            // half of it and THEN failed would corrupt a fallback that still reports
            // success.
            std::vector<bool> b(6, true);
            check(!h.textFactory().AnalyzeLineBreaks(L"hello", 0, b),
                  "a zero length is refused rather than analysed");
            check(b[0] && b[5], "and the refused call left the vector untouched");

            std::vector<bool> nullv(6, true);
            check(!h.textFactory().AnalyzeLineBreaks(nullptr, 5, nullv),
                  "a null string is refused");
            check(nullv[0] && nullv[5], "and that refusal touched nothing either");

            // Sized `length`, one slot short of the contract. A backend that wrote
            // 1..length-1 into it would be entirely in bounds and would silently
            // deprive the caller of the end-of-string slot it depends on, so this is
            // refused rather than served.
            //
            // Named `undersized` and not `small`, which MSVC rejects here for a reason
            // no Linux build could ever surface: rpcndr.h, reached through windows.h,
            // does `#define small char`. Worth the sentence -- it is invisible in the
            // portable half of the tree and the error it produces names neither the
            // macro nor the header.
            std::vector<bool> undersized(5, false);
            check(!h.textFactory().AnalyzeLineBreaks(L"hello", 5, undersized),
                  "AN UNDERSIZED VECTOR IS REFUSED, not written into as far as it fits");
        }
    }

    std::printf("%s%d render-api assertions run, %d failed (backend=%s)\n",
                failures == 0 ? "all " : "", checks, failures, h.name());
    return failures;
}
