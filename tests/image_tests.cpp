// Assertions for include/image_api.h -- the image seam. Both backends, one file.
//
// WHAT THIS TEST CAN AND CANNOT PROVE, first, because the split is uneven.
//
// It CAN prove two of the three claims DecodedImage makes about its artifact. Tightly
// packed premultiplied BGRA is really the channel ORDER, the PREMULTIPLICATION, and
// the absence of row PADDING, each silently wrong in a way that draws a picture rather
// than crashing: a channel-swapped decode draws a blue apple, an unpremultiplied one
// draws a halo, a padded one draws a slanted image. The 2x2 PNG below separates the
// first two -- an opaque red pixel distinguishes BGRA from RGBA, and a WHITE pixel at
// alpha 0 distinguishes premultiplied from straight alpha, because 255 times 0 is 0
// while a straight-alpha decode leaves 255,255,255 sitting there. Both were confirmed
// by planted controls that failed on exactly these assertions.
//
// It CANNOT separate the third, and no fixture in this file can. At 32 bits per pixel
// a QImage scanline is already 4-byte aligned, so bytesPerLine is width * 4 for every
// image here, and a whole-buffer copy would agree with the row-by-row one at every
// size tested. The `pixels.size() == w * h * 4` assertion below proves the OUTPUT
// buffer is tightly packed, which is the contract callers depend on; it is not
// sensitive to a stride bug, and image_qt.cpp copies row by row anyway on the argument
// that a later format change should produce a slanted image rather than corruption.
// That argument is unasserted, and this paragraph is the record of it.
//
// It CANNOT prove anything about fetchToTempFile succeeding. That needs a network and
// a server, so the only assertions here are on its failure path, and the successful
// end of it is named as unwitnessed at its site below rather than left to look tested.
//
// The PNG is EMITTED, not checked in. Two reasons, both learned the hard way in this
// tree: a binary fixture in git is a fixture nobody can read a diff of, and a test
// file's own encoding is part of what it measures -- MSVC without /utf-8 reads source
// in the active code page, so every byte that matters here is written as a number.
// There is no non-ASCII character in this file at all.
//
// The stored-deflate detail is deliberate. A PNG needs a zlib stream, and zlib allows
// an UNCOMPRESSED block, so this needs no zlib dependency and no compressor: the
// bytes in the file are the bytes asserted on, which is what makes the expected values
// below hand-computable rather than copied out of a previous run.

#include "image_api.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int failures = 0;
long long checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

