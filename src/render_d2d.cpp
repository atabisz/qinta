// The guard that used to sit in render_d2d.h. It is here rather than there
// because this is the file that cannot exist off Windows: the header only
// declares wrappers around forward-declared interfaces, while every line below
// calls Direct2D. tests/portable-sources.txt asserts this file is never in the
// portable build; this makes the same statement to the compiler.
#ifndef _WIN32
#error "render_d2d.cpp is the Windows backend; off Windows build render_qt.cpp."
#endif

#include "render_d2d.h"

#include <cassert>

// See include/render_d2d.h for why this backend exists (it is the test of the
// seam's central claim, not preparation for it) and why it borrows rather than
// owns. Everything here is forwarding; nothing decides policy.

// THE PAIRING render_api.h PROMISED. It says the AntialiasMode values match
// D2D1_ANTIALIAS_MODE "so the Windows backend can cast rather than switch", and
// that the assertion has to live here because render_api.h must not include
// <d2d1.h>. This is that assertion. BOTH values are live at the 9
// PushAxisAlignedClip sites -- 5 aliased, 4 per-primitive -- so a drift in either
// direction is a visual change with no compile error anywhere: aliased clips would
// start antialiasing instead of snapping to pixels, or per-primitive clips would
// start snapping.
static_assert(static_cast<int>(tinta::AntialiasMode::PerPrimitive) ==
                  static_cast<int>(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE),
              "tinta::AntialiasMode::PerPrimitive must match D2D1_ANTIALIAS_MODE_PER_PRIMITIVE");
static_assert(static_cast<int>(tinta::AntialiasMode::Aliased) ==
                  static_cast<int>(D2D1_ANTIALIAS_MODE_ALIASED),
              "tinta::AntialiasMode::Aliased must match D2D1_ANTIALIAS_MODE_ALIASED");

// The same pairing for the bitmap interpolation mode, which the DrawBitmap
// forwarder below casts rather than switches on. Nothing in the tree passes
// NearestNeighbor today -- all 3 sites that name a mode name Linear -- so this
// assertion is the ONLY thing standing between a renumbered D2D enum and every
// scaled bitmap in the app silently changing filter.
static_assert(static_cast<int>(tinta::BitmapInterpolationMode::NearestNeighbor) ==
                  static_cast<int>(D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),
              "tinta::BitmapInterpolationMode::NearestNeighbor must match "
              "D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR");
static_assert(static_cast<int>(tinta::BitmapInterpolationMode::Linear) ==
                  static_cast<int>(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR),
              "tinta::BitmapInterpolationMode::Linear must match "
              "D2D1_BITMAP_INTERPOLATION_MODE_LINEAR");

// The same pairing for the text options, which live here rather than in
// text_d2d.cpp because D2D1_DRAW_TEXT_OPTIONS is declared in <d2d1.h> and not in
// <dwrite.h>: the flags belong to the render target that draws the text, not to
// the font machinery that shapes it. EnableColorFont is the only value any call
// site passes (src/main_d2d.cpp and src/math_render.cpp, one each), and it is the
// one whose silent loss would be hardest to spot -- emoji would simply render
// monochrome.
static_assert(static_cast<int>(tinta::DrawTextOptions::None) ==
                  static_cast<int>(D2D1_DRAW_TEXT_OPTIONS_NONE),
              "tinta::DrawTextOptions::None must match D2D1_DRAW_TEXT_OPTIONS_NONE");
static_assert(static_cast<int>(tinta::DrawTextOptions::NoSnap) ==
                  static_cast<int>(D2D1_DRAW_TEXT_OPTIONS_NO_SNAP),
              "tinta::DrawTextOptions::NoSnap must match D2D1_DRAW_TEXT_OPTIONS_NO_SNAP");
static_assert(static_cast<int>(tinta::DrawTextOptions::Clip) ==
                  static_cast<int>(D2D1_DRAW_TEXT_OPTIONS_CLIP),
              "tinta::DrawTextOptions::Clip must match D2D1_DRAW_TEXT_OPTIONS_CLIP");
static_assert(static_cast<int>(tinta::DrawTextOptions::EnableColorFont) ==
                  static_cast<int>(D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT),
              "tinta::DrawTextOptions::EnableColorFont must match "
              "D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT");

namespace {

// Downcasting is the standing cost of an interface that passes its own Brush type
// through: the seam cannot name ID2D1Brush, and this backend cannot draw without
// one. Both backends could be present in one binary on a Windows machine with Qt
// installed, and handing a QtBrush to a D2DRenderTarget would compile and then be
// undefined at runtime -- so the debug build pays for a dynamic_cast to turn that
// into a caught error rather than a wrong picture.
ID2D1Brush* nativeBrush(tinta::Brush* brush) {
    if (!brush) return nullptr;
    assert(dynamic_cast<D2DBrush*>(brush) != nullptr &&
           "a non-Direct2D Brush reached D2DRenderTarget");
    return static_cast<D2DBrush*>(brush)->native();
}

ID2D1StrokeStyle* nativeStrokeStyle(tinta::StrokeStyle* style) {
    if (!style) return nullptr;
    assert(dynamic_cast<D2DStrokeStyle*>(style) != nullptr &&
           "a non-Direct2D StrokeStyle reached D2DRenderTarget");
    return static_cast<D2DStrokeStyle*>(style)->native();
}

ID2D1Bitmap* nativeBitmap(tinta::Bitmap* bitmap) {
    if (!bitmap) return nullptr;
    assert(dynamic_cast<D2DBitmap*>(bitmap) != nullptr &&
           "a non-Direct2D Bitmap reached D2DRenderTarget");
    return static_cast<D2DBitmap*>(bitmap)->native();
}

}  // namespace

