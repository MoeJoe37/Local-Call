#include "nlohmann/json.hpp"
using json = nlohmann::json;
#include "PeerDiscovery.h"
#include "SigMsg.h"
#include "Helpers.h"
#include <QNetworkInterface>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QThread>
#include <QTcpSocket>
#include <QtConcurrent>
#include <QFuture>
#include <QFutureSynchronizer>
#include <QAtomicInt>
#include <algorithm>


PeerDiscovery::PeerDiscovery(const QString& myId, const QString& myName, QObject* parent)
    : QObject(parent), m_myId(myId), m_myName(myName)
{}

PeerDiscovery::~PeerDiscovery() { stop(); }

void PeerDiscovery::updateName(const QString& name) { m_myName = name; }

void PeerDiscovery::start()
{
    m_running = true;
    // UDP socket for broadcast + multicast
    m_socket = new QUdpSocket(this);
    m_socket->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 0);

    // Bind to discovery port
    bool bound = m_socket->bind(QHostAddress::AnyIPv4,
                                MediaSettings::BroadcastPort,
                                QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (!bound) {
        log(QString("Cannot bind UDP :%1 — check firewall").arg(MediaSettings::BroadcastPort));
    }

    // Join multicast on all interfaces
    int joined = 0;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (m_socket->joinMulticastGroup(QHostAddress(MulticastGroup), iface)) ++joined;
    }
    m_socket->joinMulticastGroup(QHostAddress(MulticastGroup));
    log(QString("Listener bound — multicast joined on %1 interface(s)").arg(joined));

    connect(m_socket, &QUdpSocket::readyRead, this, &PeerDiscovery::onReadyRead);

    // Send timer: beacon every 1s
    m_sendTimer = new QTimer(this);
    connect(m_sendTimer, &QTimer::timeout, this, &PeerDiscovery::onSendTimer);
    m_sendTimer->start(MediaSettings::BroadcastIntervalMs);

    // Prune timer: remove stale peers every 1s
    m_pruneTimer = new QTimer(this);
    connect(m_pruneTimer, &QTimer::timeout, this, &PeerDiscovery::onPruneTimer);
    m_pruneTimer->start(1000);

    // Deferred TCP scan — run entirely off the UI thread via QtConcurrent
    QTimer::singleShot(2000, this, [this]{
        (void)QtConcurrent::run([this]{ runTcpScan(); });
    });
}

void PeerDiscovery::stop()
{
    m_running = false;
    if (m_sendTimer)  { m_sendTimer->stop(); }
    if (m_pruneTimer) { m_pruneTimer->stop(); }
    if (m_socket)     { m_socket->close(); }
}

void PeerDiscovery::forceRescan()
{
    (void)QtConcurrent::run([this]{ runTcpScan(); });
}

// ── Beacon ────────────────────────────────────────────────────────────────────

void PeerDiscovery::onSendTimer() { sendBeacon(); }

void PeerDiscovery::sendBeacon()
{
    QJsonObject obj;
    obj["protocol"] = QString::fromStdString(LocalCallProtocol::Name);
    obj["schema"]   = LocalCallProtocol::Schema;
#ifdef LOCALCALL_VERSION
    obj["version"]  = QString(LOCALCALL_VERSION);
#endif
#if defined(Q_OS_WIN)
    obj["platform"] = "windows";
#elif defined(Q_OS_MACOS)
    obj["platform"] = "macos";
#elif defined(Q_OS_LINUX)
    obj["platform"] = "linux";
#else
    obj["platform"] = "unknown";
#endif
    obj["id"]       = m_myId;
    obj["name"]     = m_myName;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    auto sendTo = [&](const QHostAddress& addr) {
        m_socket->writeDatagram(data, addr, MediaSettings::BroadcastPort);
    };

    sendTo(QHostAddress::Broadcast);
    sendTo(QHostAddress(MulticastGroup));

    // Subnet broadcasts
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (!entry.broadcast().isNull())
                sendTo(entry.broadcast());
        }
    }
}

// ── Receive ───────────────────────────────────────────────────────────────────

void PeerDiscovery::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(m_socket->pendingDatagramSize());
        QHostAddress sender;
        m_socket->readDatagram(data.data(), data.size(), &sender);
        parsePacket(data, sender.toString().replace("::ffff:", ""));
    }
}

void PeerDiscovery::parsePacket(const QByteArray& data, const QString& senderIp)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    auto obj  = doc.object();
    auto id   = obj["id"].toString();
    auto name = obj["name"].toString();
    if (id.isEmpty() || id == m_myId) return;
    addPeer(id, name, senderIp, "udp");
}

