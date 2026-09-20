#include "qt_launch.h"

#include <QGuiApplication>
#include <QList>
#include <QRect>
#include <QScreen>
#include <QWidget>

#include "app.h"
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

}  // namespace tinta
