#pragma once
#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QString>
#include <QMap>
#include <QList>

struct SignalingMessage {
    QString type;
    QString fromId;
    QString toId;
    QString sdp;
    QString candidate;
    QString mid;
    int     mlineIndex{0};
};

class RtcSignalingServer : public QObject {
    Q_OBJECT
public:
    explicit RtcSignalingServer(quint16 port, QObject* parent = nullptr);
    ~RtcSignalingServer() override;

    bool    start();
    void    stop();
    quint16 port() const noexcept;
    bool    isListening() const noexcept;

    void sendTo(const QString& peerId, const SignalingMessage& msg);
    void broadcast(const SignalingMessage& msg, const QString& excludeId = {});

    [[nodiscard]] QList<QString> connectedPeerIds() const;

signals:
    void peerConnected(QString peerId);
    void peerDisconnected(QString peerId);
    void messageReceived(SignalingMessage msg);

private slots:
    void onNewConnection();
    void onTextMessage(const QString& text);
    void onSocketDisconnected();

private:
    static QByteArray  serialise(const SignalingMessage& msg);
    static SignalingMessage deserialise(const QByteArray& data);

    QString socketPeerId(QWebSocket* socket) const;

    quint16                  m_port;
    QWebSocketServer*        m_server{nullptr};
    QMap<QString, QWebSocket*> m_sockets;
    QMap<QWebSocket*, QString> m_ids;
};

class RtcSignalingClient : public QObject {
    Q_OBJECT
public:
    explicit RtcSignalingClient(const QString& localId,
                                 const QString& localName,
                                 QObject*       parent = nullptr);
    ~RtcSignalingClient() override;

    void connectToServer(const QHostAddress& host, quint16 port);
    void disconnect();
    void send(const SignalingMessage& msg);

    bool isConnected() const noexcept;

signals:
    void connected();
    void disconnected();
    void messageReceived(SignalingMessage msg);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString& text);

private:
    QString     m_localId;
    QString     m_localName;
    QWebSocket* m_socket{nullptr};
};
