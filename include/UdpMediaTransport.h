#pragma once

#include "MediaTransport.h"
#include <QHostAddress>

class QUdpSocket;

/// Default LAN media transport.
///
/// A single UDP socket carries audio, video and control, multiplexed by the
/// LCM3 tag.  Earlier builds bound one socket per media kind, which needed two
/// firewall holes and let a call come up with audio working and video silently
/// blocked.
class UdpMediaTransport : public MediaTransport {
    Q_OBJECT
public:
    UdpMediaTransport(const QString& peerIp, const QString& localId, QObject* parent = nullptr);
    ~UdpMediaTransport() override;

    bool start() override;
    void stop() override;
    QString name() const override { return QStringLiteral("UDP"); }

protected:
    bool writeChunk(const QByteArray& chunk, bool dropIfCongested) override;

private slots:
    void onReadyRead();

private:
    QUdpSocket*  m_socket{nullptr};
    QHostAddress m_peerAddress;
};
