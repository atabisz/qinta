// Exercises include/geometry.h.
//
// WHY THIS TARGET EXISTS, AND WHY IT IS NOT WINDOWS-ONLY
//
// geometry.h was added on 2026-09-10 to move the plain-value colour and
// rectangle types off <d2d1.h>. Every one of the ~1,929 sites it replaced is in
// a file that is still Windows-only, so when the header landed, the non-Windows
// build compiled NONE of it -- the portable half of the header, which is its
// entire reason for existing, had no instrument at all. A one-off g++ run proved
// it compiled once; nothing kept that true.
//
// So this target is deliberately outside if(WIN32) in CMakeLists.txt, and
// scripts/probe-portable-linux.sh asserts that this file was compiled. The
// assertions below are mostly value checks rather than type checks, because the
// static_asserts inside the header already cover layout on Windows and are
// switched off everywhere else -- what a non-Windows compiler can uniquely tell
// us is whether the aggregates, the factories and the defaulted alpha behave.

#include "geometry.h"

#include <cstdio>
#include <cmath>
#include <type_traits>

static int failures = 0;
// COUNTED, not just printed. A green run with no count is indistinguishable from
// a run where half the assertions were compiled out -- and this file has a whole
// block behind #ifdef _WIN32, so "how many ran" is a real question here rather
// than a formality.
static int checks = 0;

static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// Named `nearly` and not `near`: <windef.h> (reached through d2d1.h on Windows)
// still carries the 16-bit-era `#define near` / `#define far`, which expand to
// nothing. g++ compiled a function called near() happily; MSVC saw
// `static bool (float a, float b)` and reported a syntax error at the
// parenthesis. Portability breakage runs in both directions.
static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-6f; }

