#ifndef TINTA_RENDER_API_H
#define TINTA_RENDER_API_H

#include "geometry.h"
#include "text_api.h"

// Named directly rather than leaned on through text_api.h. PathSink::Close returns
// a unique_ptr and AddLines takes a uint32_t, so this header owns those two
// dependencies whether or not another one happens to satisfy them today.
#include <cstdint>
#include <memory>

// THE DrawText MACRO, WHICH IS NOT A NUISANCE BUT AN ODR HAZARD
//
// <winuser.h>:8268 does `#define DrawText DrawTextW` under UNICODE, and every
// tinta target compiles with UNICODE defined. That macro rewrites the token
// everywhere it appears afterwards -- the DECLARATION below as well as all 109
// call sites -- so a translation unit that has seen <windows.h> declares
// `RenderTarget::DrawTextW` and calls `DrawTextW`, which is self-consistent and
// works. Direct2D relies on exactly this: ID2D1RenderTarget::DrawText is spelled
// plainly in d2d1.h and reached through the macro.
//
// THIS COMMENT USED TO CITE src/editor.cpp:2767 AS THE SITE THAT "already calls it
// as DrawTextW", AND THAT CITATION IS NOW WRONG RATHER THAN MERELY MOVED --
// corrected 2026-09-18 when editor.cpp was ported. The site now spells the call
// `DrawText`, which is the seam's real name, because off Windows there is no macro
// to rewrite it and the file has to compile there. **The change is a no-op in
// Windows object code**: the macro rewrites `DrawText` to `DrawTextW` at that call
// site exactly as it rewrites the declaration below, so both spellings produce the
// same symbol on Windows and only one of them produces any symbol off it. Kept as a
// correction rather than a deletion because a stale FIGURE reads as wrong
// eventually while a stale IDENTIFIER reads as a considered decision forever.
//
// The 109 above is re-measured, and the counting rule is published with it so the
// next reader can re-take it rather than trust it: `(->|\.)DrawText\s*\(` under src/
// and include/ reads 109, plus the 5 declarations and definitions of the method
// itself, which is the 114 a bare `\bDrawText\s*\(` returns. It read 107 before this
// edit, and **the delta is deliberately not attributed**, because the earlier
// figure's counting rule was never written down -- two call sites arriving and a
// slightly different regex are indistinguishable from here, and guessing which
// would be inventing a measurement.
//
// ONE `DrawTextW` REMAINS IN THE TREE AND IT IS NOT A STALE SPELLING:
// src/tabs.cpp:1470 calls the raw GDI DrawTextW on a memory DC, which is a
// different function from a different API that happens to share a name with this
// seam's method. A sweep that "finished the rename" by rewriting it would break the
// Windows build, so it is named here to stop that happening.
//
// The hazard is a MIXTURE. A Windows TU that includes this header WITHOUT
// <windows.h> declares `DrawText` instead, so the two TUs disagree about the name
// and the vtable slot of the same virtual function on the same class. That is an
// ODR violation the linker is not obliged to diagnose, and the failure it would
// produce -- a call landing in the wrong virtual slot -- would look like a
// rendering bug rather than a build error.
//
// So this is a hard requirement rather than a convention, and it fails at the
// point that would cause the mismatch rather than somewhere downstream. Off
// Windows there is no macro and nothing to reconcile.
#if defined(_WIN32) && !defined(DrawText)
#error "On Windows, include <windows.h> before render_api.h: the DrawText macro must be visible here and at every call site, or RenderTarget::DrawText and ::DrawTextW become different virtual functions in different translation units."
#endif

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
//   * The text TYPES. DrawText and DrawTextLayout are here, but TextFormat,
//     TextLayout, TextMetrics and the font enums live in text_api.h, which this
//     header includes. That split is not tidiness: text_api.h is usable on its
//     own by the 73 sites that create and measure a layout without drawing it,
//     and the reasoning behind its cut -- which turned out to contradict the plan
//     of record -- is long enough to deserve its own file to be argued in.
//   * Text ANTIALIASING. SetTextAntialiasMode and SetTextRenderingParams are one
//     site each, both in src/d2d_init.cpp, and both are ClearType gamma tuning on
//     a device this seam does not own. d2d_init.cpp is the Direct2D backend's own
//     construction and will never be portable, so those two sites keep talking to
//     the Direct2D object directly rather than being smuggled through a portable
//     interface that only one backend could mean anything by.
//   * Object creation and device lifetime. BeginDraw/EndDraw, CreateSolidColorBrush,
//     resize and device-loss handling are backend-specific in kind, not just in
//     implementation: D2D's EndDraw returns an HRESULT that callers inspect, and
//     a portable spelling of that is a design question rather than a rename. A
//     concrete backend owns its own construction; this interface is about the
//     drawing calls only.
//     THIS ENTRY SURVIVED A PORT THAT LOOKED LIKE IT WOULD NARROW IT, AND THE
//     SURVIVAL IS RECORDED BECAUSE AN UNRECORDED ONE READS LATER AS AN OVERSIGHT.
//     src/export_docx.cpp rasterised diagrams and math boxes offscreen by building
//     a WIC bitmap, wrapping it in a raw ID2D1RenderTarget and drawing through
//     Direct2D directly -- 51 non-portable identifiers, and the one thing it needed
//     that this interface did not have was A TARGET TO DRAW INTO. Every drawing verb
//     was already here. The obvious fix was a CreateOffscreenTarget virtual below,
//     and it was REJECTED: an offscreen render target is construction, so adding it
//     would have made this paragraph false while claiming to implement it.
//     What shipped instead is a FREE FUNCTION -- tinta::rasterizeToImage in
//     image_api.h -- whose two implementations each live inside their own backend
//     (src/image_win32.cpp holds the CreateWicBitmapRenderTarget, src/image_qt.cpp a
//     QImage plus QPainter), taking a live RenderTarget& only to reach that
//     backend's factories. So "a concrete backend owns its own construction" is
//     SATISFIED rather than narrowed, and NO virtual was added to the interface
//     below. That is a better outcome than the CreateBitmap precedent two entries
//     down, which narrowed its exclusion after an ownership redesign; here no
//     redesign was needed because the construction never escapes the backend.
//     THE HONEST LIMIT, AND IT HAS MOVED ONCE: this entry read "that seam serves
//     ONE caller", meaning src/export_docx.cpp, and as of 2026-09-19 it serves TWO
//     -- src/print.cpp's renderPeekBitmap routes through it, on the measurement
//     recorded in the CreateBitmap entry below. The retired sentence is kept here
//     because the COUNT was never the argument and the reasoning under it is
//     unchanged: there are exactly two CreateWicBitmapRenderTarget sites in the tree
//     and the other is src/image_win32.cpp itself, which must not route through a
//     seam it implements. So the justification is still its callers' portability and
//     NOT deduplication -- unlike decodeIconBytesToBitmap, whose shape it copies. Two
//     callers does not make "it's shared" true either; a seam's stated reason gets
//     quoted back, and that one would still be false.
//   * Bitmap creation FROM A WIC SOURCE, and bitmap creation of a device-context
//     RENDER TARGET. This entry used to exclude bitmap creation entirely, on the
//     ground that "they hand back an OWNED object, and six App members hold the
//     result as an AddRef'd ID2D1Bitmap*, so a portable spelling is an ownership
//     redesign rather than a rename". That was true, and it was an argument for
//     doing the redesign rather than for the exclusion: CreateBitmap is now in the
//     interface below and the redesign is done -- App::ImageEntry::bitmap and
//     App::LayoutBitmap::bitmap are shared_ptr<tinta::Bitmap>, which is what
//     "AddRef'd while open" always meant. What is left excluded is narrower, and
//     it is TWO kinds rather than one:
//       - CreateBitmapFromWicBitmap, 3 sites. Two (src/startpage.cpp,
//         src/tabs.cpp) convert a Win32 HICON, so they are Windows-terminal no
//         matter which backend draws. The third (src/render.cpp) decodes an image
//         file straight into a device bitmap, and routing it would add a
//         full-image memcpy to every local image -- a regression, which a
//         portability seam is not the place to accept. See image_api.h's "WHAT IS
//         NOT IN HERE".
//       - ID2D1DeviceContext::CreateBitmap with NULL pixels, TWO sites, both in
//         src/print.cpp's rasterizePreviewPage. These allocate an ID2D1Bitmap1 to
//         draw INTO, which is device-lifetime work of the kind this whole list
//         excludes, not an image handed to the drawing code.
//         THERE WERE FIVE AND THIS ENTRY HAS NOW BEEN WRONG TWICE, IN THE SAME
//         DIRECTION, ABOUT THE SAME SENTENCE. It first said "a GPU target plus a
//         CPU staging copy" of all five; that was corrected to "THE FIVE ARE NOT
//         ONE CLASS", which found the peek and is kept below in its own terms. The
//         correction then left ONE SENTENCE covering the two remaining
//         target+staging PAIRS: "They STAY -- a seam that draws offscreen and hands
//         back a pixel vector is not what a printer readback wants, and routing
//         them would add a third copy to the two they already make."
//         THE COPY ARITHMETIC WAS RIGHT AND THE PAIRS WERE NOT ONE CLASS EITHER,
//         which is the part a re-grouping is most likely to get wrong: having split
//         five into four-plus-one, nobody asked whether the four were alike.
//         printDebugPages -- the --printpages harness -- encodes its pixels to a
//         file with nothing waiting on it, and is ON THE SEAM as of 2026-09-20; the
//         WIC target is already CPU memory so the GPU->CPU copy disappears and the
//         net cost is one extra full-buffer copy per page, on a switch run from a
//         test script. rasterizePreviewPage re-rasterises on every page flip, paper
//         change and orientation toggle with the preview open and wants the pixels
//         in App::printPreviewPixels to DISPLAY. Same code shape; only the second
//         is the live-UI readback the sentence described, and it stays on that
//         reason unweakened. The arithmetic for both is at printDebugPages.
//         SOFTWARE RASTERISATION IS A GAIN AT THAT SITE AND NOT A COST, unlike the
//         peek below where it had to be priced: several slices in this tree use
//         --printpages page hashes as their falsifier, and the new body is software
//         unconditionally rather than at app.useHardwareRT's discretion, so the
//         instrument cannot become a function of the driver for any caller.
//         THIS SENTENCE SAID "AND THE OLD BODY RASTERISED ON THE GPU" UNTIL THE
//         HASHES WERE READ, and that was wrong: useHardwareRT starts false and
//         src/main_d2d.cpp returns from --printpages ~40 lines before it starts the
//         GPU warm-up, so the harness path was ALREADY software and all 143 baseline
//         pages came back byte-identical. Holding an ID2D1Device is not evidence of
//         hardware rasterisation, which is the inference that produced the error.
//         THE FIFTH WAS
//         renderPeekBitmap: one OPTIONS_TARGET bitmap at 192 DPI, the peek panel
//         drawn into it, kept. No staging bitmap, no CPU readback -- which is
//         exactly tinta::rasterizeToImage's shape.
//         IT WAS EXCLUDED FOR A STATED PRICE, THE PRICE WAS MEASURED, AND IT IS
//         PORTED -- 2026-09-19. The retired wording asked for precisely this and
//         is quoted so the exchange is legible: "routing it would trade GPU
//         rasterisation for the WIC software path plus two full-buffer copies per
//         dwell (out to a DecodedImage vector, back up through CreateBitmap) on a
//         panel that reaches roughly 520x460 DIPs at 2x ... a measurement showing
//         the copies are cheap at this size would settle it."
//         Measured at the size that entry itself named -- 520x460 DIPs at 2x =
//         1040x920 px, 3,827,200 bytes a buffer -- best of 7 warm runs, BOTH
//         copies inside the timing, over a wrapped text layout rather than a
//         rectangle because software GLYPH rasterisation is the part that could
//         plausibly blow a budget:
//             Direct2D  2.47 ms raster + 0.21 ms CreateBitmap = 2.68 ms
//             Qt        3.77 ms raster + 0.42 ms CreateBitmap = 4.19 ms
//         against a budget of 25 ms fixed before the instrument existed -- a
//         quarter of the ~100 ms at which a hover stops feeling instant, on a
//         panel drawn ONCE per dwell and cached (the handle lives until the peek
//         disarms; see App::linkPeekPanel in app.h). Roughly ten times under, so
//         the exclusion's own falsifier fired.
//         THE 2x IS NOW A TRANSFORM AND NOT A DPI, which is the one thing that did
//         not survive the move unchanged. The native bitmap carried the scale in
//         its own 192 DPI; src/image_win32.cpp builds its WIC target hardcoded at
//         96, so the ported body scales with SetTransform(matrixScale(2,2)) inside
//         the callback and puts the 192 on CreateBitmap below instead. Getting
//         either half wrong is silent -- a peek at half size in the corner of a
//         blank panel, or the right pixels at twice the intended DIP extent -- so
//         both are asserted in tests/render_api_shared.cpp on both backends, with
//         the unscaled draw as the control. That is the CLOSEST arm that exists and
//         not a direct one -- but the reason written here was wrong until 2026-09-20.
//         It read: "src/print.cpp appears exactly once in CMakeLists.txt, in the
//         application target, so no test binary compiles renderPeekBitmap itself."
//         SIX targets compile that file: it is in set(SOURCES), and CMakeLists re-uses
//         SOURCES as INPUT_TEST_SOURCES minus src/main_d2d.cpp alone, so five test
//         binaries carry it besides the app. What makes this arm indirect is not the
//         build but the coverage -- none of those six calls renderPeekBitmap, and the
//         two targets holding these assertions do not compile the file at all.
//         Compiled is not covered, and a count of a file's appearances in CMakeLists'
//         TEXT says nothing about how many TARGETS consume it.
//         WHAT THIS DOES NOT BUY: src/print.cpp is still not portable and still
//         not in basis B's manifest -- ID2D1PrintControl, DEVMODE and the printer
//         dialog stay. One Direct2D-terminal BODY and this seam's second caller is
//         the whole of it. (printDebugPages made it TWO bodies and a third caller
//         on 2026-09-20, and the sentence above still holds for the file: the
//         printer dialog and the XPS package target are what keep it here.)
//     THE COUNT IN THE OLD ENTRY WAS WRONG, and wrong in the way a comment count
//     usually is: it said "CreateBitmap 2" having grepped only src/render.cpp. The
//     tree has THREE CPU-buffer sites -- the two in src/render.cpp and the print
//     preview in src/overlays.cpp:2508, which carried its own comment saying it
//     "stays Direct2D-terminal because CreateBitmap is native by the seam's own
//     exclusion policy". A site excluded by a stale count reads as a considered
//     decision, which is worse than reading as an omission. All three now route.
// SetTransform/GetTransform WERE on this list. They are now in the interface, and
// tinta::Matrix3x2F in geometry.h is the 3x2 matrix type they needed. DrawBitmap
// was on it too, and tinta::Bitmap below is the handle type it needed.
//
// GEOMETRY PATHS WERE ON IT TOO, and the entry read "FillGeometry 2, DrawGeometry
// 2. They need a path object, which is a handle type like StrokeStyle rather than
// a value type." The diagnosis was right and the count was wrong: measured over
// code lines only, it is SIX of each -- src/export_docx.cpp x2, src/print.cpp x2,
// and src/paint_document.cpp x2, WHICH THIS ENTRY CALLED src/main_d2d.cpp UNTIL
// 2026-09-18. The count survived the correction and the file name did not: the pass
// moved when paint_document.cpp was extracted, which is the same staleness the
// AddLines entry below carried, from the same extraction, found in the same grep.
// All six draw through the seam today. I found the count out by grepping this header for FillGeometry,
// getting a hit, and reading it as a declaration when it was this comment -- so
// the stale number cost a wrong estimate before it cost anything else. A count in
// a comment is a claim, and a grep hit inside a comment is not a call site.
//
// tinta::PathGeometry below is the handle type the entry asked for, and PathSink is
// how one gets built. Both are here rather than in geometry.h because a path is a
// backend OBJECT (a Direct2D ID2D1PathGeometry, a Qt QPainterPath) and not a
// layout-compatible value like PointF -- ArcSegment is next to PathSink for the
// same reason, and is deliberately NOT the same shape as D2D1_ARC_SEGMENT.
//
// Naming stays qualified as tinta::, for the reason geometry.h gives: bare Color
// is already ambiguous across most of src/.

