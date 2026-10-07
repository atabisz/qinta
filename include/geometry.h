#ifndef TINTA_GEOMETRY_H
#define TINTA_GEOMETRY_H

// <cmath> for arcCenterFromEndpoints below, which is the first function in this
// file that computes rather than aggregates. Everything above it is a brace and a
// struct name; that one has a sign convention and a degenerate case, which is
// exactly why it lives here where both compilers can assert it.
#include <cmath>
#include <cstddef>

// Plain-value geometry and colour types, with no rendering backend attached.
//
// WHY THIS EXISTS
//
// Measured on 2026-09-10: 60% of the lines in src+include sit in files that name
// ID2D1, IDWrite or D2D1_. Most of that is genuine Direct2D work, but a large
// slice is not: D2D1_COLOR_F, D2D1_RECT_F, D2D1_POINT_2F and friends are plain
// structs of floats that happen to be declared in a Windows SDK header. Using
// them as the type of a theme colour or a layout rectangle drags <d2d1.h> into
// files that never call a Direct2D method -- and, worse, into include/app.h,
// where they are MEMBER TYPES of the application state, so every consumer of
// App inherits the dependency. That is why replacing them is tree-wide rather
// than file-local, and why it has to happen before app.h can be split.
//
// So these are the same four-float and two-float shapes, declared here, with
// nothing behind them.
//
// NAMING -- READ BEFORE ADDING A using-DIRECTIVE
//
// Everything here is spelled tinta::Something at every call site, on purpose.
// Do NOT write `using namespace tinta;`. The tree already has two other types
// called Color:
//
//   qmd::Color     (include/types.h)   -- four floats, but with constructors and
//                                         a fromHex helper; include/app.h line 26
//                                         does `using namespace qmd;`, so it is
//                                         already visible UNQUALIFIED in most of
//                                         the tree.
//   mermaid::Color (include/mermaid.h) -- a different representation entirely:
//                                         packed uint32 rgb plus a float alpha.
//
// A using-directive here would make bare `Color` ambiguous across most of src/.
// Qualifying is also what makes the sweep greppable: `rg 'tinta::'` finds every
// site that went through this header.
//
// qmd::Rect is deliberately NOT reused for RectF. It stores x/y/width/height,
// where D2D_RECT_F stores left/top/right/bottom -- same size, different meaning,
// so reusing it would have silently reinterpreted 257 rectangles.

// The Direct2D interop that stood here -- <d2d1.h>, an implicit conversion operator on
// each value type, fromD2D and asD2DPointArray, and the layout static_asserts that made
// those casts safe -- left with the Direct2D stack on 2026-10-07. Nothing here names a
// platform type now, on any platform.

