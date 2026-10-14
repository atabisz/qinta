#ifndef TINTA_IMAGE_API_H
#define TINTA_IMAGE_API_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// The one header from this tree that this one includes, and the exception is narrow
// enough to be worth stating. Everything else the image seam names -- Bitmap,
// RenderTarget, Brush -- is forward-declared below precisely so that render_api.h's
// windows.h ordering requirement does not spread to every consumer. tinta::Color
// cannot take that treatment: `rasterizeToImage` takes one BY VALUE, so its size has
// to be known here. geometry.h is safe to include where render_api.h is not -- it
// includes no Windows header (its `#ifdef _WIN32` <d2d1.h> went with the Direct2D stack)
// and imposes no include order.
#include "geometry.h"

// The image seam: fetch, decode, and SVG rasterization, with no backend in it.
//
// WHY THIS SEAM EXISTS AND WHY IT IS SHAPED THIS WAY
//
// render.cpp was 3,498 lines and carried three unguarded Windows-only includes --
// <urlmon.h>, <shlwapi.h> and <d2d1_3.h>. Unguarded is the operative word: app.h
// put d2d1.h, dwrite.h and wincodec.h behind `#ifdef _WIN32` and forward-declares
// the COM types off Windows, so every other native mention in render.cpp is an
// ordinary compile ERROR. These three were FATAL, and a fatal header means the file
// was never measured at all -- render.cpp read `errors=1, fatal=1` at urlmon.h:19
// for six spans running, which is one fact about the include path and none about
// the file.
//
// The census that sized this seam found all three confined to TWO functions:
//
//   asyncImageWorker  urlmon   URLDownloadToCacheFileW           1 call
//   loadSvgBitmap     shlwapi  SHCreateMemStream                 1 call
//   loadSvgBitmap     d2d1_3   ID2D1DeviceContext5 x4,
//                              ID2D1SvgDocument x2               6 sites
//
// Both functions are image loading and both produce the same artifact -- premultiplied
// BGRA pixels -- so one seam covers all three headers. That is why a network download
// and an SVG rasterizer share a header: not because they are the same kind of
// operation, but because they are the same dependency edge.
//
// THE SPLIT BETWEEN FETCH AND DECODE IS DELIBERATE, AND IT IS WHAT MAKES THIS
// SEAM TESTABLE
//
// A network fetch cannot be asserted from a test suite -- it needs a live host, and
// a suite that reached one would be issuing outbound requests from a developer
// machine every time it ran. Decoding a known image to known pixels can be asserted,
// on both platforms, through two independent backends. So the two are separate entry
// points rather than one `fetchAndDecode`, and the untestable half is kept as thin as
// it can be: one call, no branching, one string in and one path out. The same split
// took the keyboard seam from untestable to 65,545 assertions.
//
// Which end is unwitnessed, stated plainly because the asymmetry is invisible
// otherwise: `fetchToTempFile` is witnessed on Windows only by the application's
// existing behaviour and on Qt by nothing at all. Everything else here is asserted
// by tests/image_tests.cpp against hand-computed pixel values.
//
// THE THIRD IMAGE PATH IS NOW IN HERE, AND THE MEMCPY OBJECTION IS WHAT SHAPED IT
//
// This section used to be headed "WHAT IS NOT IN HERE" and excluded render.cpp's
// local-raster branch -- the part of getOrLoadImage that decodes through WIC straight
// into an ID2D1Bitmap without ever materializing pixels. The exclusion was argued twice
// and the argument was right both times, so it is quoted rather than deleted:
//
//   "going via DecodedImage would add a full-image memcpy to every local image, which
//    is a regression, and a portability seam is not the place to accept one"
//   "the memcpy objection is about DECODING through DecodedImage, not about where the
//    bitmap comes from afterwards"
//
// That second sentence is the answer. The objection is against ONE PATH -- decode to a
// DecodedImage, then upload -- and not against a seam. decodeImageFileToBitmap below
// does not take that path: each backend decodes straight into its own bitmap type, so
// the copy the objection is about does not happen on either platform. The branch is
// ported and the regression is not accepted; the exclusion is answered rather than
// reversed.
//
// MEASURED, because "fewer copies" is the entire justification and it would be worth
// nothing as an assertion. Direct2D: WIC decoder -> IWICFormatConverter ->
// CreateBitmapFromWicBitmap, which is the original's own call sequence moved verbatim,
// so ZERO copies and byte-identical behaviour. Qt: QImage::load straight into a
// QtBitmap, which holds a QImage by value and is copy-on-write -- also zero. Routing
// the same image through decodeImageFile instead would cost TWO on Qt, not one: the
// decode fills a std::vector, and RenderTarget::CreateBitmap then calls
// QImage::copy on the caller's buffer at render_qt.cpp:323 because it may not alias it.
//
// WHAT THIS COSTS, stated because it is a real cost and not a rounding error: the
// format-set divergence noted on decodeImageFile below now applies to a second entry
// point, and the two are NOT guaranteed to accept the same files -- one goes through
// WIC's codecs and Qt's image plugins by way of a pixel buffer, the other by way of
// each platform's own bitmap. Agreement on PNG is asserted for both in
// tests/image_tests.cpp; beyond PNG they are two independent decoders and this header
// does not pretend otherwise.
//
// App::ImageEntry::bitmap was already a shared_ptr<tinta::Bitmap>, which is what made
// this a one-line change at the call site -- the prediction this section used to end
// with ("when it is ported, one line changes and nothing downstream of it does") held.
//
// Paths are std::wstring to match the rest of the tree, on the same reasoning
// platform_paths.h sets out at length: on Windows these hold UTF-16, on POSIX
// UTF-32, and each implementation converts at the OS boundary. This seam does not
// prejudge the string migration either way.