void PeerDiscovery::addPeer(const QString& id, const QString& name,
                            const QString& ip, const QString& via)
{
    bool isNew = false;
    {
        QMutexLocker lock(&m_peersMutex);
        isNew = !m_peers.contains(id);
        PeerInfo p;
        p.id       = id.toStdString();
        p.name     = name.toStdString();
        p.ip       = ip.toStdString();
        p.lastSeen = std::chrono::steady_clock::now();
        m_peers[id] = p;
    }
    if (isNew) log(QString("Found: %1 (%2) via %3").arg(name, ip, via));
    publishPeers();
}

// ── Prune ─────────────────────────────────────────────────────────────────────

void PeerDiscovery::onPruneTimer()
{
    auto cutoff = std::chrono::steady_clock::now() -
                  std::chrono::seconds(MediaSettings::PeerTimeoutSeconds);
    {
        QMutexLocker lock(&m_peersMutex);
        QList<QString> toRemove;
        for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
            if (it.value().lastSeen < cutoff)
                toRemove.append(it.key());
        }
        for (const auto& k : toRemove) m_peers.remove(k);
    }
    publishPeers();
}

void PeerDiscovery::publishPeers()
{
    QMutexLocker lock(&m_peersMutex);
    emit peersUpdated(m_peers);
}

// ── TCP Scan ──────────────────────────────────────────────────────────────────

void PeerDiscovery::runTcpScan()
{
    if (!m_running) return;

    QStringList locals;
    for (const auto& ip : Helpers::localIPv4Addresses(false))
        locals << QString::fromStdString(ip);

    if (locals.isEmpty()) return;
    log(QString("TCP scan starting (%1 usable interface(s))…").arg(locals.size()));

    QAtomicInt found(0);
    for (const QString& local : locals) {
        QStringList parts = local.split(".");
        if (parts.size() != 4) continue;

        // Launch all 254 probes, then wait for them — all off the UI thread.
        QFutureSynchronizer<bool> sync;
        for (int i = 1; i <= 254; ++i) {
            if (!m_running) break;
            QString target = parts[0]+"."+parts[1]+"."+parts[2]+"."+QString::number(i);
            if (target == local) continue;
            sync.addFuture(QtConcurrent::run([this, target, &found]() -> bool {
                if (!m_running) return false;
                QTcpSocket sock;
                sock.connectToHost(target, MediaSettings::SignalingPort);
                if (!sock.waitForConnected(80)) return false;

                SigMsg probe;
                probe.protocol  = LocalCallProtocol::Name;
                probe.schema    = LocalCallProtocol::Schema;
#ifdef LOCALCALL_VERSION
                probe.app_version = LOCALCALL_VERSION;
#endif
#if defined(Q_OS_WIN)
                probe.platform = "windows";
#elif defined(Q_OS_MACOS)
                probe.platform = "macos";
#elif defined(Q_OS_LINUX)
                probe.platform = "linux";
#else
                probe.platform = "unknown";
#endif
                probe.type      = SigType::DiscProbe;
                probe.from_id   = m_myId.toStdString();
                probe.from_name = m_myName.toStdString();
                probe.ts        = Helpers::nowMs();
                auto enc = SigMsgEncode(probe);
                sock.write(reinterpret_cast<const char*>(enc.data()), enc.size());
                if (!sock.waitForBytesWritten(500)) return false;
                if (!sock.waitForReadyRead(400))    return false;

                QByteArray hdr;
                while (hdr.size() < 4) {
                    if (sock.bytesAvailable() == 0 && !sock.waitForReadyRead(400)) return false;
                    hdr += sock.read(4 - hdr.size());
                }
                uint32_t len = ((uint8_t)hdr[0] << 24) | ((uint8_t)hdr[1] << 16) |
                               ((uint8_t)hdr[2] << 8)  |  (uint8_t)hdr[3];
                if (len == 0 || len > 8192) return false;
                QByteArray body;
                while ((uint32_t)body.size() < len) {
                    if (sock.bytesAvailable() == 0 && !sock.waitForReadyRead(400)) return false;
                    body += sock.read(len - body.size());
                }
                try {
                    auto msg = json::parse(body.toStdString()).get<SigMsg>();
                    if (msg.type != SigType::DiscResp || msg.from_id.empty()) return false;
                    if (msg.from_id == m_myId.toStdString()) return false;
                    addPeer(QString::fromStdString(msg.from_id),
                            QString::fromStdString(msg.from_name),
                            target, "tcp");
                    found.fetchAndAddRelaxed(1);
                    return true;
                } catch (...) { return false; }
            }));
        }
        sync.waitForFinished(); // blocks this worker thread, not the UI thread
    }
    log(QString("TCP scan done — %1 peer(s) found").arg(found.loadRelaxed()));
}

void PeerDiscovery::log(const QString& msg) {
    emit diagLog(QDateTime::currentDateTime().toString("[HH:mm:ss] ") + msg);
}
