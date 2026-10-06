#include "instance_channel_qt.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>

// The protocol is one line each way: the client writes the path and a newline, the server
// answers "ok\n" once it has handed the path on. The acknowledgement is what lets the client
// tell "a window took my document" from "something accepted a connection and dropped it", and
// only the first is a reason for the second process to exit without a window.

namespace tinta {

QString instanceChannelName() {
    // Per user: the home directory identifies the account on every platform, and hashing it keeps
    // the name short and free of the separators a pipe or socket name cannot hold.
    const QByteArray home = QDir::homePath().toUtf8();
    const QByteArray digest =
        QCryptographicHash::hash(home, QCryptographicHash::Sha1).toHex().left(16);
    return QStringLiteral("qinta-instance-") + QString::fromLatin1(digest);
}

bool sendToRunningInstance(const QString& name, const std::string& absolutePathUtf8,
                           int timeoutMs) {
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(timeoutMs)) return false;
    QByteArray line(absolutePathUtf8.data(), static_cast<int>(absolutePathUtf8.size()));
    line.append('\n');
    socket.write(line);
    if (!socket.waitForBytesWritten(timeoutMs)) return false;
    QByteArray reply;
    while (!reply.contains('\n')) {
        if (!socket.waitForReadyRead(timeoutMs)) return false;
        reply += socket.readAll();
    }
    return reply.startsWith("ok");
}

InstanceServer::InstanceServer(std::function<void(const std::string&)> onPath, QObject* parent)
    : QObject(parent), onPath_(std::move(onPath)), server_(new QLocalServer(this)) {
    QObject::connect(server_, &QLocalServer::newConnection, this, [this]() { accept(); });
}

InstanceServer::~InstanceServer() = default;

bool InstanceServer::listen(const QString& name) {
    if (server_->listen(name)) return true;
    // A crashed qinta leaves its Unix socket file behind, and listen() then fails with
    // AddressInUseError although nobody is listening. Ask first: if a live server answers, the
    // name is genuinely taken and must not be stolen.
    QLocalSocket probe;
    probe.connectToServer(name);
    if (probe.waitForConnected(200)) return false;
    QLocalServer::removeServer(name);
    return server_->listen(name);
}

void InstanceServer::accept() {
    while (QLocalSocket* socket = server_->nextPendingConnection()) {
        QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QLocalSocket::readyRead, socket, [this, socket]() {
            QByteArray buffered = socket->property("tintaLine").toByteArray() + socket->readAll();
            const int nl = buffered.indexOf('\n');
            if (nl < 0) {
                socket->setProperty("tintaLine", buffered);
                return;
            }
            const std::string path(buffered.constData(), static_cast<size_t>(nl));
            if (onPath_ && !path.empty()) onPath_(path);
            socket->write("ok\n");
            socket->flush();
            socket->disconnectFromServer();
        });
    }
}

}  // namespace tinta
