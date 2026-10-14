#ifndef TINTA_EXPORT_H
#define TINTA_EXPORT_H

#include "app.h"
#include "mermaid_ext.h"
// Real windows.h here, stand-ins for its value types off Windows. This header
// names HWND/WPARAM/LPARAM by value and nothing else from Win32 -- no COM
// interfaces, no API calls -- so that swap is the whole of its port cost.
#include "win32_types.h"
#include <string>

// "Export as..." (#export_as): a save dialog whose file-type filter picks
// the format. HTML is generated directly from the element tree with the
// active theme inlined; diagrams and math become standalone SVG. DOCX packs
// OOXML into a stored zip, with diagrams and math rasterized to PNG through
// the same primitives.

// Opens the dialog and dispatches on the chosen filter/extension
void exportDocumentAs(App& app);

// Direct writers (also callable from tests/tools)
bool exportHtmlFile(App& app, const std::wstring& path);
bool exportDocxFile(App& app, const std::wstring& path);
// PDF rides the print pipeline into the Microsoft Print to PDF driver with
// the job output redirected to the file: vector pages, embedded fonts, no
// dialog (print.cpp)
bool exportPdfFile(App& app, const std::wstring& path);

// Any native diagram source -> prims at the given scale; flowcharts go
// through their export mirror, other families through mermaidext. ok=false
// means the caller should show the source instead. (export_html.cpp)
mermaidext::Built buildDiagramPrims(App& app, const std::string& sourceUtf8,
                                    float scale);
mermaidext::Built buildFlowchartPrims(App& app, const std::string& sourceUtf8,
                                      float scale);

// Rasterizes a diagram at 2x and puts it on the clipboard as a bitmap plus
// a PNG, for pasting into slides/chats/documents (export_docx.cpp)
//
// NO HWND, 2026-09-19: the clipboard write goes through include/clipboard.h's seam, which
// takes no window handle for the reason that header sets out at length -- the handle only
// ever reached OpenClipboard, and QClipboard is application-global. The format names moved
// with it, which is why this comment no longer says CF_DIB: on Windows it still is one,
// off Windows it is a QImage on a QMimeData, and this declaration is true either way.
bool copyDiagramImage(App& app, const std::string& sourceUtf8);

// Clipboard bitmap -> PNG file (editor image paste; export_docx.cpp)
//
// NO App EITHER, and that is a different loss from the HWND above: `app` was here to
// reach the WIC factory, and the read is GDI now with the encode going through
// tinta::encodePng. Nothing left in the body wants the application state.
bool clipboardImageToPngFile(const std::wstring& path);

#endif  // TINTA_EXPORT_H
