#ifndef TINTA_QT_TEST_APP_H
#define TINTA_QT_TEST_APP_H

// The Qt backends for a test that needs a laid-out App, and a configuration home that is NOT the
// user's. Written 2026-10-06 when the Direct2D stack was retired: six tests used to get their
// App from initD2D (src/d2d_init.cpp), which is gone, and they now get it from the same
// installQtBackends qinta itself calls.
//
// THE CONFIGURATION HOME IS PINNED BEFORE ANYTHING CAN RESOLVE IT, as tests/qt_entry_tests.cpp
// does it: settings, drafts and the recent list would otherwise land in the real per-user
// directory, which on Windows is the settings.ini the user's qinta reads. Not an APPDATA
// redirect -- the Win32 path backend asks SHGetFolderPathW, which ignores the variable. A pin
// that did not take makes ok() false, and the test refuses to run rather than write the file.

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>

#include <string>

#include "app.h"
#include "config_dir.h"
#include "platform_paths.h"
#include "qt_init.h"
#include "settings.h"

class QtTestApp {
public:
    QtTestApp(int& argc, char** argv) : qapp_(argc, argv) {
        const std::wstring realConfigDir = platform::userConfigDirectory(L"Tinta");
        if (!home_.isValid()) return;
        const std::wstring pinned = QDir::toNativeSeparators(home_.path()).toStdWString();
        pinTintaConfigDir(pinned);
        const std::wstring settingsPath = getSettingsPath();
        pinned_ = settingsPath.rfind(pinned, 0) == 0 &&
                  (realConfigDir.empty() || settingsPath.rfind(realConfigDir, 0) != 0);
    }

    // True when the configuration home is a temporary directory. A test checks this first and
    // exits 2 when it is false, which ctest reports as a failure rather than a pass.
    bool ok() const { return pinned_; }

    // installQtBackends with the guard the Direct2D initD2D used to give: false if no text
    // factory came up, so a test does not measure layouts against a null factory.
    static bool install(App& app) {
        installQtBackends(app);
        return app.textFactory != nullptr;
    }

private:
    QApplication qapp_;
    QTemporaryDir home_;
    bool pinned_ = false;
};

#endif  // TINTA_QT_TEST_APP_H
