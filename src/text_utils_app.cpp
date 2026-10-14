#include "utils.h"
#include "app.h"

#include <memory>

// The App-aware, backend-free half of the text measurement helpers. Same split, same
// reasoning and the same naming as src/i18n_app.cpp -- see that file's own note, which
// this one deliberately does not repeat.
//
// WHY ONE FUNCTION EARNS A TRANSLATION UNIT, measured rather than asserted. The Qt
// frontend's link closure was read with a linker on 2026-09-16: render.cpp was compiled
// with qinta's own flags, dropped onto qinta's own link line, and the 23 unresolved
// symbols were mapped back to the objects that define them. Twenty-two of the twenty-three
// came from files that are already portable or from math_render.cpp. Exactly ONE came from
// src/utils.cpp -- this function -- and utils.cpp is `fatal error: windows.h` off Windows,
// which is the worst kind of blocker because it means the file was never measured at all.
//
// So the choice was: port a 198-line Windows FEATURE file (window titles, shell handoffs,
// clipboard, path canonicalisation) to unblock one measurement helper, or move the helper.
// Moving it is a topology change with nothing behind it -- the function was ALREADY written
// entirely in tinta:: terms and names no DirectWrite type, which utils.cpp's own comment
// said in as many words before this moved. It is a move and not a rewrite: the body below
// is byte-for-byte what stood at src/utils.cpp:23.
//
// NOT text_utils.cpp, and that is the one judgement call here. That file's stated
// invariant is that it holds the text helpers which need no App and no backend, which is
// what lets it compile with two includes and no app.h. Adding an App& taking function
// would have made its own header comment false. i18n.cpp faced exactly this and split the
// same way rather than relaxing.
//
// The NATIVE overload -- measureText(App&, const std::wstring&, IDWriteTextFormat*) --
// lived in d2d_init.cpp, declared in d2d_init.h, and was Windows-only by its argument
// type. THIS PARAGRAPH USED TO SAY IT WAS IN utils.cpp AND THAT FOUR CALL SITES MADE IT
// "live code rather than a leftover". Both halves went false: it moved, and the four
// callers were the space-width measurements in updateTextFormats, which the seam overload
// took over when the eleven document formats stopped being native. That left it ZERO
// callers, and it was deliberately not deleted in that same change -- a removal's falsifier
// is a link error in another translation unit, which is a different arm from the page hashes
// that proved the de-duplication. It went with the retired d2d_init.cpp in 0b39ae6.
//
// WHAT ASSERTS THE TWO AGREE, stated as measured rather than as inherited. utils.cpp used
// to say the agreement was "asserted rather than assumed -- see
// tests/text_factory_tests.cpp", and checking it while moving this function found that
// overstated: that test calls neither overload. What it does assert is the LAYER BENEATH
// -- that app.textFactory->CreateTextLayout and dwriteFactory->CreateTextLayout return
// identical TextMetrics for the same string and format, across eleven fields. Since each
// overload is a thin measure over exactly one of those two calls, the agreement follows
// from that; it is a derivation, not a direct assertion, and the difference is worth the
// two sentences because a derivation breaks silently when either body stops being thin.
// Splitting across translation units does not weaken it -- that test links both.

float measureText(App& app, const std::wstring& text, tinta::TextFormat* format) {
    if (!app.textFactory || !format) return 0;
    std::unique_ptr<tinta::TextLayout> layout = app.textFactory->CreateTextLayout(
        text.c_str(), (uint32_t)text.length(), format, 10000.0f, 100.0f);
    if (!layout) return 0;

    // RAII, because tinta::TextLayout OWNS what it wraps (its native, in the retired
    // text_d2d.h) -- the manual Release the native path needed is gone here, not merely moved.
    tinta::TextMetrics metrics{};
    layout->GetMetrics(&metrics);
    return metrics.widthIncludingTrailingWhitespace;
}
