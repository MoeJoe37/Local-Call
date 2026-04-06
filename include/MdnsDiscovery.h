#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QString>
#include <QList>
#include <QMap>

struct RtcPeerInfo {
    QString      id;
    QString      name;
    QHostAddress address;
    quint16      wsPort{0};

    bool operator==(const RtcPeerInfo& o) const noexcept { return id == o.id; }
};

class MdnsDiscovery : public QObject {
    Q_OBJECT
public:
    explicit MdnsDiscovery(const QString& localId,
                            const QString& localName,
                            quint16        wsPort,
                            QObject*       parent = nullptr);
    ~MdnsDiscovery() override;

    void start();
    void stop();

    [[nodiscard]] QList<RtcPeerInfo> activePeers() const;

signals:
    void peerDiscovered(RtcPeerInfo peer);
    void peerExpired(QString peerId);

private slots:
    void onReadyRead();
    void sendAnnouncement();
    void pruneStale();

private:
    static QByteArray encodeDnsName(const QString& name);
    static QString    decodeDnsName(const QByteArray& pkt, int& offset);
    static void       appendU16(QByteArray& buf, quint16 v);
    static void       appendU32(QByteArray& buf, quint32 v);
    static quint16    readU16(const QByteArray& buf, int off);

    QByteArray buildResponsePacket() const;
    QByteArray buildQueryPacket()    const;
    void       parsePacket(const QByteArray& data, const QHostAddress& sender);

    QString      m_localId;
    QString      m_localName;
    quint16      m_wsPort;
    QUdpSocket*  m_socket        {nullptr};
    QTimer*      m_announceTimer {nullptr};
    QTimer*      m_pruneTimer    {nullptr};

    QMap<QString, QPair<RtcPeerInfo, qint64>> m_table;

    static constexpr const char* MDNS_ADDR     = "224.0.0.251";
    static constexpr quint16     MDNS_PORT      = 5353;
    static constexpr int         ANNOUNCE_MS    = 2000;
    static constexpr int         PRUNE_MS       = 1000;
    static constexpr qint64      TTL_MS         = 8000;
    static constexpr quint32     DNS_TTL        = 4500;
    static constexpr const char* SERVICE_TYPE   = "_localcall._tcp.local";
};
