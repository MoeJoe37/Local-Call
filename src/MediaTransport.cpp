#include "MediaTransport.h"
#include "MediaSettings.h"

#include <QTimer>
#include <QHash>
#include <QtGlobal>
#include <algorithm>
#include <cstring>

namespace {
constexpr int  STATS_INTERVAL_MS   = 1000;
constexpr int  MAX_PENDING_FRAMES  = 32;
}

MediaTransport::MediaTransport(const QString& peerIp, const QString& localId, QObject* parent)
    : QObject(parent), m_peerIpText(peerIp)
{
    QByteArray seed = localId.toUtf8();
    if (seed.isEmpty()) seed = QByteArray::number(reinterpret_cast<quintptr>(this));
    m_localToken = static_cast<quint32>(qHash(seed));
    if (m_localToken == 0) m_localToken = 1;

    m_clock.start();

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(STATS_INTERVAL_MS);
    connect(m_statsTimer, &QTimer::timeout, this, &MediaTransport::onStatsTick);
    m_statsTimer->start();
}

MediaTransport::~MediaTransport() = default;

CallStats MediaTransport::stats() const { return m_stats; }

void MediaTransport::markConnected()
{
    if (m_connectedEmitted) return;
    m_connectedEmitted = true;
    emit connected();
}

void MediaTransport::sendPacket(MediaPacket::Tag tag, quint8 flags,
                                const QByteArray& payload, bool dropIfCongested)
{
    if (!m_running) return;
    if (payload.size() > MediaPacket::MaxFrameBytes) return;

    const int frameSize = payload.size();
    const int chunkCount = std::max(1, (frameSize + MediaPacket::MaxPayload - 1) / MediaPacket::MaxPayload);
    if (chunkCount > MediaPacket::MaxChunks) return;

    MediaPacket::Header h;
    h.tag         = tag;
    h.flags       = flags;
    h.timestampMs = static_cast<quint32>(m_clock.elapsed());
    h.chunkCount  = static_cast<quint16>(chunkCount);
    h.senderToken = m_localToken;

    if (tag == MediaPacket::Tag::Audio)      h.seq = m_audioSeq++;
    else if (tag == MediaPacket::Tag::Video) h.seq = m_videoSeq++;
    else                                     h.seq = 0;

    for (int i = 0; i < chunkCount; ++i) {
        const int offset = i * MediaPacket::MaxPayload;
        const int len    = std::min(MediaPacket::MaxPayload, frameSize - offset);
        h.chunkIndex = static_cast<quint16>(i);
        const QByteArray chunk = MediaPacket::build(h, payload.constData() + offset, len);

        if (!writeChunk(chunk, dropIfCongested)) {
            // Backpressure: abandon the rest of this frame rather than
            // half-sending it. A partially transmitted frame can never be
            // reassembled, so the remaining chunks would be pure waste.
            if (MediaPacket::isMediaTag(tag)) ++m_droppedOut;
            return;
        }
        countBytesOut(chunk.size());
    }

    if (MediaPacket::isMediaTag(tag)) ++m_framesOut;
}

void MediaTransport::sendPing()
{
    if (!m_running) return;
    m_lastPingSentMs = m_clock.elapsed();
    sendPacket(MediaPacket::Tag::Ping, MediaPacket::FlagNone, QByteArray(1, '\0'));
}

void MediaTransport::ingest(const QByteArray& packet)
{
    MediaPacket::Header h;
    if (!MediaPacket::read(packet.constData(), packet.size(), h)) return;

    // Reject our own media looped back by a stale friend IP, a dual-stack
    // adapter, or two clients running on one machine.
    if (h.senderToken != 0 && h.senderToken == m_localToken) return;

    const int available = packet.size() - MediaPacket::HeaderSize;
    if (available < h.payloadLen) return;
    const QByteArray payload = packet.mid(MediaPacket::HeaderSize, h.payloadLen);

    m_bytesIn += packet.size();
    markConnected();

    switch (h.tag) {
    case MediaPacket::Tag::Ping:
        sendPacket(MediaPacket::Tag::Pong, MediaPacket::FlagNone, payload);
        return;
    case MediaPacket::Tag::Pong:
        if (m_lastPingSentMs >= 0)
            m_rttMs = static_cast<int>(m_clock.elapsed() - m_lastPingSentMs);
        return;
    case MediaPacket::Tag::Hello:
    case MediaPacket::Tag::KeyframeRequest:
        emit packetReceived(static_cast<int>(h.tag), h.flags, h.seq, h.timestampMs, payload);
        return;
    case MediaPacket::Tag::Audio:
    case MediaPacket::Tag::Video:
        break;
    default:
        return;
    }

    QByteArray frame;
    if (!tryAssemble(h, payload, frame)) return;

    ++m_framesIn;
    emit packetReceived(static_cast<int>(h.tag), h.flags, h.seq, h.timestampMs, frame);
}

