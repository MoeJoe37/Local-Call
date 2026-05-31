#pragma once
#include <QObject>
#include <QByteArray>
#include <QMutex>

#ifdef HAS_MULTIMEDIA
#  include <QAudioSource>
#  include <QAudioFormat>
#  include <QAudioDevice>
#  include <QIODevice>
#endif

class VoiceNoteRecorder : public QObject {
    Q_OBJECT
public:
    explicit VoiceNoteRecorder(QObject* parent = nullptr);
    ~VoiceNoteRecorder();

    bool isRecording() const { return m_recording; }
    bool start();
    QByteArray stop(); // returns PCM WAV bytes

private:
#ifdef HAS_MULTIMEDIA
    static QAudioFormat chooseInputFormat(const QAudioDevice& device);
    static QByteArray normalizeToPcm16(const QByteArray& input, const QAudioFormat& fmt);
#endif
    static QByteArray buildWav(const QByteArray& pcm, int sampleRate,
                               int channels, int bitsPerSample = 16);
#ifdef HAS_MULTIMEDIA
    QAudioSource* m_source = nullptr;
    QIODevice*    m_device = nullptr;
    QAudioFormat  m_format;
    QByteArray    m_pcm;
#endif
    mutable QMutex m_mutex;
    bool   m_recording  = false;
    int    m_sampleRate = 16000;
};