namespace tinta {

// Forward declarations rather than #include "render_api.h", and not for compile time.
// render_api.h has an #error at its top demanding <windows.h> come first on Windows,
// because RenderTarget::DrawText must see the DrawText macro at every include site or
// two translation units end up with different virtual functions in the same vtable slot.
// Including it from here would put that requirement on every consumer of the image seam,
// including ones that touch no render target at all. A declaration costs nothing and
// carries no such condition.
class Bitmap;
class RenderTarget;
// Brush joins the two above for the same reason and on the same terms: rasterizeToImage
// below hands one to its callback, and a pointer parameter needs no definition here.
class Brush;

// A decoded raster image in premultiplied BGRA, tightly packed. The invariant,
// asserted in tests/image_tests.cpp: whenever `ok` is true,
//   pixels.size() == size_t(width) * height * 4
// and both dimensions are non-zero. When `ok` is false the other fields are
// unspecified and callers must not read them -- every call site in the tree
// checks `ok` first.
struct DecodedImage {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<uint8_t> pixels;
    bool ok = false;
};

// Exclusive upper bound on either dimension of a decoded raster image -- a dimension
// EQUAL to this value is rejected, which is the original `w < 16384` test preserved
// exactly rather than rounded to a friendlier `<=`. A hostile or corrupt
// file can declare enormous dimensions, and the allocation happens before anything
// has decoded a single pixel: at 4 bytes per pixel a declared 65535x65535 asks for
// 17 GB. The bound lives in the seam rather than at the call site because it is a
// property of "decode an untrusted file". There is one backend since 0b39ae6
// (src/image_qt.cpp); when there were two, both enforced it, so they could not drift.
inline constexpr unsigned kMaxImageDimension = 16384;

// Download a remote resource to a local temporary file and return its path, or an
// empty string on any failure. Blocking -- callers run it off the UI thread.
//
// Fetch-to-file, not fetch-to-memory, and that is preserved from the code this
// replaced rather than chosen fresh. Decoding from memory would remove a temp file
// and change both peak memory and the URL cache's behaviour; those may well be
// improvements, but they are not portability, and smuggling them in under a seam is
// how a "no behaviour change" claim becomes false.
std::wstring fetchToTempFile(const std::string& url);

// Decode an encoded raster image file -- PNG, JPEG, GIF, BMP and whatever else the
// platform's codec set covers -- to premultiplied BGRA. Returns `ok == false` for a
// missing file, an unrecognized format, a zero dimension, or a dimension at or above
// kMaxImageDimension. On failure `pixels` is empty, so the `ok == false` case is
// distinguishable from a successful decode of a zero-area image (which cannot occur,
// since a zero dimension is itself a failure).
//
// The format set is Qt's image plugins'. When the retired WIC backend existed the two
// overlapped on the common web formats and diverged at the edges, so what is asserted
// is PNG, which is the format the test emits and the one the seam is exercised with.
DecodedImage decodeImageFile(const std::wstring& path);

// Rasterize an SVG document at `scale`, reporting the document's intrinsic size in
// DIPs through intrinsicW/intrinsicH. On success the pixel dimensions are
// ceil(intrinsic * scale) and the intrinsic size is what the caller should use for
// layout -- these are NOT the same number, and conflating them is the specific way
// this seam can silently break every SVG on screen. render.cpp stores the intrinsic
// size in App::ImageEntry::width/height while the bitmap it holds is 2x that.
//
// Intrinsic size resolution follows the SVG spec's order and the code this replaced:
// the root element's width/height attributes, else its viewBox, else the spec's
// replaced-element default of 300x150. The result is clamped to 4096 DIPs per axis
// before scaling.
//
// The full SVG specification is not supported: Qt's renderer covers SVG 1.2 Tiny (the
// retired Direct2D subset omitted <text> entirely). Only geometry the test document pins
// is asserted.
DecodedImage rasterizeSvg(const void* data, size_t size, float scale,
                          float& intrinsicW, float& intrinsicH);

// Read an SVG file and rasterize it. Exists as its own entry point because the read
// is the platform-dependent half: a wide path reaches std::ifstream only through an
// MSVC extension, and the portable spelling differs. Behaviour is otherwise exactly
// rasterizeSvg over the file's bytes, including an unreadable or empty file
// returning `ok == false`.
DecodedImage rasterizeSvgFile(const std::wstring& path, float scale,
                              float& intrinsicW, float& intrinsicH);

// Decode an encoded raster image file DIRECTLY into a backend bitmap, with no pixel
// buffer in between. Returns nullptr for a missing file, an unrecognized format, or a
// decode failure -- the same failure set decodeImageFile reports through `ok == false`.
//
// This is the local-raster branch of render.cpp's getOrLoadImage, and the long note at
// the top of this header is why it is a second entry point rather than a caller of
// decodeImageFile: the copy is the reason, and it is measured there rather than asserted.
//
// `target` is where the bitmap comes from and it MUST be the backend's own render target
// -- the same object App::renderTarget holds. The implementation downcasts it, which is
// the same asserted-downcast shape nativeTextLayout used in the retired text_d2d.h: a target
// of the wrong backend arriving there is a configuration error and trips rather than
// silently returning nullptr, because a null return is indistinguishable from an
// unreadable file and would be diagnosed as one.
//
// NOT const, and the asymmetry with every other function here is deliberate: creating a
// device bitmap was a mutation of the target on the retired Direct2D stack. The rest of this header is pure.
//
// kMaxImageDimension is NOT enforced here, and that is the one place this entry point is
// weaker than decodeImageFile. The backend allocates no pixel buffer on this path --
// the decoder writes into a device or platform bitmap and enforces its own limits -- so
// the 17 GB allocation the bound exists to prevent is not reachable by the same route.
// What a hostile file can still do is ask a GPU for a very large texture. That is the
// behaviour render.cpp has had all along, moved rather than introduced, and it is worth
// naming as an accepted risk instead of implying the bound covers both paths.
std::shared_ptr<Bitmap> decodeImageFileToBitmap(RenderTarget& target,
                                                const std::wstring& path);

// Decode a MULTI-FRAME ICON held in memory into a backend bitmap at a requested pixel
// size. The fourth cell of this header's grid: `rasterizeSvg` already takes memory but
// only understands SVG, and `decodeImageFileToBitmap` already produces a bitmap but only
// from a path. The app icon is embedded bytes in a raster container, so neither reaches it.
//
// THE FRAME-SELECTION POLICY IS PART OF THE CONTRACT, NOT AN IMPLEMENTATION DETAIL, AND
// THIS PARAGRAPH IS WRITTEN BEFORE EITHER BACKEND. Both implementations MUST follow it
// rather than defer to their image library's default:
//
//   Choose the SMALLEST frame whose width is >= pixelSize. If no frame is that large,
//   choose the LARGEST frame available. Then scale that frame to exactly pixelSize.
//
// Why it is stated rather than delegated, measured rather than argued: resources/qinta.ico
// carries frames at 256, 48, 32 and 16 and the hero icon asks for 64. There is NO 64 frame,
// so at that size "which frame" IS the visible behaviour -- and WIC's ICO decoder and Qt's
// ICO handler do not choose alike. Two libraries left to their defaults would render a
// different hero icon on the two platforms with nothing in the build to say so, which is
// exactly the class of divergence a seam is supposed to prevent rather than launder. The
// 32 px caller, by contrast, hits an exact frame on any sane policy, so a test at 32 alone
// would pass while the interesting size diverged.
//
// The policy deliberately prefers DOWNSCALING a larger frame to upscaling a smaller one:
// at 64 that means the 256 frame rather than the 48, because a downscale loses detail it
// had and an upscale invents detail it never had.
//
// Returns nullptr for empty or truncated data, a container with no usable frame, a
// non-positive pixelSize, or a decode failure -- the same all-failures-are-null shape as
// decodeImageFileToBitmap, and null is likewise indistinguishable from bad bytes, so
// callers must treat it as "no icon" and not as an error to report.
//
// `target` and the non-constness carry decodeImageFileToBitmap's contract above verbatim:
// the backend's own render target, asserted downcast, mutating because creating a device
// bitmap is a mutation on Direct2D.
//
// NOT a general "decode bytes to bitmap" entry point, and the narrower name is deliberate.
// A general one would have no principled answer to the frame question, which is the only
// reason this function exists.
std::shared_ptr<Bitmap> decodeIconBytesToBitmap(RenderTarget& target, const void* data,
                                                size_t size, int pixelSize);

// --------------------------------------------------------------------------
// OFFSCREEN RASTERIZATION, AND THE OTHER DIRECTION THROUGH THIS HEADER
// --------------------------------------------------------------------------
//
// Everything above decodes. These two encode, and adding the return direction is what
// makes them belong here rather than in a header of their own: `decodeImageFile` turns
// bytes into a DecodedImage, `encodePng` turns a DecodedImage back into bytes, and
// `rasterizeToImage` is how a DecodedImage gets produced by DRAWING rather than by
// decoding. Same value type, three directions, one seam.
//
// WHY A FREE FUNCTION AND NOT A METHOD ON RenderTarget, WHICH IS THE WHOLE DESIGN.
// render_api.h's "WHAT IS NOT IN HERE" excludes object creation and device lifetime from
// the RenderTarget interface, on the ground that "a concrete backend owns its own
// construction; this interface is about the drawing calls only." An offscreen target IS
// construction, so a `CreateOffscreenTarget` virtual would narrow that exclusion. This
// shape does not: each backend implements this function in its own translation unit and
// constructs its own target there, which is exactly the sentence the exclusion states.
// So the exclusion is SATISFIED here rather than re-argued, and no virtual was added.
// That is worth writing down because an exclusion that quietly survives reads later like
// one nobody thought about.
//
// WHAT IT SERVES, STATED HONESTLY: FOUR CALLERS, AND THAT SENTENCE READ "ONE" UNTIL
// 2026-09-19, "TWO" UNTIL 2026-09-20 AND "THREE" UNTIL 2026-10-06. src/export_docx.cpp
// (three sites, diagrams and math boxes), src/print.cpp's renderPeekBitmap (the link-peek
// panel, ported once the copies were measured), src/print.cpp's printDebugPages (the
// --printpages page dump, ported once renderPages stopped handing out command lists) and
// src/print.cpp's rasterizePreviewPage (the print preview page, ported once a page flip was
// timed). render_api.h's CreateBitmap entry carries the numbers for the print sites. Each correction is DATED rather than
// just applied, because the count was load-bearing in an argument two headers away and
// a count that silently increments cannot be caught being stale.
//
// THE ARGUMENT DOES NOT CHANGE WITH THE COUNT, which is the point of restating it here:
// `decodeIconBytesToBitmap` above had two call sites and deduplication was half its
// justification. This still has none of that. The tree's only other offscreen target was
// in the retired src/image_win32.cpp (deleted in 0b39ae6), the Windows image backend, which
// could not route through a seam it implemented, so all four callers are here for PORTABILITY and none is here to
// share code with the others -- one rasterises a diagram into a docx part, one draws a
// hover panel at 2x, one paints a paginated A4 page for a test harness, one paints the
// page the print preview shows. Said plainly because a seam's stated reason gets quoted
// back, and "it's shared" would still be false at four.
//
// THE THIRD CALLER IS ALSO THE FIRST ONE THAT USES `draw` AS A VALUE IT WAS HANDED rather
// than as a lambda written at the call site: src/print.cpp's renderPages builds a
// PagePainter per page and printDebugPages passes it straight in, because PagePainter is
// declared as exactly this parameter's type. That is worth naming here as something the
// signature now supports on purpose -- if this parameter were ever narrowed to a template
// or a raw function pointer, that pass-through is what would break.
//
// `draw` is called exactly once, between the backend's own begin and end, with a target
// sized to the request and a black scratch brush bound to it -- the same
// mutate-the-color-per-call brush the 991 sites named by App::brush already use, except
// owned by this call rather than by App, because a Direct2D brush belongs to its target.
// Callers draw through the ordinary tinta::RenderTarget interface and name no backend type.
//
// THE CONTRACT, STATED BEFORE EITHER BACKEND, because two image libraries left to their
// own conventions here would diverge silently in a way only a pixel would show:
//
//   * The target is cleared to `clearColor` before `draw` is called, and the clear happens
//     HERE rather than in the callback so that neither backend can hand `draw` an
//     uninitialised buffer. Direct2D's WIC target does not start cleared and QImage's
//     buffer is uninitialised, so on both arms the untouched margins of a diagram would
//     otherwise be whatever the allocator last left there.
//
//     THE COLOUR IS A PARAMETER AND NOT A CONSTANT, and this entry is written the long way
//     because the short version was wrong. This contract first said "cleared to FULLY
//     TRANSPARENT -- these images are embedded in a document that supplies its own
//     background", which is a reasonable-sounding sentence about a document format and a
//     false one about this program: all three call sites in src/export_docx.cpp clear to
//     `app.theme.background`. Under a dark theme that is a dark fill behind light text, and
//     a transparent raster composited onto a white document page would have rendered light
//     text on white. Fixing a contract before it shipped is the entire reason it is written
//     above the implementations, so the near-miss is recorded rather than quietly corrected.
//     Pass tinta::colorF(0,0,0,0) for a transparent raster if a future caller wants one.
//   * Pixels come back PREMULTIPLIED BGRA, 8 bits per channel, TOP-DOWN, with stride
//     exactly width*4 and no row padding. That is DecodedImage's existing convention
//     from decodeImageFile and rasterizeSvg, carried rather than chosen.
//   * `width` and `height` are in device pixels and must each be >= 1 and < the
//     kMaxImageDimension bound above. A request outside that returns `ok == false`.
//   * Every failure -- an out-of-range size, a target that could not be created, a
//     brush that could not be created -- returns a DecodedImage with `ok == false` and
//     an EMPTY pixel vector. Never a partial buffer, so a caller that checks only `ok`
//     cannot read half-drawn pixels.
//   * `draw` may itself fail to draw anything; that is not detectable here and is not
//     an error. An empty-but-valid image is a legitimate result.
//
// ONE DELIBERATE BEHAVIOUR CHANGE, RECORDED RATHER THAN SLIPPED IN: the code this
// replaced in export_docx.cpp refused sizes above 16000, and this refuses at
// kMaxImageDimension (16384). The bound exists to stop a hostile dimension asking for
// gigabytes, both values are three orders of magnitude below that, and unifying on the
// seam's existing constant is worth more than preserving a number whose only property
// was being round.
DecodedImage rasterizeToImage(RenderTarget& target, unsigned width, unsigned height,
                              const Color& clearColor,
                              const std::function<void(RenderTarget&, Brush*)>& draw);

// Encode a DecodedImage as PNG and return the file bytes, or an EMPTY string on any
// failure -- including `!image.ok`, a zero dimension, or a pixel buffer whose size is
// not exactly width*height*4, which is checked rather than trusted because the buffer
// arrives from a caller and a short one would otherwise be read past.
//
// Takes no RenderTarget: encoding pixels needs no device, and requiring one would make
// this unusable from the places that have pixels and no target. The asymmetry with
// rasterizeToImage above is therefore deliberate and not an oversight.
//
// PNG rather than a format parameter, because both call sites want PNG: the docx image
// part and the clipboard's registered PNG format. A format enum with one value is a
// parameter that only documents an intention.
std::string encodePng(const DecodedImage& image);

} // namespace tinta

#endif // TINTA_IMAGE_API_H
