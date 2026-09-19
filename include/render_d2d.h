#ifndef TINTA_RENDER_D2D_H
#define TINTA_RENDER_D2D_H

// THIS HEADER IS DECLARATION-ONLY OFF WINDOWS, AND THE GUARD MOVED TO THE .CPP
// TO SAY SO PRECISELY.
//
// It used to `#error` here, on the grounds that off Windows you want
// render_qt.h. That was aimed at the right mistake but caught the wrong event:
// app.h embeds D2DRenderTarget, D2DBrush, D2DStrokeStyle and D2DBitmap BY VALUE,
// so a non-Windows compiler cannot even declare App without this header, and the
// guard was gating a *declaration* rather than a *use*.
//
// Nothing here can be used off Windows, and that is a measured property rather
// than a promise: every wrapper in this file only STORES a native pointer, and
// there are ZERO inline dereferences of one -- every method that touches Direct2D
// is out-of-line in render_d2d.cpp, which is where the #error now lives. So off
// Windows the classes below declare cleanly against forward declarations and the
// first attempt to actually build the backend still fails loudly.
#ifdef _WIN32
// Before render_api.h, not for tidiness: that header hard-errors without the
// DrawText macro in scope, because a TU that declares RenderTarget::DrawText while
// its neighbours declare RenderTarget::DrawTextW has an ODR violation the linker
// need not diagnose. render_api.h explains it at length.
#include <windows.h>
#endif

#include "render_api.h"
#include "text_d2d.h"

#ifdef _WIN32
#include <d2d1.h>
#else
// The SIX Direct2D interfaces this header names, and it names them ONLY as
// pointers -- 0 by value. A forward declaration is therefore sufficient, and
// deliberately leaves them incomplete so that any inline use added later fails here
// instead of compiling into a portable build. The 0-by-value half is the property
// this block depends on; the occurrence count next to it never was.
//
// THE COUNT IS GONE, ON ITS THIRD STALENESS. This read "four"/"12", then "16", and at
// the time of writing measures 28 live occurrences -- so the sentence that already
// explained how such a number goes stale went stale itself, one paragraph below its own
// explanation. The paragraph was right and insufficient: naming the failure mode does
// not re-measure the figure. Neither does remembering it, which is why the sibling
// block in include/app.h now carries the command instead of a number too:
//
//   rg -o "ID2D1RenderTarget|ID2D1SolidColorBrush|ID2D1StrokeStyle|ID2D1Bitmap|\
//   ID2D1PathGeometry|ID2D1GeometrySink" include/render_d2d.h | wc -l
//
// with `| grep -vE "^[0-9]+: *//"` on an `rg -n` if the comments are to be excluded --
// and note the two conventions differ by more than the six `struct` lines here, which
// is most of why the old figure could not be checked at all.
//
// The path pair is still worth its sentence: ID2D1PathGeometry and ID2D1GeometrySink
// are why D2DPathGeometry's destructor is OUT OF LINE -- releasing an incomplete type
// is exactly the inline use this forward declaration exists to reject.
//
// ID2D1RenderTarget IS NO LONGER DECLARED FOR SOMEONE ELSE, AND THAT SENTENCE IS RETIRED
// RATHER THAN DELETED because it was true, acted on, and is now the reverse. It read:
// "include/app.h names it live and does NOT declare it, relying on this block via its own
// include of this header", and therefore that deleting this line on the grounds that
// D2DRenderTarget went away would break a header that never mentioned the change. app.h
// declares it itself now -- its own #else block adopted the rule "this block declares what
// this header names" for exactly that reason, so neither header leans on the other and
// this line is load-bearing only inside this file again.
//
// ID2D1Bitmap NOW SITS IN THE OPPOSITE POSITION, AND IT IS WORTH KNOWING BEFORE TOUCHING
// IT. app.h stopped naming it when the link peek became a shared_ptr<tinta::Bitmap>, and
// its declaration there was removed -- so nothing outside this file needs this one. But
// app.h includes this header BEFORE it declares App, which means this line keeps that name
// in scope inside App whether app.h declares it or not. Measured, not reasoned: planting a
// live `ID2D1Bitmap*` member in App with app.h's own declaration deleted left
// scripts/probe-portable-linux.sh GREEN, because of this line. So this declaration
// silently absorbs a class of mistake in a header it does not know about, and the portable
// probe cannot attribute an ID2D1Bitmap diagnostic to either file while both declare it.
// The same plant spelled `ID2D1Bitmap1*` -- declared in no #else block anywhere -- fails
// the probe with 53 errors, which is how that green was shown to be real rather than the
// branch going uncompiled.
struct ID2D1RenderTarget;
struct ID2D1SolidColorBrush;
struct ID2D1StrokeStyle;
struct ID2D1Bitmap;
struct ID2D1PathGeometry;
struct ID2D1GeometrySink;
#endif

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

