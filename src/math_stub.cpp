// THE MATH SEAM'S PORTABLE SIDE, AND IT IS A STUB ON PURPOSE.
//
// src/math_render.cpp measures LaTeX with DirectWrite and draws it with Direct2D.
// It does not compile off Windows -- 34 errors, first cause an incomplete
// IDWriteTextLayout -- and it is 861 lines. Until it is ported, this file supplies
// the six symbols the portable renderer needs, so that tinta_qt links and draws a
// real document TODAY instead of waiting on the math port.
//
// A link closure over tinta_qt measured the residue exactly: mathParse,
// mathBoxWidth, mathBoxHeight, mathBoxBaseline, mathExpandLine, mathBoxRetain --
// six symbols, all owned by math_render.cpp, and nothing else in the whole
// twenty-file closure was blocked. This file is that list and nothing more; it
// deliberately does NOT define mathBoxDraw, mathBoxDrawTo, mathClearCache or
// mathBoxSvg, which math_render.h also declares. Nothing in the closure calls
// those, and defining them would make this file a fake math backend rather than a
// measured patch over one gap. If a future target needs one, the linker will say
// so, which is the only instrument that has been right about this tree's link
// dependencies every time.
//
// WHAT THIS DOES TO THE PRODUCT, stated plainly because it is a visible behaviour
// and not an internal detail: equations are NOT typeset. Display math renders as a
// code block containing its own LaTeX source, and inline math renders as `$latex$`
// in inline-code style.
//
// THAT IS THE TREE'S OWN DESIGNED FALLBACK, NOT A NEW FAILURE MODE, and that
// distinction is the entire argument for this file existing. include/math_render.h
// states the contract in its header comment: "Unsupported input returns null and the
// caller falls back to showing the raw source in code style - the mermaid pattern."
// Every caller in src/render.cpp already implements it, and all three sites were
// re-read rather than assumed:
//
//   :439  if (!mathBoxes[i]) continue;                -- inline measure pass
//   :519  if (!box) { ... text = L"$" + latex + L"$"; break; }
//   :895  if (!box) { ... layoutCodeBlock(...); return; }
//
// So a Qt build shows every equation's source where the equation goes. A reader sees
// the LaTeX rather than the typeset result -- worse than Windows, and honestly worse,
// because the fallback is visibly a fallback rather than a blank space or a crash.
//
// THE OTHER FIVE FUNCTIONS ARE NOT STUB BEHAVIOUR. They are the real
// implementations' own null branch, copied deliberately:
//
//   math_render.cpp:707  float mathBoxWidth(const MathBoxPtr& box) { return box ? box->width : 0; }
//   math_render.cpp:713  void mathExpandLine(...) { if (!box) return; ... }
//   math_render.cpp:749  void mathBoxRetain(...) { if (!box) return; ... }
//
// Those functions are null-tolerant by contract, so for a null box this file and the
// real backend agree exactly. That matters: it means the five are not approximations
// whose divergence has to be reasoned about, and the ONLY semantic difference between
// this file and math_render.cpp is that mathParse never succeeds.
//
// Given that, the five are also unreachable from src/render.cpp by construction --
// every call site is behind one of the three guards above. So each asserts that the
// box really is null, because a non-null MathBox arriving here means some other
// translation unit built one and this file is being asked a question it cannot
// answer. The assertion is a tripwire for a future change, not a runtime check for
// today, and its limit is stated rather than left to be discovered: under -DNDEBUG
// assert() is inert, so what remains is the null branch's 0 / no-op. That is a safe
// degradation rather than undefined behaviour, which is why the null branch is
// written out even though the assert "should" make it unreachable.
//
// DELETING THIS FILE IS THE GOAL. It goes when math_render.cpp compiles off Windows,
// and the CMake comment on tinta_qt says the same thing at the other end.

#include "math_render.h"

#include <cassert>

MathBoxPtr mathParse(App& app, const std::wstring& latex, float fontSize,
                     bool display) {
    // Every parameter is deliberately unused rather than removed: the signature must
    // match math_render.h exactly or the two files are not interchangeable, and a
    // named-but-unused parameter documents what the real backend consumes.
    (void)app;
    (void)latex;
    (void)fontSize;
    (void)display;
    // Null is the contract's "unsupported input", which every caller handles.
    return nullptr;
}

// --- The null branch of the real implementations -------------------------------
//
// Each assert says "this cannot be reached with a real box while mathParse above
// returns null". Each return value is what math_render.cpp returns for null.

// (void)box in every one of these, and it is load-bearing rather than tidy: under
// -DNDEBUG the assert disappears and `box` becomes an unused parameter, which is a
// warning in a tree built with warnings on. The cast keeps this file's NDEBUG build
// as clean as its debug build, so nobody ever has to decide whether a new warning
// here is real.

float mathBoxWidth(const MathBoxPtr& box) {
    assert(!box && "math_stub.cpp: a non-null MathBox reached mathBoxWidth, so "
                   "something other than this file's mathParse built one");
    (void)box;
    return 0.0f;
}

float mathBoxHeight(const MathBoxPtr& box) {
    assert(!box && "math_stub.cpp: a non-null MathBox reached mathBoxHeight");
    (void)box;
    return 0.0f;
}

float mathBoxBaseline(const MathBoxPtr& box) {
    assert(!box && "math_stub.cpp: a non-null MathBox reached mathBoxBaseline");
    (void)box;
    return 0.0f;
}

void mathExpandLine(const MathBoxPtr& box, float textBaseline, float lineHeight,
                    float& above, float& below) {
    assert(!box && "math_stub.cpp: a non-null MathBox reached mathExpandLine");
    (void)box;
    (void)textBaseline;
    (void)lineHeight;
    // above and below are IN/OUT and must be left exactly as found -- the real
    // implementation's null branch returns before touching either, and a stub that
    // zeroed them would silently collapse the line height of any line that also
    // holds a real box in some future build.
    (void)above;
    (void)below;
}

void mathBoxRetain(App& app, const MathBoxPtr& box, float x, float y,
                   const tinta::Color& color) {
    assert(!box && "math_stub.cpp: a non-null MathBox reached mathBoxRetain");
    (void)box;
    (void)app;
    (void)x;
    (void)y;
    (void)color;
    // Nothing is appended to app's retained-draw lists, which is the real null
    // branch's behaviour and the reason a missing equation leaves no artefact.
}
