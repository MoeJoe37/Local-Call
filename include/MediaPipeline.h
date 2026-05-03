#pragma once
#include <QObject>
#include <QByteArray>
#include <QSize>
#include <QMutex>
#include <QThread>
#include <QImage>
#include <memory>

#include <wels/codec_api.h>
#include <opus/opus.h>

class QCamera;
class QMediaCaptureSession;
class QVideoSink;
class QVideoFrame;
class QImage;
class QAudioSource;
class QAudioSink;
class QIODevice;

struct EncoderSettings {
    int   width      {1280};
    int   height     {720};
    float fps        {30.0f};
    int   bitrate    {2'000'000};
    int   sampleRate {48000};
    int   channels   {1};
    int   opusBitrate{32'000};
};

class VideoEncoderWorker : public QObject {
    Q_OBJECT
public:
    explicit VideoEncoderWorker(const EncoderSettings& s, QObject* parent = nullptr);
    ~VideoEncoderWorker() override;

    bool init();
    void shutdown();

public slots:
    void encodeFrame(const QVideoFrame& frame);

signals:
    void encodedNalu(QByteArray data);

private:
    QByteArray convertToI420(const QVideoFrame& frame);

    EncoderSettings  m_settings;
    ISVCEncoder*     m_enc{nullptr};
    QByteArray       m_i420Buf;
};

class VideoDecoderWorker : public QObject {
    Q_OBJECT
public:
    explicit VideoDecoderWorker(QObject* parent = nullptr);
    ~VideoDecoderWorker() override;

    bool init();
    void shutdown();
    void setOutputSink(QVideoSink* sink);

signals:
    void decodedImage(QImage image);

public slots:
    void decodeNalu(QByteArray data);

private:
    ISVCDecoder* m_dec{nullptr};
    QVideoSink*  m_sink{nullptr};
    QMutex       m_sinkMutex;
};

class MediaPipeline : public QObject {
    Q_OBJECT
public:
    explicit MediaPipeline(const EncoderSettings& settings = {},
                            QObject* parent = nullptr);
    ~MediaPipeline() override;

    bool startCapture();
    void stopCapture();

    void setLocalVideoSink(QVideoSink* sink);
    void setRemoteVideoSink(QVideoSink* sink);

    bool isCapturing() const noexcept;

public slots:
    void onRemoteVideoFrame(const QByteArray& h264AnnexB);
    void onRemoteAudioFrame(const QByteArray& opusPacket);

signals:
    void encodedVideoFrame(QByteArray h264AnnexB);
    void encodedAudioFrame(QByteArray opusPacket);
    void remoteVideoImage(QImage image);

private slots:
    void onVideoFrame(const QVideoFrame& frame);
    void onAudioData();

private:
    bool initAudioEncoder();
    bool initAudioDecoder();
    void cleanupAudio();

    EncoderSettings        m_settings;

    QCamera*               m_camera        {nullptr};
    QMediaCaptureSession*  m_captureSession{nullptr};
    QVideoSink*            m_localSink     {nullptr};
    QVideoSink*            m_captureSink   {nullptr};

    VideoEncoderWorker*    m_videoEncoder  {nullptr};
    VideoDecoderWorker*    m_videoDecoder  {nullptr};
    QThread*               m_encoderThread {nullptr};
    QThread*               m_decoderThread {nullptr};

    QAudioSource*          m_audioSrc      {nullptr};
    QIODevice*             m_audioDevice   {nullptr};
    OpusEncoder*           m_opusEnc       {nullptr};

    QAudioSink*            m_audioSink     {nullptr};
    QIODevice*             m_audioOut      {nullptr};
    OpusDecoder*           m_opusDec       {nullptr};

    bool                   m_capturing     {false};
    mutable QMutex         m_audioEncMutex;
    mutable QMutex         m_audioDecMutex;

    static constexpr int OPUS_FRAME_SAMPLES = 480;  // 10 ms at 48 kHz for lower latency
    QByteArray             m_audioCaptureBuffer;
};
