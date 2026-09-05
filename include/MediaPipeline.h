#pragma once

#include <QObject>
#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QSize>
#include <QString>
#include <QThread>
#include <QAudioFormat>

#include <opus/opus.h>

#ifdef HAS_MEDIA_VIDEO
#include <wels/codec_api.h>
#endif

class QCamera;
class QMediaCaptureSession;
class QVideoSink;
class QVideoFrame;
class QAudioSource;
class QAudioSink;
class QIODevice;
class QTimer;
class JitterBuffer;

struct EncoderSettings {
    int   width      {640};
    int   height     {360};
    float fps        {30.0f};
    int   bitrate    {800'000};
    bool  videoEnabled{true};
};

#ifdef HAS_MEDIA_VIDEO
/// H.264 encoder. Lives on its own thread — colour conversion, scaling and
/// entropy coding must never run in a camera callback on the GUI thread.
class VideoEncoderWorker : public QObject {
    Q_OBJECT
public:
    explicit VideoEncoderWorker(const EncoderSettings& s, QObject* parent = nullptr);
    ~VideoEncoderWorker() override;

    bool init();
    void shutdown();

public slots:
    void encodeImage(QImage image);
    void setTarget(QSize size, float fps, int bitrate);
    void forceKeyframe();

signals:
    void encodedNalu(QByteArray data, bool keyframe);

private:
    QByteArray imageToI420(const QImage& image, int& w, int& h);
    QByteArray scaleI420(const QByteArray& src, int srcW, int srcH, int dstW, int dstH);
    void encodeI420Frame(const QByteArray& i420, int w, int h);
    bool applyDimensions(int width, int height);

    EncoderSettings  m_settings;
    ISVCEncoder*     m_enc{nullptr};
    bool             m_forceKeyframe{false};
};

/// H.264 decoder, also on its own thread.
class VideoDecoderWorker : public QObject {
    Q_OBJECT
public:
    explicit VideoDecoderWorker(QObject* parent = nullptr);
    ~VideoDecoderWorker() override;

    bool init();
    void shutdown();

signals:
    void decodedImage(QImage image);
    /// Raised when the decoder cannot produce a picture, so the call can ask
    /// the sender for a fresh IDR instead of showing black indefinitely.
    void keyframeNeeded();

public slots:
    void decodeNalu(QByteArray data, bool keyframe);

private:
    ISVCDecoder* m_dec{nullptr};
    bool         m_haveKeyframe{false};
    int          m_framesSinceRequest{0};
};
#endif  // HAS_MEDIA_VIDEO

/// Capture, encode, decode and play back one call's media.
///
/// Wire formats are Opus (48 kHz mono, 20 ms frames) and H.264 Annex-B. The
/// pipeline always encodes audio at 48 kHz mono regardless of what the capture
/// device offers, resampling if needed, so the two peers never have to agree on
/// a device-specific format.
class MediaPipeline : public QObject {
    Q_OBJECT
public:
    explicit MediaPipeline(const EncoderSettings& settings = {}, QObject* parent = nullptr);
    ~MediaPipeline() override;

    bool startCapture();
    void stopCapture();

    void setMuted(bool muted);
    void setCameraEnabled(bool enabled);
    void setScreenSharing(bool enabled);
    void setScreenAudioEnabled(bool enabled);
    void setVideoTarget(const QSize& size, float fps, int bitrate);

    bool isCapturing()   const noexcept { return m_capturing; }
    bool hasAudio()      const noexcept { return m_audioReady; }
    bool hasVideo()      const noexcept;
    QString audioCodecName() const;
    QString videoCodecName() const;
    QSize   remoteVideoSize() const { return m_remoteSize; }

public slots:
    void onRemoteVideoFrame(const QByteArray& nalu, bool keyframe);
    void onRemoteAudioFrame(quint32 seq, const QByteArray& opusPacket, bool silence);
    /// The peer asked us for an IDR (it just joined or lost the stream).
    void onKeyframeRequested();

signals:
    void encodedVideoFrame(QByteArray nalu, bool keyframe);
    void encodedAudioFrame(QByteArray opusPacket, bool silence);
    void remoteVideoImage(QImage image);
    void localVideoImage(QImage image);
    /// Ask the transport to send a keyframe request to the peer.
    void keyframeRequestNeeded();
    void videoTargetChanged(QSize size, float fps, int bitrate);
    void frameToEncode(QImage image);

private slots:
    void onVideoFrame(const QVideoFrame& frame);
    void onAudioData();
    void onScreenFrameTimer();

private:
    bool initAudioCapture();
    bool initAudioPlayback();
    void cleanupAudio();
    bool startVideoWorkers();
    void stopVideoWorkers();
    void startCameraCapture();
    void stopCameraCapture();
    void startScreenCapture();
    void stopScreenCapture();
    bool shouldEmitFrameNow();

    /// Converts an arbitrary capture buffer to 48 kHz mono Int16.
    QByteArray toEncoderFormat(const QByteArray& devicePcm) const;

    EncoderSettings       m_settings;

    QCamera*              m_camera        {nullptr};
    QMediaCaptureSession* m_captureSession{nullptr};
    QVideoSink*           m_captureSink   {nullptr};
    QTimer*               m_screenTimer   {nullptr};

#ifdef HAS_MEDIA_VIDEO
    VideoEncoderWorker*   m_videoEncoder  {nullptr};
    VideoDecoderWorker*   m_videoDecoder  {nullptr};
    QThread*              m_encoderThread {nullptr};
    QThread*              m_decoderThread {nullptr};
#endif

    QAudioSource*         m_audioSrc      {nullptr};
    QIODevice*            m_audioDevice   {nullptr};
    OpusEncoder*          m_opusEnc       {nullptr};
    QAudioSink*           m_audioSink     {nullptr};
    JitterBuffer*         m_jitter        {nullptr};
    QAudioFormat          m_captureFormat;

    QByteArray            m_captureBuffer;   // device-rate PCM awaiting resample
    QByteArray            m_encodeBuffer;    // 48 kHz mono PCM awaiting Opus framing

    bool                  m_capturing     {false};
    bool                  m_audioReady    {false};
    bool                  m_videoReady    {false};
    bool                  m_muted         {false};
    bool                  m_cameraEnabled {true};
    bool                  m_screenSharing {false};
    bool                  m_screenAudioEnabled {true};

    QSize                 m_remoteSize;
    QElapsedTimer         m_frameClock;
    qint64                m_lastFrameMs   {-1};
    mutable QMutex        m_audioEncMutex;
};