// STILL THE BORROWED KIND, and it is no longer the only kind -- see
// D2DOwnedStrokeStyle below. `final` came off for the reason D2DBitmap's did: an owning
// subclass needs to derive from this one so that nativeStrokeStyle's single
// dynamic_cast in src/render_d2d.cpp covers both kinds, and a second unwrapper for a
// second spelling is exactly the shape that lets two paths disagree.
//
// WHO STILL BORROWS ONE. Nothing in App does any more -- App's member is owning. This
// stays because the seam's DrawLine takes a tinta::StrokeStyle* it does not own, so a
// caller that already holds a native and wants to pass it through one call has a
// wrapper to do it with, and because deleting it would put the only ID2D1StrokeStyle
// wrapper in the owning class, where a borrowed native would be double-released.
class D2DStrokeStyle : public tinta::StrokeStyle {
public:
    explicit D2DStrokeStyle(ID2D1StrokeStyle* style) : style_(style) {}
    ID2D1StrokeStyle* native() const { return style_; }

    // Not final any more, so the rule of five is stated rather than inherited by luck
    // -- the same sentence D2DBitmap carries, and for the same reason: copying a
    // borrowed wrapper is harmless, copying an OWNING one would double-release, and the
    // owning subclass deletes them for itself.
    D2DStrokeStyle(const D2DStrokeStyle&) = default;
    D2DStrokeStyle& operator=(const D2DStrokeStyle&) = default;

protected:
    ID2D1StrokeStyle* style_;  // borrowed
};

// OWNS its stroke style: it came from ID2D1Factory::CreateStrokeStyle, which hands the
// caller the only reference. Derives from D2DStrokeStyle rather than reimplementing it
// so that nativeStrokeStyle's single dynamic_cast covers both kinds -- the same
// arrangement as D2DBitmap/D2DOwnedBitmap below, chosen the same way.
//
// THE DESTRUCTOR IS OUT OF LINE for the reason the forward declarations above give: off
// Windows ID2D1StrokeStyle is incomplete, and calling Release on an incomplete type
// inline here would be the exact mistake those declarations exist to catch.
//
// WHY THIS EXISTS AT ALL, which is the part worth reading. App used to hold the dashed
// stroke style as a native pointer PLUS a Seated<> storage PLUS a seam pointer, and
// commit fc9ceb0 moved CreateStrokeStyle's out-parameter from the seam spelling to the
// native one without adding the line that seated the wrapper over it. Both spellings
// compiled; the wrong one was merely never read. Every dashed mermaid connector drew
// SOLID for several commits, and because the null check was the create-once latch,
// CreateStrokeStyle re-ran on every frame with a dashed connector and overwrote the
// previous pointer with no Release -- a silent behaviour change and a per-frame COM
// leak, from a seam whose entire contract is that behaviour does not change. An owning
// wrapper installed into a single unique_ptr member leaves one spelling, so that
// particular pair cannot come apart again.
class D2DOwnedStrokeStyle final : public D2DStrokeStyle {
public:
    explicit D2DOwnedStrokeStyle(ID2D1StrokeStyle* style) : D2DStrokeStyle(style) {}
    ~D2DOwnedStrokeStyle() override;