namespace tinta {

// Member names and declaration order below were taken from the Windows SDK headers
// while Direct2D was a backend, when static_asserts here held each type to its SDK twin.
// The asserts left with that backend on 2026-10-07; the record of what matched stays:
//   D2D_COLOR_F   = D3DCOLORVALUE  {float r, g, b, a}            (d3d9types.h)
//   D2D_RECT_F    {FLOAT left, top, right, bottom}               (um/dcommon.h)
//   D2D_POINT_2F  {FLOAT x, y}                                   (um/dcommon.h)
//   D2D_SIZE_F    {FLOAT width, height}                          (um/dcommon.h)
//   D2D1_ROUNDED_RECT {D2D1_RECT_F rect; FLOAT radiusX, radiusY}    (um/d2d1.h)
//   D2D1_ELLIPSE      {D2D1_POINT_2F point; FLOAT radiusX, radiusY} (um/d2d1.h)
//
// These stay AGGREGATES: no constructors, no base classes, no virtuals, no
// private members. That keeps `tinta::Color c{1, 1, 1, 1}` and `tinta::RectF r{}`
// working, keeps them trivially copyable, and keeps offsetof well defined. A member
// function is allowed; adding a CONSTRUCTOR is not -- it would break every braced
// initialisation in the tree at once.

struct Color {
    float r, g, b, a;
};

struct PointF {
    float x, y;
};

struct SizeF {
    float width, height;
};

struct RectF {
    float left, top, right, bottom;
};

struct RoundedRect {
    RectF rect;
    float radiusX, radiusY;
};

struct Ellipse {
    PointF point;
    float radiusX, radiusY;
};

// A 2D affine transform, laid out to match D2D1_MATRIX_3X2_F.
//
// READ THE SDK BEFORE EDITING THE MEMBER LIST. Unlike every other type in this
// file, D2D_MATRIX_3X2_F (dcommon.h) is not a flat struct -- it is a UNION of three
// anonymous structs offering the same 24 bytes under three spellings: {m11, m12,
// m21, m22, dx, dy}, {_11, _12, _21, _22, _31, _32}, and FLOAT m[3][2]. The names
// used here are the first of those. The union is why the conversion operator below
// assigns through a named member rather than using a braced initialiser: brace
// -initialising a union sets its FIRST member, which happens to be the struct we
// want, but that would be relying on the SDK's declaration order for correctness
// rather than on the member name. (That conversion operator left with the Direct2D
// stack on 2026-10-07; the layout note stands as the reason for the member names.)
//
// The convention is ROW-VECTOR -- a point is a row multiplied on the LEFT, so
// `p * A * B` applies A and then B, and dx/dy are the translation. Qt's QTransform
// agrees, measured rather than assumed: see operator* below.
struct Matrix3x2F {
    float m11, m12, m21, m22, dx, dy;
};

// --- Factory helpers ---------------------------------------------------------
//
// These mirror the d2d1helper.h D2D1:: helpers one-for-one, INCLUDING the
// defaulted alpha on colorF, because they replaced them mechanically: the sweep
// rewrote `D2D1::RectF(` to `tinta::rectF(` and left the arguments and the
// closing paren untouched. That is only safe while the signatures match, so do
// not reorder or add parameters here without checking the call sites.
//
// They exist at all rather than relying on braced initialisation because a
// brace-for-paren rewrite is not mechanically safe: `D2D1::RectF(x, y, x + w,
// dpi(app, 4))` has a nested call, so "replace the matching close paren with a
// brace" needs a parser, and a rename does not.

inline Color colorF(float r, float g, float b, float a = 1.0f) {
    return Color{r, g, b, a};
}

// 0xRRGGBB, matching the single-argument D2D1::ColorF(UINT32) constructor and
// qmd::Color::fromHex. Deliberately NOT an overload of colorF: an overload set
// where one member takes a uint32 and another takes floats would silently accept
// colorF(0.5f) as a packed-integer colour, because float converts to uint32_t.
inline Color colorFromHex(unsigned int rgb, float a = 1.0f) {
    return Color{((rgb >> 16) & 0xFF) / 255.0f,
                 ((rgb >> 8) & 0xFF) / 255.0f,
                 (rgb & 0xFF) / 255.0f,
                 a};
}

inline RectF rectF(float left, float top, float right, float bottom) {
    return RectF{left, top, right, bottom};
}

inline PointF pointF(float x, float y) {
    return PointF{x, y};
}

inline SizeF sizeF(float width, float height) {
    return SizeF{width, height};
}

inline RoundedRect roundedRect(const RectF& rect, float radiusX, float radiusY) {
    return RoundedRect{rect, radiusX, radiusY};
}

inline Ellipse ellipse(const PointF& point, float radiusX, float radiusY) {
    return Ellipse{point, radiusX, radiusY};
}

// --- Arc conversion ----------------------------------------------------------
//
// Direct2D describes an arc by where it ENDS (D2D1_ARC_SEGMENT: endpoint, radii,
// a large/small flag, a sweep direction). Qt describes the same arc by the BOX it
// is inscribed in plus a start angle and a sweep angle (QPainterPath::arcTo). Both
// are complete descriptions and neither can be turned into the other by renaming
// fields, so somebody has to solve for the centre.
//
// This is that solve, and it is HERE rather than inside QtPathSink for one reason:
// off Windows the Qt backend is the only thing that would exercise it, and a
// Windows build would then never compile it at all. As a free function in this
// header both compilers see it and tests/geometry_tests.cpp asserts it on both
// platforms -- which matters more than usual, because the failure mode of a wrong
// sign here is an arc that bulges the wrong way. That is a picture nobody's exit
// code reports.
//
// SIGN CONVENTION, stated because it is the part most easily got backwards. All of
// this tree's coordinates are y-DOWN (a screen, not a maths textbook). So an
// increasing atan2 angle rotates from +x towards +y, which on screen is CLOCKWISE
// -- the same direction D2D1_SWEEP_DIRECTION_CLOCKWISE means, and the reason this
// function assumes clockwise rather than taking it as a parameter: the census found
// all 4 arc sites in the tree passing CLOCKWISE, and render_api.h::ArcSegment
// therefore has no field for it.
//
// Returns false when there is no such arc, and the SHAPE of that failure is
// deliberate. Direct2D's own behaviour for a degenerate segment (endpoint equal to
// the current point) is to draw nothing useful, and src/export_docx.cpp's slice
// builder relies on that: it lacks render.cpp's full-circle special case, so a
// 360-degree slice reaches D2D as start == end and comes out empty. That is a REAL
// BUG in the DOCX export, and this function preserving it is a choice, not an
// oversight -- fixing it here would silently change what a DOCX file looks like
// under a commit whose subject line says "portability". It is recorded as its own
// fixable defect instead.
//
// The radius is likewise NOT corrected upwards when it is too small to reach
// (r < d/2). The term under the root is clamped at zero, which yields the r = d/2
// semicircle -- the same thing SVG and Direct2D both do. No call site reaches it
// today (all 4 compute their endpoint from the same radius they pass), so this is
// a documented boundary rather than tested behaviour.
inline bool arcCenterFromEndpoints(PointF start, PointF end, float radius,
                                   bool largeArc, PointF* center,
                                   float* startAngle, float* sweepAngle) {
    if (!center || !startAngle || !sweepAngle) return false;

    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float d = std::sqrt(dx * dx + dy * dy);

    // Coincident endpoints: no arc. See the paragraph above on why this is not
    // reinterpreted as a full circle.
    if (d < 1e-6f || radius <= 0.0f) return false;

    // Half-chord and the perpendicular offset from the chord's midpoint to the
    // centre. Clamped rather than rejected -- see above.
    const float half = d * 0.5f;
    float hSquared = radius * radius - half * half;
    if (hSquared < 0.0f) hSquared = 0.0f;
    const float h = std::sqrt(hSquared);

    const float midX = (start.x + end.x) * 0.5f;
    const float midY = (start.y + end.y) * 0.5f;

    // Unit perpendicular to the chord. Either sign gives a valid circle through
    // both endpoints; which one is wanted is decided below by measuring the sweep
    // each produces, rather than by a table of cases.
    const float ux = dx / d;
    const float uy = dy / d;
    const float px = -uy;
    const float py = ux;

    const float twoPi = 6.28318530718f;
    const float pi = 3.14159265359f;

    // Both candidates are computed and then compared, because the relationship
    // between the perpendicular's sign and the large-arc flag depends on the
    // chord's direction -- so a hard-coded sign is right for half the inputs. The
    // test asserts all four (large/small x each winding) for exactly this reason.
    for (int side = 0; side < 2; ++side) {
        const float sign = side == 0 ? 1.0f : -1.0f;
        const float cx = midX + sign * h * px;
        const float cy = midY + sign * h * py;

        const float a0 = std::atan2(start.y - cy, start.x - cx);
        const float a1 = std::atan2(end.y - cy, end.x - cx);

        // Normalised into (0, 2pi): increasing angle is clockwise on screen, so
        // this is the clockwise sweep from start to end whichever quadrant pair
        // they fall in.
        float sweep = a1 - a0;
        while (sweep <= 0.0f) sweep += twoPi;
        while (sweep > twoPi) sweep -= twoPi;

        if ((sweep > pi) == largeArc) {
            *center = PointF{cx, cy};
            *startAngle = a0;
            *sweepAngle = sweep;
            return true;
        }
    }

    // Unreachable for a well-formed input: the two candidates are supplementary,
    // so one of them always satisfies the largeArc test. It IS reachable when
    // h == 0 (the clamped r = d/2 case), where both candidates are the same
    // semicircle with sweep == pi and neither test passes -- so the semicircle is
    // handed back explicitly rather than reported as no arc at all.
    const float a0 = std::atan2(start.y - midY, start.x - midX);
    *center = PointF{midX, midY};
    *startAngle = a0;
    *sweepAngle = pi;
    return true;
}

// --- Transform helpers -------------------------------------------------------
//
// These mirror D2D1::Matrix3x2F::Identity/Translation/Scale, which is what the 17
// SetTransform sites currently call. Named as free functions rather than static
// members of Matrix3x2F because a static member function would be fine but an
// aggregate with member functions is a step towards the constructor this file
// forbids, and the free-function spelling keeps the rename mechanical:
// `D2D1::Matrix3x2F::Translation(` -> `tinta::matrixTranslation(`.
//
// Deliberately NOT provided: Rotation and Skew. No call site uses them, and the
// pattern this file already records is that measuring removes methods -- an
// unused rotation helper would be untested code carrying a sign convention that
// is easy to get wrong in exactly the way nothing would notice.

inline Matrix3x2F matrixIdentity() {
    return Matrix3x2F{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
}

inline Matrix3x2F matrixTranslation(float dx, float dy) {
    return Matrix3x2F{1.0f, 0.0f, 0.0f, 1.0f, dx, dy};
}

inline Matrix3x2F matrixScale(float scaleX, float scaleY) {
    return Matrix3x2F{scaleX, 0.0f, 0.0f, scaleY, 0.0f, 0.0f};
}

// Scale about a CENTRE that stays put, mirroring D2D1::Matrix3x2F::Scale's
// three-argument overload. Added under the rule this section already states -- the
// site that needs it lands in the same commit -- and that site is the print path's
// shrink band (src/print.cpp), which scales an oversized block about its band's
// top-left corner.
//
// WRITTEN IN D2D'S CLOSED FORM, NOT AS A COMPOSITION, and the difference is
// load-bearing. The obvious spelling is
// `matrixTranslation(-cx, -cy) * matrixScale(s, s) * matrixTranslation(cx, cy)`,
// which is algebraically the same matrix. This is the closed form instead because
// the call site's whole claim is that the printed page does not change by one byte,
// and a composition reaches the same fields through six multiplies and four adds in
// operator* rather than through one multiply and one subtract. Those agree exactly
// for this pattern of zeros and ones -- but "exactly" would then be a floating-point
// argument a reader has to redo, where the closed form is the same arithmetic
// Direct2D itself performs.
//
// The equality with D2D's own helper is ASSERTED rather than asserted-in-prose:
// tests/geometry_tests.cpp builds both and compares all six fields, which is the
// same two-independent-constructions instrument the layout static_asserts below use.
inline Matrix3x2F matrixScale(float scaleX, float scaleY, PointF center) {
    return Matrix3x2F{scaleX, 0.0f, 0.0f, scaleY,
                      center.x - scaleX * center.x,
                      center.y - scaleY * center.y};
}

// Row-vector composition: `a * b` applies a FIRST, then b.
//
// MEASURED against Qt rather than inferred from the member names, because both
// libraries spell their members m11/m12/m21/m22/dx/dy and that similarity is
// exactly what makes a wrong assumption here invisible. Qt 6.4.2, mapping the
// point (1,1): translate(10,0) alone -> (11,1); scale(2,2) alone -> (2,2);
// T * S -> (22,2); and the control S * T -> (12,2), a DIFFERENT answer, so the
// pair is non-commutative and the agreement is evidence rather than luck. (22,2)
// is what row-vector `p * T * S` predicts, so Qt and Direct2D compose in the same
// order and neither backend needs an operand swap.
inline Matrix3x2F operator*(const Matrix3x2F& a, const Matrix3x2F& b) {
    return Matrix3x2F{a.m11 * b.m11 + a.m12 * b.m21,
                      a.m11 * b.m12 + a.m12 * b.m22,
                      a.m21 * b.m11 + a.m22 * b.m21,
                      a.m21 * b.m12 + a.m22 * b.m22,
                      a.dx * b.m11 + a.dy * b.m21 + b.dx,
                      a.dx * b.m12 + a.dy * b.m22 + b.dy};
}


}  // namespace tinta

#endif  // TINTA_GEOMETRY_H
