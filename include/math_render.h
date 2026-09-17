#ifndef TINTA_MATH_RENDER_H
#define TINTA_MATH_RENDER_H

#include "app.h"

#include <memory>
#include <string>

// Native TeX-subset math rendering (#80). No MathJax, no KaTeX, no web
// engine: a recursive-descent parser builds a box tree (runs, scripts,
// fractions, grids, stretchy delimiters, decorations), the text seam measures it,
// the render seam draws it. Unsupported input returns null and the caller falls
// back to showing the raw source in code style - the mermaid pattern.
//
// "The text seam measures it" used to read "DirectWrite measures it, Direct2D draws
// it", and the change of wording is the whole of this file's port: every measurement
// goes through app.textFactory and every primitive through a tinta::RenderTarget, so
// src/math_render.cpp compiles off Windows and src/math_stub.cpp -- which existed only
// to supply six symbols while it did not -- is gone.

struct MathBox;
using MathBoxPtr = std::shared_ptr<MathBox>;

// Parse + lay out one equation at the given base font size. Display mode
// uses slightly more generous spacing. Results are cached per
// (source, size, display); the cache is cleared by mathClearCache.
MathBoxPtr mathParse(App& app, const std::wstring& latex, float fontSize,
                     bool display);

float mathBoxWidth(const MathBoxPtr& box);
float mathBoxHeight(const MathBoxPtr& box);
// Distance from the box top to the text baseline (for inline alignment)
float mathBoxBaseline(const MathBoxPtr& box);

// Accumulate the extra space needed above/below a normal text line to
// contain baseline-aligned inline equations. Call once for each equation.
void mathExpandLine(const MathBoxPtr& box, float textBaseline, float lineHeight,
                    float& above, float& below);

// Draw with the box's top-left corner at (x, y) in the current target
void mathBoxDraw(App& app, const MathBoxPtr& box, float x, float y,
                 const tinta::Color& color);

// Same, into an arbitrary target (offscreen export rasterization)
void mathBoxDrawTo(tinta::RenderTarget* target, tinta::Brush* brush,
                   const MathBoxPtr& box, float x, float y,
                   const tinta::Color& color);

// Push the box's runs/rules/lines into the retained paint lists at (x, y).
// Run layouts are SHARED: the retained list and the cached MathBox each hold a
// reference, and the layout dies with the last one. clearLayoutCache dropping the
// retained list is therefore safe while the box is still in the math cache -- which
// was the point of the AddRef this sentence used to describe.
void mathBoxRetain(App& app, const MathBoxPtr& box, float x, float y,
                   const tinta::Color& color);

// Drop all cached boxes (zoom / theme font changed)
void mathClearCache();

// Standalone SVG markup for a laid-out box (HTML export): text runs at
// their measured positions, rules as rects, arrow strokes as lines
std::string mathBoxSvg(const MathBoxPtr& box, const std::string& colorCss,
                       const std::string& fontFamilyCss);

#endif // TINTA_MATH_RENDER_H
