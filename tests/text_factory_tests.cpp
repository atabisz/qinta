// Assertions for the text FACTORY half of the text seam.
//
// WHAT THIS FILE EXISTS TO CATCH
//
// Until this slice, tinta::TextFactory had no production caller at all: both backends
// implemented it, the shared render-api assertions exercised it, and all 36 layout
// creations in src/ went straight to app.dwriteFactory. An interface that only its own
// tests call has never had to agree with anything, so the two risks worth asserting
// here are the two that only appear once a real caller exists.
//
// FIRST: does the seam path produce the SAME MEASUREMENTS as the native path? Every
// migrated site feeds tm.width into a layout decision -- textRight += tm.width, a
// centred label's x -- so a seam that measured differently would not crash or fail to
// draw. It would move text by a few pixels, on one platform, and nothing would say so.
// That is a parity claim and it needs the baseline measured in the same run.
//
// SECOND: does the returned unique_ptr actually RELEASE? tinta::TextLayout OWNS its
// IDWriteTextLayout (text_d2d.h), which is the opposite of D2DBitmap, and the migrated
// sites deleted their manual layout->Release() on the strength of that. A wrapper that
// leaked would pass every drawing test forever.
//
// Unlike bitmap_seam_tests.cpp, these use REAL DirectWrite objects, because they can:
// DWriteCreateFactory is device-independent and needs no HWND, no render target and no
// display. So the factory under test here is the same class d2d_init.cpp seats.

#include "app.h"

#include <cstdio>
#include <memory>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

const wchar_t* const kText = L"Fit 1:1 measuring wide";
const uint32_t kTextLen = 22;
const float kMaxW = 400.0f;
const float kMaxH = 50.0f;

// --- seating -----------------------------------------------------------------

void seatingFollowsTheFactory(App& app, IDWriteFactory* real) {
    // Ordering trap this asserts against: seatTextFactory() must be called AFTER the
    // native factory exists. App::seatSeamWrappers() -- which is where a seater would
    // naturally be added -- has ZERO callers, so a seat added only there compiles
    // clean and leaves app.textFactory null for the whole run.
    app.dwriteFactory = nullptr;
    app.seatTextFactory();
    check(app.textFactory == nullptr, "seating with no native factory yields a null seam factory");

    app.dwriteFactory = real;
    app.seatTextFactory();
    check(app.textFactory != nullptr, "seating with a real native factory yields a usable seam factory");

    // Idempotent, like the render target's seater: the wrapper borrows, so reseating
    // is a plain assignment and must not disturb anything.
    tinta::TextFactory* first = app.textFactory;
    app.seatTextFactory();
    check(app.textFactory == first, "reseating an unchanged factory is stable");
}

// --- parity: the seam must measure what DirectWrite measures ------------------

void seamAndNativeAgreeOnMetrics(App& app, IDWriteFactory* real,
                                 IDWriteTextFormat* nativeFormat,
                                 tinta::TextFormat* seamFormat) {
    // BASELINE, measured in this same run rather than recalled -- the native path
    // exactly as the migrated sites used to spell it.
    IDWriteTextLayout* nativeLayout = nullptr;
    real->CreateTextLayout(kText, kTextLen, nativeFormat, kMaxW, kMaxH, &nativeLayout);
    check(nativeLayout != nullptr, "the native baseline layout was created (otherwise parity is vacuous)");
    if (!nativeLayout) return;

    DWRITE_TEXT_METRICS nativeMetrics{};
    nativeLayout->GetMetrics(&nativeMetrics);
    nativeLayout->Release();

    std::unique_ptr<tinta::TextLayout> seamLayout = app.textFactory->CreateTextLayout(
        kText, kTextLen, seamFormat, kMaxW, kMaxH);
    check(seamLayout != nullptr, "the seam layout was created");
    if (!seamLayout) return;

    tinta::TextMetrics seamMetrics{};
    seamLayout->GetMetrics(&seamMetrics);

    // Exact equality, not a tolerance: both paths end in the same
    // IDWriteTextLayout::GetMetrics call on an identically-constructed layout, so any
    // difference at all is a field being dropped or reordered in the struct copy, not
    // a rounding difference. A tolerance here would hide exactly the bug worth finding.
    check(seamMetrics.width == nativeMetrics.width, "seam and native agree on width");
    check(seamMetrics.height == nativeMetrics.height, "seam and native agree on height");
    check(seamMetrics.widthIncludingTrailingWhitespace ==
              nativeMetrics.widthIncludingTrailingWhitespace,
          "seam and native agree on widthIncludingTrailingWhitespace");
    check(seamMetrics.left == nativeMetrics.left, "seam and native agree on left");
    check(seamMetrics.top == nativeMetrics.top, "seam and native agree on top");
    check(seamMetrics.layoutWidth == nativeMetrics.layoutWidth,
          "seam and native agree on layoutWidth");
    check(seamMetrics.layoutHeight == nativeMetrics.layoutHeight,
          "seam and native agree on layoutHeight");
    check(seamMetrics.lineCount == nativeMetrics.lineCount,
          "seam and native agree on lineCount");
    check(seamMetrics.maxBidiReorderingDepth == nativeMetrics.maxBidiReorderingDepth,
          "seam and native agree on maxBidiReorderingDepth");

    // COUNTER-CASE (claim 18): the assertions above would also pass if GetMetrics
    // returned a zeroed struct on both paths. A width of 0 for non-empty text at
    // 400 DIP would mean neither path measured anything, and every equality above
    // would still be true.
    check(nativeMetrics.width > 0.0f, "the baseline actually measured something non-zero");
    check(seamMetrics.lineCount >= 1, "the seam layout has at least one line");
}

