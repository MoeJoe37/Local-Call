#include "RtcPeer.h"

#include <QMutexLocker>
#include <QPointer>
#include <QMetaObject>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <variant>

namespace {
constexpr char VIDEO_LABEL[] = "localcall-video";
constexpr char AUDIO_LABEL[] = "localcall-audio";
constexpr int  CHUNK_HEADER_SIZE = 16;
constexpr int  CHUNK_PAYLOAD_SIZE = 16 * 1024;
constexpr int  MAX_FRAME_BYTES = 2 * 1024 * 1024;
constexpr int  MAX_PENDING_FRAMES = 8;

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

rtc::binary toRtcBinary(const QByteArray& bytes)
{
    const auto* begin = reinterpret_cast<const std::byte*>(bytes.constData());
    const auto* end   = begin + static_cast<std::ptrdiff_t>(bytes.size());
    return rtc::binary(begin, end);
}

QByteArray fromRtcBinary(const rtc::binary& data)
{
    return QByteArray(reinterpret_cast<const char*>(data.data()),
                      static_cast<int>(data.size()));
}

rtc::DataChannelInit lowLatencyChannelInit()
{
    rtc::DataChannelInit init;
    init.reliability.unordered = true;
    init.reliability.maxRetransmits = 0;
    return init;
}
}

RtcPeer::RtcPeer(const QString&   localId,
                 const QString&   remoteId,
                 const RtcConfig& cfg,
                 QObject*         parent)
    : QObject(parent), m_localId(localId), m_remoteId(remoteId), m_cfg(cfg)
{
    rtc::Configuration config;
    config.disableAutoNegotiation = true;
    if (!cfg.localNetworkOnly) {
        if (cfg.iceServers.empty()) {
            config.iceServers.emplace_back("stun:stun.l.google.com:19302");
        } else {
            for (const auto& server : cfg.iceServers)
                config.iceServers.emplace_back(server);
        }
    }

    m_pc = std::make_shared<rtc::PeerConnection>(config);
    setupCallbacks();
}

RtcPeer::~RtcPeer()
{
    close();
}

void RtcPeer::setupCallbacks()
{
    QPointer<RtcPeer> safeThis = this;

    m_pc->onLocalDescription([safeThis](rtc::Description desc) {
        if (!safeThis) return;
        QString type = QString::fromStdString(desc.typeString());
        QString sdp  = QString::fromStdString(std::string(desc));
        QMetaObject::invokeMethod(safeThis.data(), [safeThis, type, sdp]() {
            if (safeThis) emit safeThis->localDescriptionReady(type, sdp);
        }, Qt::QueuedConnection);
    });

    m_pc->onLocalCandidate([safeThis](rtc::Candidate cand) {
        if (!safeThis) return;
        QString candidate = QString::fromStdString(std::string(cand));
        QString mid       = QString::fromStdString(cand.mid());
        QMetaObject::invokeMethod(safeThis.data(), [safeThis, candidate, mid]() {
            if (safeThis) emit safeThis->localCandidateReady(candidate, mid, 0);
        }, Qt::QueuedConnection);
    });

    m_pc->onStateChange([safeThis](rtc::PeerConnection::State state) {
        if (!safeThis) return;
        QMetaObject::invokeMethod(safeThis.data(), [safeThis, state]() {
            if (!safeThis) return;
            if (state == rtc::PeerConnection::State::Connected) {
                safeThis->m_connected = true;
                emit safeThis->connected();
            } else if (state == rtc::PeerConnection::State::Disconnected ||
                       state == rtc::PeerConnection::State::Closed) {
                safeThis->m_connected = false;
                emit safeThis->disconnected();
            } else if (state == rtc::PeerConnection::State::Failed) {
                safeThis->m_connected = false;
                emit safeThis->failed();
            }
        }, Qt::QueuedConnection);
    });

    m_pc->onDataChannel([safeThis](std::shared_ptr<rtc::DataChannel> channel) {
        if (!safeThis || !channel) return;
        std::string label;
        try { label = channel->label(); } catch (...) {}

        QMetaObject::invokeMethod(safeThis.data(), [safeThis, channel, label]() {
            if (!safeThis || !channel) return;

            const bool isAudio = (label == AUDIO_LABEL || label.find("audio") != std::string::npos);
            const bool isVideo = (label == VIDEO_LABEL || label.find("video") != std::string::npos || !isAudio);

            // Important: the answering peer receives the caller-created DataChannels
            // through onDataChannel().  Older builds only configured callbacks here
            // but never stored the channel pointers, so the callee could receive
            // media but could not send microphone/camera frames back.
            if (isAudio) safeThis->m_audioChannel = channel;
            else if (isVideo) safeThis->m_videoChannel = channel;

            safeThis->configureDataChannel(channel, isVideo);
        }, Qt::QueuedConnection);
    });
}

