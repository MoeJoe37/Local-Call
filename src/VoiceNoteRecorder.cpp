#include "VoiceNoteRecorder.h"
#include <QMutexLocker>

#ifdef HAS_MULTIMEDIA
#  include <QMediaDevices>
#  include <QAudioDevice>
#  include <QtGlobal>
#endif

VoiceNoteRecorder::VoiceNoteRecorder(QObject* parent) : QObject(parent) {}

VoiceNoteRecorder::~VoiceNoteRecorder() { if (m_recording) stop(); }

bool VoiceNoteRecorder::start()
{
#ifdef HAS_MULTIMEDIA
    QMutexLocker lock(&m_mutex);
    if (m_recording) return true;

    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) return false;

    m_format = chooseInputFormat(device);
    if (!m_format.isValid()) return false;

    m_pcm.clear();
    m_source = new QAudioSource(device, m_format, this);
    m_source->setBufferSize(qMax<qsizetype>(4096, m_format.bytesForDuration(200 * 1000)));
    m_device = m_source->start();
    if (!m_device) {
        m_source->deleteLater();
        m_source = nullptr;
        return false;
    }

    connect(m_device, &QIODevice::readyRead, this, [this]() {
        QMutexLocker guard(&m_mutex);
        if (m_device) m_pcm.append(m_device->readAll());
    });

    m_recording = true;
    return true;
#else
    return false;
#endif
}

QByteArray VoiceNoteRecorder::stop()
{
#ifdef HAS_MULTIMEDIA
    QAudioSource* source = nullptr;
    QByteArray captured;
    QAudioFormat fmt;

    {
        QMutexLocker lock(&m_mutex);
        if (!m_recording) return {};
        m_recording = false;

        if (m_device) {
            m_pcm.append(m_device->readAll());
            m_device = nullptr;
        }

        source = m_source;
        m_source = nullptr;
        captured = m_pcm;
        m_pcm.clear();
        fmt = m_format;
    }

    if (source) {
        source->stop();
        source->deleteLater();
    }

    if (captured.isEmpty()) return {};

    QByteArray pcm16 = normalizeToPcm16(captured, fmt);
    if (pcm16.isEmpty()) return {};

    const int sampleRate = fmt.sampleRate() > 0 ? fmt.sampleRate() : m_sampleRate;
    const int channels   = fmt.channelCount() > 0 ? fmt.channelCount() : 1;
    return buildWav(pcm16, sampleRate, channels, 16);
#else
    return {};
#endif
}

#ifdef HAS_MULTIMEDIA
QAudioFormat VoiceNoteRecorder::chooseInputFormat(const QAudioDevice& device)
{
    const int rates[] = {16000, 24000, 48000, 44100};
    for (int rate : rates) {
        QAudioFormat fmt;
        fmt.setSampleRate(rate);
        fmt.setChannelCount(1);
        fmt.setSampleFormat(QAudioFormat::Int16);
        if (device.isFormatSupported(fmt)) return fmt;
    }

    QAudioFormat preferred = device.preferredFormat();
    if (preferred.isValid()) return preferred;
    return {};
}
#endif

#ifdef HAS_MULTIMEDIA
QByteArray VoiceNoteRecorder::normalizeToPcm16(const QByteArray& input, const QAudioFormat& fmt)
{
    if (input.isEmpty()) return {};

    const auto sf = fmt.sampleFormat();
    if (sf == QAudioFormat::Int16) return input;

    QByteArray out;
    if (sf == QAudioFormat::UInt8) {
        out.resize(input.size() * 2);
        auto* dst = reinterpret_cast<qint16*>(out.data());
        const auto* src = reinterpret_cast<const quint8*>(input.constData());
        for (int i = 0; i < input.size(); ++i)
            dst[i] = static_cast<qint16>((int(src[i]) - 128) << 8);
        return out;
    }

    if (sf == QAudioFormat::Int32) {
        const int samples = input.size() / 4;
        out.resize(samples * 2);
        auto* dst = reinterpret_cast<qint16*>(out.data());
        const auto* src = reinterpret_cast<const qint32*>(input.constData());
        for (int i = 0; i < samples; ++i)
            dst[i] = static_cast<qint16>(qBound(-32768, int(src[i] >> 16), 32767));
        return out;
    }

    if (sf == QAudioFormat::Float) {
        const int samples = input.size() / 4;
        out.resize(samples * 2);
        auto* dst = reinterpret_cast<qint16*>(out.data());
        const auto* src = reinterpret_cast<const float*>(input.constData());
        for (int i = 0; i < samples; ++i) {
            const float clamped = qBound(-1.0f, src[i], 1.0f);
            dst[i] = static_cast<qint16>(clamped * 32767.0f);
        }
        return out;
    }

    return {};
}
#endif

QByteArray VoiceNoteRecorder::buildWav(const QByteArray& pcm, int sampleRate,
                                        int channels, int bitsPerSample)
{
    QByteArray wav;
    int byteRate   = sampleRate * channels * bitsPerSample / 8;
    int blockAlign = channels * bitsPerSample / 8;
    int dataSize   = pcm.size();

    auto w4 = [&](uint32_t v) {
        wav.append(char(v & 0xFF)); wav.append(char((v>>8)&0xFF));
        wav.append(char((v>>16)&0xFF)); wav.append(char((v>>24)&0xFF));
    };
    auto w2 = [&](uint16_t v) {
        wav.append(char(v & 0xFF)); wav.append(char((v>>8)&0xFF));
    };

    wav.append("RIFF"); w4(36 + dataSize);
    wav.append("WAVE");
    wav.append("fmt "); w4(16); w2(1); w2(channels);
    w4(sampleRate); w4(byteRate); w2(blockAlign); w2(bitsPerSample);
    wav.append("data"); w4(dataSize);
    wav.append(pcm);
    return wav;
}
