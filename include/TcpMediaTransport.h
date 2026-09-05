#pragma once

#include "MediaTransport.h"
#include <QHash>
#include <QHostAddress>

class QTcpServer;
class QTcpSocket;
class QTimer;

/// Fallback media transport for networks that block UDP.
///
/// Both sides listen on one fixed port and both sides also dial the peer; the
/// first socket to come up in either direction carries media.  Sending picks
/// any connected socket rather than only the locally-initiated one — when one
/// side's outbound connect was blocked, the old code reported "connected" and
/// then never sent a single byte in that direction.
class TcpMediaTransport : public MediaTransport {
    Q_OBJECT
public:
    TcpMediaTransport(const QString& peerIp, const QString& localId, QObject* parent = nullptr);
    ~TcpMediaTransport() override;

    bool start() override;
    void stop() override;
    QString name() const override { return QStringLiteral("TCP"); }

protected:
    bool writeChunk(const QByteArray& chunk, bool dropIfCongested) override;

private slots:
    void onNewConnection();
    void onSocketReadyRead();
    void connectToPeer();

private:
    QTcpSocket* pickSendSocket() const;
    void parseSocket(QTcpSocket* socket);
    void closeSocket(QTcpSocket* socket);

    QTcpServer*  m_server{nullptr};
    QTcpSocket*  m_outgoing{nullptr};
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QTimer*      m_reconnectTimer{nullptr};
    QHostAddress m_peerAddress;
};
