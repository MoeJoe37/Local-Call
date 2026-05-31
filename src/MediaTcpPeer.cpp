#include "MediaTcpPeer.h"
#include "MediaSettings.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QAbstractSocket>
#include <QtEndian>
#include <QTimer>
#include <algorithm>

namespace {
constexpr char MAGIC[] = {'L','C','M','2'};
constexpr int HEADER_SIZE = 9;              // magic(4) + tag(1) + length(4)
constexpr int MAX_MEDIA_PACKET = 4 * 1024 * 1024;

static quint32 ipv4(const QHostAddress& a)
{
    return a.toIPv4Address();
}
}

MediaTcpPeer::MediaTcpPeer(const QString& peerIp, QObject* parent)
    : QObject(parent), m_peerIpText(peerIp), m_peerAddress(peerIp)
{
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(700);
    m_reconnectTimer->setSingleShot(false);
    connect(m_reconnectTimer, &QTimer::timeout, this, &MediaTcpPeer::connectToPeer);
}

MediaTcpPeer::~MediaTcpPeer()
{
    stop();
}

bool MediaTcpPeer::start()
{
    if (m_running) return true;
    if (m_peerAddress.isNull()) {
        emit failed(QStringLiteral("Invalid peer IP address"));
        return false;
    }

    m_server = new QTcpServer(this);
    m_server->setMaxPendingConnections(8);
    connect(m_server, &QTcpServer::newConnection, this, &MediaTcpPeer::onNewConnection);
    if (!m_server->listen(QHostAddress::AnyIPv4, MediaSettings::MediaTcpPort)) {
        const QString reason = QStringLiteral("Could not listen on LAN media TCP port %1: %2")
            .arg(MediaSettings::MediaTcpPort)
            .arg(m_server->errorString());
        stop();
        emit failed(reason);
        return false;
    }

    m_running = true;
    connectToPeer();
    m_reconnectTimer->start();
    return true;
}

void MediaTcpPeer::stop()
{
    m_running = false;
    if (m_reconnectTimer) m_reconnectTimer->stop();

    if (m_outgoing) {
        m_outgoing->disconnect(this);
        m_outgoing->abort();
        m_outgoing->deleteLater();
        m_outgoing = nullptr;
    }

    const auto sockets = m_buffers.keys();
    for (QTcpSocket* s : sockets) closeSocket(s);
    m_buffers.clear();

    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_connectedEmitted = false;
}

void MediaTcpPeer::connectToPeer()
{
    if (!m_running) return;
    if (m_outgoing && (m_outgoing->state() == QAbstractSocket::ConnectedState ||
                       m_outgoing->state() == QAbstractSocket::ConnectingState))
        return;

    if (m_outgoing) {
        m_buffers.remove(m_outgoing);
        m_outgoing->disconnect(this);
        m_outgoing->abort();
        m_outgoing->deleteLater();
        m_outgoing = nullptr;
    }

    m_outgoing = new QTcpSocket(this);
    m_outgoing->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_outgoing, &QTcpSocket::connected, this, &MediaTcpPeer::onOutgoingConnected);
    connect(m_outgoing, &QTcpSocket::disconnected, this, &MediaTcpPeer::onOutgoingDisconnected);
    connect(m_outgoing, &QTcpSocket::readyRead, this, &MediaTcpPeer::onSocketReadyRead);
    connect(m_outgoing, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_running && m_reconnectTimer && !m_reconnectTimer->isActive()) m_reconnectTimer->start();
    });
    m_buffers[m_outgoing] = {};
    m_outgoing->connectToHost(m_peerAddress, MediaSettings::MediaTcpPort);
}

void MediaTcpPeer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        if (!socket) continue;

        const QHostAddress sender = socket->peerAddress();
        if (!sender.isNull() && ipv4(sender) != 0 && ipv4(m_peerAddress) != 0 &&
            ipv4(sender) != ipv4(m_peerAddress)) {
            socket->deleteLater();
            continue;
        }

        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_buffers[socket] = {};
        connect(socket, &QTcpSocket::readyRead, this, &MediaTcpPeer::onSocketReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() { closeSocket(socket); });
        if (!m_connectedEmitted) {
            m_connectedEmitted = true;
            emit connected();
        }
    }
}

void MediaTcpPeer::onOutgoingConnected()
{
    if (!m_connectedEmitted) {
        m_connectedEmitted = true;
        emit connected();
    }
}

void MediaTcpPeer::onOutgoingDisconnected()
{
    if (m_running && m_reconnectTimer && !m_reconnectTimer->isActive()) m_reconnectTimer->start();
}

void MediaTcpPeer::onSocketReadyRead()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    parseSocket(socket);
}

QByteArray MediaTcpPeer::makeFrame(char tag, const QByteArray& payload)
{
    QByteArray frame;
    frame.reserve(HEADER_SIZE + payload.size());
    frame.append(MAGIC, 4);
    frame.append(tag);
    char lenBytes[4];
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), lenBytes);
    frame.append(lenBytes, 4);
    frame.append(payload);
    return frame;
}

void MediaTcpPeer::sendAudioFrame(const QByteArray& audioPacket)
{
    sendFrame('A', audioPacket);
}

void MediaTcpPeer::sendVideoFrame(const QByteArray& videoPacket)
{
    sendFrame('V', videoPacket);
}

void MediaTcpPeer::sendFrame(char tag, const QByteArray& payload)
{
    if (!m_running || payload.isEmpty() || payload.size() > MAX_MEDIA_PACKET) return;
    if (!m_outgoing || m_outgoing->state() != QAbstractSocket::ConnectedState) {
        connectToPeer();
        return;
    }
    const QByteArray frame = makeFrame(tag, payload);
    const qint64 written = m_outgoing->write(frame.constData(), frame.size());
    if (written < 0) {
        m_outgoing->abort();
        connectToPeer();
    } else {
        m_outgoing->flush();
    }
}

void MediaTcpPeer::parseSocket(QTcpSocket* socket)
{
    if (!socket) return;
    QByteArray& buf = m_buffers[socket];
    buf.append(socket->readAll());

    while (buf.size() >= HEADER_SIZE) {
        if (buf[0] != 'L' || buf[1] != 'C' || buf[2] != 'M' || buf[3] != '2') {
            // Resynchronise rather than killing the call because of one corrupt byte.
            const int next = buf.indexOf(QByteArray(MAGIC, 4), 1);
            if (next < 0) { buf.clear(); return; }
            buf.remove(0, next);
            if (buf.size() < HEADER_SIZE) return;
        }

        const char tag = buf[4];
        const quint32 len = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(buf.constData() + 5));
        if (len == 0 || len > MAX_MEDIA_PACKET) {
            closeSocket(socket);
            return;
        }
        if (buf.size() < HEADER_SIZE + static_cast<int>(len)) return;

        QByteArray payload = buf.mid(HEADER_SIZE, static_cast<int>(len));
        buf.remove(0, HEADER_SIZE + static_cast<int>(len));

        if (!m_connectedEmitted) {
            m_connectedEmitted = true;
            emit connected();
        }
        if (tag == 'A') emit remoteAudioFrame(payload);
        else if (tag == 'V') emit remoteVideoFrame(payload);
    }
}

void MediaTcpPeer::closeSocket(QTcpSocket* socket)
{
    if (!socket) return;
    m_buffers.remove(socket);
    if (socket == m_outgoing) m_outgoing = nullptr;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}
