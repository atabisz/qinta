#ifndef TINTA_INSTANCE_CHANNEL_QT_H
#define TINTA_INSTANCE_CHANNEL_QT_H

#include <QObject>
#include <QString>

#include <functional>
#include <string>

class QLocalServer;

// THE SINGLE-INSTANCE HAND-OFF ON QT: qinta's equivalent of WinMain's FindWindowW("Tinta") +
// WM_COPYDATA. A running window owns an InstanceServer; a second launch that
// tinta::shouldJoinExistingWindow says should join sends its document's absolute path through
// sendToRunningInstance and exits, and the running window opens it as a tab. No server reachable
// means no window to join, and the launch opens its own -- WinMain's fallback when FindWindowW
// returns null.
//
// QLocalServer/QLocalSocket: a named pipe on Windows, a Unix domain socket elsewhere. One channel
// per USER, so two accounts on one machine do not hand documents to each other.
namespace tinta {

// The channel name this user's windows listen on. Tests pass their own name so they never talk to
// a running qinta on the same machine.
QString instanceChannelName();

// One document path, UTF-8 and absolute, to whoever is listening on `name`. True only when the
// listener acknowledged it; false when nobody is listening or it did not answer in time.
bool sendToRunningInstance(const QString& name, const std::string& absolutePathUtf8,
                           int timeoutMs = 2000);

// Listens on `name` and hands every received path to `onPath`, on the GUI thread.
class InstanceServer : public QObject {
public:
    explicit InstanceServer(std::function<void(const std::string&)> onPath,
                            QObject* parent = nullptr);
    ~InstanceServer() override;

    // Starts listening. A stale Unix socket left by a crashed qinta is removed and retried once;
    // on Windows a pipe cannot outlive its owner. False if the name is held by a live listener.
    bool listen(const QString& name);

private:
    void accept();
    std::function<void(const std::string&)> onPath_;
    QLocalServer* server_ = nullptr;
};

// THE OTHER WAY A DOCUMENT REACHES A RUNNING qinta, and the only one macOS Finder uses: a
// QFileOpenEvent sent to the QApplication (double-click, Open With, a drop on the Dock icon).
// Installed as an event filter on qApp; every local path the event carries goes to `onPath`, on
// the GUI thread. An event naming no local file -- a URL scheme -- is left to Qt. Harmless off
// macOS, where Qt sends no such event, which is also why the test runs it on every platform.
class FileOpenRouter : public QObject {
public:
    explicit FileOpenRouter(std::function<void(const std::string&)> onPath,
                            QObject* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::function<void(const std::string&)> onPath_;
};

}  // namespace tinta

#endif  // TINTA_INSTANCE_CHANNEL_QT_H
