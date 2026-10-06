#ifndef TINTA_PRINT_QT_H
#define TINTA_PRINT_QT_H

#include "app.h"

class QPrinter;

// The half of the Qt printDocument that a test can reach. printDocument shows a QPrintDialog
// and, on Accept, calls this with the printer the user chose; a test calls it with a QPrinter
// set to PdfFormat, which needs neither a dialog nor a printer. It paginates exactly as the
// preview, the --printpages dump and the PDF export do (src/print_pages.h), at the printer's
// own full page size, and returns false if no page was painted.
bool printDocumentTo(App& app, QPrinter& printer);

#endif  // TINTA_PRINT_QT_H
