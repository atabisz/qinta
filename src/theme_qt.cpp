#include "theme_apply.h"

#include <QFontDatabase>
#include <QString>
#include <QStringList>

#include <algorithm>

// The Qt arms of include/theme_apply.h's three backend hooks. The Windows arms are in
// src/d2d_init.cpp; no target links both files.

// A REAL GAP, not a design: the title bar keeps the platform's colours when the theme
// changes. Qt has no portable caption-colour API, and QWidget::setWindowTitle's frame is the
// window manager's. Nothing is drawn wrong -- the document and tab strip follow the theme --
// but a dark theme under a light title bar is visible, and this is where it would be fixed.
void applyWindowChrome(App&) {}

// Nothing to install. QFont resolves missing glyphs per font from the system's fallback
// configuration (fontconfig on Linux, CoreText on macOS, DirectWrite underneath Qt on
// Windows), so there is no chain object to build or hand to QtTextFactory.
//
// What that loses, stated so it is not mistaken for parity: the Windows chain orders CJK
// ideograph families by the UI language (#155), so kanji on a Japanese system render with a
// Japanese face. Here the order is whatever the platform's configuration says.
void installFontFallback(App&) {}

// Same contract as the Windows arm: enumerated once, sorted, and later calls do nothing.
// QFontDatabase::families is STATIC in Qt 6, as src/text_qt.cpp records for hasFamily.
// It returns names in the application's writing system, where the Windows arm prefers the
// en-US name -- the same family can therefore be spelled differently on the two stacks.
void enumerateSystemFontFamilies(App& app) {
    if (!app.systemFontFamilies.empty()) return;
    const QStringList families = QFontDatabase::families();
    app.systemFontFamilies.reserve(static_cast<size_t>(families.size()));
    for (const QString& family : families) {
        if (!family.isEmpty()) app.systemFontFamilies.push_back(family.toStdWString());
    }
    std::sort(app.systemFontFamilies.begin(), app.systemFontFamilies.end());
}
