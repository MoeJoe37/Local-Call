#include "TcpMediaTransport.h"
#include "MediaSettings.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QAbstractSocket>
#include <QTimer>

TcpMediaTransport::TcpMediaTransport(const QString& peerIp, const QString& localId, QObject* parent)
    : MediaTransport(peerIp, localId, parent), m_peerAddress(peerIp)
{
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(700);
    connect(m_reconnectTimer, &QTimer::timeout, this, &TcpMediaTransport::connectToPeer);
}

TcpMediaTransport::~TcpMediaTransport() { stop(); }

bool TcpMediaTransport::start()
{
    if (m_running) return true;
    if (m_peerAddress.isNull()) {
        emit failed(QStringLiteral("Invalid peer IP address"));
        return false;
    }

    m_server = new QTcpServer(this);
    m_server->setMaxPendingConnections(8);
    connect(m_server, &QTcpServer::newConnection, this, &TcpMediaTransport::onNewConnection);
    if (!m_server->listen(QHostAddress::AnyIPv4, MediaSettings::MediaTcpPort)) {
        const QString reason = QStringLiteral("Could not listen on LAN media TCP port %1: %2")
            .arg(MediaSettings::MediaTcpPort).arg(m_server->errorString());
        stop();
        emit failed(reason);
        return false;
    }

    m_running = true;
    connectToPeer();
    m_reconnectTimer->start();
    return true;
}

void TcpMediaTransport::stop()
{
    m_running = false;
    if (m_reconnectTimer) m_reconnectTimer->stop();

    const auto sockets = m_buffers.keys();
    for (QTcpSocket* s : sockets) closeSocket(s);
    m_buffers.clear();
    m_outgoing = nullptr;

    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
}

void TcpMediaTransport::connectToPeer()
{
    if (!m_running) return;
    if (m_outgoing && (m_outgoing->state() == QAbstractSocket::ConnectedState ||
                       m_outgoing->state() == QAbstractSocket::ConnectingState))
        return;

    if (m_outgoing) {
        closeSocket(m_outgoing);
        m_outgoing = nullptr;
    }

    m_outgoing = new QTcpSocket(this);
    m_outgoing->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_outgoing, &QTcpSocket::readyRead, this, &TcpMediaTransport::onSocketReadyRead);
    connect(m_outgoing, &QTcpSocket::connected, this, [this]() { markConnected(); });
    connect(m_outgoing, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_running && m_reconnectTimer && !m_reconnectTimer->isActive()) m_reconnectTimer->start();
    });
    m_buffers[m_outgoing] = {};
    m_outgoing->connectToHost(m_peerAddress, MediaSettings::MediaTcpPort);
}

void TcpMediaTransport::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        if (!socket) continue;

        const QHostAddress sender = socket->peerAddress();
        if (!sender.isNull() && !m_peerAddress.isNull() &&
            sender.toIPv4Address() != m_peerAddress.toIPv4Address()) {
            socket->deleteLater();
            continue;
        }

        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_buffers[socket] = {};
        connect(socket, &QTcpSocket::readyRead, this, &TcpMediaTransport::onSocketReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() { closeSocket(socket); });
        markConnected();
    }
}

QTcpSocket* TcpMediaTransport::pickSendSocket() const
{
    if (m_outgoing && m_outgoing->state() == QAbstractSocket::ConnectedState)
        return m_outgoing;
    // Fall back to an inbound socket. This is the path that keeps media
    // flowing when only one direction of TCP connect is permitted.
    for (auto it = m_buffers.constBegin(); it != m_buffers.constEnd(); ++it) {
        QTcpSocket* s = it.key();
        if (s && s->state() == QAbstractSocket::ConnectedState) return s;
    }
    return nullptr;
}

bool TcpMediaTransport::writeChunk(const QByteArray& chunk, bool dropIfCongested)
{
    QTcpSocket* socket = pickSendSocket();
    if (!socket) {
        connectToPeer();
        return false;
    }

    // Without this check TCP buffers realtime media indefinitely: the socket
    // accepts every write, the kernel queue grows, and end-to-end latency rises
    // until the call is unusable. Shedding video is far better than delaying it.
    if (dropIfCongested && socket->bytesToWrite() > MediaSettings::MaxTcpQueueBytes)
        return false;

    if (socket->write(chunk) < 0) {
        closeSocket(socket);
        connectToPeer();
        return false;
    }
    return true;
}

void TcpMediaTransport::onSocketReadyRead()
{
    if (auto* socket = qobject_cast<QTcpSocket*>(sender())) parseSocket(socket);
}

void TcpMediaTransport::parseSocket(QTcpSocket* socket)
{
    if (!socket || !m_buffers.contains(socket)) return;
    QByteArray& buf = m_buffers[socket];
    buf.append(socket->readAll());

    while (buf.size() >= MediaPacket::HeaderSize) {
        MediaPacket::Header h;
        if (!MediaPacket::read(buf.constData(), buf.size(), h)) {
            // Resynchronise on the next magic rather than dropping the call
            // because of one corrupt byte.
            const int next = buf.indexOf(QByteArray(MediaPacket::Magic, 4), 1);
            if (next < 0) { buf.clear(); return; }
            buf.remove(0, next);
            continue;
        }
        if (h.payloadLen > MediaPacket::MaxPayload) { closeSocket(socket); return; }

        const int total = MediaPacket::HeaderSize + h.payloadLen;
        if (buf.size() < total) return;

        ingest(buf.left(total));
        buf.remove(0, total);
    }
}

void TcpMediaTransport::closeSocket(QTcpSocket* socket)
{
    if (!socket) return;
    m_buffers.remove(socket);
    if (socket == m_outgoing) m_outgoing = nullptr;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}
