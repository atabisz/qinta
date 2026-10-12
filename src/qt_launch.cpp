#include "qt_launch.h"

#include <cmath>
#include <filesystem>
#include <system_error>

#include <QFileInfo>
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
#include "startpage.h"  // startPageActive
#include "tabs.h"  // tabOpenPath, for a document handed to the running window
#include "utils.h"  // launchSecondInstance
#include "theme_apply.h"
#include "theme_data.h"
#include "text_utils.h"

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
    storePreferences(settings, app);
    settings.windowMaximized = window.isMaximized();
    if (!window.isMaximized() && !window.isFullScreen()) {
        settings.windowX = window.x();
        settings.windowY = window.y();
        settings.windowWidth = window.width();
        settings.windowHeight = window.height();
    }
    saveSettings(settings);
}

namespace {
bool isHarnessRun(const LaunchOptions& options) {
    return !options.printPagesDir.empty() || !options.exportHtmlPath.empty() ||
           !options.exportDocxPath.empty() || !options.exportPdfPath.empty();
}
}  // namespace

std::vector<std::string> sessionToRestore(const LaunchOptions& options, const Settings& settings,
                                          int& active) {
    active = 0;
    std::vector<std::string> paths;
    if (!options.inputFile.empty() || options.quickNote || options.cascade ||
        options.startInEditMode || isHarnessRun(options) || !settings.openInTabs) {
        return paths;
    }
    for (size_t i = 0; i < settings.sessionTabs.size(); i++) {
        const std::string& path = settings.sessionTabs[i];
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::path(toWide(path)), ec)) continue;
        if ((int)i == settings.sessionActive) active = (int)paths.size();
        paths.push_back(path);
    }
    return paths;
}

std::string absoluteLaunchPath(const std::string& path) {
    if (path.empty()) return path;
    return QFileInfo(QString::fromStdString(path)).absoluteFilePath().toStdString();
}

bool ownsSession(const LaunchOptions& options) {
    return !options.cascade && !options.quickNote;
}

void restoreLaunchDocument(App& app, const Settings& settings, const LaunchOptions& options,
                           bool namedOnCommandLine) {
    if (!app.currentFile.empty()) {
        if (namedOnCommandLine && !isHarnessRun(options)) persistRecentFile(app.currentFile);
        if (!options.startInEditMode) {
            app.pendingScrollRestore = findReadingPosition(settings, app.currentFile);
            const float zoom = findReadingZoom(settings, app.currentFile);
            if (zoom > 0.0f && std::fabs(zoom - app.zoomFactor) > 0.01f) app.zoomFactor = zoom;
        }
    }
    if (app.zoomFactor != app.appliedZoomFactor) updateTextFormats(app);
}

namespace {

// WM_COPYDATA's restore-and-foreground, for every document that arrives from outside.
void bringForward(QWidget& window) {
    if (window.isMinimized()) window.showNormal();
    window.raise();
    window.activateWindow();
}

}  // namespace

void openArrivedDocument(App& app, QWidget& window, const std::string& utf8Path) {
    bringForward(window);
    tabOpenPath(app, nullptr, utf8Path, true);
}

void openFileOpenEventDocument(App& app, QWidget& window, const std::string& utf8Path) {
    LaunchOptions asLaunch;
    asLaunch.inputFile = utf8Path;
    if (startPageActive(app)) {
        // A COLD FINDER LAUNCH LANDS HERE: no file argument, so the start page is up when the event
        // arrives. It gives way as it does for its own recent-file click, which leaves the one
        // document `qinta <path>` would have opened, not a start-page tab beside it. FIRST, and with
        // tabs off too: a window showing the start page holds no document, so taking this one is
        // not a second document in it -- and asking the tabs setting first left a cold launch
        // with an empty start-page window and the document in a second one (measured 2026-10-13).
        bringForward(window);
        tabOpenPathReplacingStartPage(app, nullptr, utf8Path);
    } else if (!shouldJoinExistingWindow(asLaunch, app.openInTabs)) {
        launchSecondInstance(utf8Path);
    } else {
        openArrivedDocument(app, window, utf8Path);
    }
}

}  // namespace tinta