int main() {
    // --- The aggregates stay aggregates ------------------------------------
    //
    // These are not decoration. Adding a constructor to any of these structs
    // would break every braced initialisation in the tree at once, and the tree
    // has thousands. is_aggregate is the property that guarantees `tinta::RectF
    // r{1, 2, 3, 4}` keeps meaning what it means here.
    check(std::is_aggregate<tinta::Color>::value, "tinta::Color is an aggregate");
    check(std::is_aggregate<tinta::RectF>::value, "tinta::RectF is an aggregate");
    check(std::is_aggregate<tinta::PointF>::value, "tinta::PointF is an aggregate");
    check(std::is_aggregate<tinta::SizeF>::value, "tinta::SizeF is an aggregate");
    check(std::is_aggregate<tinta::RoundedRect>::value, "tinta::RoundedRect is an aggregate");
    check(std::is_aggregate<tinta::Ellipse>::value, "tinta::Ellipse is an aggregate");

    check(std::is_trivially_copyable<tinta::Color>::value, "tinta::Color is trivially copyable");
    check(std::is_trivially_copyable<tinta::RectF>::value, "tinta::RectF is trivially copyable");
    check(std::is_standard_layout<tinta::RectF>::value, "tinta::RectF is standard layout");

    // Value-initialisation has to zero every member. Several call sites return
    // `tinta::RectF{}` as an empty rectangle and then compare or draw it.
    tinta::RectF zeroed{};
    check(nearly(zeroed.left, 0) && nearly(zeroed.top, 0) && nearly(zeroed.right, 0) && nearly(zeroed.bottom, 0),
          "tinta::RectF{} zeroes all four members");
    tinta::Color zeroColor{};
    check(nearly(zeroColor.a, 0), "tinta::Color{} leaves alpha 0, i.e. it does NOT default to opaque");

    // --- Member order --------------------------------------------------------
    //
    // Braced initialisation is positional, so the declaration order of these
    // members is a contract with ~1,929 call sites. left/top/right/bottom is the
    // Direct2D order and NOT the x/y/width/height order that qmd::Rect in
    // include/types.h uses -- confusing the two would silently reinterpret every
    // rectangle in the tree, which is why types.h was not reused.
    tinta::RectF r{1, 2, 3, 4};
    check(nearly(r.left, 1) && nearly(r.top, 2) && nearly(r.right, 3) && nearly(r.bottom, 4),
          "tinta::RectF braced init is left, top, right, bottom");
    tinta::Color c{0.1f, 0.2f, 0.3f, 0.4f};
    check(nearly(c.r, 0.1f) && nearly(c.g, 0.2f) && nearly(c.b, 0.3f) && nearly(c.a, 0.4f),
          "tinta::Color braced init is r, g, b, a");
    tinta::SizeF s{5, 6};
    check(nearly(s.width, 5) && nearly(s.height, 6), "tinta::SizeF braced init is width, height");

    // --- Factories -----------------------------------------------------------
    //
    // The sweep rewrote `D2D1::RectF(` to `tinta::rectF(` and left the arguments
    // and the closing paren alone, so these signatures have to keep matching the
    // d2d1helper.h ones they replaced. The defaulted alpha in particular: 19 of
    // the swept colour sites pass three arguments and rely on it being 1.0f.
    tinta::Color three = tinta::colorF(0.25f, 0.5f, 0.75f);
    check(nearly(three.a, 1.0f), "colorF with three arguments defaults alpha to 1.0 (opaque)");
    check(nearly(three.r, 0.25f) && nearly(three.g, 0.5f) && nearly(three.b, 0.75f),
          "colorF passes r, g, b through in order");
    check(nearly(tinta::colorF(0, 0, 0, 0.5f).a, 0.5f), "colorF with four arguments uses the given alpha");

    tinta::RectF fr = tinta::rectF(10, 20, 30, 40);
    check(nearly(fr.left, 10) && nearly(fr.top, 20) && nearly(fr.right, 30) && nearly(fr.bottom, 40),
          "rectF argument order matches D2D1::RectF");
    tinta::PointF fp = tinta::pointF(7, 8);
    check(nearly(fp.x, 7) && nearly(fp.y, 8), "pointF argument order matches D2D1::Point2F");
    tinta::SizeF fs = tinta::sizeF(9, 11);
    check(nearly(fs.width, 9) && nearly(fs.height, 11), "sizeF argument order matches D2D1::SizeF");

    // roundedRect and ellipse nest a value rather than taking loose floats,
    // exactly as D2D1::RoundedRect and D2D1::Ellipse do -- 179 and 23 swept
    // sites respectively pass a rect or a point as the first argument.
    tinta::RoundedRect rr = tinta::roundedRect(tinta::rectF(1, 2, 3, 4), 5, 6);
    check(nearly(rr.rect.left, 1) && nearly(rr.rect.bottom, 4) && nearly(rr.radiusX, 5) && nearly(rr.radiusY, 6),
          "roundedRect nests the rect and keeps radiusX, radiusY order");
    tinta::Ellipse e = tinta::ellipse(tinta::pointF(1, 2), 3, 4);
    check(nearly(e.point.x, 1) && nearly(e.point.y, 2) && nearly(e.radiusX, 3) && nearly(e.radiusY, 4),
          "ellipse nests the point and keeps radiusX, radiusY order");

    // --- colorFromHex --------------------------------------------------------
    //
    // 0xRRGGBB, matching the single-argument D2D1::ColorF(UINT32) it replaced in
    // tests/math_layout_tests.cpp. Checked against hand-computed values because
    // the whole risk in a hex unpack is getting a shift or a mask wrong, and a
    // wrong shift still produces a plausible-looking colour.
    tinta::Color hex = tinta::colorFromHex(0x17212B);
    check(nearly(hex.r, 0x17 / 255.0f), "colorFromHex takes red from bits 16-23");
    check(nearly(hex.g, 0x21 / 255.0f), "colorFromHex takes green from bits 8-15");
    check(nearly(hex.b, 0x2B / 255.0f), "colorFromHex takes blue from bits 0-7");
    check(nearly(hex.a, 1.0f), "colorFromHex defaults alpha to 1.0");
    check(nearly(tinta::colorFromHex(0xFFFFFF).r, 1.0f), "colorFromHex maps 0xFF to exactly 1.0");
    check(nearly(tinta::colorFromHex(0x000000).r, 0.0f), "colorFromHex maps 0x00 to exactly 0.0");
    check(nearly(tinta::colorFromHex(0x808080, 0.25f).a, 0.25f), "colorFromHex honours an explicit alpha");

    // --- arcCenterFromEndpoints --------------------------------------------------
    //
    // The first function in this header that COMPUTES. Everything above is a brace
    // and a field order, where the failure mode is a wrong number in an obvious
    // place; this one has a sign convention, a branch cut and a degenerate case, and
    // its failure mode is an arc that bulges the wrong way -- a picture, not a
    // crash. tests/render_api_shared.cpp asserts the drawn result on both backends;
    // these assert the arithmetic itself, on both platforms, where the pixel test
    // cannot run at all.
    //
    // Every expected value below is hand-computed from the geometry, not read back
    // from a first run. Angles are y-DOWN, so increasing angle is clockwise on
    // screen and that is the only direction this function produces.
    {
        const float pi = 3.14159265359f;
        // Loose against the float noise of a sqrt and two atan2s. 1e-6 (what
        // `nearly` uses) is too tight for a value that went through three
        // transcendental calls; 1e-4 on a radius-1 circle is still a thousandth of a
        // pixel at any size this tree draws at.
        auto close = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };

        // A quarter arc on the unit circle at the origin: (1,0) to (0,1), the small
        // way round. Clockwise from angle 0 to angle pi/2.
        tinta::PointF ctr{999, 999};
        float a0 = 999.0f, sweep = 999.0f;
        bool ok = tinta::arcCenterFromEndpoints(tinta::pointF(1, 0), tinta::pointF(0, 1),
                                                1.0f, false, &ctr, &a0, &sweep);
        check(ok, "a quarter arc between two unit-circle points is solvable");
        check(close(ctr.x, 0.0f) && close(ctr.y, 0.0f), "and its centre is the origin");
        check(close(a0, 0.0f), "its start angle is 0, measured from +x");
        check(close(sweep, pi / 2), "and its clockwise sweep is pi/2");

        // THE SAME TWO ENDPOINTS, largeArc = true. There are exactly two circles of a
        // given radius through two points, and this is the other one -- centre (1,1),
        // sweeping 3pi/2 the long way round. Nothing about the endpoints distinguishes
        // the two cases, so a function that ignored largeArc would pass the block
        // above and fail here.
        ctr = tinta::PointF{999, 999};
        a0 = 999.0f;
        sweep = 999.0f;
        ok = tinta::arcCenterFromEndpoints(tinta::pointF(1, 0), tinta::pointF(0, 1), 1.0f,
                                           true, &ctr, &a0, &sweep);
        check(ok, "the same endpoints with largeArc are also solvable");
        check(close(ctr.x, 1.0f) && close(ctr.y, 1.0f),
              "but the OTHER circle is chosen, so largeArc is read and not ignored");
        check(close(sweep, 3 * pi / 2), "and the sweep is the major one, 3pi/2");
        check(sweep > pi, "CONTROL: a major sweep exceeds pi, which is what largeArc means");

        // ACROSS THE atan2 BRANCH CUT. From angle 3pi/4 to angle -3pi/4, which is a
        // clockwise quarter turn passing through pi -- the discontinuity where atan2
        // jumps from +pi to -pi. A subtraction without the wrap below would report
        // -3pi/2 here, and the sign alone would send the arc the wrong way.
        const float r2 = 0.70710678f;
        ctr = tinta::PointF{999, 999};
        sweep = 999.0f;
        ok = tinta::arcCenterFromEndpoints(tinta::pointF(-r2, r2), tinta::pointF(-r2, -r2),
                                           1.0f, false, &ctr, &a0, &sweep);
        check(ok && close(ctr.x, 0.0f) && close(ctr.y, 0.0f),
              "an arc crossing atan2's +pi/-pi discontinuity still finds the origin");
        check(close(sweep, pi / 2),
              "and its sweep is a quarter turn, not the -3pi/2 a bare subtraction gives");

        // THE SLICE SHAPE render.cpp ACTUALLY DRAWS: centre at (radius, radius) in
        // local space, endpoints computed from cos/sin of the two angles. Included as
        // itself rather than only as unit-circle algebra, because this is the caller.
        const float radius = 10.0f;
        ctr = tinta::PointF{999, 999};
        sweep = 999.0f;
        ok = tinta::arcCenterFromEndpoints(tinta::pointF(20, 10), tinta::pointF(10, 20),
                                           radius, false, &ctr, &a0, &sweep);
        check(ok && close(ctr.x, 10.0f) && close(ctr.y, 10.0f),
              "a mermaid pie slice's arc recovers the centre render.cpp put it at");
        check(close(sweep, pi / 2), "with the quarter sweep its two angles asked for");

        // THE DEGENERATE CASE, AND IT IS A DELIBERATE BUG-PRESERVING ANSWER.
        // src/export_docx.cpp's slice builder has no full-circle special case, so a
        // 360-degree slice reaches the backend as start == end. Direct2D draws nothing
        // for that; returning false here keeps the Qt backend agreeing with it. Fixing
        // it in this function would silently change what a DOCX export looks like, so
        // the divergence stays a separately fixable defect. geometry.h says so too --
        // this is the assertion that makes the comment load-bearing.
        ctr = tinta::PointF{7, 7};
        a0 = 7.0f;
        sweep = 7.0f;
        ok = tinta::arcCenterFromEndpoints(tinta::pointF(5, 5), tinta::pointF(5, 5), 10.0f,
                                           false, &ctr, &a0, &sweep);
        check(!ok, "coincident endpoints are NOT an arc, matching what Direct2D draws");
        // Poison values, unchanged. A function that returned false having already
        // written garbage through its out-params would pass the check above and hand a
        // caller a plausible centre.
        check(nearly(ctr.x, 7.0f) && nearly(a0, 7.0f) && nearly(sweep, 7.0f),
              "and a rejected arc writes NOTHING through its out-params");

        check(!tinta::arcCenterFromEndpoints(tinta::pointF(0, 0), tinta::pointF(1, 0), 1.0f,
                                             false, nullptr, &a0, &sweep),
              "a null out-param is refused rather than dereferenced");

        // A radius too small to reach: the chord is 2 units, the radius 0.6, so no
        // circle of that radius passes through both. Clamped to the r = d/2
        // semicircle, which is what SVG and Direct2D both do. No call site reaches
        // this today -- asserted so that the boundary is a decision on record rather
        // than whatever the arithmetic happened to produce.
        ctr = tinta::PointF{999, 999};
        sweep = 999.0f;
        ok = tinta::arcCenterFromEndpoints(tinta::pointF(-1, 0), tinta::pointF(1, 0), 0.6f,
                                           false, &ctr, &a0, &sweep);
        check(ok, "a radius too small for the chord is clamped, not rejected");
        check(close(ctr.x, 0.0f) && close(ctr.y, 0.0f) && close(sweep, pi),
              "and yields the semicircle on the chord's midpoint");
    }

