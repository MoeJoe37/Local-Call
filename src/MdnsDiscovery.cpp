#include "MdnsDiscovery.h"
#include <QNetworkInterface>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>

MdnsDiscovery::MdnsDiscovery(const QString& localId,
                               const QString& localName,
                               quint16        wsPort,
                               QObject*       parent)
    : QObject(parent), m_localId(localId), m_localName(localName), m_wsPort(wsPort)
{
    m_socket = new QUdpSocket(this);
    m_announceTimer = new QTimer(this);
    m_pruneTimer    = new QTimer(this);

    m_announceTimer->setInterval(ANNOUNCE_MS);
    m_pruneTimer->setInterval(PRUNE_MS);

    connect(m_socket,        &QUdpSocket::readyRead, this, &MdnsDiscovery::onReadyRead);
    connect(m_announceTimer, &QTimer::timeout,       this, &MdnsDiscovery::sendAnnouncement);
    connect(m_pruneTimer,    &QTimer::timeout,       this, &MdnsDiscovery::pruneStale);
}

MdnsDiscovery::~MdnsDiscovery()
{
    stop();
}

void MdnsDiscovery::start()
{
    const QHostAddress mdnsAddr(QString::fromLatin1(MDNS_ADDR));

    m_socket->bind(QHostAddress::AnyIPv4,
                   MDNS_PORT,
                   QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp))      continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack)   continue;
        m_socket->joinMulticastGroup(mdnsAddr, iface);
    }

    m_socket->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 0);

    m_announceTimer->start();
    m_pruneTimer->start();

    sendAnnouncement();
    m_socket->writeDatagram(buildQueryPacket(), mdnsAddr, MDNS_PORT);
}

void MdnsDiscovery::stop()
{
    m_announceTimer->stop();
    m_pruneTimer->stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->close();
}

QList<RtcPeerInfo> MdnsDiscovery::activePeers() const
{
    QList<RtcPeerInfo> out;
    for (const auto& kv : m_table)
        out.append(kv.first);
    return out;
}

void MdnsDiscovery::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray  data;
        QHostAddress sender;
        quint16     senderPort;
        data.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);
        parsePacket(data, sender);
    }
}

void MdnsDiscovery::sendAnnouncement()
{
    const QHostAddress mdnsAddr(QString::fromLatin1(MDNS_ADDR));
    m_socket->writeDatagram(buildResponsePacket(), mdnsAddr, MDNS_PORT);
}

void MdnsDiscovery::pruneStale()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<QString> expired;

    for (auto it = m_table.begin(); it != m_table.end(); ++it) {
        if (now - it.value().second > TTL_MS)
            expired.append(it.key());
    }

    for (const QString& id : expired) {
        m_table.remove(id);
        emit peerExpired(id);
    }
}

void MdnsDiscovery::appendU16(QByteArray& buf, quint16 v)
{
    buf.append(static_cast<char>((v >> 8) & 0xFF));
    buf.append(static_cast<char>(v & 0xFF));
}

void MdnsDiscovery::appendU32(QByteArray& buf, quint32 v)
{
    buf.append(static_cast<char>((v >> 24) & 0xFF));
    buf.append(static_cast<char>((v >> 16) & 0xFF));
    buf.append(static_cast<char>((v >>  8) & 0xFF));
    buf.append(static_cast<char>(v & 0xFF));
}

quint16 MdnsDiscovery::readU16(const QByteArray& buf, int off)
{
    if (off + 1 >= buf.size()) return 0;
    return static_cast<quint16>(
        (static_cast<quint8>(buf[off]) << 8) | static_cast<quint8>(buf[off + 1]));
}

QByteArray MdnsDiscovery::encodeDnsName(const QString& name)
{
    QByteArray result;
    const QStringList labels = name.split(QLatin1Char('.'));
    for (const QString& label : labels) {
        if (label.isEmpty()) continue;
        const QByteArray l = label.toUtf8();
        result.append(static_cast<char>(l.size() & 0xFF));
        result.append(l);
    }
    result.append('\0');
    return result;
}

QString MdnsDiscovery::decodeDnsName(const QByteArray& pkt, int& offset)
{
    QString name;
    int safety = 0;
    while (offset < pkt.size() && safety++ < 128) {
        const quint8 len = static_cast<quint8>(pkt[offset]);
        if (len == 0) { ++offset; break; }
        if ((len & 0xC0) == 0xC0) {
            if (offset + 1 >= pkt.size()) break;
            int ptr = ((len & 0x3F) << 8) | static_cast<quint8>(pkt[offset + 1]);
            offset += 2;
            int tmp = ptr;
            name += decodeDnsName(pkt, tmp);
            return name;
        }
        ++offset;
        if (offset + len > pkt.size()) break;
        if (!name.isEmpty()) name += QLatin1Char('.');
        name += QString::fromUtf8(pkt.mid(offset, len));
        offset += len;
    }
    return name;
}

