#ifndef TINTA_TEXT_FORMATS_H
#define TINTA_TEXT_FORMATS_H

// The twenty-two cached DOCUMENT text formats, created through the seam and owned by App.
//
// WHY THIS HEADER EXISTS: the same eleven declarations were being built TWICE, once per
// backend, and the two copies were line-for-line parallel with one of them naming
// DirectWrite. src/d2d_init.cpp's updateTextFormats called
// dwriteFactory->CreateTextFormat eleven times into App::native*Format and then seated a
// wrapper over each; src/qt_init.cpp's installQtTextFormats called
// textFactory->CreateTextFormat with the same families, sizes and weights and installed
// the result. That file's own comment already said what the duplication cost -- "a number
// that disagrees with d2d_init is a divergence, not a Qt decision" -- which is a warning
// about a shape, not a fix for it. There is now one copy of the numbers, and since the
// retired d2d_init.cpp went in 0b39ae6 one initialiser calls it.
//
// THE TWELVE, WHICH IS TWENTY-TWO OBJECTS: textFormat, headingFormat, codeFormat,
// boldFormat, italicFormat, boldItalicFormat, codeBoldFormat, codeItalicFormat,
// codeBoldItalicFormat, supSubFormat, headingFormats[6] and headingItalicFormats[6].
// Every bold/italic combination exists because inline spans nest -- ***both***,
// **`code`**, *`code`* -- and a heading's *italic* needs the heading's own size.
//
// THE CHROME FORMATS ARE HERE NOW TOO, and the paragraph that stood here is corrected
// rather than deleted because it was the roadmap and the roadmap has been walked. It read:
// "NOT THE CHROME FORMATS... the ~20 others are updateOverlayFormats and
// ensureThemePreviewFormats, and those two still create theirs natively... a mixed shape
// that is temporary by construction, since the second kind disappears as each of the other
// three functions follows this one." All three have now followed. `updateOverlayFormats` and
// `ensureThemePreviewFormats` are gone from src/d2d_init.cpp; their bodies are
// installChromeFormats and ensureThemePreviewFormats below, and DirectWrite creates no text
// format anywhere in this tree any more.
//
// NOTHING IS MIXED ANY MORE, and the paragraph that stood here described the last of it: the
// chrome formats' storage was still Seated<tinta::TextFormat, IDWriteTextFormat> where the
// document formats' was a plain unique_ptr, so the coupling to DirectWrite had left the CODE
// and survived in the TYPE. That follow-on slice has landed. EVERY text-format storage in App
// is std::unique_ptr<tinta::TextFormat> -- no count is written here on purpose, because the
// theme-preview storages are three per installed theme and a figure would age the way the one
// above it did -- one makeFormat serves all 25 call sites in src/text_formats_app.cpp, and
// include/app.h declares no DirectWrite text type at all.
//
// THIS COMMENT IS WHY THE SWEEP FOR THAT SLICE WAS INCOMPLETE AT PLAN TIME, which is worth
// leaving on the page. The plan enumerated the stale-comment sites by grepping the thirteen
// STORAGE MEMBER NAMES, and this header names none of them -- it names only the type. So the
// first census returned three files and the census keyed on the TYPE SPELLING returned four,
// and this was the fourth. Enumerate on the failure mode, not on the shape you already fixed.
//
// PORTABLE: nothing here or in src/text_formats_app.cpp names a Windows type, which is
// what lets a non-Windows build create a document format at all. Before this existed, off
// Windows App had a text FACTORY and no text FORMATS -- the first document handed to a Qt
// window segfaulted on a null baseFormat -- and the fix lived in a Qt-only file, so the
// portability was real and unshared.

#include "text_api.h"

struct App;

// THE TWO TYPOGRAPHY SETS, AS FUNCTIONS RATHER THAN AS A TABLE INSIDE AN App-TAKING ONE.
//
// They live in this header, and not in the .cpp beside installDocumentTextFormats, for one
// measured reason: tests/typography_tests.cpp is HEADER-ONLY -- it links no source file at
// all -- and it used to build its own local copies of both sets. Its comment said why, and
// named two blockers: "Reproduced here rather than called, because that function takes an
// App& and lives in a Windows-only file." Moving the file expires the second blocker and
// not the first, so the sets themselves have to stop taking an App& before that test can
// assert the real thing. Now it calls these, and a plant in either set fails it. Before, a
// plant in createTypography passed the test that exists to guard it.
//
// The tags are the four-character OpenType names, and the divergence between the backends
// is documented once at tinta::qtCanHonourFeature in text_api.h -- including that dropping
// `dlig` on Qt is a decision (Alex, 2026-09-15) rather than an omission.
namespace tinta {

// Body font: standard ligatures, kerning, contextual alternates. Qt honours all three.
inline Typography bodyTypographySet() {
    Typography t;
    t.add("liga");
    t.add("kern");
    t.add("calt");
    return t;
}

// Code font: programming ligatures, for faces like Cascadia Code and Fira Code.
inline Typography codeTypographySet() {
    Typography t;
    t.add("liga");
    t.add("dlig");
    return t;
}

}  // namespace tinta

