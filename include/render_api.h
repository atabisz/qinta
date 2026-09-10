#ifndef TINTA_RENDER_API_H
#define TINTA_RENDER_API_H

#include "geometry.h"

// The drawing seam: what a rendering backend has to provide, with no backend in it.
//
// WHY THIS SHAPE, AND WHY THESE METHOD NAMES
//
// Measured on 2026-09-11, over src+include: there are 116 distinct COM methods
// called through 1,836 sites (`rg '->[A-Z][A-Za-z0-9_]*\('`, whose PascalCase
// pattern is a reliable discriminator here because tinta's own member functions
// are camelCase -- the whole tree has 48 camelCase `->` calls, several of them
// std::). That surface is extremely top-heavy:
//
//   SetColor 474 · Release 271 · FillRoundedRectangle 131 · DrawLine 111
//   DrawText 106 · DrawRoundedRectangle 70 · FillRectangle 60 · GetMetrics 40
//   CreateTextFormat 37 · CreateTextLayout 36        -- 73% of all sites
//
// So the number that matters for the port is 116 methods, not 1,836 sites, and
// 271 of those sites are Release(): COM lifetime management that a portable
// backend DELETES rather than reimplements.
//
// The method names here are deliberately the Direct2D ones, PascalCase and all.
// That is not deference to Windows, it is what makes the port mechanical: the
// 0e54418 geometry sweep already replaced every argument type with a portable
// one, so a call site today reads
//
//     app.renderTarget->FillRoundedRectangle(tinta::roundedRect(rect, 2, 2), app.brush);
//
// where the ONLY non-portable things left are the receiver and the brush. Give
// those two portable types with matching signatures and roughly 915 sites compile
// unchanged -- half the total surface -- with no diff to review and no chance of
// a transcription error in a hand-edited paint path. Renaming to a Qt-flavoured
// or house-flavoured API is a later, separable, purely cosmetic pass; doing it in
// the same step as introducing a second backend would mean a 915-site diff in
// which a real behavioural change could hide.
//
// WHAT IS DELIBERATELY NOT HERE
//
//   * Text. DrawText and DrawTextLayout need a TextFormat carrying font,
//     alignment, wrap mode and trimming (SetTextAlignment 35, GetFontSize 24,
//     SetParagraphAlignment 24, SetWordWrapping 19, SetTrimming 7). The Qt side
//     of that contract is already measured in tests/qt_text_tests.cpp, but it is
//     its own slice and does not belong in a header about geometry.
//   * Object creation and device lifetime. BeginDraw/EndDraw, CreateSolidColorBrush,
//     resize and device-loss handling are backend-specific in kind, not just in
//     implementation: D2D's EndDraw returns an HRESULT that callers inspect, and
//     a portable spelling of that is a design question rather than a rename. A
//     concrete backend owns its own construction; this interface is about the
//     drawing calls only.
//   * SetTransform/GetTransform (17 + 6 sites). They need a 3x2 matrix type that
//     geometry.h does not have yet.
//
// Naming stays qualified as tinta::, for the reason geometry.h gives: bare Color
// is already ambiguous across most of src/.

namespace tinta {

// Values match D2D1_ANTIALIAS_MODE so the Windows backend can cast rather than
// switch, and so the 9 call sites that pass D2D1_ANTIALIAS_MODE_ALIASED today
// keep meaning exactly what they meant. render_d2d will static_assert the pairing
// the way geometry.h asserts its layouts -- an assertion cannot live here,
// because this header must not include <d2d1.h>.
enum class AntialiasMode {
    PerPrimitive = 0,
    Aliased = 1,
};

// An opaque handle. Dash patterns are built once at startup and only ever passed
// back to DrawLine (2 sites, both `dashed ? style : nullptr`), so nothing in the
// tree needs to READ one -- which is why this has no methods rather than a
// guessed-at accessor.
class StrokeStyle {
public:
    virtual ~StrokeStyle() = default;
};

// The 474 SetColor sites are the single largest item in the whole COM surface,
// and they are all this: recolour the one scratch brush, then draw. GetColor
// exists because a backend that is handed a Brush* has to be able to ask what
// colour it is; Direct2D reads it back the same way.
class Brush {
public:
    virtual ~Brush() = default;
    virtual void SetColor(const Color& color) = 0;
    virtual Color GetColor() const = 0;
};

// Every drawing call tinta makes that is not text.
//
// The defaulted strokeWidth and strokeStyle mirror Direct2D's own defaults, so
// the 3-argument DrawLine calls that make up the bulk of the 111 sites and the
// 5-argument dashed-connector calls in main_d2d.cpp and print.cpp both bind
// without edits.
class RenderTarget {
public:
    virtual ~RenderTarget() = default;

    virtual void Clear(const Color& color) = 0;

    virtual void FillRectangle(const RectF& rect, Brush* brush) = 0;
    virtual void DrawRectangle(const RectF& rect, Brush* brush,
                               float strokeWidth = 1.0f) = 0;

    virtual void FillRoundedRectangle(const RoundedRect& rect, Brush* brush) = 0;
    virtual void DrawRoundedRectangle(const RoundedRect& rect, Brush* brush,
                                      float strokeWidth = 1.0f) = 0;

    virtual void FillEllipse(const Ellipse& ellipse, Brush* brush) = 0;
    virtual void DrawEllipse(const Ellipse& ellipse, Brush* brush,
                             float strokeWidth = 1.0f) = 0;

    virtual void DrawLine(PointF start, PointF end, Brush* brush,
                          float strokeWidth = 1.0f,
                          StrokeStyle* strokeStyle = nullptr) = 0;

    // Clips nest, and every push in the tree is matched by a pop in the same
    // function, so a backend may implement these with a save/restore stack.
    virtual void PushAxisAlignedClip(const RectF& rect, AntialiasMode mode) = 0;
    virtual void PopAxisAlignedClip() = 0;
};

}  // namespace tinta

#endif  // TINTA_RENDER_API_H
