#ifndef TINTA_IMAGE_API_H
#define TINTA_IMAGE_API_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The image seam: fetch, decode, and SVG rasterization, with no backend in it.
//
// WHY THIS SEAM EXISTS AND WHY IT IS SHAPED THIS WAY
//
// render.cpp is 3,498 lines and carried three unguarded Windows-only includes --
// <urlmon.h>, <shlwapi.h> and <d2d1_3.h>. Unguarded is the operative word: app.h
// puts d2d1.h, dwrite.h and wincodec.h behind `#ifdef _WIN32` and forward-declares
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
// WHAT IS NOT IN HERE
//
// render.cpp has a THIRD image path -- the local-raster branch of getOrLoadImage --
// which decodes through WIC straight into an ID2D1Bitmap without ever materializing
// pixels. It is not routed through this seam, and that is a choice rather than an
// oversight: going via DecodedImage would add a full-image memcpy to every local
// image, which is a regression, and a portability seam is not the place to accept
// one.
//
// THAT SEAM NOW EXISTS -- RenderTarget::CreateBitmap in render_api.h -- and this entry
// used to end "it is the natural next consumer once a bitmap-from-pixels seam exists".
// The premise has changed and the conclusion has not: the memcpy objection is about
// DECODING through DecodedImage, not about where the bitmap comes from afterwards, so
// the branch is still excluded for its own reason. What did change is that the branch
// now feeds a PORTABLE field -- App::ImageEntry::bitmap is a
// shared_ptr<tinta::Bitmap>, and render.cpp wraps the WIC result in a D2DOwnedBitmap
// on the spot. So this is one Windows-terminal producer behind a portable type rather
// than a Windows-terminal path through the cache, and when it is ported, one line
// changes and nothing downstream of it does.
//
// Paths are std::wstring to match the rest of the tree, on the same reasoning
// platform_paths.h sets out at length: on Windows these hold UTF-16, on POSIX
// UTF-32, and each implementation converts at the OS boundary. This seam does not
// prejudge the string migration either way.

namespace tinta {

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
// property of "decode an untrusted file", and both backends enforce it identically
// so the two cannot drift.
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
// The two backends do NOT support identical format sets, and this seam does not
// pretend otherwise: WIC and Qt's image plugins overlap on the common web formats
// and diverge at the edges. What is asserted is agreement on PNG, which is the
// format the test emits and the one the seam is exercised with.
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
// Neither backend supports the full SVG specification. Direct2D's SVG subset omits
// <text> entirely; Qt's renderer covers SVG 1.2 Tiny. A document that renders
// differently on the two platforms is expected, and only geometry the test document
// pins is asserted.
DecodedImage rasterizeSvg(const void* data, size_t size, float scale,
                          float& intrinsicW, float& intrinsicH);

// Read an SVG file and rasterize it. Exists as its own entry point because the read
// is the platform-dependent half: a wide path reaches std::ifstream only through an
// MSVC extension, and the portable spelling differs. Behaviour is otherwise exactly
// rasterizeSvg over the file's bytes, including an unreadable or empty file
// returning `ok == false`.
DecodedImage rasterizeSvgFile(const std::wstring& path, float scale,
                              float& intrinsicW, float& intrinsicH);

} // namespace tinta

#endif // TINTA_IMAGE_API_H
