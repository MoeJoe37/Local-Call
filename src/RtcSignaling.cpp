#include "RtcSignaling.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QUrl>

RtcSignalingServer::RtcSignalingServer(quint16 port, QObject* parent)
    : QObject(parent), m_port(port)
{
    m_server = new QWebSocketServer(
        QStringLiteral("LocalCallSignaling"),
        QWebSocketServer::NonSecureMode,
        this);

    connect(m_server, &QWebSocketServer::newConnection,
            this,     &RtcSignalingServer::onNewConnection);
}

RtcSignalingServer::~RtcSignalingServer()
{
    stop();
}

bool RtcSignalingServer::start()
{
    return m_server->listen(QHostAddress::Any, m_port);
}

void RtcSignalingServer::stop()
{
    for (auto* ws : m_sockets.values()) {
        ws->close();
        ws->deleteLater();
    }
    m_sockets.clear();
    m_ids.clear();
    m_server->close();
}

quint16 RtcSignalingServer::port() const noexcept
{
    return m_server->serverPort();
}

bool RtcSignalingServer::isListening() const noexcept
{
    return m_server->isListening();
}

void RtcSignalingServer::sendTo(const QString& peerId, const SignalingMessage& msg)
{
    auto it = m_sockets.find(peerId);
    if (it != m_sockets.end())
        it.value()->sendTextMessage(QString::fromUtf8(serialise(msg)));
}

void RtcSignalingServer::broadcast(const SignalingMessage& msg, const QString& excludeId)
{
    const QByteArray data = serialise(msg);
    for (auto it = m_sockets.begin(); it != m_sockets.end(); ++it) {
        if (it.key() != excludeId)
            it.value()->sendTextMessage(QString::fromUtf8(data));
    }
}

QList<QString> RtcSignalingServer::connectedPeerIds() const
{
    return m_sockets.keys();
}

void RtcSignalingServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QWebSocket* ws = m_server->nextPendingConnection();

        connect(ws, &QWebSocket::textMessageReceived,
                this, &RtcSignalingServer::onTextMessage);
        connect(ws, &QWebSocket::disconnected,
                this, &RtcSignalingServer::onSocketDisconnected);
    }
}

void RtcSignalingServer::onTextMessage(const QString& text)
{
    auto* ws  = qobject_cast<QWebSocket*>(sender());
    if (!ws) return;

    const SignalingMessage msg = deserialise(text.toUtf8());

    if (msg.type == QLatin1String("hello") && !msg.fromId.isEmpty()) {
        m_sockets[msg.fromId] = ws;
        m_ids[ws]             = msg.fromId;
        emit peerConnected(msg.fromId);
        return;
    }

    if (!m_ids.contains(ws)) return;

    emit messageReceived(msg);

    if (!msg.toId.isEmpty())
        sendTo(msg.toId, msg);
}

void RtcSignalingServer::onSocketDisconnected()
{
    auto* ws = qobject_cast<QWebSocket*>(sender());
    if (!ws) return;

    const QString id = m_ids.value(ws);
    m_ids.remove(ws);
    m_sockets.remove(id);
    ws->deleteLater();

    if (!id.isEmpty())
        emit peerDisconnected(id);
}

QString RtcSignalingServer::socketPeerId(QWebSocket* socket) const
{
    return m_ids.value(socket);
}

QByteArray RtcSignalingServer::serialise(const SignalingMessage& msg)
{
    QJsonObject obj;
    obj[QLatin1String("type")]       = msg.type;
    obj[QLatin1String("fromId")]     = msg.fromId;
    obj[QLatin1String("toId")]       = msg.toId;
    if (!msg.sdp.isEmpty())
        obj[QLatin1String("sdp")]    = msg.sdp;
    if (!msg.candidate.isEmpty()) {
        obj[QLatin1String("candidate")]   = msg.candidate;
        obj[QLatin1String("mid")]         = msg.mid;
        obj[QLatin1String("mlineIndex")]  = msg.mlineIndex;
    }
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

SignalingMessage RtcSignalingServer::deserialise(const QByteArray& data)
{
    SignalingMessage msg;
    const QJsonObject obj = QJsonDocument::fromJson(data).object();
    msg.type        = obj[QLatin1String("type")].toString();
    msg.fromId      = obj[QLatin1String("fromId")].toString();
    msg.toId        = obj[QLatin1String("toId")].toString();
    msg.sdp         = obj[QLatin1String("sdp")].toString();
    msg.candidate   = obj[QLatin1String("candidate")].toString();
    msg.mid         = obj[QLatin1String("mid")].toString();
    msg.mlineIndex  = obj[QLatin1String("mlineIndex")].toInt();
    return msg;
}

RtcSignalingClient::RtcSignalingClient(const QString& localId,
                                         const QString& localName,
                                         QObject*       parent)
    : QObject(parent), m_localId(localId), m_localName(localName)
{
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected,            this, &RtcSignalingClient::onConnected);
    connect(m_socket, &QWebSocket::disconnected,         this, &RtcSignalingClient::onDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived,  this, &RtcSignalingClient::onTextMessage);
}

RtcSignalingClient::~RtcSignalingClient()
{
    disconnect();
}

void RtcSignalingClient::connectToServer(const QHostAddress& host, quint16 port)
{
    const QUrl url(QString(QLatin1String("ws://%1:%2")).arg(host.toString()).arg(port));
    m_socket->open(url);
}

void RtcSignalingClient::disconnect()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->close();
}

void RtcSignalingClient::send(const SignalingMessage& msg)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) return;
    m_socket->sendTextMessage(QString::fromUtf8(RtcSignalingServer::serialise(msg)));
}

bool RtcSignalingClient::isConnected() const noexcept
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void RtcSignalingClient::onConnected()
{
    SignalingMessage hello;
    hello.type   = QLatin1String("hello");
    hello.fromId = m_localId;
    send(hello);
    emit connected();
}

void RtcSignalingClient::onDisconnected()
{
    emit disconnected();
}

void RtcSignalingClient::onTextMessage(const QString& text)
{
    emit messageReceived(RtcSignalingServer::deserialise(text.toUtf8()));
}
