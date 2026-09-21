#include "zen_mode_qt.h"

#include <QRect>

#include "qt_launch.h"

namespace tinta {
namespace {

WindowRect rectFrom(const QRect& r) {
    WindowRect out;
    out.x = r.x();
    out.y = r.y();
    out.width = r.width();
    out.height = r.height();
    return out;
}

}  // namespace

ZenRestore zenModeEnter(QWidget& window) {
    ZenRestore restore;

    // normalGeometry() is Qt's own name for the Win32 arm's `rcNormalPosition`: where the window
    // goes when it is neither maximized nor fullscreen. Using geometry() instead would capture the
    // MAXIMIZED rectangle for a maximized window, and then leaving zen mode would restore a
    // screen-sized non-maximized window -- which looks almost right, which is the problem.
    restore.normal = rectFrom(window.normalGeometry());
    restore.maximized = window.isMaximized();
    restore.valid = true;

    // Move onto the monitor zen mode is going to cover BEFORE going fullscreen, because
    // showFullScreen() fills the screen the window is already on. Without this the window would go
    // fullscreen on whichever screen Qt currently associates it with -- see the header on the one
    // input where that differs from the Win32 arm.
    const WindowRect cover = zenFullscreenRect(rectFrom(window.frameGeometry()), monitorsFromQt());
    window.setGeometry(cover.x, cover.y, cover.width, cover.height);
    window.showFullScreen();

    return restore;
}

bool zenModeLeave(QWidget& window, const ZenRestore& restore) {
    if (!restore.valid) return false;

    if (restore.maximized) {
        // showMaximized() computes its own rectangle from the screen, so the remembered one is
        // deliberately not applied -- applying both would fight, and the maximized geometry of a
        // monitor is the window manager's answer rather than ours.
        window.showMaximized();
        return true;
    }

    // Normal state first, then the rectangle: setGeometry on a window still marked fullscreen is
    // overridden when Qt clears the flag. Same ordering constraint the Win32 arm has between
    // SetWindowLongW and SetWindowPlacement, arrived at from the other direction.
    window.showNormal();
    window.setGeometry(restore.normal.x, restore.normal.y, restore.normal.width,
                       restore.normal.height);
    return true;
}

}  // namespace tinta