namespace {

void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

// The PNG CRC, table-less. Polynomial 0xEDB88320, pre- and post-inverted.
uint32_t crc32Of(const std::vector<uint8_t>& data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t b : data) {
        crc ^= b;
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t adler32Of(const std::vector<uint8_t>& data) {
    uint32_t a = 1, b = 0;
    for (uint8_t byte : data) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void appendChunk(std::vector<uint8_t>& out, const char* type,
                 const std::vector<uint8_t>& data) {
    be32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> typed;
    for (int i = 0; i < 4; ++i) typed.push_back(static_cast<uint8_t>(type[i]));
    typed.insert(typed.end(), data.begin(), data.end());
    out.insert(out.end(), typed.begin(), typed.end());
    be32(out, crc32Of(typed));
}

// A 2x2 8-bit RGBA PNG, straight (non-premultiplied) alpha, filter type 0 on every
// row, one stored deflate block. Pixel values chosen for what they discriminate:
//
//   (0,0) 255,  0,  0,255  opaque red    -> BGRA 0,0,255,255   (order)
//   (1,0)   0,255,  0,255  opaque green  -> BGRA 0,255,0,255   (order, again)
//   (0,1)   0,  0,255,128  half blue     -> BGRA 128,0,0,128   (premultiply)
//   (1,1) 255,255,255,  0  clear white   -> BGRA 0,0,0,0       (premultiply, hard)
std::vector<uint8_t> makePng2x2() {
    const std::vector<uint8_t> raw = {
        0, 255, 0,   0,   255, 0,   255, 0,   255,  // row 0: filter, red, green
        0, 0,   0,   255, 128, 255, 255, 255, 0     // row 1: filter, blue, white
    };

    std::vector<uint8_t> zlib;
    zlib.push_back(0x78);  // CMF: deflate, 32K window
    zlib.push_back(0x01);  // FLG: no dictionary, and 0x7801 % 31 == 0 as required
    zlib.push_back(0x01);  // BFINAL=1, BTYPE=00 (stored)
    const uint16_t len = static_cast<uint16_t>(raw.size());
    zlib.push_back(static_cast<uint8_t>(len & 0xFF));
    zlib.push_back(static_cast<uint8_t>(len >> 8));
    zlib.push_back(static_cast<uint8_t>(~len & 0xFF));
    zlib.push_back(static_cast<uint8_t>((~len >> 8) & 0xFF));
    zlib.insert(zlib.end(), raw.begin(), raw.end());
    be32(zlib, adler32Of(raw));

    std::vector<uint8_t> ihdr;
    be32(ihdr, 2);         // width
    be32(ihdr, 2);         // height
    ihdr.push_back(8);     // bit depth
    ihdr.push_back(6);     // colour type 6 == truecolour with alpha
    ihdr.push_back(0);     // compression: deflate
    ihdr.push_back(0);     // filter method 0
    ihdr.push_back(0);     // no interlace

    std::vector<uint8_t> png = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    appendChunk(png, "IHDR", ihdr);
    appendChunk(png, "IDAT", zlib);
    appendChunk(png, "IEND", {});
    return png;
}

// A pixel comparison that is exact where the answer is exact and tolerant by exactly
// one where it cannot be. Premultiplying 255 by an alpha of 128 is 128 under Qt's
// rounding and 128 under WIC's, but the two round independently and the seam's
// contract is the byte layout, not the last bit of a rounding rule. Where alpha is
// 255 or 0 there is nothing to round, so those are asserted exactly -- which is where
// all three of the discriminating claims live anyway.
bool near1(uint8_t got, uint8_t want) {
    const int d = static_cast<int>(got) - static_cast<int>(want);
    return d <= 1 && d >= -1;
}

std::filesystem::path scratchDir() {
    return std::filesystem::temp_directory_path();
}

} // namespace

int main() {
    // --- decodeImageFile: the artifact contract -------------------------------
    const std::filesystem::path pngPath = scratchDir() / "tinta_image_seam_2x2.png";
    {
        const std::vector<uint8_t> png = makePng2x2();
        std::ofstream out(pngPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()),
                  static_cast<std::streamsize>(png.size()));
        out.close();
        check(std::filesystem::exists(pngPath), "the emitted PNG reached disk");
        // 8 signature + (12 + 13) IHDR + (12 + 11 + 18) IDAT + 12 IEND = 86.
        // Asserted because a mechanically built buffer that is the wrong LENGTH still
        // decodes if the decoder is lenient, and then every pixel assertion below is
        // measuring a file nobody predicted the shape of.
        check(std::filesystem::file_size(pngPath) == 86,
              "the emitted PNG is the 86 bytes its chunk arithmetic predicts");
    }

    // path::wstring() rather than a byte-wise widening loop: the seam takes wstring by
    // design (UTF-16 on Windows, UTF-32 on POSIX, converted at the OS boundary), and
    // filesystem::path is the one thing that knows how to cross that on both.
    const tinta::DecodedImage decoded = tinta::decodeImageFile(pngPath.wstring());
    check(decoded.ok, "the 2x2 PNG decodes");
    check(decoded.width == 2, "decoded width is 2");
    check(decoded.height == 2, "decoded height is 2");
    check(decoded.pixels.size() == 2 * 2 * 4,
          "pixels are tightly packed: size is exactly w * h * 4, with no row padding");

    if (decoded.ok && decoded.pixels.size() == 16) {
        const uint8_t* p = decoded.pixels.data();
        // (0,0) opaque red. This quartet alone separates BGRA from RGBA: a decoder
        // handing back RGBA would put 255 in the first byte, not the third.
        check(p[0] == 0, "(0,0) blue channel is 0 -- red is not in the blue slot");
        check(p[1] == 0, "(0,0) green channel is 0");
        check(p[2] == 255, "(0,0) red channel is 255 in BGRA's third byte");
        check(p[3] == 255, "(0,0) alpha is 255");
        // (1,0) opaque green -- the same claim from the other side.
        check(p[4] == 0, "(1,0) blue channel is 0");
        check(p[5] == 255, "(1,0) green channel is 255");
        check(p[6] == 0, "(1,0) red channel is 0");
        check(p[7] == 255, "(1,0) alpha is 255");
        // (0,1) blue at alpha 128. The one quartet with rounding in it.
        check(near1(p[8], 128), "(0,1) blue is premultiplied to about 128");
        check(p[9] == 0, "(0,1) green channel is 0");
        check(p[10] == 0, "(0,1) red channel is 0");
        check(p[11] == 128, "(0,1) alpha survives premultiplication unchanged at 128");
        // (1,1) WHITE at alpha 0. The hard one, and the reason it is in the fixture:
        // straight alpha leaves 255,255,255,0 here and premultiplied gives 0,0,0,0.
        // No tolerance, because multiplying by zero has nothing to round.
        check(p[12] == 0 && p[13] == 0 && p[14] == 0,
              "(1,1) clear white premultiplies to 0,0,0 -- not left at 255,255,255");
        check(p[15] == 0, "(1,1) alpha is 0");
    } else {
        std::cerr << "SKIPPED 16 pixel assertions: the decode did not produce a "
                     "16-byte buffer, so there was nothing to read them from\n";
        failures++;
    }

    // --- decodeImageFile: the failure contract --------------------------------
    // Not padding. `ok == false` has to be distinguishable from a successful decode of
    // a zero-area image, which is what image_api.h promises by clearing pixels, and a
    // backend that resizes the buffer before discovering the failure breaks it.
    {
        const std::filesystem::path missing =
            scratchDir() / "tinta_image_seam_does_not_exist.png";
        std::filesystem::remove(missing);
        const tinta::DecodedImage bad = tinta::decodeImageFile(missing.wstring());
        check(!bad.ok, "a missing file does not decode");
        check(bad.pixels.empty(), "a failed decode leaves pixels empty, not sized");
        check(bad.width == 0 && bad.height == 0,
              "a failed decode leaves the dimensions at 0");
    }
    {
        // A file that exists and is not an image. Distinct from the missing-file case:
        // one fails at open, the other inside the decoder, and only the second can
        // leave a partly built buffer behind.
        const std::filesystem::path junk = scratchDir() / "tinta_image_seam_junk.png";
        std::ofstream out(junk, std::ios::binary);
        out << "this is not a PNG";
        out.close();
        const tinta::DecodedImage bad = tinta::decodeImageFile(junk.wstring());
        check(!bad.ok, "a non-image file does not decode");
        check(bad.pixels.empty(), "a failed decode of junk leaves pixels empty");
        std::filesystem::remove(junk);
    }

    // --- rasterizeSvg: intrinsic size is NOT pixel size -----------------------
    // This distinction is the trap the whole seam is built around. loadSvgBitmap
    // reports the document's intrinsic size to the layout engine while holding twice
    // as many pixels, and conflating the two draws every SVG in the document at double
    // size with nothing else looking wrong. So both numbers are asserted, separately,
    // at a scale that is not 1.
    {
        const std::string svg =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"20\">"
            "<rect width=\"10\" height=\"20\" fill=\"#ff0000\"/></svg>";
        float iw = -1.0f, ih = -1.0f;  // poisoned: a backend that writes NOTHING fails
        const tinta::DecodedImage r =
            tinta::rasterizeSvg(svg.data(), svg.size(), 2.0f, iw, ih);
        check(r.ok, "a width/height SVG rasterizes");
        check(iw == 10.0f, "intrinsic width is the document's 10 DIPs, not 20 pixels");
        check(ih == 20.0f, "intrinsic height is the document's 20 DIPs");
        check(r.width == 20, "raster width is 10 DIPs at scale 2 == 20 pixels");
        check(r.height == 40, "raster height is 20 DIPs at scale 2 == 40 pixels");
        check(r.pixels.size() == static_cast<size_t>(r.width) * r.height * 4,
              "the rasterized SVG is tightly packed too");
        if (r.ok && r.pixels.size() >= 4) {
            // The rect covers the whole document, so the top-left pixel is opaque red
            // premultiplied: 0,0,255,255. This is the assertion that proves something
            // was DRAWN rather than a correctly sized transparent buffer returned.
            check(r.pixels[3] == 255, "the SVG actually drew: top-left alpha is 255");
            check(r.pixels[2] == 255 && r.pixels[1] == 0 && r.pixels[0] == 0,
                  "the SVG drew RED at the top left, in BGRA order");
        }
    }
    {
        // viewBox-only, the second branch of the size resolution both backends walk.
        const std::string svg =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 30 40\">"
            "<rect width=\"30\" height=\"40\" fill=\"#00ff00\"/></svg>";
        float iw = -1.0f, ih = -1.0f;
        const tinta::DecodedImage r =
            tinta::rasterizeSvg(svg.data(), svg.size(), 1.0f, iw, ih);
        check(r.ok, "a viewBox-only SVG rasterizes");
        check(iw == 30.0f, "intrinsic width falls back to the viewBox width");
        check(ih == 40.0f, "intrinsic height falls back to the viewBox height");
        check(r.width == 30 && r.height == 40, "at scale 1 pixels equal DIPs");
    }
    {
        // The 4096 clamp, exercised cheaply. A 5000-DIP document at scale 0.1 is a
        // 410-pixel raster if the clamp fires and 500 if it does not, so this reads
        // the clamp without allocating a 100-megapixel buffer -- and it is the only
        // assertion here at a scale below 1.
        const std::string svg =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"5000\" height=\"5000\">"
            "<rect width=\"5000\" height=\"5000\" fill=\"#0000ff\"/></svg>";
        float iw = -1.0f, ih = -1.0f;
        const tinta::DecodedImage r =
            tinta::rasterizeSvg(svg.data(), svg.size(), 0.1f, iw, ih);
        check(r.ok, "an oversized SVG still rasterizes, clamped");
        check(iw == 4096.0f, "intrinsic width is clamped to 4096 DIPs, not 5000");
        check(ih == 4096.0f, "intrinsic height is clamped to 4096 DIPs");
        check(r.width == 410 && r.height == 410,
              "4096 DIPs at scale 0.1 is 410 pixels: ceil, from the CLAMPED size");
    }
    {
        // Failure. Both out-params stay poisoned only if the backend writes them, so
        // this asserts they are RESET to 0 -- the contract is that a caller reading
        // them after a failure sees zero, not whatever it passed in.
        const std::string junk = "this is not an SVG";
        float iw = -1.0f, ih = -1.0f;
        const tinta::DecodedImage r =
            tinta::rasterizeSvg(junk.data(), junk.size(), 2.0f, iw, ih);
        check(!r.ok, "junk does not rasterize as an SVG");
        check(r.pixels.empty(), "a failed rasterize leaves pixels empty");
        check(iw == 0.0f && ih == 0.0f,
              "a failed rasterize zeroes the intrinsic out-params rather than "
              "leaving the caller's values");
    }
    {
        // Degenerate arguments, which reach the guards rather than either backend.
        float iw = -1.0f, ih = -1.0f;
        check(!tinta::rasterizeSvg(nullptr, 10, 2.0f, iw, ih).ok,
              "a null buffer is rejected");
        const std::string svg = "<svg/>";
        check(!tinta::rasterizeSvg(svg.data(), 0, 2.0f, iw, ih).ok,
              "a zero-length buffer is rejected");
        check(!tinta::rasterizeSvg(svg.data(), svg.size(), 0.0f, iw, ih).ok,
              "a zero scale is rejected rather than producing a 0x0 raster");
        check(!tinta::rasterizeSvg(svg.data(), svg.size(), -1.0f, iw, ih).ok,
              "a negative scale is rejected");
    }

    // --- rasterizeSvgFile: the same, through a path ---------------------------
    {
        const std::filesystem::path svgPath = scratchDir() / "tinta_image_seam.svg";
        std::ofstream out(svgPath, std::ios::binary);
        out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"4\">"
               "<rect width=\"8\" height=\"4\" fill=\"#ff0000\"/></svg>";
        out.close();
        float iw = -1.0f, ih = -1.0f;
        const tinta::DecodedImage r = tinta::rasterizeSvgFile(svgPath.wstring(), 2.0f,
                                                             iw, ih);
        check(r.ok, "an SVG on disk rasterizes");
        check(iw == 8.0f && ih == 4.0f, "the file path reports the same intrinsic size");
        check(r.width == 16 && r.height == 8, "the file path scales the same way");
        std::filesystem::remove(svgPath);

        float mw = -1.0f, mh = -1.0f;
        const std::filesystem::path missing =
            scratchDir() / "tinta_image_seam_missing.svg";
        std::filesystem::remove(missing);
        check(!tinta::rasterizeSvgFile(missing.wstring(), 2.0f, mw, mh).ok,
              "a missing SVG file does not rasterize");
        check(mw == 0.0f && mh == 0.0f,
              "a missing SVG file zeroes the intrinsic out-params");
    }

    // --- fetchToTempFile: the failure path only, and said plainly -------------
    //
    // [DEFERRED-VERIFY] THE SUCCESSFUL END OF THIS FUNCTION IS UNWITNESSED. Nothing
    // below fetches anything. A real fetch needs a network and a server, and a test
    // that reaches one is a test that fails in a tunnel, so the successful path is
    // asserted nowhere and this comment is the only honest record of that. What the
    // two assertions DO buy is not nothing: they prove the seam resolves at link time
    // on this platform and that a malformed URL returns empty rather than a garbage
    // path a caller would then try to open.
    //
    // Note the two backends fail here for DIFFERENT reasons, which is why the
    // assertion is on the return value and not on any mechanism: on Windows
    // URLDownloadToCacheFileW rejects the scheme, while on Qt there is no
    // QCoreApplication in this binary, so QNetworkAccessManager could not deliver
    // through an event loop even if the URL were good.
    check(tinta::fetchToTempFile("").empty(), "an empty URL fetches nothing");
    check(tinta::fetchToTempFile("not-a-scheme://nowhere/x.png").empty(),
          "an unfetchable URL returns an empty path, not a bogus one");

    // --- the constant itself --------------------------------------------------
    // kMaxImageDimension is an EXCLUSIVE bound -- the original test was `w < 16384`
    // and a dimension equal to it is rejected. Asserted because the friendlier `<=`
    // is exactly the tidy-up someone makes later, and no image in any fixture here is
    // big enough to catch it.
    check(tinta::kMaxImageDimension == 16384, "the dimension bound is 16384");

    std::filesystem::remove(pngPath);

    if (failures == 0) {
        std::cout << "image: all " << checks << " assertions passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s) out of " << checks << " assertions\n";
    return 1;
}
