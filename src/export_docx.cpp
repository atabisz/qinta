// DOCX export (#export_as): walks the parsed element tree and packs OOXML
// into a stored zip written by hand (no third-party code). Text, lists,
// tables, quotes, and alerts map to WordprocessingML with the active theme
// baked in; mermaid diagrams and math rasterize to PNG at 2x through the
// same primitives the viewer draws, and land as embedded pictures.

#include "export.h"

// For platform::setClipboardImage and platform::clipboardImage. The clipboard half of
// this file is now two calls into a seam with a Qt implementation behind it, and NOTHING
// here is Windows-only any more. The clause that used to end this comment -- "what stays
// Windows-only here is the DOCX half, for objbase.h and not for the clipboard" -- named a
// dependency the DOCX half never had: the zip and the WordprocessingML are written by
// hand through <fstream> and std::string, with no COM, no GUID and no WIC anywhere in
// them. See the block below the includes for what that cost.
#include "clipboard.h"
#include "editor.h"
#include "image_api.h"
#include "math_render.h"
#include "mermaid_ext.h"
// For ensureDashedStrokeStyle. The dashed stroke style used to be built HERE, from
// app.d2dFactory, with properties byte-identical to the one the paint pass builds --
// so this file now borrows the app's rather than keeping a second copy. The #ifdef that
// chooses between a Direct2D factory object and the portable marker stays behind that
// call, in the file that owns the object, which is why this one has no backend guard of
// its own.
#include "paint_document.h"
#include "render.h"
#include "utils.h"

// THREE WINDOWS HEADERS STOOD HERE AND NOTHING IN THIS FILE USED THEM ANY MORE, which
// is recorded rather than quietly deleted because the stale comment is the interesting
// part. It read: "Still Windows headers, and the narrowness is the point. What survives
// in this file is the CLIPBOARD half -- CF_DIB, the registered L"PNG" format, the
// CF_BITMAP handle read and the WIC decode they feed -- which include/clipboard.h
// deliberately left outside that seam because moving it would drag WIC types through a
// header that has none."
//
// Every clause of that was true when written and none of it was true by the time it was
// read. The image half DID move into the seam -- clipboard.h declares clipboardHasImage,
// setClipboardImage and clipboardImage, the last returning a tinta::DecodedImage through
// a forward declaration precisely so the header still names no WIC type. What was left
// behind was <objbase.h>, <ole2.h> and <wincodec.h> with no remaining caller, plus a
// comment asserting a dependency that had already been ported out from under it.
//
// THE COST WAS NOT THE THREE LINES, IT WAS THE MEASUREMENT THEY BLOCKED. The census
// (scripts/census-nonportable-linux.sh) stops at the first unresolvable header, so this
// file reported `fatal 1, errors 0` -- indistinguishable at a glance from a file with no
// work left, and in fact a file that had never been read at all.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace qmd;

namespace {

// --------------------------------------------------------------------------
// Zip container (stored entries only)
// --------------------------------------------------------------------------

uint32_t crc32Of(const std::string& data) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char byte : data) {
        crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

struct ZipWriter {
    std::string out;
    struct Entry {
        std::string name;
        uint32_t crc, size, offset;
    };
    std::vector<Entry> entries;

    void u16(uint16_t v) {
        out += (char)(v & 0xFF);
        out += (char)(v >> 8);
    }
    void u32(uint32_t v) {
        out += (char)(v & 0xFF);
        out += (char)((v >> 8) & 0xFF);
        out += (char)((v >> 16) & 0xFF);
        out += (char)(v >> 24);
    }

    void add(const std::string& name, const std::string& data) {
        Entry entry{name, crc32Of(data), (uint32_t)data.size(),
                    (uint32_t)out.size()};
        u32(0x04034B50u);
        u16(20);      // version needed
        u16(0);       // flags
        u16(0);       // method: stored
        u16(0x6000);  // 12:00
        u16(0x5D16);  // 2026-08-22
        u32(entry.crc);
        u32(entry.size);
        u32(entry.size);
        u16((uint16_t)name.size());
        u16(0);
        out += name;
        out += data;
        entries.push_back(std::move(entry));
    }

    std::string finish() {
        uint32_t directoryStart = (uint32_t)out.size();
        for (const auto& entry : entries) {
            u32(0x02014B50u);
            u16(20);
            u16(20);
            u16(0);
            u16(0);
            u16(0x6000);
            u16(0x5D16);
            u32(entry.crc);
            u32(entry.size);
            u32(entry.size);
            u16((uint16_t)entry.name.size());
            u16(0);
            u16(0);
            u16(0);
            u16(0);
            u32(0);
            u32(entry.offset);
            out += entry.name;
        }
        uint32_t directorySize = (uint32_t)out.size() - directoryStart;
        u32(0x06054B50u);
        u16(0);
        u16(0);
        u16((uint16_t)entries.size());
        u16((uint16_t)entries.size());
        u32(directorySize);
        u32(directoryStart);
        u16(0);
        return std::move(out);
    }
};

// --------------------------------------------------------------------------
// Small helpers
// --------------------------------------------------------------------------

std::string xmlEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:
                // Control characters are invalid in XML 1.0
                if ((unsigned char)c >= 0x20 || c == '\t') out += c;
        }
    }
    return out;
}

std::string hex6(tinta::Color c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02X%02X%02X", (int)(c.r * 255 + 0.5f),
             (int)(c.g * 255 + 0.5f), (int)(c.b * 255 + 0.5f));
    return buf;
}

// Alpha-composited over the theme background, as Word has no alpha colors
std::string hexOver(tinta::Color c, tinta::Color background) {
    tinta::Color mixed;
    mixed.r = c.r * c.a + background.r * (1 - c.a);
    mixed.g = c.g * c.a + background.g * (1 - c.a);
    mixed.b = c.b * c.a + background.b * (1 - c.a);
    mixed.a = 1.0f;
    return hex6(mixed);
}

bool readBinaryFile(const std::wstring& path, std::string& out) {
    // fs::path, not the bare wstring: claim C58. This used to end "This file is FATAL on
    // objbase.h in the Linux census, so g++ cannot reach these three sites to report them
    // -- they were classified from the signature and verified through a stubbed read, not
    // from a census row. A class that is invisible to the instrument is still present."
    //
    // THE WORKAROUND WAS RIGHT AND IS NOW UNNECESSARY, which is the happier way for a note
    // like this to expire: the objbase.h that stopped the read was a dead include, so as of
    // 2026-09-20 the census compiles this whole file and these three sites are covered by a
    // row rather than by a stub. The reasoning it records is kept because it is the same
    // reasoning that would have found the dead include a slice earlier -- somebody who
    // stubs a header to read past it is one question away from asking whether the header is
    // needed at all, and that question was not asked.
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return !out.empty() && out.size() < 20u * 1024 * 1024;
}

// PNG / GIF / JPEG pixel dimensions from the header bytes
bool imageDimensions(const std::string& data, int& width, int& height) {
    const unsigned char* b = (const unsigned char*)data.data();
    size_t n = data.size();
    if (n > 24 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') {
        width = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
        height = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
        return width > 0 && height > 0;
    }
    if (n > 10 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F') {
        width = b[6] | (b[7] << 8);
        height = b[8] | (b[9] << 8);
        return width > 0 && height > 0;
    }
    if (n > 4 && b[0] == 0xFF && b[1] == 0xD8) {
        size_t i = 2;
        while (i + 9 < n) {
            if (b[i] != 0xFF) break;
            unsigned char marker = b[i + 1];
            size_t length = (b[i + 2] << 8) | b[i + 3];
            if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 &&
                marker != 0xC8 && marker != 0xCC) {
                height = (b[i + 5] << 8) | b[i + 6];
                width = (b[i + 7] << 8) | b[i + 8];
                return width > 0 && height > 0;
            }
            i += 2 + length;
        }
    }
    return false;
}

// --------------------------------------------------------------------------
// Rasterization: prims and math boxes -> PNG bytes through the image seam
// --------------------------------------------------------------------------

constexpr float kRasterScale = 2.0f;

