#pragma once

#include <QIODevice>
#include <QByteArray>
#include <QMap>
#include <QMutex>
#include <cstdint>

struct OpusDecoder;

/// Adaptive de-jitter buffer and Opus decoder, exposed as the QIODevice that
/// QAudioSink pulls from.
///
/// The previous build wrote every arriving audio packet straight into the sink
/// as it landed. Any reordering produced audible scrambling, any loss produced
/// a click, and clock drift between the two machines slowly filled or drained
/// the sink until audio broke. Pulling on the sink's own cadence fixes all
/// three, because this device is always able to answer a read — with concealed
/// audio if nothing arrived in time.
class JitterBuffer : public QIODevice {
    Q_OBJECT
public:
    explicit JitterBuffer(QObject* parent = nullptr);
    ~JitterBuffer() override;

    /// Creates the Opus decoder. Safe to call again to change format.
    bool configure(int sampleRate, int channels);
    /// Drops every queued frame and rearms the prefill. Not QIODevice::reset().
    void resetBuffer();

    /// Called from the network side with a decoded-in-order-unknown Opus frame.
    void pushPacket(quint32 seq, const QByteArray& opusPayload, bool silence);

    int  targetDelayMs() const;
    int  measuredJitterMs() const;

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

protected:
    qint64 readData(char* data, qint64 maxSize) override;
    qint64 writeData(const char* data, qint64 maxSize) override;

private:
    int  frameSamples() const;
    int  frameBytes() const;
    /// Decodes the next packet in sequence, or synthesises concealment audio
    /// when it has not arrived. Returns PCM bytes.
    QByteArray nextFrame();

    mutable QMutex m_mutex;
    OpusDecoder*   m_decoder{nullptr};

    int m_sampleRate{48000};
    int m_channels{1};

    QMap<quint32, QByteArray> m_packets;   // seq -> opus payload
    QByteArray m_pcm;                      // decoded, not yet pulled by the sink

    quint32 m_nextSeq{0};
    bool    m_primed{false};               // waiting to accumulate target delay
    int     m_targetDelayMs{0};
    int     m_jitterMs{0};
    qint64  m_lastArrivalMs{-1};
    int     m_consecutiveLosses{0};
};