#ifdef _WIN32
    // On Windows the header also carries the Direct2D interop. The layout
    // static_asserts fire at compile time, so reaching this line at all is part
    // of the evidence; these check that the conversions move the right fields,
    // which an offsetof assertion cannot.
    D2D1_RECT_F nativeRect = fr;
    check(nearly(nativeRect.left, 10) && nearly(nativeRect.bottom, 40),
          "RectF converts implicitly to D2D1_RECT_F with fields in place");
    D2D1_COLOR_F nativeColor = three;
    check(nearly(nativeColor.r, 0.25f) && nearly(nativeColor.a, 1.0f),
          "Color converts implicitly to D2D1_COLOR_F with fields in place");
    D2D1_ROUNDED_RECT nativeRounded = rr;
    check(nearly(nativeRounded.rect.left, 1) && nearly(nativeRounded.radiusY, 6),
          "RoundedRect converts implicitly, nested rect included");
    D2D1_ELLIPSE nativeEllipse = e;
    check(nearly(nativeEllipse.point.y, 2) && nearly(nativeEllipse.radiusX, 3),
          "Ellipse converts implicitly, nested point included");

    tinta::RectF back = tinta::fromD2D(nativeRect);
    check(nearly(back.left, 10) && nearly(back.bottom, 40), "fromD2D round-trips a rect");
    tinta::Color backColor = tinta::fromD2D(nativeColor);
    check(nearly(backColor.r, 0.25f) && nearly(backColor.a, 1.0f), "fromD2D round-trips a colour");

    // asD2DPointArray is the one place the layout assertions are load-bearing:
    // it hands Direct2D a run of contiguous points without copying. Two points
    // are enough to catch a stride error, which is the failure that a
    // single-element check would miss.
    const tinta::PointF run[3] = {{1, 2}, {3, 4}, {5, 6}};
    const D2D1_POINT_2F* asNative = tinta::asD2DPointArray(run);
    check(nearly(asNative[0].x, 1) && nearly(asNative[1].x, 3) && nearly(asNative[2].y, 6),
          "asD2DPointArray preserves stride across a run of points");

    // matrixScale's centred form against D2D's own three-argument Scale, field by
    // field and with EXACT equality rather than `nearly`.
    //
    // Exact is the whole point. The site that needs this helper -- src/print.cpp's
    // shrink band -- previously called D2D1::Matrix3x2F::Scale directly, and the
    // claim being made about that port is that the printed page does not change by
    // one byte. `nearly` would pass on a matrix that differed in the last mantissa
    // bit, which is exactly the difference that would move an antialiased edge and
    // break the page hash. So this asserts the two helpers perform the same
    // arithmetic, not merely that they agree to a tolerance.
    //
    // The centre is asymmetric (7, 13) and the scales differ (0.5, 0.25) so that a
    // transposed field or a centre applied to the wrong axis cannot pass.
    const D2D1_MATRIX_3X2_F d2dScale =
        D2D1::Matrix3x2F::Scale(0.5f, 0.25f, D2D1::Point2F(7.0f, 13.0f));
    const tinta::Matrix3x2F ours =
        tinta::matrixScale(0.5f, 0.25f, tinta::pointF(7.0f, 13.0f));
    check(ours.m11 == d2dScale.m11 && ours.m12 == d2dScale.m12 &&
          ours.m21 == d2dScale.m21 && ours.m22 == d2dScale.m22 &&
          ours.dx == d2dScale.dx && ours.dy == d2dScale.dy,
          "matrixScale(sx, sy, centre) is bit-identical to D2D1::Matrix3x2F::Scale");
#endif

    std::printf("%d/%d geometry assertions passed\n", checks - failures, checks);
    std::printf("%s\n", failures == 0 ? "all geometry assertions passed"
                                      : "geometry assertions FAILED");
    return failures == 0 ? 0 : 1;
}