// The old RasterTarget::open's sizing rule, kept EXACTLY rather than deferred to
// image_api.h's own limits, because the two disagree in both directions. This rounds UP
// and floors at 1, so a 0.4-DIP-tall diagram rasterises as a single pixel row instead of
// being refused; and its ceiling of 16000 is TIGHTER than kMaxImageDimension's 16384, so
// dropping it would let a diagram between the two through where it used to fail. Neither
// is a change this port gets to make while calling itself a portability change.
constexpr unsigned kRasterMaxDimension = 16000;

bool rasterDimensions(float w, float h, unsigned& outW, unsigned& outH) {
    outW = (unsigned)std::ceil(std::max(1.0f, w));
    outH = (unsigned)std::ceil(std::max(1.0f, h));
    return outW <= kRasterMaxDimension && outH <= kRasterMaxDimension;
}

// THE REMAINDER IS GONE, 2026-09-19, AND WHAT IT PREDICTED IS WHAT HAPPENED. There used
// to be a clipboardBitmapToPng here -- 52 lines of CreateStreamOnHGlobal,
// IWICBitmapEncoder and GetHGlobalFromStream, the last survivor of an encodeWicPng that
// once had four call sites. Its comment argued it was a deliberate remainder rather than
// an unfinished port, and it named the slice that would remove it:
//
//   "Routing it through tinta::encodePng would take a GDI GetDIBits rewrite of the
//    clipboard read, which is a different slice against a recorded decision. Worse, it
//    cannot be done NAIVELY: WICBitmapIgnoreAlpha below yields 32bppBGR, and handing
//    those bytes to a function whose contract says premultiplied BGRA would encode a
//    fully transparent PNG -- a plausible-looking file with nothing in it."
//
// That is exactly the slice, and the trap it named is the reason platform::clipboardImage
// forces the alpha byte to 255 and tests/clipboard_tests.cpp asserts a PIXEL rather than a
// size. The clipboard image path lives in src/clipboard_qt.cpp behind include/clipboard.h
// (src/clipboard_win32.cpp held the Windows half until it was deleted on 2026-10-07).
//
// WHAT THIS FILE KEEPS OF WIC IS NOTHING, and what the TREE keeps of it is most of it --
// stating the second half matters more than the first, because "WIC is gone" is the kind
// of sentence that gets believed. Measured at this commit: src/print.cpp names the
// factory at five places -- four real calls (two CreatePrintControl, CreateStream,
// CreateEncoder) plus a null check -- src/render.cpp:2460 holds one null check,
// src/d2d_init.cpp:83 is what creates it, and src/image_win32.cpp is built on WIC
// throughout. The factory is as alive as it was; two of its callers moved.