QByteArray MdnsDiscovery::buildResponsePacket() const
{
    const QString svcType     = QString::fromLatin1(SERVICE_TYPE);
    const QString instanceName = m_localName + QLatin1Char('.') + svcType;
    const QString hostName    = m_localName + QLatin1String(".local");

    const QString txt1 = QLatin1String("id=") + m_localId;
    const QString txt2 = QLatin1String("port=") + QString::number(m_wsPort);

    QHostAddress localIp;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp))    continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                localIp = entry.ip();
                break;
            }
        }
        if (!localIp.isNull()) break;
    }

    QByteArray pkt;
    appendU16(pkt, 0x0000);
    appendU16(pkt, 0x8400);
    appendU16(pkt, 0x0000);
    appendU16(pkt, 0x0004);
    appendU16(pkt, 0x0000);
    appendU16(pkt, 0x0000);

    {
        pkt.append(encodeDnsName(svcType));
        appendU16(pkt, 12);
        appendU16(pkt, 0x8001);
        appendU32(pkt, DNS_TTL);
        const QByteArray rdata = encodeDnsName(instanceName);
        appendU16(pkt, static_cast<quint16>(rdata.size()));
        pkt.append(rdata);
    }

    {
        pkt.append(encodeDnsName(instanceName));
        appendU16(pkt, 33);
        appendU16(pkt, 0x8001);
        appendU32(pkt, DNS_TTL);
        const QByteArray target = encodeDnsName(hostName);
        const quint16 rdlen = static_cast<quint16>(6 + target.size());
        appendU16(pkt, rdlen);
        appendU16(pkt, 0);
        appendU16(pkt, 0);
        appendU16(pkt, m_wsPort);
        pkt.append(target);
    }

    {
        pkt.append(encodeDnsName(instanceName));
        appendU16(pkt, 16);
        appendU16(pkt, 0x8001);
        appendU32(pkt, DNS_TTL);

        QByteArray txtrdata;
        const QByteArray s1 = txt1.toUtf8();
        txtrdata.append(static_cast<char>(s1.size()));
        txtrdata.append(s1);
        const QByteArray s2 = txt2.toUtf8();
        txtrdata.append(static_cast<char>(s2.size()));
        txtrdata.append(s2);
        appendU16(pkt, static_cast<quint16>(txtrdata.size()));
        pkt.append(txtrdata);
    }

    if (!localIp.isNull()) {
        pkt.append(encodeDnsName(hostName));
        appendU16(pkt, 1);
        appendU16(pkt, 0x8001);
        appendU32(pkt, DNS_TTL);
        appendU16(pkt, 4);
        const quint32 ipv4 = localIp.toIPv4Address();
        appendU32(pkt, ipv4);
    }

    return pkt;
}

QByteArray MdnsDiscovery::buildQueryPacket() const
{
    const QString svcType = QString::fromLatin1(SERVICE_TYPE);
    QByteArray pkt;
    appendU16(pkt, 0x0000);
    appendU16(pkt, 0x0000);
    appendU16(pkt, 0x0001);
    appendU16(pkt, 0x0000);
    appendU16(pkt, 0x0000);
    appendU16(pkt, 0x0000);
    pkt.append(encodeDnsName(svcType));
    appendU16(pkt, 12);
    appendU16(pkt, 0x0001);
    return pkt;
}

void MdnsDiscovery::parsePacket(const QByteArray& data, const QHostAddress& sender)
{
    if (data.size() < 12) return;

    const quint16 flags   = readU16(data, 2);
    const quint16 ancount = readU16(data, 6);
    const bool    isResp  = (flags & 0x8000) != 0;

    if (!isResp || ancount == 0) return;

    int offset = 12;

    const quint16 qdcount = readU16(data, 4);
    for (int i = 0; i < qdcount && offset < data.size(); ++i) {
        decodeDnsName(data, offset);
        offset += 4;
    }

    QString peerId;
    quint16 wsPort = 0;
    QString peerName;
    QHostAddress peerAddr;

    for (int i = 0; i < ancount && offset < data.size(); ++i) {
        const QString rrName = decodeDnsName(data, offset);
        if (offset + 10 > data.size()) break;

        const quint16 rrType  = readU16(data, offset);     offset += 2;
        offset += 2;
        offset += 4;
        const quint16 rdlen   = readU16(data, offset);     offset += 2;
        const int     rdStart = offset;

        if (rrType == 16) {
            int p = rdStart;
            while (p < rdStart + rdlen && p < data.size()) {
                const quint8 slen = static_cast<quint8>(data[p++]);
                if (p + slen > data.size()) break;
                const QString kv = QString::fromUtf8(data.mid(p, slen));
                p += slen;
                if (kv.startsWith(QLatin1String("id=")))
                    peerId = kv.mid(3);
                else if (kv.startsWith(QLatin1String("port=")))
                    wsPort = static_cast<quint16>(kv.mid(5).toUInt());
            }
        } else if (rrType == 1 && rdlen == 4) {
            const quint32 ipv4 =
                (static_cast<quint32>(static_cast<quint8>(data[rdStart]))     << 24) |
                (static_cast<quint32>(static_cast<quint8>(data[rdStart + 1])) << 16) |
                (static_cast<quint32>(static_cast<quint8>(data[rdStart + 2])) <<  8) |
                 static_cast<quint32>(static_cast<quint8>(data[rdStart + 3]));
            peerAddr = QHostAddress(ipv4);
        } else if (rrType == 33) {
            int p = rdStart + 6;
            peerName = decodeDnsName(data, p);
            if (peerName.endsWith(QLatin1String(".local")))
                peerName.chop(6);
        }

        offset = rdStart + rdlen;
    }

    if (peerId.isEmpty() || peerId == m_localId) return;
    if (wsPort == 0) return;

    if (peerAddr.isNull())
        peerAddr = sender;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool isNew = !m_table.contains(peerId);

    RtcPeerInfo info;
    info.id      = peerId;
    info.name    = peerName.isEmpty() ? peerId : peerName;
    info.address = peerAddr;
    info.wsPort  = wsPort;

    m_table[peerId] = qMakePair(info, now);

    if (isNew)
        emit peerDiscovered(info);
}
