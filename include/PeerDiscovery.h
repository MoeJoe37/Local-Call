#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QThread>
#include <QMutex>
#include <QMap>
#include <QString>
#include <QHostAddress>
#include <map>
#include <string>
#include <atomic>
#include "PeerInfo.h"
#include "MediaSettings.h"

class PeerDiscovery : public QObject {
    Q_OBJECT
public:
    explicit PeerDiscovery(const QString& myId, const QString& myName, QObject* parent = nullptr);
    ~PeerDiscovery();

    void start();
    void stop();
    void forceRescan();
    void updateName(const QString& name);

signals:
    void peersUpdated(QMap<QString, PeerInfo> peers);
    void diagLog(const QString& msg);

private slots:
    void onSendTimer();
    void onPruneTimer();
    void onReadyRead();

private:
    void sendBeacon();
    void parsePacket(const QByteArray& data, const QString& senderIp);
    void addPeer(const QString& id, const QString& name, const QString& ip, const QString& via);
    void runTcpScan();
    void publishPeers();
    void log(const QString& msg);

    QString m_myId;
    QString m_myName;

    QUdpSocket*  m_socket   = nullptr;
    QTimer*      m_sendTimer = nullptr;
    QTimer*      m_pruneTimer = nullptr;

    mutable QMutex          m_peersMutex;
    QMap<QString, PeerInfo> m_peers;
    std::atomic<bool>       m_running{false};

    static constexpr const char* MulticastGroup = "239.255.42.99";
};