// Draws a prim list built at `scale` the way layoutMermaidExtDiagram
// translates it for the viewer.
//
// THE TARGET AND THE BRUSH ARE PASSED IN rather than being a struct this file owns,
// which is what let RasterTarget go. tinta::rasterizeToImage creates the offscreen
// surface, clears it and hands both to a callback -- so the thing this function needed
// RasterTarget for (a device to draw on) now arrives as an argument, and the thing it
// needed WIC for (getting pixels back out) is the seam's return value.
void drawPrims(App& app, tinta::RenderTarget& target, tinta::Brush* brush,
               const mermaidext::Built& built, float scale) {
    // The app's dashed stroke style, NOT a second one built here. Its properties were
    // compared field by field against the D2D1_STROKE_STYLE_PROPERTIES this file used to
    // fill in -- FLAT/FLAT/FLAT, MITER, 10.0f, DASH, 0.0f -- and they are identical, so
    // reuse is a behaviour-preserving change rather than a close-enough one.
    //
    // Unconditionally, where the paint pass asks std::any_of first: this walks mermaid
    // prims and has no connector list to interrogate, and a null stroke style makes
    // DrawLine draw SOLID without complaining. That silent-solid failure is exactly what
    // ensureDashedStrokeStyle exists to stop, and it is why the lazy creation could not
    // stay inside the paint pass once a second caller appeared.
    ensureDashedStrokeStyle(app);
    tinta::StrokeStyle* dashed = app.dashedStrokeStyle.get();

    auto fillPolygon = [&](const std::vector<mermaidext::Point>& pts,
                           const tinta::Color& fill,
                           const tinta::Color& stroke, float strokeWidth) {
        if (pts.size() < 3) return;
        std::unique_ptr<tinta::PathSink> sink = target.BeginPath();
        if (!sink) return;
        // BeginFigure and EndFigure lost their enum arguments to the seam, on a census
        // rather than a guess: FILLED at 7 of 7 sites and CLOSED at 7 of 7, this one
        // among them. See include/render_api.h:294-297.
        sink->BeginFigure(tinta::pointF(pts[0].x, pts[0].y));
        for (size_t i = 1; i < pts.size(); i++) {
            sink->AddLine(tinta::pointF(pts[i].x, pts[i].y));
        }
        sink->EndFigure();
        std::unique_ptr<tinta::PathGeometry> geometry = sink->Close();
        // The old code released the geometry on this path too and drew nothing, so an
        // early return here is the same behaviour with the release done by unique_ptr.
        if (!geometry) return;
        if (fill.a > 0) {
            brush->SetColor(fill);
            target.FillGeometry(geometry.get(), brush);
        }
        if (stroke.a > 0 && strokeWidth > 0) {
            brush->SetColor(stroke);
            target.DrawGeometry(geometry.get(), brush, strokeWidth);
        }
    };

    for (const auto& prim : built.prims) {
        tinta::Color fill = resolveDiagramRole(app, prim, prim.fill);
        tinta::Color stroke = resolveDiagramRole(app, prim, prim.stroke);
        float strokeWidth = prim.strokeWidth > 0 ? prim.strokeWidth : 1.2f;
        switch (prim.type) {
            case mermaidext::PrimType::Rect: {
                tinta::RectF rect =
                    tinta::rectF(prim.x1, prim.y1, prim.x2, prim.y2);
                if (fill.a > 0) {
                    brush->SetColor(fill);
                    target.FillRectangle(rect, brush);
                }
                if (stroke.a > 0 && prim.strokeWidth > 0) {
                    brush->SetColor(stroke);
                    target.DrawRectangle(rect, brush, strokeWidth);
                }
                break;
            }
            case mermaidext::PrimType::RoundRect: {
                tinta::RoundedRect rect = tinta::roundedRect(
                    tinta::rectF(prim.x1, prim.y1, prim.x2, prim.y2),
                    prim.radius, prim.radius);
                if (fill.a > 0) {
                    brush->SetColor(fill);
                    target.FillRoundedRectangle(rect, brush);
                }
                if (stroke.a > 0 && prim.strokeWidth > 0) {
                    brush->SetColor(stroke);
                    target.DrawRoundedRectangle(rect, brush, strokeWidth);
                }
                break;
            }
            case mermaidext::PrimType::Ellipse: {
                tinta::Ellipse ellipse = tinta::ellipse(
                    tinta::pointF((prim.x1 + prim.x2) * 0.5f,
                                  (prim.y1 + prim.y2) * 0.5f),
                    (prim.x2 - prim.x1) * 0.5f, (prim.y2 - prim.y1) * 0.5f);
                if (fill.a > 0) {
                    brush->SetColor(fill);
                    target.FillEllipse(ellipse, brush);
                }
                if (stroke.a > 0 && prim.strokeWidth > 0) {
                    brush->SetColor(stroke);
                    target.DrawEllipse(ellipse, brush, strokeWidth);
                }
                break;
            }
            case mermaidext::PrimType::Line: {
                float x1 = prim.x1, y1 = prim.y1;
                float x2 = prim.x2, y2 = prim.y2;
                float endX = x2, endY = y2;
                brush->SetColor(stroke);
                if (prim.arrow) {
                    // Filled triangle head, line shortened under it
                    float dx = x2 - x1, dy = y2 - y1;
                    float length = std::sqrt(dx * dx + dy * dy);
                    if (length > 0.01f) {
                        dx /= length;
                        dy /= length;
                        float head = 9.0f * scale;
                        float wing = 4.5f * scale;
                        endX = x2 - dx * head * 0.7f;
                        endY = y2 - dy * head * 0.7f;
                        fillPolygon(
                            {{x2, y2},
                             {x2 - dx * head + dy * wing,
                              y2 - dy * head - dx * wing},
                             {x2 - dx * head - dy * wing,
                              y2 - dy * head + dx * wing}},
                            stroke, tinta::colorF(0, 0, 0, 0), 0.0f);
                    }
                }
                target.DrawLine(tinta::pointF(x1, y1),
                                tinta::pointF(endX, endY), brush, strokeWidth,
                                prim.dashed ? dashed : nullptr);
                if (prim.openArrow) {
                    float dx = x2 - x1, dy = y2 - y1;
                    float length = std::sqrt(dx * dx + dy * dy);
                    if (length > 0.01f) {
                        dx /= length;
                        dy /= length;
                        float size = 8.0f * scale;
                        float wing = 4.0f * scale;
                        tinta::PointF tip = tinta::pointF(x2, y2);
                        target.DrawLine(
                            tip,
                            tinta::pointF(x2 - dx * size + dy * wing,
                                          y2 - dy * size - dx * wing),
                            brush, strokeWidth);
                        target.DrawLine(
                            tip,
                            tinta::pointF(x2 - dx * size - dy * wing,
                                          y2 - dy * size + dx * wing),
                            brush, strokeWidth);
                    }
                }
                break;
            }
            case mermaidext::PrimType::Polygon:
                fillPolygon(prim.pts, fill,
                            prim.stroke != mermaidext::Role::None
                                ? stroke
                                : tinta::colorF(0, 0, 0, 0),
                            strokeWidth);
                break;
            case mermaidext::PrimType::Slice: {
                // KNOWN DIVERGENCE, deliberately left standing: src/render.cpp's
                // buildSliceGeometry special-cases a full circle (a1 - a0 >= 2pi) as
                // two half arcs, because a single arc from a point back to itself is
                // degenerate. This case does not, so a 360-degree slice reaches the
                // backend with start == end and draws NOTHING here while drawing
                // correctly on screen.
                //
                // Not fixed as part of the path seam, on purpose. include/render_api.h
                // records the reasoning: a description-shaped seam -- CreateSlice(centre,
                // radius, a0, a1) -- would have had to pick one of the two behaviours,
                // hiding this fix inside a portability change and silently altering what
                // an exported .docx looks like. It is a real bug with its own fix, and
                // mirroring the sink is what keeps it visible and separately fixable.
                //
                // IT IS NOW ON THE SEAM, which the previous version of this comment said
                // it was not, for a reason that has since gone away: `target` used to be
                // a raw ID2D1RenderTarget over a WIC bitmap that no tinta::RenderTarget
                // wrapped. tinta::rasterizeToImage supplies a wrapped one, so the excuse
                // is spent. The divergence above is untouched by that -- moving the calls
                // is not the same as changing which shape they describe.
                float r = prim.radius;
                tinta::PointF center = tinta::pointF(prim.x1, prim.y1);
                tinta::PointF start =
                    tinta::pointF(prim.x1 + r * std::cos(prim.a0),
                                  prim.y1 + r * std::sin(prim.a0));
                tinta::PointF end =
                    tinta::pointF(prim.x1 + r * std::cos(prim.a1),
                                  prim.y1 + r * std::sin(prim.a1));
                std::unique_ptr<tinta::PathSink> sink = target.BeginPath();
                if (!sink) break;
                sink->BeginFigure(center);
                sink->AddLine(start);
                // The three D2D1_ARC_SEGMENT fields tinta::ArcSegment does not carry were
                // constants at all four sites, this one included: size was {r, r},
                // rotationAngle 0, sweepDirection CLOCKWISE. largeArc is the one that
                // varied, and the test below is character-for-character the old one.
                sink->AddArc(tinta::ArcSegment{
                    end, r, (prim.a1 - prim.a0) > 3.14159265f});
                sink->EndFigure();
                std::unique_ptr<tinta::PathGeometry> geometry = sink->Close();
                if (!geometry) break;
                if (fill.a > 0) {
                    brush->SetColor(fill);
                    target.FillGeometry(geometry.get(), brush);
                }
                if (stroke.a > 0 && prim.strokeWidth > 0) {
                    brush->SetColor(stroke);
                    target.DrawGeometry(geometry.get(), brush, strokeWidth);
                }
                break;
            }
            case mermaidext::PrimType::Text: {
                std::wstring wide = toWide(prim.text);
                if (wide.empty()) break;
                // Guarded where the old code dereferenced app.dwriteFactory outright.
                // Not a loosening or a tightening of anything observable: on both
                // backends the factory is seated during init and the export path cannot
                // be reached before then, so this branch is unreachable today. It is
                // here because a null factory is now expressible -- app.textFactory is a
                // pointer into Seated storage, not a COM pointer the window owns -- and
                // an unreachable check is cheaper than a crash if that ever changes.
                if (!app.textFactory) break;
                float fontSize =
                    (prim.style.mono ? 13.0f : 14.0f) *
                    (prim.style.scale > 0 ? prim.style.scale : 1.0f) * scale;
                std::unique_ptr<tinta::TextFormat> format =
                    app.textFactory->CreateTextFormat(
                        prim.style.mono ? app.theme.codeFontFamily
                                        : app.theme.fontFamily,
                        prim.style.bold ? tinta::FontWeight::SemiBold
                                        : tinta::FontWeight::Normal,
                        prim.style.italic ? tinta::FontStyle::Italic
                                          : tinta::FontStyle::Normal,
                        tinta::FontStretch::Normal, fontSize, L"en-us");
                if (!format) break;
                // The old `format->Release()` right after the layout was built is GONE
                // rather than translated: the unique_ptr frees it at the end of this
                // case, which is later than the original did but no different in effect
                // -- a layout holds its own reference to the format it was built from.
                std::unique_ptr<tinta::TextLayout> layout =
                    app.textFactory->CreateTextLayout(
                        wide.c_str(), (uint32_t)wide.size(), format.get(),
                        std::max(1.0f, prim.x2 - prim.x1 + 0.5f),
                        std::max(1.0f, prim.y2 - prim.y1));
                if (!layout) break;
                layout->SetTextAlignment(
                    prim.alignH < 0   ? tinta::TextAlignment::Leading
                    : prim.alignH > 0 ? tinta::TextAlignment::Trailing
                                      : tinta::TextAlignment::Center);
                layout->SetParagraphAlignment(
                    prim.alignV < 0   ? tinta::ParagraphAlignment::Near
                    : prim.alignV > 0 ? tinta::ParagraphAlignment::Far
                                      : tinta::ParagraphAlignment::Center);
                // The `if (app.fontFallback)` guard is dropped for src/editor.cpp:103's
                // reason, and dropping it is not a loosening: ApplyFontFallback is
                // documented to do nothing when the backend has no chain or has not
                // built one yet, which is exactly what that guard meant. The Qt backend
                // implements it as a genuine no-op.
                layout->ApplyFontFallback();
                tinta::Color color = resolveDiagramRole(
                    app, prim,
                    prim.fill != mermaidext::Role::None
                        ? prim.fill
                        : mermaidext::Role::Text);
                brush->SetColor(color);
                target.DrawTextLayout(tinta::pointF(prim.x1, prim.y1),
                                      layout.get(), brush);
                break;
            }
        }
    }
    // No `dashed->Release()`. The style is App's now, created once and reused every
    // frame by the paint pass, so releasing it here would have been the per-frame
    // double-free half of the same mistake the fc9ceb0 note in src/paint_document.cpp
    // describes from the other direction.
}