    D2DOwnedStrokeStyle(const D2DOwnedStrokeStyle&) = delete;
    D2DOwnedStrokeStyle& operator=(const D2DOwnedStrokeStyle&) = delete;
};

// THE FOURTH OWNING WRAPPER IN THIS FILE, and this line used to read "THE ONE OWNING
// WRAPPER IN THIS FILE" -- which was already false by two before this slice touched
// anything. Measured rather than adjusted: the owning wrappers are D2DPathGeometry
// (below), D2DPathSink, D2DOwnedBitmap and now D2DOwnedStrokeStyle, against the
// borrowed D2DStrokeStyle, D2DBitmap, D2DBrush and D2DRenderTarget, off
//
//   rg -n "^class D2D|~D2D[A-Za-z]*\(\) override|// OWNED|// borrowed" include/render_d2d.h
//
// A count inside a header ages with no gate to catch it -- the figure gate reads the
// README, and nothing reads this. So the command is written beside the number, which is
// the only form of it that can be re-derived instead of trusted.
//
// The header comment above still says wrappers here BORROW, and that remains the rule
// this and the three others are stated exceptions to. Everything else here borrows
// because App already owns and already Releases the thing being wrapped. A path
// geometry has no such owner: it did not exist before
// BeginPath was called, and the only member that holds one
// (App::LayoutShape::geometry) is now a unique_ptr precisely so that destroying a
// shape destroys its path. Borrowing would leave nobody to Release it -- the two
// manual release loops that used to do that job (App::clearLayoutCache and the
// partial-rollback loop in render.cpp) are deleted in the same commit, because a
// unique_ptr member makes both of them the compiler's work.
//
// The destructor is out of line for the reason the forward declarations above give:
// off Windows ID2D1PathGeometry is incomplete, and calling Release on an incomplete
// type inline here would be the exact mistake those declarations exist to catch.
class D2DPathGeometry final : public tinta::PathGeometry {
public:
    explicit D2DPathGeometry(ID2D1PathGeometry* geometry) : geometry_(geometry) {}
    ~D2DPathGeometry() override;

    D2DPathGeometry(const D2DPathGeometry&) = delete;
    D2DPathGeometry& operator=(const D2DPathGeometry&) = delete;

    ID2D1PathGeometry* native() const { return geometry_; }

private:
    ID2D1PathGeometry* geometry_;  // OWNED -- see above
};

// Owns the sink for as long as the path is open, and the geometry until Close hands
// it over. Both are out-of-line for the incomplete-type reason above.
//
// A sink that is never Closed still has to Release both, which is why the
// destructor exists rather than trusting Close to run: three of the six builders
// this replaces have an early-return path between Open and Close.
class D2DPathSink final : public tinta::PathSink {
public:
    D2DPathSink(ID2D1PathGeometry* geometry, ID2D1GeometrySink* sink)
        : geometry_(geometry), sink_(sink) {}
    ~D2DPathSink() override;

    D2DPathSink(const D2DPathSink&) = delete;
    D2DPathSink& operator=(const D2DPathSink&) = delete;

    void BeginFigure(tinta::PointF start) override;
    void AddLine(tinta::PointF point) override;
    void AddLines(const tinta::PointF* points, uint32_t count) override;
    void AddArc(const tinta::ArcSegment& arc) override;
    void EndFigure() override;
    std::unique_ptr<tinta::PathGeometry> Close() override;

private:
    ID2D1PathGeometry* geometry_;  // OWNED until Close
    ID2D1GeometrySink* sink_;      // OWNED until Close
};

