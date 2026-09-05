#include "JitterBuffer.h"
#include "MediaSettings.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QtGlobal>
#include <opus/opus.h>
#include <algorithm>
#include <cstring>

namespace {
constexpr int MAX_QUEUED_PACKETS = 96;   // ~2 s at 20 ms frames
constexpr int MAX_CONCEAL_FRAMES = 8;    // stop synthesising after ~160 ms of silence
}

JitterBuffer::JitterBuffer(QObject* parent)
    : QIODevice(parent), m_targetDelayMs(MediaSettings::JitterTargetMs)
{}

JitterBuffer::~JitterBuffer()
{
    QMutexLocker lk(&m_mutex);
    if (m_decoder) { opus_decoder_destroy(m_decoder); m_decoder = nullptr; }
}

bool JitterBuffer::configure(int sampleRate, int channels)
{
    QMutexLocker lk(&m_mutex);
    if (m_decoder && sampleRate == m_sampleRate && channels == m_channels) return true;

    if (m_decoder) { opus_decoder_destroy(m_decoder); m_decoder = nullptr; }

    int err = OPUS_OK;
    m_decoder = opus_decoder_create(sampleRate, channels, &err);
    if (err != OPUS_OK || !m_decoder) {
        m_decoder = nullptr;
        return false;
    }
    m_sampleRate = sampleRate;
    m_channels   = channels;
    m_packets.clear();
    m_pcm.clear();
    m_nextSeq = 0;
    m_primed = false;
    return true;
}

void JitterBuffer::resetBuffer()
{
    QMutexLocker lk(&m_mutex);
    m_packets.clear();
    m_pcm.clear();
    m_nextSeq = 0;
    m_primed = false;
    m_consecutiveLosses = 0;
}

int JitterBuffer::frameSamples() const
{
    return m_sampleRate * MediaSettings::OpusFrameMs / 1000;
}

int JitterBuffer::frameBytes() const
{
    return frameSamples() * m_channels * 2;   // Int16
}

int JitterBuffer::targetDelayMs() const
{
    QMutexLocker lk(const_cast<QMutex*>(&m_mutex));
    return m_targetDelayMs;
}

int JitterBuffer::measuredJitterMs() const
{
    QMutexLocker lk(const_cast<QMutex*>(&m_mutex));
    return m_jitterMs;
}

void JitterBuffer::pushPacket(quint32 seq, const QByteArray& opusPayload, bool silence)
{
    QMutexLocker lk(&m_mutex);
    if (!m_decoder) return;

    // Inter-arrival jitter, smoothed. Feeds the adaptive target delay so a
    // clean LAN keeps latency low while a noisy link buys itself more headroom.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastArrivalMs >= 0) {
        const int delta = static_cast<int>(now - m_lastArrivalMs);
        const int deviation = qAbs(delta - MediaSettings::OpusFrameMs);
        m_jitterMs = (m_jitterMs * 7 + deviation) / 8;
        const int wanted = qBound(MediaSettings::JitterTargetMs,
                                  MediaSettings::JitterTargetMs + m_jitterMs * 2,
                                  MediaSettings::JitterMaxMs);
        // Grow quickly, shrink slowly — the reverse causes audible re-buffering.
        m_targetDelayMs = (wanted > m_targetDelayMs) ? wanted
                                                     : (m_targetDelayMs * 15 + wanted) / 16;
    }
    m_lastArrivalMs = now;

    if (m_nextSeq == 0) m_nextSeq = seq;

    // A packet that is already older than the playout point is useless.
    if (seq < m_nextSeq) return;

    m_packets.insert(seq, silence ? QByteArray() : opusPayload);
    while (m_packets.size() > MAX_QUEUED_PACKETS) m_packets.erase(m_packets.begin());
}

QByteArray JitterBuffer::nextFrame()
{
    // Caller holds m_mutex.
    if (!m_decoder) return {};

    const int samples = frameSamples();
    QByteArray pcm(frameBytes(), Qt::Uninitialized);

    auto it = m_packets.find(m_nextSeq);
    if (it != m_packets.end()) {
        const QByteArray payload = it.value();
        m_packets.erase(it);
        ++m_nextSeq;
        m_consecutiveLosses = 0;

        if (payload.isEmpty()) {
            // Explicit silence / DTX frame.
            pcm.fill('\0');
            return pcm;
        }
        const int decoded = opus_decode(
            m_decoder,
            reinterpret_cast<const unsigned char*>(payload.constData()),
            payload.size(),
            reinterpret_cast<opus_int16*>(pcm.data()),
            samples, 0);
        if (decoded <= 0) { pcm.fill('\0'); return pcm; }
        pcm.resize(decoded * m_channels * 2);
        return pcm;
    }

    // The expected packet is not here. If a later one is waiting we have a real
    // gap: conceal it with Opus PLC and move on rather than stalling the sink.
    if (!m_packets.isEmpty() && m_consecutiveLosses < MAX_CONCEAL_FRAMES) {
        ++m_nextSeq;
        ++m_consecutiveLosses;
        const int decoded = opus_decode(m_decoder, nullptr, 0,
                                        reinterpret_cast<opus_int16*>(pcm.data()),
                                        samples, 0);
        if (decoded <= 0) { pcm.fill('\0'); return pcm; }
        pcm.resize(decoded * m_channels * 2);
        return pcm;
    }

    // Nothing buffered at all — the peer is muted, paused, or gone.
    // Re-arm priming so playout restarts with a full cushion.
    if (m_packets.isEmpty()) {
        m_primed = false;
        m_consecutiveLosses = 0;
        if (m_nextSeq > 0) m_nextSeq = 0;
    }
    return {};
}

qint64 JitterBuffer::bytesAvailable() const
{
    QMutexLocker lk(const_cast<QMutex*>(&m_mutex));
    return m_pcm.size() + QIODevice::bytesAvailable();
}

qint64 JitterBuffer::readData(char* data, qint64 maxSize)
{
    if (maxSize <= 0) return 0;
    QMutexLocker lk(&m_mutex);

    // Hold playout until the target delay is buffered. Starting immediately is
    // what makes calls stutter for the first second.
    if (!m_primed) {
        const int queuedMs = m_packets.size() * MediaSettings::OpusFrameMs;
        if (queuedMs < m_targetDelayMs) {
            std::memset(data, 0, static_cast<size_t>(maxSize));
            return maxSize;   // feed silence; never underrun the sink
        }
        m_primed = true;
        if (!m_packets.isEmpty() && m_nextSeq == 0) m_nextSeq = m_packets.firstKey();
    }

    while (m_pcm.size() < maxSize) {
        const QByteArray frame = nextFrame();
        if (frame.isEmpty()) break;
        m_pcm.append(frame);
    }

    const qint64 n = std::min<qint64>(maxSize, m_pcm.size());
    if (n > 0) {
        std::memcpy(data, m_pcm.constData(), static_cast<size_t>(n));
        m_pcm.remove(0, static_cast<int>(n));
    }
    // Pad the remainder with silence so QAudioSink keeps a steady clock instead
    // of reporting an underrun and resetting itself.
    if (n < maxSize) std::memset(data + n, 0, static_cast<size_t>(maxSize - n));
    return maxSize;
}

qint64 JitterBuffer::writeData(const char*, qint64)
{
    return -1;   // read-only device
}
