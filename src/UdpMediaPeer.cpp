#include "UdpMediaPeer.h"
#include "MediaSettings.h"

#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QMutexLocker>
#include <QMetaObject>
#include <algorithm>
#include <cstring>

namespace {
constexpr int HEADER_SIZE = 20;
constexpr int PAYLOAD_SIZE = 1180;      // Keep below typical MTU after UDP/IP headers.
constexpr int MAX_FRAME_BYTES = 2 * 1024 * 1024;
constexpr int MAX_PENDING_FRAMES = 24;

inline void putU16(char* p, quint16 v)
{
    p[0] = static_cast<char>((v >> 8) & 0xff);
    p[1] = static_cast<char>(v & 0xff);
}

inline void putU32(char* p, quint32 v)
{
    p[0] = static_cast<char>((v >> 24) & 0xff);
    p[1] = static_cast<char>((v >> 16) & 0xff);
    p[2] = static_cast<char>((v >> 8) & 0xff);
    p[3] = static_cast<char>(v & 0xff);
}

inline quint16 getU16(const char* p)
{
    return static_cast<quint16>((static_cast<unsigned char>(p[0]) << 8) |
                                static_cast<unsigned char>(p[1]));
}

inline quint32 getU32(const char* p)
{
    return (static_cast<quint32>(static_cast<unsigned char>(p[0])) << 24) |
           (static_cast<quint32>(static_cast<unsigned char>(p[1])) << 16) |
           (static_cast<quint32>(static_cast<unsigned char>(p[2])) << 8)  |
            static_cast<quint32>(static_cast<unsigned char>(p[3]));
}
}

UdpMediaPeer::UdpMediaPeer(const QString& peerIp, const QString& localId, QObject* parent)
    : QObject(parent), m_peerIpText(peerIp), m_peerAddress(peerIp)
{
    QByteArray seed = localId.toUtf8();
    if (seed.isEmpty()) seed = QByteArray::number(reinterpret_cast<quintptr>(this));
    m_localToken = qHash(seed);
    if (m_localToken == 0) m_localToken = 1;
}

UdpMediaPeer::~UdpMediaPeer()
{
    stop();
}

bool UdpMediaPeer::start()
{
    if (m_running) return true;
    if (m_peerAddress.isNull()) {
        emit failed(QStringLiteral("Invalid peer IP address"));
        return false;
    }

    m_audioSocket = new QUdpSocket(this);
    m_videoSocket = new QUdpSocket(this);

    const auto flags = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
    const bool audioOk = m_audioSocket->bind(QHostAddress::AnyIPv4,
                                             MediaSettings::MediaAudioPort,
                                             flags);
    const bool videoOk = m_videoSocket->bind(QHostAddress::AnyIPv4,
                                             MediaSettings::MediaVideoPort,
                                             flags);
    if (!audioOk || !videoOk) {
        const QString reason = QStringLiteral("Could not bind LAN media ports %1/%2")
            .arg(MediaSettings::MediaAudioPort)
            .arg(MediaSettings::MediaVideoPort);
        stop();
        emit failed(reason);
        return false;
    }

    connect(m_audioSocket, &QUdpSocket::readyRead, this, &UdpMediaPeer::onAudioReadyRead);
    connect(m_videoSocket, &QUdpSocket::readyRead, this, &UdpMediaPeer::onVideoReadyRead);
    m_running = true;
    return true;
}

void UdpMediaPeer::stop()
{
    m_running = false;
    if (m_audioSocket) {
        m_audioSocket->close();
        m_audioSocket->deleteLater();
        m_audioSocket = nullptr;
    }
    if (m_videoSocket) {
        m_videoSocket->close();
        m_videoSocket->deleteLater();
        m_videoSocket = nullptr;
    }
    clearAssemblers();
    m_connectedEmitted = false;
}

void UdpMediaPeer::sendAudioFrame(const QByteArray& opusPacket)
{
    QMutexLocker lk(&m_sendMutex);
    sendFrame(m_audioSocket, MediaSettings::MediaAudioPort, opusPacket, m_audioSeq, 'A');
}

void UdpMediaPeer::sendVideoFrame(const QByteArray& h264AnnexB)
{
    QMutexLocker lk(&m_sendMutex);
    sendFrame(m_videoSocket, MediaSettings::MediaVideoPort, h264AnnexB, m_videoSeq, 'V');
}