// Diagram source -> PNG at 2x plus its logical (1x) size
bool diagramToPng(App& app, const std::string& source, std::string& png,
                  float& logicalWidth, float& logicalHeight) {
    mermaidext::Built built = buildDiagramPrims(app, source, kRasterScale);
    if (!built.ok || built.prims.empty()) return false;
    if (!app.renderTarget) return false;
    unsigned width = 0, height = 0;
    if (!rasterDimensions(built.width, built.height, width, height)) return false;
    // app.theme.background, NOT transparent, and the parameter exists because of this
    // call site. image_api.h's first draft hardcoded a transparent clear on the reasoning
    // that a docx image sits on a background the document supplies -- plausible, and
    // refuted by reading what it replaced: all three raster sites here cleared to the
    // theme background, so a dark theme would have shipped light text on white paper.
    tinta::DecodedImage image = tinta::rasterizeToImage(
        *app.renderTarget, width, height, app.theme.background,
        [&](tinta::RenderTarget& target, tinta::Brush* brush) {
            drawPrims(app, target, brush, built, kRasterScale);
        });
    if (!image.ok) return false;
    png = tinta::encodePng(image);
    logicalWidth = built.width / kRasterScale;
    logicalHeight = built.height / kRasterScale;
    return !png.empty();
}

// TeX source -> PNG at 2x plus logical size and baseline
bool mathToPng(App& app, const std::string& tex, bool display,
               std::string& png, float& logicalWidth, float& logicalHeight,
               float& logicalBaseline) {
    MathBoxPtr box = mathParse(app, toWide(tex),
                               (display ? 18.0f : 16.0f) * kRasterScale,
                               display);
    if (!box) return false;
    float width = mathBoxWidth(box);
    float height = mathBoxHeight(box);
    if (width < 1 || height < 1) return false;
    if (!app.renderTarget) return false;
    unsigned pixelWidth = 0, pixelHeight = 0;
    if (!rasterDimensions(width, height, pixelWidth, pixelHeight)) return false;
    // The two D2DRenderTarget/D2DBrush wrappers this used to build by hand are gone:
    // mathBoxDrawTo already took the portable seam, and the seam is now what the
    // callback is handed. That pair was the last Direct2D type named in this file's
    // rasterisation half.
    tinta::DecodedImage image = tinta::rasterizeToImage(
        *app.renderTarget, pixelWidth, pixelHeight, app.theme.background,
        [&](tinta::RenderTarget& target, tinta::Brush* brush) {
            mathBoxDrawTo(&target, brush, box, 0, 0, app.theme.text);
        });
    if (!image.ok) return false;
    png = tinta::encodePng(image);
    logicalWidth = width / kRasterScale;
    logicalHeight = height / kRasterScale;
    logicalBaseline = mathBoxBaseline(box) / kRasterScale;
    return !png.empty();
}

// --------------------------------------------------------------------------
// WordprocessingML generation
// --------------------------------------------------------------------------

constexpr long long kEmuPerPixel = 9525;
constexpr long long kMaxImageEmu = 6100000;  // fits A4 with 2cm margins

struct RunProps {
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool highlight = false;
    bool code = false;
    bool isLink = false;
    bool superScript = false;
    bool subScript = false;
    std::string color;  // hex override (links, wiki links)
    bool underline = false;
};

struct DocxCtx {
    // A constructor, so DocxCtx is not an aggregate and `DocxCtx ctx{app}` is not read as
    // an initializer list that forgot its other members (-Wmissing-field-initializers).
    explicit DocxCtx(App& a) : app(a) {}
    App& app;
    std::string body;
    std::vector<std::pair<std::string, std::string>> media;  // name, bytes
    std::vector<std::string> relationships;   // xml lines, rId4 onward
    int nextRelId = 4;  // 1 = styles, 2 = numbering, 3 = settings
    int nextPictureId = 1;
    std::vector<int> orderedListStarts;       // one numbering instance each
    std::wstring baseDir;

    // Theme palette, guarded so dark themes stay readable on paper
    std::string textHex, headingHex, mutedHex, codeBgHex, borderHex;
    std::string accentHex, linkHex, quoteBorderHex;
    std::string inlineCodeHex;  // explicit override; legacy paper colors otherwise
    std::string bodyFont, monoFont;
};

int addImageRel(DocxCtx& ctx, const std::string& extension,
                const std::string& bytes) {
    std::string name =
        "image" + std::to_string(ctx.media.size() + 1) + "." + extension;
    ctx.media.push_back({name, bytes});
    int rel = ctx.nextRelId++;
    ctx.relationships.push_back(
        "<Relationship Id=\"rId" + std::to_string(rel) +
        "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/image\" Target=\"media/" + name + "\"/>");
    return rel;
}

int addLinkRel(DocxCtx& ctx, const std::string& url) {
    int rel = ctx.nextRelId++;
    ctx.relationships.push_back(
        "<Relationship Id=\"rId" + std::to_string(rel) +
        "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/hyperlink\" Target=\"" + xmlEscape(url) +
        "\" TargetMode=\"External\"/>");
    return rel;
}

std::string runPropsXml(const DocxCtx& ctx, const RunProps& props) {
    std::string xml;
    if (props.code) {
        xml += "<w:rFonts w:ascii=\"" + ctx.monoFont + "\" w:hAnsi=\"" +
               ctx.monoFont + "\" w:cs=\"" + ctx.monoFont + "\"/>";
        xml += "<w:sz w:val=\"21\"/><w:szCs w:val=\"21\"/>";
        xml += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"" +
               ctx.codeBgHex + "\"/>";
    }
    if (props.bold) xml += "<w:b/>";
    if (props.italic) xml += "<w:i/>";
    if (props.strike) xml += "<w:strike/>";
    if (props.highlight) xml += "<w:highlight w:val=\"yellow\"/>";
    if (props.superScript) xml += "<w:vertAlign w:val=\"superscript\"/>";
    if (props.subScript) xml += "<w:vertAlign w:val=\"subscript\"/>";
    if (props.code && !props.isLink && !ctx.inlineCodeHex.empty()) {
        xml += "<w:color w:val=\"" + ctx.inlineCodeHex + "\"/>";
    } else if (!props.color.empty()) {
        xml += "<w:color w:val=\"" + props.color + "\"/>";
    }
    if (props.underline) xml += "<w:u w:val=\"single\"/>";
    return xml.empty() ? "" : "<w:rPr>" + xml + "</w:rPr>";
}

void emitTextRun(DocxCtx& ctx, const std::string& text,
                 const RunProps& props, std::string& out) {
    if (text.empty()) return;
    out += "<w:r>" + runPropsXml(ctx, props) +
           "<w:t xml:space=\"preserve\">" + xmlEscape(text) + "</w:t></w:r>";
}

std::string drawingXml(DocxCtx& ctx, int relId, long long cx, long long cy) {
    int id = ctx.nextPictureId++;
    std::string n = std::to_string(id);
    std::string rid = std::to_string(relId);
    return "<w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" "
           "distR=\"0\"><wp:extent cx=\"" + std::to_string(cx) + "\" cy=\"" +
           std::to_string(cy) + "\"/><wp:docPr id=\"" + n +
           "\" name=\"Picture " + n + "\"/><a:graphic "
           "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/"
           "main\"><a:graphicData "
           "uri=\"http://schemas.openxmlformats.org/drawingml/2006/"
           "picture\"><pic:pic "
           "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/"
           "picture\"><pic:nvPicPr><pic:cNvPr id=\"" + n +
           "\" name=\"Picture " + n +
           "\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip "
           "r:embed=\"rId" + rid +
           "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr>"
           "<a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" +
           std::to_string(cx) + "\" cy=\"" + std::to_string(cy) +
           "\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
           "</pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline>"
           "</w:drawing>";
}

// Fit a pixel size into the page, preserving aspect
void fitEmu(float widthPx, float heightPx, long long& cx, long long& cy) {
    cx = (long long)(widthPx * kEmuPerPixel);
    cy = (long long)(heightPx * kEmuPerPixel);
    if (cx > kMaxImageEmu) {
        cy = (long long)((double)cy * kMaxImageEmu / cx);
        cx = kMaxImageEmu;
    }
}

// Paragraph-level context: style, list state, quote borders
struct ParaProps {
    std::string styleId;
    int numId = 0;
    int ilvl = 0;
    std::string borderColor;  // left border (quotes, alerts)
    int indentTwips = 0;
    bool center = false;
    bool right = false;
    std::string extraRunProps;  // colored quote text etc.
};