namespace tinta {

// Values match D2D1_ANTIALIAS_MODE so the Windows backend can cast rather than
// switch, and so the call sites that name a D2D1_ANTIALIAS_MODE_* constant today
// keep meaning exactly what they meant. render_d2d will static_assert the pairing
// the way geometry.h asserts its layouts -- an assertion cannot live here,
// because this header must not include <d2d1.h>.
//
// BOTH VALUES ARE LIVE, and an earlier version of this comment said otherwise. It
// claimed all 9 PushAxisAlignedClip sites pass D2D1_ANTIALIAS_MODE_ALIASED. Five
// do (src/editor.cpp x2, src/editrail.cpp, src/main_d2d.cpp, src/overlays.cpp);
// the other four pass D2D1_ANTIALIAS_MODE_PER_PRIMITIVE (src/overlays.cpp x3,
// src/tabs.cpp). The method count was right and the uniformity was invented. It
// matters because render_qt.cpp branches on this value, and a reader who believed
// the old comment would have been entitled to delete the branch -- silently
// hardening four antialiased clips.
enum class AntialiasMode {
    PerPrimitive = 0,
    Aliased = 1,
};

// Values match D2D1_BITMAP_INTERPOLATION_MODE, asserted in render_d2d.cpp for the
// same reason and in the same place as AntialiasMode's pairing.
//
// The default below is Linear because that is Direct2D's own default for the
// 2- and 3-argument DrawBitmap overloads, and because the three sites that name a
// mode all name Linear -- so every one of the 7 sites means Linear today. Nearest
// is here anyway rather than being dropped as unused: it is the only value that
// makes the interpolation argument OBSERVABLE, so the shared assertions need it to
// prove a backend reads the argument at all instead of hardcoding smooth scaling.
enum class BitmapInterpolationMode {
    NearestNeighbor = 0,
    Linear = 1,
};

// An opaque handle. Dash patterns are built once at startup and only ever passed
// back to DrawLine (2 sites, both `dashed ? style : nullptr`), so nothing in the
// tree needs to READ one -- which is why this has no methods rather than a
// guessed-at accessor.
class StrokeStyle {
public:
    virtual ~StrokeStyle() = default;
};

// A drawable image. Opaque like StrokeStyle, but with ONE accessor, and the
// method list is a measurement rather than a guess: over every bitmap-typed
// receiver in src+include the tree calls Release 15 times, GetSize 3, AddRef once
// and CopyPixels once. Release and AddRef are COM reference counting, which a
// portable backend DELETES rather than reimplements; CopyPixels is the DOCX export
// path, which reads pixels out rather than drawing them. That leaves GetSize.
//
// Creating one from a CPU pixel buffer IS here now -- RenderTarget::CreateBitmap
// below -- and this comment used to say the opposite ("NOT here, deliberately").
// The exclusion list above records what is still out and why. What has not changed
// is that RenderHarness::makeBitmap still builds its own, per backend, rather than
// calling the seam: two independent construction paths that must produce the same
// pixels is a stronger instrument than one path checked against itself, and the
// shared assertions now compare them.
class Bitmap {
public:
    virtual ~Bitmap() = default;