bool MediaTransport::tryAssemble(const MediaPacket::Header& h, const QByteArray& payload,
                                 QByteArray& frameOut)
{
    if (h.chunkCount == 0 || h.chunkIndex >= h.chunkCount) return false;

    const bool isVideo = (h.tag == MediaPacket::Tag::Video);
    auto& map = isVideo ? m_videoAssembly : m_audioAssembly;

    // Single-chunk frames are the common case for audio and for small video
    // slices — skip the map entirely.
    if (h.chunkCount == 1) {
        if (!isVideo) {
            m_highestAudioSeq = std::max(m_highestAudioSeq, h.seq);
            ++m_deliveredAudio;
        }
        frameOut = payload;
        return !frameOut.isEmpty();
    }

    pruneAssemblies(map, h.seq);

    Assembly& a = map[h.seq];
    if (a.expectedChunks == 0) {
        a.expectedChunks = h.chunkCount;
        a.chunks.resize(h.chunkCount);
        a.flags = h.flags;
        a.timestampMs = h.timestampMs;
    }
    if (a.expectedChunks != h.chunkCount) {
        map.remove(h.seq);
        return false;
    }
    if (a.chunks[h.chunkIndex].isEmpty()) {
        a.chunks[h.chunkIndex] = payload;
        ++a.receivedChunks;
        a.totalBytes += payload.size();
    }
    if (a.receivedChunks != a.expectedChunks) return false;
    if (a.totalBytes <= 0 || a.totalBytes > MediaPacket::MaxFrameBytes) {
        map.remove(h.seq);
        return false;
    }

    QByteArray assembled;
    assembled.reserve(a.totalBytes);
    for (const QByteArray& chunk : a.chunks) assembled.append(chunk);
    map.remove(h.seq);

    if (!isVideo) {
        m_highestAudioSeq = std::max(m_highestAudioSeq, h.seq);
        ++m_deliveredAudio;
    }
    frameOut = assembled;
    return !frameOut.isEmpty();
}

void MediaTransport::pruneAssemblies(QMap<quint32, Assembly>& map, quint32 newestSeq)
{
    // Frames older than the newest arrival will never complete once their
    // successor is in flight; holding them only grows memory and latency.
    while (map.size() > MAX_PENDING_FRAMES) map.erase(map.begin());

    for (auto it = map.begin(); it != map.end(); ) {
        if (newestSeq > it.key() && newestSeq - it.key() > MAX_PENDING_FRAMES) it = map.erase(it);
        else ++it;
    }
}

void MediaTransport::onStatsTick()
{
    CallStats s = m_stats;
    s.transport = name();
    s.kbpsUp    = static_cast<int>((m_bytesOut * 8) / 1000);
    s.kbpsDown  = static_cast<int>((m_bytesIn  * 8) / 1000);
    s.rttMs     = m_rttMs;
    s.fpsIn     = m_framesIn;
    s.fpsOut    = m_framesOut;
    s.droppedFrames = m_droppedOut;

    // Inbound audio loss over the last window: sequence numbers are contiguous
    // per tag, so the gap between the newest sequence and the delivered count
    // is the loss.
    if (m_windowAudioBase > 0 && m_highestAudioSeq > m_windowAudioBase) {
        const quint32 expected = m_highestAudioSeq - m_windowAudioBase;
        if (expected > 0) {
            const int lost = static_cast<int>(expected) - static_cast<int>(m_deliveredAudio);
            s.lossPercent = qBound(0, lost * 100 / static_cast<int>(expected), 100);
        }
    }
    m_windowAudioBase = m_highestAudioSeq;
    m_deliveredAudio  = 0;

    m_bytesIn = m_bytesOut = 0;
    m_framesIn = m_framesOut = 0;

    m_stats = s;
    emit statsUpdated(s);

    if (m_running) sendPing();
}