// Assigns both sets into App. One call, at startup, from src/qt_init.cpp -- and on the Qt
// stack that was new: createTypography, which this replaced, had exactly one caller in the
// tree (the retired src/main_d2d.cpp), so the Qt path ran with two default-constructed sets and applied no
// OpenType feature list to any of render.cpp's 13 createLayout sites. Held by
// tests/qt_entry_tests.cpp, whose two assertions read count 0 before this function existed.
//
// THAT ABSENCE WAS NOT COSMETIC, and the figure is worth carrying next to the declaration
// because the obvious reasoning says otherwise. Measured on 121 printed pages across four
// fixtures on the Windows backend: installing no typography differs from the shipped
// rendering on 85 of them, and installing the three tags but dropping `kern` differs on
// 112. So the Qt stack was on the 85-pages-different side of that comparison, and the only
// instrument sensitive enough to say so runs on the backend that was already correct.
//
// WHAT THIS DOES NOT DO: make the two backends render alike. It makes them hold the same
// data. QtTextLayout::SetTypography can act on one of the three tags and that one is
// already Qt's default, so Qt renders on shaper defaults either way -- the divergence is
// now measured and named instead of invisible, and closing it needs QFont::setFeature and
// therefore a Qt floor of 6.7, which is Alex's call. See src/qt_init.cpp's call site for
// the full table, including the one prediction it refuted.
//
// Not idempotent-sensitive: the sets are values, so a second call overwrites rather than
// accumulating, which is why the Windows path's single call from WinMain and the Qt path's
// single call from installQtBackends need no release counterpart. There is nothing to free
// -- App used to hold two IDWriteTypography objects here and holds two values now.
void installTypography(App& app);

// Creates all twenty-two against App's CURRENT theme, contentScale and zoomFactor, and
// re-measures the four cached space widths. Idempotent: it releases first, so the theme
// change, zoom step and DPI change that call it repeatedly cannot stack formats.
//
// Requires App::textFactory to be seated. Returns having created nothing if it is not --
// stated rather than asserted, because the Windows caller seats it in initD2D and the Qt
// caller in installQtBackends, and a caller that forgets would otherwise get twenty-two
// nulls and a blank document with no complaint from anything.
void installDocumentTextFormats(App& app);

// Drops all twenty-two and zeroes the four space widths. A stale width against a released
// format is the same class of bug as a stale wrapper over a released native, and it would
// survive a reinstall silently because the new formats measure the same.
void releaseDocumentTextFormats(App& app);

// THE TEN CHROME FORMATS: search box, small signal, theme title, theme header, stats,
// folder browser, both TOC rows, editor text and editor gutter. Created against App's
// CURRENT theme and contentScale -- and the editor pair against contentScale * zoomFactor,
// which is the one asymmetry in the function and the one worth checking a change against.
//
// IDEMPOTENT, AND THE REASON IS A DEFECT THIS PORT WOULD OTHERWISE HAVE INTRODUCED.
// It releases first, like installDocumentTextFormats. That is not symmetry for its own
// sake: three of the ten want an ellipsis trimming sign, App::trimmingSigns owns those
// signs because Qt does not retain them, and the Qt body this function is MOVED FROM
// pushed three per call and cleared none. Safe there, and its own comment said why -- "here
// there is one caller and one lifetime" (installQtBackends, at startup). This function has
// FOUR callers' worth of frequency instead: updateTextFormats runs it on every theme
// change, every zoom step and every DPI change, so the un-released body would have leaked
// three inline objects per call into a vector nothing trims. The Windows function this
// replaces was idempotent for exactly this reason and said so; the property had to survive
// the move, and a count assertion holds it rather than this paragraph.
//
// Requires App::textFactory. Returns having created nothing if it is absent -- but releases
// FIRST even then, so a caller that has just dropped its factory cannot leave the previous
// theme's formats live behind it.
void installChromeFormats(App& app);

// Drops the ten, clears the per-theme preview vector and zeroes App::editorCharWidth.
//
// THE PREVIEW VECTOR IS CLEARED AND NOT MERELY DROPPED, and it is the one line here that
// would be wrong the other way: ensureThemePreviewFormats keys its early return on the
// vector being EMPTY, so leaving ten slots of seated nulls behind makes the lazy door a
// permanent no-op and the theme chooser finds null formats forever after the first release.
// Both backends' predecessors cleared it. The measured editorCharWidth goes for the same
// reason the document space widths do: it survives a reinstall silently, because the rebuilt
// format measures the same right up to the DPI or zoom change that means it does not.
void releaseChromeFormats(App& app);

// THE PER-THEME PREVIEW TRIPLE -- name, preview and code face for every built-in theme, so
// three times themeCount() formats. LAZY, and deliberately not part of installChromeFormats:
// they are wanted only when the theme chooser opens, and building them at startup would put
// ~30 CreateTextFormat calls on the critical path for a panel most runs never show.
//
// Early-returns when the vector is already non-empty, which is what makes it safe to call
// from a render path -- src/overlays.cpp does exactly that, once per chooser paint.
// releaseChromeFormats is the other half: it CLEARS the vector so this door reopens.
void ensureThemePreviewFormats(App& app);

#endif // TINTA_TEXT_FORMATS_H