void RtcPeer::ensureOutgoingChannels()
{
    if (!m_pc) return;

    if (!m_videoChannel) {
        m_videoChannel = m_pc->createDataChannel(VIDEO_LABEL, lowLatencyChannelInit());
        configureDataChannel(m_videoChannel, true);
    }
    if (!m_audioChannel) {
        m_audioChannel = m_pc->createDataChannel(AUDIO_LABEL, lowLatencyChannelInit());
        configureDataChannel(m_audioChannel, false);
    }
}

void RtcPeer::configureDataChannel(const std::shared_ptr<rtc::DataChannel>& channel, bool isVideo)
{
    if (!channel) return;

    QPointer<RtcPeer> safeThis = this;

    channel->onOpen([safeThis, isVideo]() {
        if (!safeThis) return;
        QMetaObject::invokeMethod(safeThis.data(), [safeThis, isVideo]() {
            if (!safeThis) return;
            if (isVideo) safeThis->m_videoOpen = true;
            else         safeThis->m_audioOpen = true;
        }, Qt::QueuedConnection);
    });

    channel->onClosed([safeThis, isVideo]() {
        if (!safeThis) return;
        QMetaObject::invokeMethod(safeThis.data(), [safeThis, isVideo]() {
            if (!safeThis) return;
            if (isVideo) safeThis->m_videoOpen = false;
            else         safeThis->m_audioOpen = false;
        }, Qt::QueuedConnection);
    });

    channel->onMessage([safeThis, isVideo](rtc::message_variant message) {
        if (!safeThis) return;
        const auto* bin = std::get_if<rtc::binary>(&message);
        if (!bin) return;
        safeThis->handleChannelMessage(isVideo, *bin);
    });
}

void RtcPeer::createOffer()
{
    ensureOutgoingChannels();
    m_pc->setLocalDescription(rtc::Description::Type::Offer);
}

void RtcPeer::setRemoteDescription(const QString& type, const QString& sdp)
{
    rtc::Description::Type descType = rtc::Description::Type::Unspec;
    if (type == QLatin1String("offer"))  descType = rtc::Description::Type::Offer;
    if (type == QLatin1String("answer")) descType = rtc::Description::Type::Answer;

    m_pc->setRemoteDescription(rtc::Description(sdp.toStdString(), descType));
    m_remoteDescriptionSet = true;
    flushPendingCandidates();

    if (descType == rtc::Description::Type::Offer)
        m_pc->setLocalDescription(rtc::Description::Type::Answer);
}

void RtcPeer::applyRemoteCandidate(const QString& candidate,
                                   const QString& mid,
                                   int            /*mlineIndex*/)
{
    if (!m_pc || candidate.trimmed().isEmpty()) return;
    try {
        m_pc->addRemoteCandidate(
            rtc::Candidate(candidate.toStdString(), mid.toStdString()));
    } catch (...) {
        // Ignore malformed/stale candidates. ICE will continue with the rest.
    }
}

void RtcPeer::flushPendingCandidates()
{
    const auto queued = m_pendingCandidates;
    m_pendingCandidates.clear();
    for (const auto& c : queued)
        applyRemoteCandidate(c.candidate, c.mid, c.mlineIndex);
}

void RtcPeer::addRemoteCandidate(const QString& candidate,
                                 const QString& mid,
                                 int            mlineIndex)
{
    if (!m_remoteDescriptionSet) {
        if (m_pendingCandidates.size() < 128)
            m_pendingCandidates.push_back({candidate, mid, mlineIndex});
        return;
    }
    applyRemoteCandidate(candidate, mid, mlineIndex);
}

void RtcPeer::close()
{
    if (m_videoChannel) {
        try { m_videoChannel->close(); } catch (...) {}
        m_videoChannel.reset();
    }
    if (m_audioChannel) {
        try { m_audioChannel->close(); } catch (...) {}
        m_audioChannel.reset();
    }
    if (m_pc) {
        try { m_pc->close(); } catch (...) {}
        m_pc.reset();
    }
    clearAssemblers();
    m_pendingCandidates.clear();
    m_remoteDescriptionSet = false;
    m_videoOpen = false;
    m_audioOpen = false;
    m_connected = false;
}