std::string paraPropsXml(const ParaProps& props) {
    std::string xml;
    if (!props.styleId.empty()) {
        xml += "<w:pStyle w:val=\"" + props.styleId + "\"/>";
    }
    if (props.numId > 0) {
        xml += "<w:numPr><w:ilvl w:val=\"" + std::to_string(props.ilvl) +
               "\"/><w:numId w:val=\"" + std::to_string(props.numId) +
               "\"/></w:numPr>";
    }
    if (!props.borderColor.empty()) {
        xml += "<w:pBdr><w:left w:val=\"single\" w:sz=\"24\" w:space=\"12\" "
               "w:color=\"" + props.borderColor + "\"/></w:pBdr>";
    }
    if (props.indentTwips > 0) {
        xml += "<w:ind w:left=\"" + std::to_string(props.indentTwips) +
               "\"/>";
    }
    if (props.center) xml += "<w:jc w:val=\"center\"/>";
    else if (props.right) xml += "<w:jc w:val=\"right\"/>";
    return xml.empty() ? "" : "<w:pPr>" + xml + "</w:pPr>";
}

void walkBlocks(DocxCtx& ctx, const ElementPtr& elem, ParaProps props,
                int listDepth);

// align= from an HTML paragraph or heading
void alignProps(const ElementPtr& elem, ParaProps& props) {
    if (elem->align == 2) props.center = true;
    else if (elem->align == 3) props.right = true;
}

// Inline content of one paragraph into `out`
void walkInline(DocxCtx& ctx, const ElementPtr& elem, RunProps props,
                std::string& out) {
    if (!elem) return;
    switch (elem->type) {
        case ElementType::Text:
            emitTextRun(ctx, elem->text, props, out);
            break;
        case ElementType::Code: {
            RunProps code = props;
            code.code = true;
            emitTextRun(ctx, elem->text, code, out);
            for (const auto& child : elem->children) {
                walkInline(ctx, child, code, out);
            }
            break;
        }
        case ElementType::Emphasis: {
            RunProps italic = props;
            italic.italic = true;
            for (const auto& child : elem->children) {
                walkInline(ctx, child, italic, out);
            }
            break;
        }
        case ElementType::Strong: {
            RunProps bold = props;
            bold.bold = true;
            for (const auto& child : elem->children) {
                walkInline(ctx, child, bold, out);
            }
            break;
        }
        case ElementType::Highlight: {
            RunProps mark = props;
            mark.highlight = true;
            for (const auto& child : elem->children) {
                walkInline(ctx, child, mark, out);
            }
            break;
        }
        case ElementType::Strikethrough: {
            RunProps strike = props;
            strike.strike = true;
            for (const auto& child : elem->children) {
                walkInline(ctx, child, strike, out);
            }
            break;
        }
        case ElementType::Superscript: {
            RunProps sup = props;
            sup.superScript = true;
            for (const auto& child : elem->children) {
                walkInline(ctx, child, sup, out);
            }
            break;
        }
        case ElementType::Subscript: {
            RunProps sub = props;
            sub.subScript = true;
            for (const auto& child : elem->children) {
                walkInline(ctx, child, sub, out);
            }
            break;
        }
        case ElementType::Link: {
            bool wiki = elem->url.rfind("wiki:", 0) == 0;
            if (elem->url.rfind("fileref:", 0) == 0) {
                // Detected plain-text .md reference (#127): stays plain
                for (const auto& child : elem->children) {
                    walkInline(ctx, child, props, out);
                }
            } else if (wiki) {
                RunProps wikiProps = props;
                wikiProps.isLink = true;
                wikiProps.color = ctx.linkHex;
                for (const auto& child : elem->children) {
                    walkInline(ctx, child, wikiProps, out);
                }
            } else {
                int rel = addLinkRel(ctx, elem->url);
                RunProps linkProps = props;
                linkProps.isLink = true;
                linkProps.color = ctx.linkHex;
                linkProps.underline = true;
                out += "<w:hyperlink r:id=\"rId" + std::to_string(rel) +
                       "\" w:history=\"1\">";
                for (const auto& child : elem->children) {
                    walkInline(ctx, child, linkProps, out);
                }
                out += "</w:hyperlink>";
            }
            break;
        }
        case ElementType::Image: {
            std::string src = elem->url;
            bool remote = src.rfind("http://", 0) == 0 ||
                          src.rfind("https://", 0) == 0;
            std::string data;
            int width = 0, height = 0;
            std::string extension;
            if (!remote) {
                // fs::path rather than a "\\" join and a drive-letter test,
                // which only ever resolved on Windows
                std::filesystem::path imagePath(toWide(src));
                if (imagePath.is_relative() && !ctx.baseDir.empty())
                    imagePath = std::filesystem::path(ctx.baseDir) / imagePath;
                std::wstring widePath = imagePath.wstring();
                size_t dot = src.find_last_of('.');
                if (dot != std::string::npos) {
                    extension = src.substr(dot + 1);
                    for (char& c : extension) {
                        c = (char)tolower((unsigned char)c);
                    }
                    if (extension == "jpg") extension = "jpeg";
                }
                if ((extension == "png" || extension == "jpeg" ||
                     extension == "gif") &&
                    readBinaryFile(widePath, data) &&
                    imageDimensions(data, width, height)) {
                    int rel = addImageRel(ctx, extension, data);
                    // HTML width=/height= (CSS px), the other side following the aspect
                    float w = (float)width, h = (float)height;
                    if (w > 0 && h > 0 && (elem->width > 0 || elem->height > 0)) {
                        float sw = elem->width > 0 ? elem->width : elem->height * w / h;
                        float sh = elem->height > 0 ? elem->height : elem->width * h / w;
                        w = sw;
                        h = sh;
                    }
                    long long cx, cy;
                    fitEmu(w, h, cx, cy);
                    out += "<w:r>" + drawingXml(ctx, rel, cx, cy) + "</w:r>";
                    break;
                }
            }
            // Unsupported or remote: alt text plus the address as a link
            std::string alt;
            for (const auto& child : elem->children) {
                if (child->type == ElementType::Text) alt += child->text;
            }
            RunProps altProps = props;
            altProps.italic = true;
            emitTextRun(ctx, alt.empty() ? elem->url : alt + " (" +
                        elem->url + ")", altProps, out);
            break;
        }
        case ElementType::SoftBreak:
            emitTextRun(ctx, " ", props, out);
            break;
        case ElementType::HardBreak:
            out += "<w:r><w:br/></w:r>";
            break;
        case ElementType::Ruby: {
            for (const auto& child : elem->children) {
                if (child->type == ElementType::RubyText) {
                    RunProps ruby = props;
                    ruby.superScript = true;
                    emitTextRun(ctx, "(" + child->text + ")", ruby, out);
                } else {
                    walkInline(ctx, child, props, out);
                }
            }
            break;
        }
        case ElementType::RubyText:
            break;
        case ElementType::MathInline: {
            std::string png;
            float width, height, baseline;
            if (mathToPng(ctx.app, elem->text, false, png, width, height,
                          baseline)) {
                int rel = addImageRel(ctx, "png", png);
                long long cx, cy;
                fitEmu(width, height, cx, cy);
                // Sink the picture so its baseline meets the text baseline
                int drop =
                    -(int)std::lround((height - baseline) * 1.5f);
                out += "<w:r><w:rPr><w:position w:val=\"" +
                       std::to_string(drop) + "\"/></w:rPr>" +
                       drawingXml(ctx, rel, cx, cy) + "</w:r>";
            } else {
                RunProps code = props;
                code.code = true;
                emitTextRun(ctx, "$" + elem->text + "$", code, out);
            }
            break;
        }
        default:
            for (const auto& child : elem->children) {
                walkInline(ctx, child, props, out);
            }
            break;
    }
}

void emitParagraph(DocxCtx& ctx, const ElementPtr& elem,
                   const ParaProps& props) {
    ctx.body += "<w:p>" + paraPropsXml(props);
    RunProps runProps;
    if (!props.extraRunProps.empty()) {
        // Colored quote text rides on every run via the color override
        runProps.color = props.extraRunProps;
    }
    for (const auto& child : elem->children) {
        walkInline(ctx, child, runProps, ctx.body);
    }
    ctx.body += "</w:p>";
}

