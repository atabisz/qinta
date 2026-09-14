#ifndef TINTA_TEXT_D2D_H
#define TINTA_TEXT_D2D_H

#ifndef _WIN32
#error "text_d2d.h is the Windows backend; off Windows use text_qt.h."
#endif

#include <windows.h>

#include "text_api.h"

#include <dwrite.h>

// The DirectWrite backend for the text seam.
//
// OWNERSHIP HERE IS OWNED, WHICH IS THE OPPOSITE OF render_d2d.h
//
// render_d2d.h's wrappers BORROW, because App already created and already Releases
// the render target and the scratch brush. These wrappers OWN, because the factory
// below is what CREATES the underlying DirectWrite object and is therefore holding
// the only reference to it.
//
// The rule that covers both files, and the only one worth remembering: whoever
// created the COM object owns it. App creates the target and the brush, so those
// wrappers borrow; TextFactory creates formats, layouts and trimming signs, so
// those wrappers Release in their destructors. Getting this backwards in either
// direction is a leak or a double-free, and the asymmetry is not an inconsistency
// -- it is one rule producing different answers from different inputs.
//
// This also means the 73 CreateTextFormat/CreateTextLayout sites lose their
// explicit Release() calls when they are rewired, which is a real part of the
// 271-Release figure render_api.h says a portable backend deletes rather than
// reimplements.
//
// WHY D2DTextLayout DOES NOT DERIVE FROM D2DTextFormat
//
// text_api.h has tinta::TextLayout derive from tinta::TextFormat, mirroring
// DirectWrite and keeping the 35 SetTextAlignment sites from forking by receiver.
// The obvious way to implement that is to have the concrete classes mirror it too
// -- and it is a trap. D2DTextLayout would then inherit tinta::TextFormat twice,
// once through D2DTextFormat and once through tinta::TextLayout, giving two
// distinct base subobjects and making `D2DTextLayout*` to `tinta::TextFormat*`
// ambiguous. Fixing that needs virtual inheritance in the portable header, which
// is a real cost imposed on every backend to save a few lines in one of them.
//
// Instead each class implements the format interface against its own pointer. That
// costs six forwarding calls, and it is not even duplication: an IDWriteTextLayout
// IS an IDWriteTextFormat, so both classes call the same free helpers below on the
// same kind of pointer.

// The six TextFormat operations, against a raw DirectWrite format. Free functions
// rather than a base class precisely so that D2DTextLayout can reuse them without
// inheriting anything -- an IDWriteTextLayout* converts to IDWriteTextFormat* on
// its own, because that is DirectWrite's own inheritance doing the work.
namespace tinta_d2d_text {
void setTextAlignment(IDWriteTextFormat* format, tinta::TextAlignment alignment);
void setParagraphAlignment(IDWriteTextFormat* format, tinta::ParagraphAlignment alignment);
void setWordWrapping(IDWriteTextFormat* format, tinta::WordWrapping wrapping);
void setTrimming(IDWriteTextFormat* format, const tinta::Trimming& trimming,
                 tinta::InlineObject* trimmingSign);
void setLineSpacing(IDWriteTextFormat* format, tinta::LineSpacingMethod method,
                    float lineSpacing, float baseline);
float getFontSize(const IDWriteTextFormat* format);
}  // namespace tinta_d2d_text

class D2DInlineObject final : public tinta::InlineObject {
public:
    explicit D2DInlineObject(IDWriteInlineObject* object) : object_(object) {}
    ~D2DInlineObject() override;

    D2DInlineObject(const D2DInlineObject&) = delete;
    D2DInlineObject& operator=(const D2DInlineObject&) = delete;

    IDWriteInlineObject* native() const { return object_; }

private:
    IDWriteInlineObject* object_;  // owned
};

class D2DTextFormat final : public tinta::TextFormat {
public:
    explicit D2DTextFormat(IDWriteTextFormat* format) : format_(format) {}
    ~D2DTextFormat() override;

    D2DTextFormat(const D2DTextFormat&) = delete;
    D2DTextFormat& operator=(const D2DTextFormat&) = delete;

    void SetTextAlignment(tinta::TextAlignment alignment) override;
    void SetParagraphAlignment(tinta::ParagraphAlignment alignment) override;
    void SetWordWrapping(tinta::WordWrapping wrapping) override;
    void SetTrimming(const tinta::Trimming& trimming,
                     tinta::InlineObject* trimmingSign) override;
    void SetLineSpacing(tinta::LineSpacingMethod method, float lineSpacing,
                        float baseline) override;

    float GetFontSize() const override;

    IDWriteTextFormat* native() const { return format_; }

private:
    IDWriteTextFormat* format_;  // owned
};

class D2DTextLayout final : public tinta::TextLayout {
public:
    explicit D2DTextLayout(IDWriteTextLayout* layout) : layout_(layout) {}
    ~D2DTextLayout() override;

    D2DTextLayout(const D2DTextLayout&) = delete;
    D2DTextLayout& operator=(const D2DTextLayout&) = delete;

    void SetTextAlignment(tinta::TextAlignment alignment) override;
    void SetParagraphAlignment(tinta::ParagraphAlignment alignment) override;
    void SetWordWrapping(tinta::WordWrapping wrapping) override;
    void SetTrimming(const tinta::Trimming& trimming,
                     tinta::InlineObject* trimmingSign) override;
    void SetLineSpacing(tinta::LineSpacingMethod method, float lineSpacing,
                        float baseline) override;

    float GetFontSize() const override;

    void GetMetrics(tinta::TextMetrics* metrics) const override;

    bool HitTestPoint(float pointX, float pointY, bool* isTrailingHit,
                      bool* isInside, tinta::HitTestMetrics* metrics) const override;
    bool HitTestTextPosition(uint32_t textPosition, bool isTrailingHit,
                             float* pointX, float* pointY,
                             tinta::HitTestMetrics* metrics) const override;
    bool HitTestTextRange(uint32_t textPosition, uint32_t textLength,
                          float originX, float originY,
                          tinta::HitTestMetrics* metrics,
                          uint32_t maxMetricsCount,
                          uint32_t* actualMetricsCount) const override;

    IDWriteTextLayout* native() const { return layout_; }

private:
    IDWriteTextLayout* layout_;  // owned
};

class D2DTextFactory final : public tinta::TextFactory {
public:
    explicit D2DTextFactory(IDWriteFactory* factory) : factory_(factory) {}

    std::unique_ptr<tinta::TextFormat> CreateTextFormat(
        const wchar_t* fontFamily, tinta::FontWeight weight, tinta::FontStyle style,
        tinta::FontStretch stretch, float fontSize,
        const wchar_t* localeName) override;

    std::unique_ptr<tinta::TextLayout> CreateTextLayout(
        const wchar_t* string, uint32_t stringLength, tinta::TextFormat* format,
        float maxWidth, float maxHeight) override;

    std::unique_ptr<tinta::InlineObject> CreateEllipsisTrimmingSign(
        tinta::TextFormat* format) override;

private:
    IDWriteFactory* factory_;  // borrowed -- App owns the DWrite factory
};

// Unwraps a seam handle back to its DirectWrite object, asserting the handle
// really came from this backend. Shared with render_d2d.cpp, which needs both to
// implement DrawText and DrawTextLayout.
IDWriteTextFormat* nativeTextFormat(tinta::TextFormat* format);
IDWriteTextLayout* nativeTextLayout(tinta::TextLayout* layout);

#endif  // TINTA_TEXT_D2D_H