bool RtcPeer::isConnected() const noexcept  { return m_connected; }
QString RtcPeer::localId()  const noexcept  { return m_localId;   }
QString RtcPeer::remoteId() const noexcept  { return m_remoteId;  }

void RtcPeer::sendVideoFrame(const QByteArray& h264AnnexB)
{
    QMutexLocker lk(&m_sendMutex);
    if (!m_videoChannel || h264AnnexB.isEmpty()) return;
    sendFrameOnChannel(m_videoChannel, h264AnnexB, m_videoSeq, 'V');
}

void RtcPeer::sendAudioFrame(const QByteArray& opusPacket)
{
    QMutexLocker lk(&m_sendMutex);
    if (!m_audioChannel || opusPacket.isEmpty()) return;
    sendFrameOnChannel(m_audioChannel, opusPacket, m_audioSeq, 'A');
}

void RtcPeer::sendFrameOnChannel(const std::shared_ptr<rtc::DataChannel>& channel,
                                 const QByteArray& frame,
                                 quint32& sequence,
                                 char mediaTag)
{
    if (!channel || frame.isEmpty() || frame.size() > MAX_FRAME_BYTES) return;

    const int frameSize = static_cast<int>(frame.size());
    const int count = std::max(1, (frameSize + CHUNK_PAYLOAD_SIZE - 1) / CHUNK_PAYLOAD_SIZE);
    if (count > 0xffff) return;

    const quint32 frameId = sequence++;
    for (int i = 0; i < count; ++i) {
        const int offset = i * CHUNK_PAYLOAD_SIZE;
        const int len = std::min(CHUNK_PAYLOAD_SIZE, frameSize - offset);
        QByteArray packet(CHUNK_HEADER_SIZE + len, Qt::Uninitialized);
        char* h = packet.data();
        h[0] = 'L'; h[1] = 'C'; h[2] = 'M'; h[3] = '1';
        h[4] = mediaTag;
        h[5] = 1; // version
        putU16(h + 6, static_cast<quint16>(i));
        putU16(h + 8, static_cast<quint16>(count));
        putU32(h + 10, frameId);
        putU16(h + 14, static_cast<quint16>(len));
        std::memcpy(packet.data() + CHUNK_HEADER_SIZE, frame.constData() + offset, static_cast<size_t>(len));

        try {
            channel->send(toRtcBinary(packet));
        } catch (...) {
            break;
        }
    }
}

void RtcPeer::handleChannelMessage(bool isVideo, const rtc::binary& payload)
{
    QByteArray packet = fromRtcBinary(payload);
    QByteArray frame;
    if (!tryAssembleFrame(isVideo, packet, frame)) return;

    QPointer<RtcPeer> safeThis = this;
    QMetaObject::invokeMethod(this, [safeThis, frame, isVideo]() {
        if (!safeThis) return;
        if (isVideo) emit safeThis->remoteVideoFrame(frame);
        else         emit safeThis->remoteAudioFrame(frame);
    }, Qt::QueuedConnection);
}

bool RtcPeer::tryAssembleFrame(bool isVideo, const QByteArray& packet, QByteArray& frameOut)
{
    if (packet.size() < CHUNK_HEADER_SIZE ||
        packet[0] != 'L' || packet[1] != 'C' || packet[2] != 'M' || packet[3] != '1') {
        frameOut = packet;
        return !frameOut.isEmpty();
    }

    const char expectedTag = isVideo ? 'V' : 'A';
    if (packet[4] != expectedTag || packet[5] != 1) return false;

    const quint16 index = getU16(packet.constData() + 6);
    const quint16 count = getU16(packet.constData() + 8);
    const quint32 frameId = getU32(packet.constData() + 10);
    const quint16 len = getU16(packet.constData() + 14);

    if (count == 0 || index >= count) return false;
    if (packet.size() != CHUNK_HEADER_SIZE + static_cast<int>(len)) return false;

    QMutexLocker lk(&m_recvMutex);
    auto& map = isVideo ? m_videoFrames : m_audioFrames;
    if (map.size() > MAX_PENDING_FRAMES)
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
        a.chunks[index] = packet.mid(CHUNK_HEADER_SIZE, len);
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

void RtcPeer::clearAssemblers()
{
    QMutexLocker lk(&m_recvMutex);
    m_videoFrames.clear();
    m_audioFrames.clear();
}