    // In DIPs, matching ID2D1Bitmap::GetSize rather than GetPixelSize: the two
    // differ on a bitmap whose dpi is not 96, and src/render.cpp sets 192 on the
    // rasterized-SVG path. Returning pixels there would halve every SVG.
    virtual SizeF GetSize() const = 0;
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

// One arc, in Direct2D's ENDPOINT parameterisation: where the arc finishes, not
// where its circle is centred. That choice is a measurement, not a preference.
//
// All FOUR arc sites in the tree already compute an endpoint (src/render.cpp x3,
// src/export_docx.cpp x1) because that is what ID2D1GeometrySink::AddArc takes, and
// re-parameterising by centre would mean rewriting the trigonometry at every one of
// them -- a behaviour change smuggled in under a portability seam. Qt's
// QPainterPath::arcTo wants a bounding box and two angles instead, so the
// conversion lives once inside QtPathSink where it can be asserted, rather than
// four times in files that cannot be compiled off Windows.
//
// THREE of D2D1_ARC_SEGMENT's five fields are absent, each because the whole
// population agrees: `size` is {r, r} at all 4 sites, so it collapses to one
// radius; `rotationAngle` is 0.0f at all 4; `sweepDirection` is CLOCKWISE at all 4.
// Only arcSize varies (2 sites choose on sweep > pi), which is why largeArc is the
// one flag that survived. A field nobody varies is not configuration, it is a
// constant with a setter, and this seam would be claiming a generality it has never
// been asked for. If a counter-clockwise or elliptical arc is ever needed, ADD the
// field then -- with the site that needs it in the same commit.
struct ArcSegment {
    PointF endpoint{};
    float radius = 0.0f;
    bool largeArc = false;
};

// An opaque, finished path. Opaque like StrokeStyle and for the same measured
// reason: over every geometry-typed receiver in src/, the tree calls Release (which
// a portable backend deletes rather than reimplements) and passes the object to
// Fill/DrawGeometry. Nothing READS one -- no bounds query, no hit test, no outline
// walk -- so this has no accessors rather than guessed-at ones.
class PathGeometry {
public:
    virtual ~PathGeometry() = default;
};

// How a PathGeometry gets built, and it deliberately MIRRORS the
// ID2D1GeometrySink call sequence that all 6 existing builders already write.
//
// The alternative was a description-shaped seam -- CreatePolygon(points) and
// CreateSlice(centre, radius, a0, a1) -- which is smaller, since those two shapes
// cover all 6 sites. It is rejected because it would move each caller's logic INTO
// the backend, and the callers do not agree: src/render.cpp's slice builder
// special-cases a full circle as two half-arcs (a single arc from a point back to
// itself degenerates), and src/export_docx.cpp's slice case does not. A
// description-shaped seam has to pick one, which would silently FIX the DOCX export
// while the commit message said "portability". Mirroring the sink keeps every
// caller's behaviour exactly where it is, and the divergence stays visible and
// separately fixable.
//
// TWO ENUM ARGUMENTS ARE GONE, on a full census rather than a sample: BeginFigure
// is D2D1_FIGURE_BEGIN_FILLED at 7 of 7 sites (0 HOLLOW) and EndFigure is
// D2D1_FIGURE_END_CLOSED at 7 of 7 (0 OPEN). So every figure this tree builds is a
// closed filled one, and the parameters existed only to say so 14 times.
//
// AddLine and AddLines are BOTH here, which a name-only census would have missed:
// 2 sites append in a loop (src/render.cpp, src/export_docx.cpp) and 2 pass a whole
// run at once. Collapsing the array form into a loop would throw away the one thing
// tinta::asD2DPointArray exists to guarantee, and geometry_tests.cpp asserts its
// stride.
//
// THE TWO ARRAY SITES BOTH CALL THIS SEAM NOW, and the entry used to name them as
// "(src/main_d2d.cpp, src/print.cpp)" passing "through tinta::asD2DPointArray" --
// which was a description of the hand-rolled ID2D1GeometrySink builders this
// interface replaced. They are src/paint_document.cpp:270 and src/print.cpp:362, both
// spelled sink->AddLines(...), and asD2DPointArray is now reached only from inside
// D2DPathSink::AddLines where it belongs. The COUNT of 2 was and is correct; the
// example list went stale twice over -- main_d2d.cpp's copy moved when the paint pass
// was extracted, and print.cpp's stopped being native when drawDocumentRange took a
// RenderTarget&. Kept as a correction rather than a deletion because the count is the
// load-bearing part: it is why both forms are on this interface at all.
class PathSink {
public:
    virtual ~PathSink() = default;

