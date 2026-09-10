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

static void check(bool ok, const char* what) {
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
#endif

    std::printf("%s\n", failures == 0 ? "all geometry assertions passed"
                                      : "geometry assertions FAILED");
    return failures == 0 ? 0 : 1;
}
