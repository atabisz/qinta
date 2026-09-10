#ifndef TINTA_GEOMETRY_H
#define TINTA_GEOMETRY_H

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

// The Direct2D include is at the top rather than in a trailing interop section
// because the conversion operators below have to be MEMBERS to be implicit, and
// a member operator needs its target type complete at the point of definition.
// Free functions would have been tidier but would not let a swept call site keep
// working unchanged, which is the whole point.
//
// The guard is _WIN32 rather than a "Direct2D backend is selected" build option
// because today those are the same condition -- Windows means Direct2D. When a
// second backend is built for Windows (Qt is the chosen direction as of
// 2026-09-10) this becomes an explicit option and the include goes with it.
//
// Note the cost while it is here: on Windows this header pulls <d2d1.h>, so
// `cl /showIncludes` on a file that only wants the value types still reports the
// Direct2D closure. That shortcut is lost; the instrument that actually settles
// portability is the non-Windows build (scripts/probe-portable-linux.sh), which
// compiles this file with the whole #ifdef switched off.
#ifdef _WIN32
#include <d2d1.h>
#endif

namespace tinta {

// Member names and declaration order below are taken from the Windows SDK
// headers, not from memory, so that the static_asserts further down are
// asserting something real:
//   D2D_COLOR_F   = D3DCOLORVALUE  {float r, g, b, a}            (d3d9types.h)
//   D2D_RECT_F    {FLOAT left, top, right, bottom}               (um/dcommon.h)
//   D2D_POINT_2F  {FLOAT x, y}                                   (um/dcommon.h)
//   D2D_SIZE_F    {FLOAT width, height}                          (um/dcommon.h)
//   D2D1_ROUNDED_RECT {D2D1_RECT_F rect; FLOAT radiusX, radiusY}    (um/d2d1.h)
//   D2D1_ELLIPSE      {D2D1_POINT_2F point; FLOAT radiusX, radiusY} (um/d2d1.h)
//
// These stay AGGREGATES: no constructors, no base classes, no virtuals, no
// private members. That keeps `tinta::Color c{1, 1, 1, 1}` and `tinta::RectF r{}`
// working, keeps them trivially copyable, and keeps offsetof well defined so the
// layout claim below means something. A conversion operator is a member function,
// which is allowed; adding a CONSTRUCTOR is not -- it would break every braced
// initialisation in the tree at once.

struct Color {
    float r, g, b, a;
#ifdef _WIN32
    operator D2D1_COLOR_F() const { return D2D1_COLOR_F{r, g, b, a}; }
#endif
};

struct PointF {
    float x, y;
#ifdef _WIN32
    operator D2D1_POINT_2F() const { return D2D1_POINT_2F{x, y}; }
#endif
};

struct SizeF {
    float width, height;
#ifdef _WIN32
    operator D2D1_SIZE_F() const { return D2D1_SIZE_F{width, height}; }
#endif
};

struct RectF {
    float left, top, right, bottom;
#ifdef _WIN32
    operator D2D1_RECT_F() const { return D2D1_RECT_F{left, top, right, bottom}; }
#endif
};

struct RoundedRect {
    RectF rect;
    float radiusX, radiusY;
#ifdef _WIN32
    operator D2D1_ROUNDED_RECT() const { return D2D1_ROUNDED_RECT{rect, radiusX, radiusY}; }
#endif
};

struct Ellipse {
    PointF point;
    float radiusX, radiusY;
#ifdef _WIN32
    operator D2D1_ELLIPSE() const { return D2D1_ELLIPSE{point, radiusX, radiusY}; }
#endif
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

#ifdef _WIN32
// --- Layout claim ------------------------------------------------------------
//
// The conversion operators above copy field by field, so they are correct even
// if the layouts diverge. These assertions are here for the OTHER direction:
// they are what makes it safe for a caller to hand a tinta value to a Direct2D
// API expecting a contiguous array or a pointer-to-struct, and they turn a
// silent reinterpretation into a build failure if a member is ever reordered or
// a type widened. They cost nothing at runtime and they are checked on every
// Windows compile of every file that includes this header.

static_assert(sizeof(Color) == sizeof(D2D1_COLOR_F), "tinta::Color must match D2D1_COLOR_F");
static_assert(offsetof(Color, r) == offsetof(D2D1_COLOR_F, r), "Color::r offset");
static_assert(offsetof(Color, g) == offsetof(D2D1_COLOR_F, g), "Color::g offset");
static_assert(offsetof(Color, b) == offsetof(D2D1_COLOR_F, b), "Color::b offset");
static_assert(offsetof(Color, a) == offsetof(D2D1_COLOR_F, a), "Color::a offset");

static_assert(sizeof(PointF) == sizeof(D2D1_POINT_2F), "tinta::PointF must match D2D1_POINT_2F");
static_assert(offsetof(PointF, x) == offsetof(D2D1_POINT_2F, x), "PointF::x offset");
static_assert(offsetof(PointF, y) == offsetof(D2D1_POINT_2F, y), "PointF::y offset");

static_assert(sizeof(SizeF) == sizeof(D2D1_SIZE_F), "tinta::SizeF must match D2D1_SIZE_F");
static_assert(offsetof(SizeF, width) == offsetof(D2D1_SIZE_F, width), "SizeF::width offset");
static_assert(offsetof(SizeF, height) == offsetof(D2D1_SIZE_F, height), "SizeF::height offset");

static_assert(sizeof(RectF) == sizeof(D2D1_RECT_F), "tinta::RectF must match D2D1_RECT_F");
static_assert(offsetof(RectF, left) == offsetof(D2D1_RECT_F, left), "RectF::left offset");
static_assert(offsetof(RectF, top) == offsetof(D2D1_RECT_F, top), "RectF::top offset");
static_assert(offsetof(RectF, right) == offsetof(D2D1_RECT_F, right), "RectF::right offset");
static_assert(offsetof(RectF, bottom) == offsetof(D2D1_RECT_F, bottom), "RectF::bottom offset");

static_assert(sizeof(RoundedRect) == sizeof(D2D1_ROUNDED_RECT), "tinta::RoundedRect must match D2D1_ROUNDED_RECT");
static_assert(offsetof(RoundedRect, rect) == offsetof(D2D1_ROUNDED_RECT, rect), "RoundedRect::rect offset");
static_assert(offsetof(RoundedRect, radiusX) == offsetof(D2D1_ROUNDED_RECT, radiusX), "RoundedRect::radiusX offset");
static_assert(offsetof(RoundedRect, radiusY) == offsetof(D2D1_ROUNDED_RECT, radiusY), "RoundedRect::radiusY offset");

static_assert(sizeof(Ellipse) == sizeof(D2D1_ELLIPSE), "tinta::Ellipse must match D2D1_ELLIPSE");
static_assert(offsetof(Ellipse, point) == offsetof(D2D1_ELLIPSE, point), "Ellipse::point offset");
static_assert(offsetof(Ellipse, radiusX) == offsetof(D2D1_ELLIPSE, radiusX), "Ellipse::radiusX offset");
static_assert(offsetof(Ellipse, radiusY) == offsetof(D2D1_ELLIPSE, radiusY), "Ellipse::radiusY offset");

// Direct2D -> tinta, for the places that read a value back OUT of a Direct2D
// object (GetSize, GetColor and similar). This direction cannot be a conversion
// operator -- it would have to be a converting constructor on the tinta type,
// which would stop it being an aggregate -- so it is an overload set instead.
// Copies field by field rather than reinterpret_cast'ing, even though the
// assertions above would permit the cast, because a copy needs no aliasing
// argument to be correct.
inline Color fromD2D(const D2D1_COLOR_F& c) { return Color{c.r, c.g, c.b, c.a}; }
inline PointF fromD2D(const D2D1_POINT_2F& p) { return PointF{p.x, p.y}; }
inline SizeF fromD2D(const D2D1_SIZE_F& s) { return SizeF{s.width, s.height}; }
inline RectF fromD2D(const D2D1_RECT_F& r) { return RectF{r.left, r.top, r.right, r.bottom}; }

// The one place the layout assertions above are load-bearing rather than
// belt-and-braces. A conversion operator converts one value; it cannot convert
// an ARRAY, and ID2D1SimplifiedGeometrySink::AddLines takes a pointer to a run
// of contiguous points. Copying the run would work but would allocate inside a
// paint path, so the array is reinterpreted instead -- valid here precisely
// because sizeof and every offsetof are asserted equal a few lines up.
//
// This helper exists so that dependency is named in ONE place. Do not spell the
// reinterpret_cast at call sites; two of them (src/print.cpp, src/main_d2d.cpp)
// is already the entire population, and a cast written inline reads like a
// workaround rather than like something the assertions license.
inline const D2D1_POINT_2F* asD2DPointArray(const PointF* points) {
    return reinterpret_cast<const D2D1_POINT_2F*>(points);
}
#endif  // _WIN32

}  // namespace tinta

#endif  // TINTA_GEOMETRY_H