// THE TWO UNWRAPPERS THAT USED TO BE DECLARED HERE ARE GONE, AND THE FOLLOW-UP THIS
// HEADER ASKED FOR IS WHAT REMOVED THEM. `nativePathGeometry` and `nativeBitmap` are now
// file-local in src/render_d2d.cpp's anonymous namespace, beside `nativeBrush` and
// `nativeStrokeStyle`, which were never exported. The escape hatch out of the Direct2D
// backend is closed rather than merely unused, and this tombstone is left because a grep
// for either name will still land here.
//
// THE INSTRUMENT THIS HEADER NAMED WAS THE WEAKER OF THE TWO AVAILABLE, and that is the
// correction worth carrying forward. The paragraph that stood here said "un-exporting a
// symbol is proven by a link". A link proves nothing CALLS the symbol; it cannot show
// that the symbol stopped being exported, and those are different facts. The object
// file carries the fact directly -- an exported free function reads
// `External | ?nativeBitmap@@...` and a file-local one reads `Static | ?nativeBitmap@?A0x...`
// with the anonymous-namespace token -- and the half a symbol table cannot see was
// covered by planting a call in src/print.cpp, the file that used to make exactly that
// call, and requiring it to fail to compile. Both readings are in the commit.
//
// WHAT THE UN-EXPORT IS WORTH, MEASURED AND NOT ASSERTED: nothing. Both symbols had
// already dropped out of the `-DTINTA_LEGACY_D2D=OFF` unresolved-external set before
// this change, because the print retarget removed their last outside callers, so
// closing the hatch moved that inventory by zero. It is worth doing anyway -- an export
// with no caller is an invitation, and the previous note's own reason was that the
// reason had expired -- but the port's own instrument does not credit it, and saying so
// here is cheaper than someone later looking for the movement.
//
// The guarantee both still make, unchanged: they assert the handle really came from this
// backend, so a QtPathGeometry arriving at D2DRenderTarget::FillGeometry trips rather
// than drawing nothing, and `nativeBitmap` accepts an owned bitmap as well as a borrowed
// one because D2DOwnedBitmap derives from D2DBitmap so one unwrapper serves both.
//
// Contrast text_d2d.h's nativeTextLayout, which stays legitimately exported --
// src/paint_document.cpp needs it for the colour-emoji ID2D1DeviceContext path, which
// has no seam equivalent at all -- and nativeTextFormat, which after this change is the
// ONLY free function still unresolved when the Direct2D backend is excluded.

// BORROWS. The native holders of a bitmap release their own, and a wrapper that took a
// reference would make their ownership depend on whether a drawing call had happened to
// be made through the seam.
//
// THE COUNT SAID 4, THEN 2, AND IT IS NOW 0 MEMBERS -- so this rule no longer governs a
// single field of App. The count is written out at each step rather than left as "the
// remaining ones" precisely because it kept falling, and this is the step where it
// reached the floor. The two app icons left when they became shared_ptr<tinta::Bitmap>
// members; nativeLinkPeekBitmap was the last and left the same way.
//
// WHAT STILL BORROWS IS LOCALS, NOT MEMBERS, and that is a weaker obligation: the two
// target+staging pairs in src/print.cpp (the preview at :532 and the page raster at :868)
// create, draw through a borrowed wrapper, read back and release inside one function, so
// the wrapper cannot outlive the native. Nothing has to be remembered across a call.
//
// The link peek used to be the strongest case for the rule and is now an illustration of
// the alternative: src/print.cpp hands its bitmap straight to D2DOwnedBitmap, the OWNING
// subclass below, so its lifetime is the returned handle's and no holder tracks it at all.
// A bitmap can do that because a bitmap is data. A render target cannot, which is why
// app.h still seats three of those.
class D2DBitmap : public tinta::Bitmap {
public:
    explicit D2DBitmap(ID2D1Bitmap* bitmap) : bitmap_(bitmap) {}

    tinta::SizeF GetSize() const override;

    ID2D1Bitmap* native() const { return bitmap_; }

