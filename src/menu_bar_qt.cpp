#include "menu_bar_qt.h"

#include <QAction>
#include <QCoreApplication>
#include <QKeyCombination>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QString>
#include <QWidget>

#include "app.h"
#include "i18n.h"
#include "input.h"  // invokeMenuCommand, toggleZenMode
#include "overlays.h"  // the CTX_* commands
#include "qt_key_map.h"

namespace tinta {

namespace {

QString label(const App& app, const char* key) {
    return QString::fromWCharArray(tr(app, key));
}

// The key as the window would have received it: press and release, through sendEvent, so
// keyPressEvent's own Ctrl+O and the router's modifier scope run exactly as for a keystroke.
void replayKey(QWidget& window, Qt::Key key, Qt::KeyboardModifiers modifiers) {
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(&window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers);
    QCoreApplication::sendEvent(&window, &release);
}

// An item the router owns: shows the key, and a click replays it. Ctrl is Cmd on macOS, which is
// also how QKeySequence prints it there.
void addKeyItem(QMenu& menu, const App& app, QWidget& window, const char* labelKey, Qt::Key key,
                Qt::KeyboardModifiers modifiers,
                QAction::MenuRole role = QAction::NoRole) {
    QAction* action = menu.addAction(label(app, labelKey));
    action->setShortcut(QKeySequence(QKeyCombination(modifiers, key)));
    action->setMenuRole(role);
    QObject::connect(action, &QAction::triggered, &window,
                     [&window, key, modifiers]() { replayKey(window, key, modifiers); });
}

// An item that shows a Mac key but replays the router's own: the menu answers `shown`, and the
// window gets `key` as if it had been typed. For keys macOS takes before the window sees them.
void addMacKeyItem(QMenu& menu, const App& app, QWidget& window, const char* labelKey,
                   QKeyCombination shown, Qt::Key key, Qt::KeyboardModifiers modifiers) {
    QAction* action = menu.addAction(label(app, labelKey));
    action->setShortcut(QKeySequence(shown));
    action->setMenuRole(QAction::NoRole);
    QObject::connect(action, &QAction::triggered, &window,
                     [&window, key, modifiers]() { replayKey(window, key, modifiers); });
}

// An item with no key: the drawn application menu's own command.
void addCommandItem(QMenu& menu, App& app, const char* labelKey, int command) {
    QAction* action = menu.addAction(label(app, labelKey));
    action->setMenuRole(QAction::NoRole);
    QObject::connect(action, &QAction::triggered, action,
                     [&app, command]() { invokeMenuCommand(app, nullptr, command); });
}

}  // namespace

void populateMenuBar(QMenuBar& bar, App& app, QWidget& window) {
    const Qt::KeyboardModifiers ctrl = Qt::ControlModifier;

    QMenu* file = bar.addMenu(label(app, "menu.file"));
    addKeyItem(*file, app, window, "ctx.new", Qt::Key_N, ctrl);
    addKeyItem(*file, app, window, "ctx.open", Qt::Key_O, ctrl);
    file->addSeparator();
    addKeyItem(*file, app, window, "ctx.save", Qt::Key_S, ctrl);
    addKeyItem(*file, app, window, "ctx.save_as", Qt::Key_S, ctrl | Qt::ShiftModifier);
    file->addSeparator();
    addKeyItem(*file, app, window, "ctx.print", Qt::Key_P, ctrl);
    addCommandItem(*file, app, "ctx.export", CTX_EXPORT);
    // PreferencesRole moves this into the application menu on macOS, where Settings belongs.
    addKeyItem(*file, app, window, "ctx.settings", Qt::Key_Comma, ctrl,
               QAction::PreferencesRole);

    QMenu* edit = bar.addMenu(label(app, "menu.edit"));
    addKeyItem(*edit, app, window, "ctx.copy", Qt::Key_C, ctrl);
    addKeyItem(*edit, app, window, "ctx.select_all", Qt::Key_A, ctrl);
    edit->addSeparator();
    addKeyItem(*edit, app, window, "ctx.search", Qt::Key_F, ctrl);
    // The router's find-and-replace is Ctrl+H, which is ⌘H on macOS, and Cocoa's Hide takes ⌘H
    // first. ⌥⌘F is the Mac convention; the item replays Ctrl+H, so it opens the replace row in
    // whichever mode the window is in, as the key does on Windows.
    addMacKeyItem(*edit, app, window, "menu.find_replace", findReplaceMenuKey(), Qt::Key_H, ctrl);

    // ZEN MODE GETS A MAC KEY HERE. Its own key is F11, and on a default Mac F11 is a media
    // key and fn+F11 is Show Desktop, so the window never sees it. ⌃⌘F is the platform's
    // full-screen key. Qt prints Meta as ⌃ on macOS, and Ctrl as ⌘. The item calls the command
    // rather than replaying a key: the router reads ⌃⌘F as ⌘F, which is search.
    QMenu* view = bar.addMenu(label(app, "menu.view"));
    QAction* zen = view->addAction(label(app, "shortcuts.zen"));
    zen->setShortcut(QKeySequence(zenMenuKey()));
    zen->setMenuRole(QAction::NoRole);
    QObject::connect(zen, &QAction::triggered, zen, [&app]() { toggleZenMode(app, nullptr); });

    QMenu* help = bar.addMenu(label(app, "menu.help"));
    addCommandItem(*help, app, "ctx.help", CTX_HELP);
}

QKeyCombination findReplaceMenuKey() {
    return QKeyCombination(Qt::AltModifier | Qt::ControlModifier, Qt::Key_F);
}

QKeyCombination zenMenuKey() {
    return QKeyCombination(Qt::MetaModifier | Qt::ControlModifier, Qt::Key_F);
}

bool keyStaysWithWindow(const QKeyEvent& event) {
    // The same test keyPressEvent makes before it routes anything.
    if (vkFromQtKey(event.key()) == 0 && event.text().isEmpty()) return false;
    // The View menu's zen key goes to the menu. Kept, the router would see ⌘F and open search.
    if (event.keyCombination() == zenMenuKey()) return false;
    if (event.keyCombination() == findReplaceMenuKey()) return false;
    if (event.modifiers() & Qt::ControlModifier) {
        switch (event.key()) {
            case Qt::Key_Q:  // Quit
            case Qt::Key_H:  // Hide
            case Qt::Key_M:  // Minimize
                return false;
            default:
                break;
        }
    }
    return true;
}

}  // namespace tinta
