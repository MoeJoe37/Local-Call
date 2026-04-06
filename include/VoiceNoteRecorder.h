#pragma once
#include <QObject>
#include <QByteArray>
#include <QMutex>

#ifdef HAS_MULTIMEDIA
#  include <QAudioSource>
#  include <QAudioFormat>
#  include <QBuffer>
#endif

class VoiceNoteRecorder : public QObject {
    Q_OBJECT
public:
    explicit VoiceNoteRecorder(QObject* parent = nullptr);
    ~VoiceNoteRecorder();

    bool isRecording() const { return m_recording; }
    void start();
    QByteArray stop(); // returns WAV bytes

private:
    static QByteArray buildWav(const QByteArray& pcm, int sampleRate = 16000,
                               int channels = 1, int bitsPerSample = 16);
#ifdef HAS_MULTIMEDIA
    QAudioSource* m_source = nullptr;
    QBuffer*      m_buffer = nullptr;
#endif
    QMutex m_mutex;
    bool   m_recording  = false;
    int    m_sampleRate = 16000;
};