    virtual void BeginFigure(PointF start) = 0;
    virtual void AddLine(PointF point) = 0;
    virtual void AddLines(const PointF* points, uint32_t count) = 0;
    virtual void AddArc(const ArcSegment& arc) = 0;
    virtual void EndFigure() = 0;

    // Hands the finished path over and ends this sink's usefulness; calling
    // anything else afterwards is a caller bug. Returns null on backend failure,
    // which is the SAME shape the 6 current sites already handle -- every one of
    // them checks the geometry before drawing -- so a failure here reaches code
    // that already copes rather than a new branch nobody has exercised.
    virtual std::unique_ptr<PathGeometry> Close() = 0;
};

// Every drawing call tinta makes.
//
// The defaulted strokeWidth and strokeStyle mirror Direct2D's own defaults, so
// the 3-argument DrawLine calls that make up the bulk of the 111 sites and the
// 5-argument dashed-connector calls both bind without edits.
//
// THE EXAMPLE LIST SAID "main_d2d.cpp and print.cpp", AND IT IS THE FOURTH SITE IN THIS
// FILE TO CARRY THE SAME EXTRACTION STALENESS -- the paint pass moved to
// paint_document.cpp and every comment naming main_d2d.cpp as a drawing caller went
// stale in one commit, silently, because no test reads a filename in a comment.
// Measured: 111 DrawLine call sites in src/ with comments stripped and the two backend
// definitions excluded -- so THAT figure reproduces exactly -- and the 5-argument form
// is at exactly THREE, one per file: the connector-with-arrow path in export_docx.cpp,
// the same path in paint_document.cpp, and print.cpp's connector case. print.cpp's is
// the one this seam newly carries; it used to be a native ID2D1RenderTarget::DrawLine
// and now binds here with no edit, which is the defaulting claim above being exercised
// rather than asserted.
//
// RE-MEASURED 2026-09-20, AND THE COUNT HELD WHILE ALL THREE ADDRESSES HAD ROTTED. This
// sentence used to name src/export_docx.cpp:419, src/paint_document.cpp:218 and
// src/print.cpp:316. Every one was wrong: :419 was a 4-argument arrowhead, :218 was an
// ensureDashedStrokeStyle call, and :316 was a comment line. The real sites are the
// first DrawLine in each file's connector case, and each file does have exactly one --
// so "111" and "exactly THREE, one per file" both reproduce, which is the whole point.
// The addresses are dropped rather than refreshed: they are decoration on a claim that
// is a COUNT, they had each rotted by a different amount in a different direction, and
// refreshing them just resets a timer. A quantity that a later reader can re-derive
// outlives a pointer that only the writer could check.
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