void alignmentSettersReachTheNative(App& app, tinta::TextFormat* seamFormat) {
    // The migrated sites call these on the LAYOUT, having previously passed
    // DWRITE_TEXT_ALIGNMENT_CENTER. text_api.h pins the enum values to the DWRITE_
    // ones so the backend can cast; this reads the value back out through DirectWrite
    // to confirm the cast lands on the value the call site used to pass literally.
    std::unique_ptr<tinta::TextLayout> layout = app.textFactory->CreateTextLayout(
        kText, kTextLen, seamFormat, kMaxW, kMaxH);
    if (!layout) { check(false, "layout for the alignment check was created"); return; }

    layout->SetTextAlignment(tinta::TextAlignment::Center);
    layout->SetParagraphAlignment(tinta::ParagraphAlignment::Center);

    IDWriteTextLayout* native = nativeTextLayout(layout.get());
    check(native != nullptr, "the seam layout unwraps to a native layout");
    if (!native) return;
    check(native->GetTextAlignment() == DWRITE_TEXT_ALIGNMENT_CENTER,
          "tinta::TextAlignment::Center reaches DirectWrite as DWRITE_TEXT_ALIGNMENT_CENTER");
    check(native->GetParagraphAlignment() == DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
          "tinta::ParagraphAlignment::Center reaches DirectWrite as DWRITE_PARAGRAPH_ALIGNMENT_CENTER");

    // Counter-case: prove the read-back can return something else, or the two
    // assertions above would pass against a backend that ignored the setter and a
    // default that happened to be CENTER.
    layout->SetTextAlignment(tinta::TextAlignment::Trailing);
    check(native->GetTextAlignment() == DWRITE_TEXT_ALIGNMENT_TRAILING,
          "the alignment read-back can return a different value (counter-case)");
}

// --- ownership: the unique_ptr must release ------------------------------------

void destroyingTheLayoutReleasesTheNative(App& app, tinta::TextFormat* seamFormat) {
    // The migrated sites deleted their manual layout->Release() because
    // tinta::TextLayout owns. Measured by holding a second reference and reading what
    // the final Release returns: 0 means the wrapper had dropped its own.
    IDWriteTextLayout* native = nullptr;
    {
        std::unique_ptr<tinta::TextLayout> layout = app.textFactory->CreateTextLayout(
            kText, kTextLen, seamFormat, kMaxW, kMaxH);
        if (!layout) { check(false, "layout for the ownership check was created"); return; }
        native = nativeTextLayout(layout.get());
        if (!native) { check(false, "the ownership-check layout unwraps"); return; }
        native->AddRef();  // refcount 2: one the wrapper owns, one this scope owns
    }
    // The wrapper is gone. If it released, our reference is the last one.
    check(native->Release() == 0,
          "destroying the seam layout released the native (RAII replaces the manual Release)");
}

}  // namespace

int main() {
    IDWriteFactory* real = nullptr;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&real));
    if (FAILED(hr) || !real) {
        // Not a silent pass. A missing DirectWrite makes every assertion below
        // unreachable, and an unreachable suite that exits 0 is claim 24's false
        // negative -- so this is a failure, loudly.
        std::printf("FAIL: DWriteCreateFactory failed (hr=0x%08lX); nothing was asserted\n",
                    (unsigned long)hr);
        return 1;
    }

    IDWriteTextFormat* nativeFormat = nullptr;
    hr = real->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us",
        &nativeFormat);
    if (FAILED(hr) || !nativeFormat) {
        std::printf("FAIL: CreateTextFormat failed (hr=0x%08lX); nothing was asserted\n",
                    (unsigned long)hr);
        real->Release();
        return 1;
    }

    App app;
    seatingFollowsTheFactory(app, real);

    // The seam format is built through the seam too, so the parity check compares two
    // whole paths rather than sharing a format object between them.
    std::unique_ptr<tinta::TextFormat> seamFormat = app.textFactory->CreateTextFormat(
        L"Segoe UI", tinta::FontWeight::Normal, tinta::FontStyle::Normal,
        tinta::FontStretch::Normal, 14.0f, L"en-us");
    if (!seamFormat) {
        std::printf("FAIL: seam CreateTextFormat returned null; parity was not asserted\n");
        nativeFormat->Release();
        real->Release();
        return 1;
    }

    seamAndNativeAgreeOnMetrics(app, real, nativeFormat, seamFormat.get());
    alignmentSettersReachTheNative(app, seamFormat.get());
    destroyingTheLayoutReleasesTheNative(app, seamFormat.get());

    // App does not own the factory this test created, and shutdown() would Release it.
    seamFormat.reset();
    app.dwriteFactory = nullptr;
    app.textFactory = nullptr;
    nativeFormat->Release();
    real->Release();

    if (failures == 0) {
        std::printf("all text-factory assertions run, 0 failed\n");
        return 0;
    }
    std::printf("%d text-factory assertion(s) failed\n", failures);
    return 1;
}