    // Not final any more, so the rule of five is stated rather than inherited by
    // luck: copying a borrowed wrapper is harmless, copying an OWNING one would
    // double-release, and the owning subclass deletes them for itself.
    D2DBitmap(const D2DBitmap&) = default;
    D2DBitmap& operator=(const D2DBitmap&) = default;

protected:
    ID2D1Bitmap* bitmap_;  // borrowed
};

// OWNS its bitmap: it came from D2DRenderTarget::CreateBitmap, which hands the
// caller the only reference. Derives from D2DBitmap rather than reimplementing it so
// that nativeBitmap's single dynamic_cast covers both kinds.
//
// THE EXAMPLE THAT USED TO END THAT SENTENCE IS GONE, AND IT IS THE EIGHTH SITE OF ONE
// STALE REASON RATHER THAN A NEW ONE. It read "...which is what keeps src/print.cpp's
// unwrap from having to know which kind it was given" -- and print.cpp no longer unwraps
// anything: drawDocumentRange takes a tinta::RenderTarget&. What is worth recording is
// WHERE this site was: forty lines below the nativeBitmap declaration whose paragraph I
// had already corrected for exactly this reason, in the same header, in the same pass.
// Correcting the paragraph a grep pointed at is not the same as correcting the header,
// and the surviving beneficiary is the one inside the backend -- render_d2d.cpp's own
// DrawBitmap, which is handed both kinds and must not care which.
//
// That declaration is now GONE, so the distance no longer resolves and is kept as
// history rather than a direction: the paragraph it refers to is the tombstone above,
// and `nativeBitmap` itself is file-local in src/render_d2d.cpp. The reason to derive
// from D2DBitmap is unchanged, because the single dynamic_cast that serves both kinds
// went file-local with it.
//
// GetSize is inherited deliberately -- the size question does not depend on who owns
// the pixels, and an override would be the same body twice.
class D2DOwnedBitmap final : public D2DBitmap {
public:
    explicit D2DOwnedBitmap(ID2D1Bitmap* bitmap) : D2DBitmap(bitmap) {}
    ~D2DOwnedBitmap() override;

    D2DOwnedBitmap(const D2DOwnedBitmap&) = delete;
    D2DOwnedBitmap& operator=(const D2DOwnedBitmap&) = delete;
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

    std::unique_ptr<tinta::PathSink> BeginPath() override;
    void FillGeometry(tinta::PathGeometry* geometry, tinta::Brush* brush) override;
    void DrawGeometry(tinta::PathGeometry* geometry, tinta::Brush* brush,
                      float strokeWidth) override;

    // Reads as DrawTextW here, because <windows.h> above has already rewritten the
    // token -- in this header, in render_api.h, and at all 107 call sites alike.
    void DrawText(const wchar_t* string, uint32_t stringLength, tinta::TextFormat* format,
                  const tinta::RectF& layoutRect, tinta::Brush* brush,
                  tinta::DrawTextOptions options) override;
    void DrawTextLayout(tinta::PointF origin, tinta::TextLayout* layout,
                        tinta::Brush* brush, tinta::DrawTextOptions options) override;

    void DrawBitmap(tinta::Bitmap* bitmap, const tinta::RectF& destRect, float opacity,
                    tinta::BitmapInterpolationMode interpolationMode,
                    const tinta::RectF* srcRect) override;

    std::unique_ptr<tinta::Bitmap> CreateBitmap(uint32_t pixelWidth, uint32_t pixelHeight,
                                               const uint8_t* premultipliedBgra,
                                               uint32_t stride, float dpi) override;

    void PushAxisAlignedClip(const tinta::RectF& rect, tinta::AntialiasMode mode) override;
    void PopAxisAlignedClip() override;

    void SetTransform(const tinta::Matrix3x2F& transform) override;
    void GetTransform(tinta::Matrix3x2F* transform) override;

    ID2D1RenderTarget* native() const { return target_; }

private:
    ID2D1RenderTarget* target_;  // borrowed
};

#endif  // TINTA_RENDER_D2D_H
