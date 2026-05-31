#pragma once
#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <QThread>
#include <QImage>
#include <QSize>
#include <QAudioFormat>
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
class QTimer;

struct EncoderSettings {
    int   width      {1280};
    int   height     {720};
    float fps        {30.0f};
    int   bitrate    {2'000'000};
    int   sampleRate {48000};
    int   channels   {1};
    int   opusBitrate{32'000};
    bool  videoEnabled{true};
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
    void encodeImage(QImage image);

signals:
    void encodedNalu(QByteArray data);

private:
    QByteArray convertToI420(const QVideoFrame& frame);
    QByteArray imageToI420(const QImage& image, int& w, int& h);
    QByteArray scaleI420(const QByteArray& src, int srcW, int srcH, int dstW, int dstH);
    void encodeI420Frame(const QByteArray& i420, int w, int h);

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

    void setMuted(bool muted);
    void setCameraEnabled(bool enabled);
    void setScreenSharing(bool enabled);
    void setScreenAudioEnabled(bool enabled);
    void setVideoTarget(const QSize& size, float fps, int bitrate);

    void setLocalVideoSink(QVideoSink* sink);
    void setRemoteVideoSink(QVideoSink* sink);

    bool isCapturing() const noexcept;

public slots:
    void onRemoteVideoFrame(const QByteArray& videoPacket);
    void onRemoteAudioFrame(const QByteArray& audioPacket);

signals:
    void encodedVideoFrame(QByteArray videoPacket);
    void encodedAudioFrame(QByteArray audioPacket);
    void remoteVideoImage(QImage image);
    void localVideoImage(QImage image);

private slots:
    void onVideoFrame(const QVideoFrame& frame);
    void onAudioData();
    void onScreenFrameTimer();

private:
    bool initAudioEncoder();
    bool initAudioDecoder();
    void cleanupAudio();
    QByteArray encodeVideoImage(const QImage& image) const;
    bool ensureRemoteAudioSink(const QAudioFormat& fmt);
    void startCameraCapture();
    void stopCameraCapture();
    void startScreenCapture();
    void stopScreenCapture();

    EncoderSettings        m_settings;

    QCamera*               m_camera        {nullptr};
    QMediaCaptureSession*  m_captureSession{nullptr};
    QVideoSink*            m_localSink     {nullptr};
    QVideoSink*            m_captureSink   {nullptr};
    QTimer*                m_screenTimer   {nullptr};

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
    QAudioFormat           m_audioFormat;
    QAudioFormat           m_remoteAudioFormat;

    bool                   m_capturing     {false};
    bool                   m_muted         {false};
    bool                   m_cameraEnabled {true};
    bool                   m_screenSharing {false};
    bool                   m_screenAudioEnabled {true};
    mutable QMutex         m_audioEncMutex;
    mutable QMutex         m_audioDecMutex;

    static constexpr int OPUS_FRAME_SAMPLES = 480;  // 10 ms at 48 kHz for lower latency
    QByteArray             m_audioCaptureBuffer;
};
