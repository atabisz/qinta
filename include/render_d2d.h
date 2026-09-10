#ifndef TINTA_RENDER_D2D_H
#define TINTA_RENDER_D2D_H

#ifndef _WIN32
#error "render_d2d.h is the Windows backend; off Windows use render_qt.h."
#endif

// Before render_api.h, not for tidiness: that header hard-errors without the
// DrawText macro in scope, because a TU that declares RenderTarget::DrawText while
// its neighbours declare RenderTarget::DrawTextW has an ODR violation the linker
// need not diagnose. render_api.h explains it at length.
#include <windows.h>

#include "render_api.h"
#include "text_d2d.h"

#include <d2d1.h>

// The Direct2D backend for the drawing seam.
//
// WHY THIS EXISTS WHEN DIRECT2D IS ALREADY WHAT THE APP CALLS
//
// include/render_api.h claims that Direct2D's drawing calls are the portable
// shape rather than a Windows-specific one -- that is why it keeps their names
// and signatures, and why roughly 915 call sites are expected to compile
// unchanged against it. Until this file existed the seam had exactly ONE
// implementation, and an interface with one implementation is not an abstraction,
// it is indirection: nothing had checked that D2D itself fits back through it.
// Every mismatch would have surfaced later, during the rewire, mixed in with a
// 915-site diff.
//
// So this backend is not preparation for the port -- it is the test of the seam's
// central claim, and it is deliberately written second, against an interface
// that was shaped by a non-Windows backend.
//
// OWNERSHIP IS BORROWED, WHICH IS THE OPPOSITE OF THE QT BACKEND
//
// App already owns its ID2D1HwndRenderTarget and its scratch ID2D1SolidColorBrush
// and already Releases them (271 Release() sites in the tree are exactly this
// work). So these wrappers BORROW: they neither AddRef nor Release, and they must
// not outlive what they wrap. QtRenderTarget goes the other way and begins a
// painting session in its constructor, because Qt's QPainter has no separate
// device object to hold.
//
// For the same reason this class does NOT call BeginDraw or EndDraw. Device
// lifetime is deliberately outside the seam (render_api.h says why: EndDraw
// returns an HRESULT that callers inspect), and main_d2d.cpp already owns that
// pair. A wrapper that quietly began its own frame would nest inside the real one.

// Values chosen to match, asserted in render_d2d.cpp where <d2d1.h> is available.
// render_api.h cannot make this assertion itself without including d2d1.h, which
// would defeat the entire point of the header.

class D2DStrokeStyle final : public tinta::StrokeStyle {
public:
    explicit D2DStrokeStyle(ID2D1StrokeStyle* style) : style_(style) {}
    ID2D1StrokeStyle* native() const { return style_; }

private:
    ID2D1StrokeStyle* style_;  // borrowed
};

class D2DBrush final : public tinta::Brush {
public:
    explicit D2DBrush(ID2D1SolidColorBrush* brush) : brush_(brush) {}

    void SetColor(const tinta::Color& color) override;
    tinta::Color GetColor() const override;

    ID2D1SolidColorBrush* native() const { return brush_; }

private:
    ID2D1SolidColorBrush* brush_;  // borrowed
};

class D2DRenderTarget final : public tinta::RenderTarget {
public:
    explicit D2DRenderTarget(ID2D1RenderTarget* target) : target_(target) {}

    void Clear(const tinta::Color& color) override;

    void FillRectangle(const tinta::RectF& rect, tinta::Brush* brush) override;
    void DrawRectangle(const tinta::RectF& rect, tinta::Brush* brush,
                       float strokeWidth) override;

    void FillRoundedRectangle(const tinta::RoundedRect& rect, tinta::Brush* brush) override;
    void DrawRoundedRectangle(const tinta::RoundedRect& rect, tinta::Brush* brush,
                              float strokeWidth) override;

    void FillEllipse(const tinta::Ellipse& ellipse, tinta::Brush* brush) override;
    void DrawEllipse(const tinta::Ellipse& ellipse, tinta::Brush* brush,
                     float strokeWidth) override;

    void DrawLine(tinta::PointF start, tinta::PointF end, tinta::Brush* brush,
                  float strokeWidth, tinta::StrokeStyle* strokeStyle) override;

    // Reads as DrawTextW here, because <windows.h> above has already rewritten the
    // token -- in this header, in render_api.h, and at all 107 call sites alike.
    void DrawText(const wchar_t* string, uint32_t stringLength, tinta::TextFormat* format,
                  const tinta::RectF& layoutRect, tinta::Brush* brush,
                  tinta::DrawTextOptions options) override;
    void DrawTextLayout(tinta::PointF origin, tinta::TextLayout* layout,
                        tinta::Brush* brush, tinta::DrawTextOptions options) override;

    void PushAxisAlignedClip(const tinta::RectF& rect, tinta::AntialiasMode mode) override;
    void PopAxisAlignedClip() override;

    ID2D1RenderTarget* native() const { return target_; }

private:
    ID2D1RenderTarget* target_;  // borrowed
};

#endif  // TINTA_RENDER_D2D_H