void UdpMediaPeer::sendFrame(QUdpSocket* socket, quint16 port, const QByteArray& frame,
                             quint32& sequence, char mediaTag)
{
    if (!m_running || !socket || frame.isEmpty() || frame.size() > MAX_FRAME_BYTES) return;

    const int frameSize = frame.size();
    const int count = std::max(1, (frameSize + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE);
    if (count > 0xffff) return;

    const quint32 frameId = sequence++;
    for (int i = 0; i < count; ++i) {
        const int offset = i * PAYLOAD_SIZE;
        const int len = std::min(PAYLOAD_SIZE, frameSize - offset);
        QByteArray packet(HEADER_SIZE + len, Qt::Uninitialized);
        char* h = packet.data();
        h[0] = 'L'; h[1] = 'C'; h[2] = 'U'; h[3] = '1';
        h[4] = mediaTag;
        h[5] = 1;
        putU16(h + 6, static_cast<quint16>(i));
        putU16(h + 8, static_cast<quint16>(count));
        putU32(h + 10, frameId);
        putU16(h + 14, static_cast<quint16>(len));
        putU32(h + 16, m_localToken);
        std::memcpy(packet.data() + HEADER_SIZE, frame.constData() + offset, static_cast<size_t>(len));
        socket->writeDatagram(packet, m_peerAddress, port);
    }
}

void UdpMediaPeer::onAudioReadyRead()
{
    drainSocket(m_audioSocket, false);
}

void UdpMediaPeer::onVideoReadyRead()
{
    drainSocket(m_videoSocket, true);
}

void UdpMediaPeer::drainSocket(QUdpSocket* socket, bool isVideo)
{
    if (!socket) return;
    while (socket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = socket->receiveDatagram();
        const QHostAddress sender = datagram.senderAddress();
        if (!sender.isNull() && sender != m_peerAddress && sender.toIPv4Address() != m_peerAddress.toIPv4Address())
            continue;

        QByteArray frame;
        if (!tryAssemble(isVideo, datagram.data(), frame)) continue;

        if (!m_connectedEmitted) {
            m_connectedEmitted = true;
            emit connected();
        }

        if (isVideo) emit remoteVideoFrame(frame);
        else         emit remoteAudioFrame(frame);
    }
}

bool UdpMediaPeer::tryAssemble(bool isVideo, const QByteArray& packet, QByteArray& frameOut)
{
    if (packet.size() < HEADER_SIZE ||
        packet[0] != 'L' || packet[1] != 'C' || packet[2] != 'U' || packet[3] != '1')
        return false;

    const char expectedTag = isVideo ? 'V' : 'A';
    if (packet[4] != expectedTag || packet[5] != 1) return false;

    const quint16 index = getU16(packet.constData() + 6);
    const quint16 count = getU16(packet.constData() + 8);
    const quint32 frameId = getU32(packet.constData() + 10);
    const quint16 len = getU16(packet.constData() + 14);
    const quint32 senderToken = getU32(packet.constData() + 16);

    // Drop our own looped-back UDP media. This can happen with stale friend IPs,
    // dual-stack adapter routing, or when testing two clients on one Windows PC.
    if (senderToken != 0 && senderToken == m_localToken) return false;

    if (count == 0 || index >= count) return false;
    if (packet.size() != HEADER_SIZE + static_cast<int>(len)) return false;

    QMutexLocker lk(&m_recvMutex);
    auto& map = isVideo ? m_videoFrames : m_audioFrames;
    while (map.size() > MAX_PENDING_FRAMES)
        map.erase(map.begin());

    FrameAssembly& a = map[frameId];
    if (a.expectedChunks == 0) {
        a.expectedChunks = count;
        a.chunks.resize(count);
    }
    if (a.expectedChunks != count) {
        map.remove(frameId);
        return false;
    }
    if (a.chunks[index].isEmpty()) {
        a.chunks[index] = packet.mid(HEADER_SIZE, len);
        a.receivedChunks++;
        a.totalBytes += len;
    }
    if (a.receivedChunks != a.expectedChunks) return false;
    if (a.totalBytes <= 0 || a.totalBytes > MAX_FRAME_BYTES) {
        map.remove(frameId);
        return false;
    }

    QByteArray assembled;
    assembled.reserve(a.totalBytes);
    for (const QByteArray& chunk : a.chunks)
        assembled.append(chunk);
    map.remove(frameId);
    frameOut = assembled;
    return !frameOut.isEmpty();
}

void UdpMediaPeer::clearAssemblers()
{
    QMutexLocker lk(&m_recvMutex);
    m_audioFrames.clear();
    m_videoFrames.clear();
}