void emitDiagramOrCode(DocxCtx& ctx, const std::string& source,
                       const std::string& language, const ParaProps& props) {
    if (language == "mermaid" || language.empty()) {
        std::string png;
        float width, height;
        if ((language == "mermaid") &&
            diagramToPng(ctx.app, source, png, width, height)) {
            int rel = addImageRel(ctx, "png", png);
            long long cx, cy;
            fitEmu(width, height, cx, cy);
            ctx.body += "<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr><w:r>" +
                        drawingXml(ctx, rel, cx, cy) + "</w:r></w:p>";
            return;
        }
    }
    // Code block: one shaded paragraph, lines separated by breaks
    ctx.body += "<w:p><w:pPr><w:pStyle w:val=\"CodeBlock\"/>";
    if (!props.borderColor.empty()) {
        ctx.body +=
            "<w:pBdr><w:left w:val=\"single\" w:sz=\"24\" w:space=\"12\" "
            "w:color=\"" + props.borderColor + "\"/></w:pBdr>";
    }
    ctx.body += "</w:pPr>";
    std::string line;
    bool first = true;
    auto flush = [&]() {
        if (!first) ctx.body += "<w:r><w:br/></w:r>";
        first = false;
        ctx.body += "<w:r><w:t xml:space=\"preserve\">" + xmlEscape(line) +
                    "</w:t></w:r>";
        line.clear();
    };
    for (char c : source) {
        if (c == '\n') flush();
        else if (c != '\r') line += c;
    }
    if (!line.empty() || first) flush();
    ctx.body += "</w:p>";
}

// GitHub alert palette (light/dark hex pairs, same table as the viewer)
struct AlertInfo {
    const char* title;
    uint32_t light;
    uint32_t dark;
};
const AlertInfo kAlerts[] = {
    {"\xE2\x93\x98\xEF\xB8\x8E  Note", 0x0969DA, 0x4493F8},
    {"\xF0\x9F\x92\xA1\xEF\xB8\x8E  Tip", 0x1A7F37, 0x3FB950},
    {"\xE2\x9D\x97\xEF\xB8\x8E  Important", 0x8250DF, 0xAB7DF8},
    {"\xE2\x9A\xA0\xEF\xB8\x8E  Warning", 0x9A6700, 0xD29922},
    {"\xE2\x9B\x94\xEF\xB8\x8E  Caution", 0xCF222E, 0xF85149},
};

std::string hexOfRgb(uint32_t rgb) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%06X", rgb);
    return buf;
}

void walkBlocks(DocxCtx& ctx, const ElementPtr& elem, ParaProps props,
                int listDepth) {
    if (!elem) return;
    switch (elem->type) {
        case ElementType::Document:
            for (const auto& child : elem->children) {
                walkBlocks(ctx, child, props, listDepth);
            }
            break;
        case ElementType::Properties:
            break;
        case ElementType::Paragraph: {
            ParaProps para = props;
            alignProps(elem, para);
            emitParagraph(ctx, elem, para);
            break;
        }
        case ElementType::Heading: {
            int level = std::max(1, std::min(6, elem->level));
            ParaProps heading = props;
            heading.styleId = "Heading" + std::to_string(level);
            alignProps(elem, heading);
            emitParagraph(ctx, elem, heading);
            break;
        }
        case ElementType::CodeBlock: {
            std::string source;
            for (const auto& child : elem->children) {
                if (child->type == ElementType::Text) source += child->text;
            }
            emitDiagramOrCode(ctx, source, elem->language, props);
            break;
        }
        case ElementType::MermaidDiagram:
            emitDiagramOrCode(ctx, elem->text, "mermaid", props);
            break;
        case ElementType::BlockQuote: {
            ParaProps quote = props;
            int kind = elem->alertKind >= 1 && elem->alertKind <= 5
                           ? elem->alertKind
                           : 0;
            if (kind) {
                const auto& alert = kAlerts[kind - 1];
                std::string color = hexOfRgb(
                    ctx.app.theme.isDark ? alert.dark : alert.light);
                quote.borderColor = color;
                quote.indentTwips = 240;
                // Title line with the viewer's icon and label
                ctx.body += "<w:p><w:pPr><w:pBdr><w:left w:val=\"single\" "
                            "w:sz=\"24\" w:space=\"12\" w:color=\"" + color +
                            "\"/></w:pBdr><w:ind w:left=\"240\"/></w:pPr>"
                            "<w:r><w:rPr><w:b/><w:color w:val=\"" + color +
                            "\"/></w:rPr><w:t xml:space=\"preserve\">" +
                            xmlEscape(alert.title) + "</w:t></w:r></w:p>";
            } else {
                quote.borderColor = ctx.quoteBorderHex;
                quote.indentTwips = 240;
                quote.extraRunProps = ctx.mutedHex;
            }
            for (const auto& child : elem->children) {
                walkBlocks(ctx, child, quote, listDepth);
            }
            break;
        }
        case ElementType::List: {
            int numId;
            if (elem->ordered) {
                ctx.orderedListStarts.push_back(elem->start);
                numId = 1 + (int)ctx.orderedListStarts.size();
            } else {
                numId = 1;
            }
            for (const auto& child : elem->children) {
                if (child->type != ElementType::ListItem) continue;
                ParaProps item = props;
                if (child->isTask) {
                    item.numId = 0;
                    item.indentTwips = 360 + listDepth * 360;
                } else {
                    item.numId = numId;
                    item.ilvl = std::min(8, listDepth);
                }
                // Direct inline children form the item's paragraph; nested
                // blocks (lists, paragraphs) recurse
                std::string runs;
                RunProps runProps;
                bool paragraphOpen = false;
                auto openItem = [&]() {
                    if (paragraphOpen) return;
                    ctx.body += "<w:p>" + paraPropsXml(item);
                    if (child->isTask) {
                        ctx.body +=
                            "<w:r><w:t xml:space=\"preserve\">" +
                            std::string(child->taskChecked
                                            ? "\xE2\x98\x91 "
                                            : "\xE2\x98\x90 ") +
                            "</w:t></w:r>";
                    }
                    paragraphOpen = true;
                };
                for (const auto& grand : child->children) {
                    bool isBlock =
                        grand->type == ElementType::List ||
                        grand->type == ElementType::Paragraph ||
                        grand->type == ElementType::CodeBlock ||
                        grand->type == ElementType::BlockQuote ||
                        grand->type == ElementType::Table;
                    if (grand->type == ElementType::Paragraph &&
                        !paragraphOpen) {
                        // First paragraph joins the numbered item itself
                        openItem();
                        for (const auto& inlineChild : grand->children) {
                            walkInline(ctx, inlineChild, runProps, ctx.body);
                        }
                        continue;
                    }
                    if (isBlock) {
                        if (paragraphOpen) {
                            ctx.body += "</w:p>";
                            paragraphOpen = false;
                        }
                        ParaProps nested = props;
                        if (grand->type != ElementType::List) {
                            nested.indentTwips =
                                720 + listDepth * 360;
                        }
                        walkBlocks(ctx, grand, nested, listDepth + 1);
                    } else {
                        openItem();
                        walkInline(ctx, grand, runProps, ctx.body);
                    }
                }
                if (paragraphOpen) {
                    ctx.body += "</w:p>";
                } else if (child->children.empty()) {
                    openItem();
                    ctx.body += "</w:p>";
                }
                (void)runs;
            }
            break;
        }
        case ElementType::ListItem:
            for (const auto& child : elem->children) {
                walkBlocks(ctx, child, props, listDepth);
            }
            break;
        case ElementType::HorizontalRule:
            ctx.body += "<w:p><w:pPr><w:pBdr><w:bottom w:val=\"single\" "
                        "w:sz=\"6\" w:space=\"1\" w:color=\"" +
                        ctx.borderHex + "\"/></w:pBdr></w:pPr></w:p>";
            break;
        case ElementType::Table: {
            ctx.body +=
                "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/>"
                "<w:tblBorders>"
                "<w:top w:val=\"single\" w:sz=\"4\" w:color=\"" +
                ctx.borderHex + "\"/>"
                "<w:left w:val=\"single\" w:sz=\"4\" w:color=\"" +
                ctx.borderHex + "\"/>"
                "<w:bottom w:val=\"single\" w:sz=\"4\" w:color=\"" +
                ctx.borderHex + "\"/>"
                "<w:right w:val=\"single\" w:sz=\"4\" w:color=\"" +
                ctx.borderHex + "\"/>"
                "<w:insideH w:val=\"single\" w:sz=\"4\" w:color=\"" +
                ctx.borderHex + "\"/>"
                "<w:insideV w:val=\"single\" w:sz=\"4\" w:color=\"" +
                ctx.borderHex + "\"/>"
                "</w:tblBorders></w:tblPr>";
            for (size_t rowIndex = 0; rowIndex < elem->children.size();
                 rowIndex++) {
                const auto& row = elem->children[rowIndex];
                if (row->type != ElementType::TableRow) continue;
                bool header = rowIndex == 0;
                ctx.body += "<w:tr>";
                for (const auto& cell : row->children) {
                    if (cell->type != ElementType::TableCell) continue;
                    ctx.body += "<w:tc><w:tcPr>";
                    if (header) {
                        ctx.body += "<w:shd w:val=\"clear\" "
                                    "w:color=\"auto\" w:fill=\"" +
                                    ctx.codeBgHex + "\"/>";
                    }
                    ctx.body += "</w:tcPr><w:p>";
                    std::string paraProps;
                    if (cell->align == 2) {
                        paraProps += "<w:jc w:val=\"center\"/>";
                    } else if (cell->align == 3) {
                        paraProps += "<w:jc w:val=\"right\"/>";
                    }
                    if (!paraProps.empty()) {
                        ctx.body += "<w:pPr>" + paraProps + "</w:pPr>";
                    }
                    RunProps cellProps;
                    cellProps.bold = header;
                    for (const auto& child : cell->children) {
                        walkInline(ctx, child, cellProps, ctx.body);
                    }
                    ctx.body += "</w:p></w:tc>";
                }
                ctx.body += "</w:tr>";
            }
            ctx.body += "</w:tbl><w:p/>";
            break;
        }
        case ElementType::TableRow:
        case ElementType::TableCell:
        case ElementType::HtmlBlock:
            for (const auto& child : elem->children) {
                walkBlocks(ctx, child, props, listDepth);
            }
            break;
        case ElementType::MathDisplay: {
            std::string png;
            float width, height, baseline;
            if (mathToPng(ctx.app, elem->text, true, png, width, height,
                          baseline)) {
                int rel = addImageRel(ctx, "png", png);
                long long cx, cy;
                fitEmu(width, height, cx, cy);
                ctx.body +=
                    "<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr><w:r>" +
                    drawingXml(ctx, rel, cx, cy) + "</w:r></w:p>";
            } else {
                emitDiagramOrCode(ctx, "$$" + elem->text + "$$", "", props);
            }
            break;
        }
        default:
            // Inline elements reaching block level wrap in a paragraph
            emitParagraph(ctx, elem, props);
            break;
    }
}