    // Paths. BeginPath is CREATION, which the exclusion list above otherwise keeps
    // off this interface, and it is the one exception the tree's own rule allows:
    // brushes and bitmaps are excluded because six App members hold the result as an
    // AddRef'd COM pointer, so a portable spelling is an ownership redesign. A path
    // is the opposite -- exactly one member holds one (App::LayoutShape::geometry),
    // it is built lazily, and it is destroyed with its shape. So the ownership
    // question has one answer here, and unique_ptr states it.
    //
    // It is on RenderTarget rather than on a factory because Direct2D's
    // CreatePathGeometry hangs off ID2D1Factory, which a D2DRenderTarget can reach
    // through GetFactory(), while QPainterPath needs no device at all. Putting it
    // here means neither backend needs a second seam object, and the caller already
    // has a render target at all 6 sites.
    //
    // Returns null if the backend cannot start a path. Every existing site already
    // branches on that, because CreatePathGeometry can fail today.
    virtual std::unique_ptr<PathSink> BeginPath() = 0;

    virtual void FillGeometry(PathGeometry* geometry, Brush* brush) = 0;
    virtual void DrawGeometry(PathGeometry* geometry, Brush* brush,
                              float strokeWidth = 1.0f) = 0;

    // The two largest text items in the tree: 106 DrawText sites and 26
    // DrawTextLayout, every one of them on app.renderTarget. They are the reason
    // this seam could not reach App at all before now -- a C++ member has one
    // static type, so App::renderTarget cannot become a tinta::RenderTarget*
    // until the calls made THROUGH it are all expressible here.
    //
    // The defaulted options argument matters: 104 of the 106 DrawText sites pass
    // nothing, and the two that do (main_d2d.cpp, math_render.cpp) pass
    // EnableColorFont. Both forms bind without an edit.
    virtual void DrawText(const wchar_t* string, uint32_t stringLength,
                          TextFormat* format, const RectF& layoutRect, Brush* brush,
                          DrawTextOptions options = DrawTextOptions::None) = 0;