tinta::SizeF D2DBitmap::GetSize() const {
    return tinta::fromD2D(bitmap_->GetSize());
}

void D2DBrush::SetColor(const tinta::Color& color) {
    // geometry.h's implicit operator D2D1_COLOR_F() is what makes this a
    // forwarding call rather than a conversion, and it is layout-asserted there.
    brush_->SetColor(color);
}

tinta::Color D2DBrush::GetColor() const {
    return tinta::fromD2D(brush_->GetColor());
}

void D2DRenderTarget::Clear(const tinta::Color& color) {
    target_->Clear(color);
}

void D2DRenderTarget::FillRectangle(const tinta::RectF& rect, tinta::Brush* brush) {
    target_->FillRectangle(rect, nativeBrush(brush));
}

void D2DRenderTarget::DrawRectangle(const tinta::RectF& rect, tinta::Brush* brush,
                                    float strokeWidth) {
    target_->DrawRectangle(rect, nativeBrush(brush), strokeWidth);
}

void D2DRenderTarget::FillRoundedRectangle(const tinta::RoundedRect& rect,
                                           tinta::Brush* brush) {
    target_->FillRoundedRectangle(rect, nativeBrush(brush));
}

void D2DRenderTarget::DrawRoundedRectangle(const tinta::RoundedRect& rect,
                                           tinta::Brush* brush, float strokeWidth) {
    target_->DrawRoundedRectangle(rect, nativeBrush(brush), strokeWidth);
}

void D2DRenderTarget::FillEllipse(const tinta::Ellipse& ellipse, tinta::Brush* brush) {
    target_->FillEllipse(ellipse, nativeBrush(brush));
}

void D2DRenderTarget::DrawEllipse(const tinta::Ellipse& ellipse, tinta::Brush* brush,
                                  float strokeWidth) {
    target_->DrawEllipse(ellipse, nativeBrush(brush), strokeWidth);
}

void D2DRenderTarget::DrawLine(tinta::PointF start, tinta::PointF end,
                               tinta::Brush* brush, float strokeWidth,
                               tinta::StrokeStyle* strokeStyle) {
    target_->DrawLine(start, end, nativeBrush(brush), strokeWidth,
                      nativeStrokeStyle(strokeStyle));
}

void D2DRenderTarget::DrawText(const wchar_t* string, uint32_t stringLength,
                               tinta::TextFormat* format, const tinta::RectF& layoutRect,
                               tinta::Brush* brush, tinta::DrawTextOptions options) {
    IDWriteTextFormat* nativeFormat = nativeTextFormat(format);
    if (!nativeFormat) return;

    // DWRITE_MEASURING_MODE_NATURAL is Direct2D's own default for this overload and
    // no call site passes anything else, which is why the seam has no parameter for
    // it. Spelled out rather than defaulted so that the day someone needs GDI
    // metrics, the place to put them is visible.
    target_->DrawText(string, stringLength, nativeFormat, layoutRect,
                      nativeBrush(brush),
                      static_cast<D2D1_DRAW_TEXT_OPTIONS>(options),
                      DWRITE_MEASURING_MODE_NATURAL);
}

void D2DRenderTarget::DrawTextLayout(tinta::PointF origin, tinta::TextLayout* layout,
                                     tinta::Brush* brush, tinta::DrawTextOptions options) {
    IDWriteTextLayout* nativeLayout = nativeTextLayout(layout);
    if (!nativeLayout) return;
    target_->DrawTextLayout(origin, nativeLayout, nativeBrush(brush),
                            static_cast<D2D1_DRAW_TEXT_OPTIONS>(options));
}

void D2DRenderTarget::DrawBitmap(tinta::Bitmap* bitmap, const tinta::RectF& destRect,
                                 float opacity,
                                 tinta::BitmapInterpolationMode interpolationMode,
                                 const tinta::RectF* srcRect) {
    ID2D1Bitmap* native = nativeBitmap(bitmap);
    if (!native) return;

    // D2D takes destRect by POINTER and treats null as "the whole target", which is
    // not a state the seam offers -- every one of the 7 sites passes a rectangle. So
    // the address of the reference goes in, and the implicit
    // tinta::RectF -> D2D1_RECT_F conversion cannot be used here for the same reason
    // it cannot be used for srcRect: it produces a temporary, and taking the address
    // of one would hand D2D a pointer into a dead object. Hence the named locals.
    const D2D1_RECT_F dest = destRect;
    const D2D1_RECT_F source = srcRect ? static_cast<D2D1_RECT_F>(*srcRect) : D2D1_RECT_F{};
    target_->DrawBitmap(native, &dest, opacity,
                        static_cast<D2D1_BITMAP_INTERPOLATION_MODE>(interpolationMode),
                        srcRect ? &source : nullptr);
}

void D2DRenderTarget::PushAxisAlignedClip(const tinta::RectF& rect,
                                          tinta::AntialiasMode mode) {
    // The cast the static_asserts above license. A switch here would be dead code
    // that hid the coupling instead of stating it.
    target_->PushAxisAlignedClip(rect, static_cast<D2D1_ANTIALIAS_MODE>(mode));
}

void D2DRenderTarget::PopAxisAlignedClip() {
    target_->PopAxisAlignedClip();
}

void D2DRenderTarget::SetTransform(const tinta::Matrix3x2F& transform) {
    // The implicit conversion operator on tinta::Matrix3x2F, copying field by
    // field. The layout static_asserts in geometry.h would license a cast instead,
    // and a copy still beats one for the reason that file gives.
    target_->SetTransform(transform);
}

void D2DRenderTarget::GetTransform(tinta::Matrix3x2F* transform) {
    if (!transform) return;
    D2D1_MATRIX_3X2_F current;
    target_->GetTransform(&current);
    *transform = tinta::fromD2D(current);
}
