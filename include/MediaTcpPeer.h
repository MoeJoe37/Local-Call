#pragma once

#include <QObject>
#include <QByteArray>
#include <QHostAddress>
#include <QString>
#include <QHash>
#include <QTimer>
#include <cstdint>

class QTcpServer;
class QTcpSocket;

// Persistent LAN media transport.
//
// This is intentionally simpler than WebRTC/ICE and more reliable than UDP on
// locked-down Windows networks: both sides listen on one fixed TCP port and
// both sides also try to connect to the peer.  The first connected socket is
// used for outgoing media; all incoming sockets are accepted for receiving.
class MediaTcpPeer : public QObject {
    Q_OBJECT
public:
    explicit MediaTcpPeer(const QString& peerIp, QObject* parent = nullptr);
    ~MediaTcpPeer() override;

    bool start();
    void stop();
    bool isRunning() const noexcept { return m_running; }

public slots:
    void sendVideoFrame(const QByteArray& videoPacket);
    void sendAudioFrame(const QByteArray& audioPacket);

signals:
    void remoteVideoFrame(QByteArray videoPacket);
    void remoteAudioFrame(QByteArray audioPacket);
    void connected();
    void failed(QString reason);

private slots:
    void onNewConnection();
    void onOutgoingConnected();
    void onOutgoingDisconnected();
    void onSocketReadyRead();
    void connectToPeer();

private:
    void sendFrame(char tag, const QByteArray& payload);
    void parseSocket(QTcpSocket* socket);
    void closeSocket(QTcpSocket* socket);
    static QByteArray makeFrame(char tag, const QByteArray& payload);

    QString     m_peerIpText;
    QHostAddress m_peerAddress;
    QTcpServer* m_server{nullptr};
    QTcpSocket* m_outgoing{nullptr};
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QTimer*     m_reconnectTimer{nullptr};
    bool        m_running{false};
    bool        m_connectedEmitted{false};
};