// --------------------------------------------------------------------------
// Package parts
// --------------------------------------------------------------------------

std::string stylesXml(const DocxCtx& ctx) {
    // Viewer pixel sizes: body 16, code 14, headings 32/26/22/18/16/14.
    // Word half-points are px * 1.5 at 96dpi.
    const int headingSizes[6] = {48, 39, 33, 27, 24, 21};
    std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:styles "
        "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/"
        "main\">"
        "<w:docDefaults><w:rPrDefault><w:rPr>"
        "<w:rFonts w:ascii=\"" + ctx.bodyFont + "\" w:hAnsi=\"" +
        ctx.bodyFont + "\" w:cs=\"" + ctx.bodyFont + "\"/>"
        "<w:sz w:val=\"24\"/><w:szCs w:val=\"24\"/>"
        "<w:color w:val=\"" + ctx.textHex + "\"/>"
        "</w:rPr></w:rPrDefault><w:pPrDefault><w:pPr>"
        "<w:spacing w:after=\"160\" w:line=\"312\" w:lineRule=\"auto\"/>"
        "</w:pPr></w:pPrDefault></w:docDefaults>"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\">"
        "<w:name w:val=\"Normal\"/></w:style>";
    for (int i = 0; i < 6; i++) {
        std::string n = std::to_string(i + 1);
        xml += "<w:style w:type=\"paragraph\" w:styleId=\"Heading" + n +
               "\"><w:name w:val=\"heading " + n +
               "\"/><w:basedOn w:val=\"Normal\"/>"
               "<w:pPr><w:keepNext/><w:spacing w:before=\"280\" "
               "w:after=\"120\"/><w:outlineLvl w:val=\"" +
               std::to_string(i) + "\"/></w:pPr><w:rPr><w:b/><w:color "
               "w:val=\"" + ctx.headingHex + "\"/><w:sz w:val=\"" +
               std::to_string(headingSizes[i]) + "\"/><w:szCs w:val=\"" +
               std::to_string(headingSizes[i]) + "\"/></w:rPr></w:style>";
    }
    xml += "<w:style w:type=\"paragraph\" w:styleId=\"CodeBlock\">"
           "<w:name w:val=\"Code Block\"/><w:basedOn w:val=\"Normal\"/>"
           "<w:pPr><w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"" +
           ctx.codeBgHex + "\"/><w:spacing w:after=\"160\"/></w:pPr>"
           "<w:rPr><w:rFonts w:ascii=\"" + ctx.monoFont + "\" w:hAnsi=\"" +
           ctx.monoFont + "\" w:cs=\"" + ctx.monoFont + "\"/>"
           "<w:sz w:val=\"21\"/><w:szCs w:val=\"21\"/></w:rPr></w:style>";
    xml += "<w:style w:type=\"character\" w:styleId=\"Hyperlink\">"
           "<w:name w:val=\"Hyperlink\"/><w:rPr><w:color w:val=\"" +
           ctx.linkHex + "\"/><w:u w:val=\"single\"/></w:rPr></w:style>";
    xml += "</w:styles>";
    return xml;
}

std::string numberingXml(const DocxCtx& ctx) {
    std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:numbering "
        "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/"
        "main\">";
    // Abstract 0: bullets cycling three glyphs
    xml += "<w:abstractNum w:abstractNumId=\"0\">";
    const char* bullets[3] = {"\xE2\x80\xA2", "\xE2\x97\xA6", "\xE2\x96\xAA"};
    for (int level = 0; level < 9; level++) {
        xml += "<w:lvl w:ilvl=\"" + std::to_string(level) +
               "\"><w:start w:val=\"1\"/><w:numFmt w:val=\"bullet\"/>"
               "<w:lvlText w:val=\"" + std::string(bullets[level % 3]) +
               "\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"" +
               std::to_string(360 + level * 360) + "\" w:hanging=\"360\"/>"
               "</w:pPr></w:lvl>";
    }
    xml += "</w:abstractNum>";
    // Abstract 1: decimal at every level
    xml += "<w:abstractNum w:abstractNumId=\"1\">";
    for (int level = 0; level < 9; level++) {
        xml += "<w:lvl w:ilvl=\"" + std::to_string(level) +
               "\"><w:start w:val=\"1\"/><w:numFmt w:val=\"decimal\"/>"
               "<w:lvlText w:val=\"%" + std::to_string(level + 1) +
               ".\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"" +
               std::to_string(360 + level * 360) + "\" w:hanging=\"360\"/>"
               "</w:pPr></w:lvl>";
    }
    xml += "</w:abstractNum>";
    xml += "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>";
    for (size_t i = 0; i < ctx.orderedListStarts.size(); i++) {
        xml += "<w:num w:numId=\"" + std::to_string(i + 2) +
               "\"><w:abstractNumId w:val=\"1\"/>"
               "<w:lvlOverride w:ilvl=\"0\"><w:startOverride w:val=\"" +
               std::to_string(std::max(1, ctx.orderedListStarts[i])) +
               "\"/></w:lvlOverride></w:num>";
    }
    xml += "</w:numbering>";
    return xml;
}

}  // namespace

