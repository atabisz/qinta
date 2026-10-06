#include "qt_launch.h"

#include <QGuiApplication>
#include <QList>
#include <QPalette>
#include <QRect>
#include <QScreen>
#include <QStyleHints>
#include <QWidget>

#include "app.h"
#include "editor.h"
#include "settings.h"
#include "theme_apply.h"
#include "theme_data.h"

namespace tinta {

std::vector<MonitorArea> monitorsFromQt() {
    std::vector<MonitorArea> monitors;
    const QList<QScreen*> screens = QGuiApplication::screens();
    monitors.reserve(static_cast<size_t>(screens.size()));
    for (const QScreen* screen : screens) {
        // Defensive rather than theoretical: QGuiApplication::screens() has returned a list
        // containing a null during display hot-plug on more than one platform, and the cost
        // of the check is one comparison per monitor at startup.
        if (screen == nullptr) continue;
        const QRect bounds = screen->geometry();
        const QRect work = screen->availableGeometry();
        MonitorArea area;
        area.bounds = {bounds.x(), bounds.y(), bounds.width(), bounds.height()};
        area.work = {work.x(), work.y(), work.width(), work.height()};
        monitors.push_back(area);
    }
    return monitors;
}

PlacementRequest placementRequestFrom(const LaunchOptions& options, const Settings& settings) {
    PlacementRequest request;
    request.savedX = settings.windowX;
    request.savedY = settings.windowY;
    request.savedWidth = settings.windowWidth;
    request.savedHeight = settings.windowHeight;
    request.cascade = options.cascade;
    request.hasPos = options.hasPos;
    request.posX = options.posX;
    request.posY = options.posY;
    return request;
}

void applyPlacement(QWidget& window, const WindowRect& placement) {
    window.resize(placement.width, placement.height);
    if (placement.x != kWindowPositionUseDefault && placement.y != kWindowPositionUseDefault) {
        window.move(placement.x, placement.y);
    }
}

void applyLightMode(App& app) {
    app.currentThemeIndex = kLightModeThemeIndex;
    app.theme = themeAt(kLightModeThemeIndex);
    app.darkMode = app.theme.isDark;
}

void applyLaunchFlags(App& app, const LaunchOptions& options) {
    app.showStats = options.showStats;
    app.forceTabStrip = options.forceTabStrip;  // drag-out satellites keep the tab row
}

void enterLaunchMode(App& app, const LaunchOptions& options) {
    if (options.quickNote) {
        enterQuickNoteMode(app);
    } else if (options.startInEditMode) {
        enterEditMode(app);
    }
}

bool systemPrefersLight() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Unknown counts as light, as WinMain's registry read does when the value is missing.
    return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Dark;
#else
    return QGuiApplication::palette().color(QPalette::Window).lightness() >= 128;
#endif
}

void applySavedTheme(App& app, const Settings& settings, bool systemPrefersLight) {
    app.followSystemTheme = settings.followSystemTheme;
    app.lightThemeIndex = settings.lightThemeIndex;
    app.darkThemeIndex = settings.darkThemeIndex;
    // WinMain checks only the top end; a negative index from a hand-edited file would be kept
    // there as currentThemeIndex while themeAt drew slot 0, so the two would disagree.
    int start = settings.followSystemTheme ? followThemeIndex(app, systemPrefersLight)
                                           : settings.themeIndex;
    if (start < 0 || start >= themeCount()) start = 0;
    app.currentThemeIndex = start;
    app.theme = themeAt(start);
    app.darkMode = app.theme.isDark;
}

void showPlaced(QWidget& window, bool maximized) {
    if (maximized) {
        window.showMaximized();
    } else {
        window.show();
    }
}

void saveClosingState(const App& app, const QWidget& window) {
    Settings settings = loadSettings();
    storeThemeChoice(settings, app);
    settings.windowMaximized = window.isMaximized();
    if (!window.isMaximized() && !window.isFullScreen()) {
        settings.windowX = window.x();
        settings.windowY = window.y();
        settings.windowWidth = window.width();
        settings.windowHeight = window.height();
    }
    saveSettings(settings);
}

}  // namespace tinta