    virtual void DrawTextLayout(PointF origin, TextLayout* layout, Brush* brush,
                                DrawTextOptions options = DrawTextOptions::None) = 0;

    // 7 sites, called THREE ways, and the defaults are what make all three bind
    // without an edit: 2 arguments (src/editrail.cpp, src/startpage.cpp,
    // src/tabs.cpp -- an icon at its natural size), 4 (src/overlays.cpp:2525, the
    // print preview), and 5 with a trailing null srcRect (src/overlays.cpp:1366 and
    // :1425, the link peek and the lightbox). The defaults are Direct2D's own, so a
    // 2-argument call means opacity 1 and linear interpolation on both backends.
    //
    // srcRect is a POINTER, not a defaulted value, because null means "all of it"
    // and that is a distinct state from any rectangle -- there is no rectangle that
    // means "whatever size this bitmap turns out to be". NO call site passes it --
    // the two that used to pass D2D's equivalent passed null, and now take the
    // default -- so the sub-rect path is exercised ONLY by the shared assertions.
    // It is implemented rather than stubbed because a backend that ignored srcRect
    // would draw a plausible picture of the wrong region, and with no caller the
    // assertions are the only thing that would ever notice.
    virtual void DrawBitmap(Bitmap* bitmap, const RectF& destRect, float opacity = 1.0f,
                            BitmapInterpolationMode interpolationMode =
                                BitmapInterpolationMode::Linear,
                            const RectF* srcRect = nullptr) = 0;