bool exportDocxFile(App& app, const std::wstring& path) {
    if (!app.root) return false;

    DocxCtx ctx{app};
    if (!app.currentFile.empty()) {
        std::wstring wide = toWide(app.currentFile);
        size_t slash = wide.find_last_of(L"/\\");
        if (slash != std::wstring::npos) ctx.baseDir = wide.substr(0, slash);
    }

    const D2DTheme& theme = app.theme;
    bool dark = theme.isDark;
    // Dark themes bake accent hues but keep readable text on white paper
    ctx.textHex = dark ? "1F1F1F" : hex6(theme.text);
    ctx.headingHex = dark ? "111111" : hex6(theme.heading);
    tinta::Color muted = theme.text;
    muted.a = 0.65f;
    ctx.mutedHex = dark ? "555555" : hexOver(muted, theme.background);
    ctx.codeBgHex = dark ? "F2F0EE" : hex6(theme.codeBackground);
    if (theme.inlineCode) ctx.inlineCodeHex = hex6(*theme.inlineCode);
    tinta::Color border = theme.text;
    border.a = 0.25f;
    ctx.borderHex = dark ? "C8C8C8" : hexOver(border, theme.background);
    ctx.accentHex = hex6(theme.accent);
    ctx.linkHex = hex6(theme.link);
    ctx.quoteBorderHex =
        dark ? "BBBBBB" : hex6(theme.blockquoteBorder);
    ctx.bodyFont = xmlEscape(toUtf8(theme.fontFamily));
    ctx.monoFont = xmlEscape(toUtf8(theme.codeFontFamily));

    ParaProps rootProps;
    walkBlocks(ctx, app.root, rootProps, 0);

    std::string document =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document "
        "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/"
        "main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships\" "
        "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/"
        "wordprocessingDrawing\">"
        "<w:body>" + ctx.body +
        "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
        "<w:pgMar w:top=\"1134\" w:right=\"1134\" w:bottom=\"1134\" "
        "w:left=\"1134\" w:header=\"708\" w:footer=\"708\" "
        "w:gutter=\"0\"/></w:sectPr></w:body></w:document>";

    std::string contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types "
        "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
        "content-types\">"
        "<Default Extension=\"rels\" "
        "ContentType=\"application/vnd.openxmlformats-package."
        "relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
        "<Default Extension=\"gif\" ContentType=\"image/gif\"/>"
        "<Override PartName=\"/word/document.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument."
        "wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument."
        "wordprocessingml.styles+xml\"/>"
        "<Override PartName=\"/word/numbering.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument."
        "wordprocessingml.numbering+xml\"/>"
        "<Override PartName=\"/word/settings.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument."
        "wordprocessingml.settings+xml\"/>"
        "</Types>";

    std::string rootRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships "
        "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
        "relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>";

    std::string documentRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships "
        "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
        "relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/styles\" Target=\"styles.xml\"/>"
        "<Relationship Id=\"rId2\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/numbering\" Target=\"numbering.xml\"/>"
        "<Relationship Id=\"rId3\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/settings\" Target=\"settings.xml\"/>";
    for (const auto& rel : ctx.relationships) documentRels += rel;
    documentRels += "</Relationships>";

    // Without a settings part Word opens the file in Compatibility Mode
    std::string settings =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:settings "
        "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/"
        "main\"><w:compat><w:compatSetting w:name=\"compatibilityMode\" "
        "w:uri=\"http://schemas.microsoft.com/office/word\" "
        "w:val=\"15\"/></w:compat></w:settings>";

    ZipWriter zip;
    zip.add("[Content_Types].xml", contentTypes);
    zip.add("_rels/.rels", rootRels);
    zip.add("word/document.xml", document);
    zip.add("word/_rels/document.xml.rels", documentRels);
    zip.add("word/styles.xml", stylesXml(ctx));
    zip.add("word/numbering.xml", numberingXml(ctx));
    zip.add("word/settings.xml", settings);
    for (const auto& media : ctx.media) {
        zip.add("word/media/" + media.first, media.second);
    }
    std::string package = zip.finish();

    std::ofstream out(std::filesystem::path(path), std::ios::binary);
    if (!out) return false;
    out.write(package.data(), package.size());
    return out.good();
}

// Rasterizes the diagram exactly like the DOCX path, then hands the pixels
// to the clipboard as CF_DIB (universal) plus a "PNG" stream (kept by apps
// that prefer lossless with metadata)
//
// THE HWND IS GONE, on include/clipboard.h's own argument for the text half: "what
// picture is on the clipboard" has no window in it, the handle only ever reached
// OpenClipboard, and QClipboard is application-global with nowhere to put one. The
// Windows backend calls OpenClipboard(nullptr), which associates the clipboard with the
// calling task rather than a window, and the header records the one thing that would make
// that unsafe -- delayed rendering, which nothing in this tree uses.
//
// WHAT IS LEFT IN THIS FUNCTION IS THE PART THAT IS ABOUT DIAGRAMS. The two things it
// used to also be about -- building a BITMAPINFOHEADER with a bottom-up row flip, and
// copying a PNG into an HGLOBAL under a registered clipboard format -- moved to
// src/clipboard_win32.cpp, and on 2026-10-07 that file was deleted in favour of the Qt
// implementation beside it, src/clipboard_qt.cpp.
bool copyDiagramImage(App& app, const std::string& sourceUtf8) {
    mermaidext::Built built = buildDiagramPrims(app, sourceUtf8, kRasterScale);
    if (!built.ok || built.prims.empty()) return false;
    if (!app.renderTarget) return false;
    unsigned width = 0, height = 0;
    if (!rasterDimensions(built.width, built.height, width, height)) return false;
    tinta::DecodedImage image = tinta::rasterizeToImage(
        *app.renderTarget, width, height, app.theme.background,
        [&](tinta::RenderTarget& target, tinta::Brush* brush) {
            drawPrims(app, target, brush, built, kRasterScale);
        });
    if (!image.ok) return false;

    // The PNG is encoded HERE and passed in rather than being computed inside the seam,
    // and that is a link decision rather than a division of labour: tinta::encodePng
    // lives in src/image_win32.cpp and src/image_qt.cpp, so calling it from the clipboard
    // seam would put the whole WIC and Direct2D link line onto tests/clipboard_tests.
    // See include/clipboard.h at setClipboardImage. An empty result is not a failure --
    // it means the image lands as a bitmap only, which is what this code always did.
    return platform::setClipboardImage(image, tinta::encodePng(image));
}

// Ctrl+V in the editor with a bitmap on the clipboard: encode it as PNG
// into the given file (screenshots land beside the document)
//
// THIS IS WHERE THE WIC DECODE WENT, and the answer is that it did not go anywhere -- it
// was deleted. platform::clipboardImage reads the CF_BITMAP with GDI's GetDIBits and
// tinta::encodePng writes the PNG, so the IWICBitmap, the CreateBitmapFromHBITMAP and the
// 52-line encoder this function used to call are all gone. See the note where that
// encoder used to be, near the top of this file, for the trap it predicted.
//
// THE app.wicFactory NULL CHECK WENT WITH THEM, and its absence is not a dropped guard:
// it was guarding the factory this function no longer touches. What replaces it is
// clipboardImage's own ok flag, which covers strictly more -- no bitmap on the clipboard,
// a clipboard that will not open, a GetDIBits that reads short, or a dimension at or above
// tinta::kMaxImageDimension.
//
// THE App PARAMETER WENT TOO, and it is worth separating from the HWND: the HWND was
// dropped by the seam's design, while `app` simply has no remaining reader here. Its only
// use was reaching the imaging factory. Keeping it as a `(void)app;` would have left a
// signature asserting a dependency that no longer exists.
bool clipboardImageToPngFile(const std::wstring& path) {
    tinta::DecodedImage image = platform::clipboardImage();
    if (!image.ok) return false;
    std::string png = tinta::encodePng(image);
    if (png.empty()) return false;
    std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(png.data(), (std::streamsize)png.size());
    return out.good();
}
