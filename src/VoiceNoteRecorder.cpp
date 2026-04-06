#include "VoiceNoteRecorder.h"

#ifdef HAS_MULTIMEDIA
#  include <QMediaDevices>
#  include <QAudioDevice>
#endif

VoiceNoteRecorder::VoiceNoteRecorder(QObject* parent) : QObject(parent) {}

VoiceNoteRecorder::~VoiceNoteRecorder() { if (m_recording) stop(); }

void VoiceNoteRecorder::start()
{
#ifdef HAS_MULTIMEDIA
    QMutexLocker lock(&m_mutex);
    if (m_recording) return;

    QAudioFormat fmt;
    fmt.setSampleRate(m_sampleRate);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);

    auto device = QMediaDevices::defaultAudioInput();
    if (!device.isFormatSupported(fmt))
        fmt = device.preferredFormat();

    m_buffer = new QBuffer(this);
    m_buffer->open(QIODevice::WriteOnly);

    m_source = new QAudioSource(device, fmt, this);
    m_source->start(m_buffer);
    m_recording = true;
#endif
}

QByteArray VoiceNoteRecorder::stop()
{
#ifdef HAS_MULTIMEDIA
    QMutexLocker lock(&m_mutex);
    if (!m_recording) return {};
    m_recording = false;

    if (m_source) { m_source->stop(); m_source->deleteLater(); m_source = nullptr; }

    QByteArray pcm;
    if (m_buffer) {
        pcm = m_buffer->data();
        m_buffer->close();
        m_buffer->deleteLater();
        m_buffer = nullptr;
    }
    if (pcm.isEmpty()) return {};
    return buildWav(pcm, m_sampleRate, 1, 16);
#else
    return {};
#endif
}

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