    // A bitmap from a CPU pixel buffer. The second exception to the "no creation"
    // rule, admitted on the same terms as BeginPath and only after the ownership
    // question had an answer: unique_ptr here, and the two App members that keep one
    // are shared_ptr<Bitmap>, which is what the old `ID2D1Bitmap*  // AddRef'd while
    // open` was doing by hand.
    //
    // THE PIXEL FORMAT IS FIXED, not a parameter, because the population is
    // unanimous: all 3 sites pass DXGI_FORMAT_B8G8R8A8_UNORM with
    // D2D1_ALPHA_MODE_PREMULTIPLIED, and tinta::DecodedImage -- which feeds two of
    // them -- documents premultiplied BGRA as its own invariant. A format argument
    // would be a parameter no caller varies, which this header's arc-segment entry
    // already calls configuration that is really a guess.
    //
    // `stride` is the byte distance between rows, passed rather than derived,
    // because Direct2D takes it and a caller with padded rows must be able to say
    // so. Every site in the tree passes pixelWidth * 4 today.
    //
    // dpi IS LOAD-BEARING AND IS THE REASON THIS TAKES FIVE ARGUMENTS. GetSize
    // above returns DIPs, so a bitmap declared at 192 dpi reports half its pixel
    // dimensions -- and src/render.cpp's SVG path depends on exactly that, drawing a
    // 2x raster at its intrinsic size. Passing 96 there would draw every SVG in the
    // document at double size and nothing else would look wrong. One scalar rather
    // than dpiX/dpiY because both sites that set it set them equal.
    //
    // Returns null on failure, which every caller already branches on: D2D's
    // CreateBitmap can fail today and all three sites check.
    virtual std::unique_ptr<Bitmap> CreateBitmap(uint32_t pixelWidth, uint32_t pixelHeight,
                                                const uint8_t* premultipliedBgra,
                                                uint32_t stride, float dpi = 96.0f) = 0;

    // Clips nest, and every push in the tree is matched by a pop in the same
    // function, so a backend may implement these with a save/restore stack.
    virtual void PushAxisAlignedClip(const RectF& rect, AntialiasMode mode) = 0;
    virtual void PopAxisAlignedClip() = 0;

    // The transform is REPLACED, not composed. Every call site in the tree already
    // does its own composing -- the pattern is GetTransform into a local, then
    // SetTransform(something * previous), then SetTransform(previous) to restore --
    // so a seam that composed instead would double-apply at all 10 renderTarget
    // sites. Qt's setWorldTransform has a combine flag defaulting to replace, and
    // that default is the one this seam means.
    //
    // GetTransform takes an out-pointer rather than returning a value because that
    // is Direct2D's signature and the six sites read
    // `GetTransform(&previous)`. Non-const because ID2D1RenderTarget::GetTransform
    // is non-const on the wrapper this seam delegates to.
    virtual void SetTransform(const Matrix3x2F& transform) = 0;
    virtual void GetTransform(Matrix3x2F* transform) = 0;
};

}  // namespace tinta

#endif  // TINTA_RENDER_API_H
