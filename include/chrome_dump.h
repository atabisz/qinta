#ifndef TINTA_CHROME_DUMP_H
#define TINTA_CHROME_DUMP_H

#include <string>

struct App;

// THE CHROME FALSIFIER, and it exists because the rig that made the last three port
// slices trustworthy is structurally blind to chrome.
//
// --printpages rasterises the DOCUMENT. The forty chrome formats -- ten flat overlay and
// editor formats, plus three per theme -- never appear on a printed page, so a byte-
// identity comparison over 121 PNGs cannot see a single one of them change. That is why
// this slice was deferred twice, and writing the reading down is the first half of it.
//
// WHY IT READS THE NATIVE OBJECT RATHER THAN THE SEAM. tinta::TextFormat
// (include/text_api.h) has five setters and exactly ONE getter, GetFontSize(). Family,
// weight, style, stretch, locale, wrapping, both alignments, trimming and line spacing
// are all write-only through the seam -- which is why tests/qt_entry_tests.cpp asserts
// chrome sizes by name and then has to MEASURE a narrow row to say anything about
// trimming. A dump built on the seam would compare one property of eleven and read as
// agreement. src/text_d2d.cpp's nativeTextFormat() resolves a seam pointer back to the
// IDWriteTextFormat behind it, and DirectWrite has a getter for every property this app
// sets, so the native read is both complete and the authority for what the renderer will
// actually use.
//
// AND IT SURVIVES THE PORT, which is the reason it is worth committing rather than
// hand-rolling. Today App owns the ten natives directly; after the chrome formats move to
// tinta::TextFactory a D2DTextFormat owns them instead. Because every read here goes
// through the SEAM pointer and resolves down, the same dump reads the same objects on both
// sides of that change -- an instrument the port would have destroyed is no instrument at
// all.
//
// WHAT IT CANNOT SEE, stated here rather than discovered later: it reads what each format
// was TOLD, never what it paints. Nothing in this tree photographs a window, so a clean
// diff of two dumps is not evidence that chrome DRAWS the same, and no write-up may read
// it that way.
//
// Returns the number of format rows written, 0 on failure -- so the caller can exit
// non-zero rather than repeating --printpages' shape, which returns 0 having printed one
// blank page for a document that does not exist (memory
// printpages-exits-zero-on-missing-file).
int dumpChromeFormats(App& app, const std::wstring& path);

#endif  // TINTA_CHROME_DUMP_H
