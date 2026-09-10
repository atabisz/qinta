#include "i18n.h"
#include "app.h"

// The App-aware half of i18n, and the only part of it that needs app.h.
//
// i18n.cpp holds the translation table, the language registry and the
// languages.ini reader -- none of which touch App, and all of which compile on
// Linux and macOS now that this one function is not sitting among them. That is
// the whole reason for a second translation unit: a single line of forwarding
// was pinning 1500 lines of platform-free string data to windows.h, d2d1.h and
// dwrite.h by way of app.h.
//
// Both overloads stay declared in i18n.h, so the 169 `tr(app, "key")` call sites
// across the tree are unaffected and did not have to change.

const wchar_t* tr(const App& app, const char* key) {
    return tr(app.currentLanguageIndex, key);
}
