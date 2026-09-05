#include "MediaPipeline.h"
#include "JitterBuffer.h"
#include "MediaSettings.h"

#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QVideoFrame>
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QGuiApplication>
#include <QScreen>
#include <QPixmap>
#include <QTimer>
#include <QMutexLocker>
#include <QtGlobal>
#include <cstring>

#ifdef HAS_MEDIA_VIDEO
#include <libyuv.h>
#endif

namespace {
constexpr int ENCODER_SAMPLE_RATE = MediaSettings::OpusSampleRate;   // 48 kHz
constexpr int ENCODER_CHANNELS    = 1;                               // mono voice
constexpr int OPUS_MAX_PACKET     = 4000;

int encoderFrameSamples() { return ENCODER_SAMPLE_RATE * MediaSettings::OpusFrameMs / 1000; }
int encoderFrameBytes()   { return encoderFrameSamples() * ENCODER_CHANNELS * 2; }
}

// ── Video encoder ────────────────────────────────────────────────────────────
#ifdef HAS_MEDIA_VIDEO

VideoEncoderWorker::VideoEncoderWorker(const EncoderSettings& s, QObject* parent)
    : QObject(parent), m_settings(s)
{
}

VideoEncoderWorker::~VideoEncoderWorker()
{
    shutdown();
}

bool VideoEncoderWorker::init()
{
    if (WelsCreateSVCEncoder(&m_enc) != 0 || !m_enc) return false;

    if (!applyDimensions(m_settings.width, m_settings.height)) {
        shutdown();
        return false;
    }

    int fmt = videoFormatI420;
    m_enc->SetOption(ENCODER_OPTION_DATAFORMAT, &fmt);
    return true;
}

bool VideoEncoderWorker::applyDimensions(int width, int height)
{
    if (!m_enc) return false;

    // H.264 and I420 both require even dimensions.
    m_settings.width  = qMax(2, width  & ~1);
    m_settings.height = qMax(2, height & ~1);

    SEncParamExt param;
    m_enc->GetDefaultParams(&param);
    param.iUsageType              = CAMERA_VIDEO_REAL_TIME;
    param.iPicWidth               = m_settings.width;
    param.iPicHeight              = m_settings.height;
    param.fMaxFrameRate           = m_settings.fps;
    param.iRCMode                 = RC_BITRATE_MODE;
    param.iTargetBitrate          = m_settings.bitrate;
    param.uiIntraPeriod           = static_cast<unsigned int>(m_settings.fps * 2);
    param.bEnableAdaptiveQuant    = true;
    param.bEnableSceneChangeDetect= true;
    param.bEnableDenoise          = false;
    param.iSpatialLayerNum        = 1;
    param.iTemporalLayerNum       = 1;
    param.bSimulcastAVC           = false;

    SSpatialLayerConfig& layer    = param.sSpatialLayers[0];
    layer.iVideoWidth             = m_settings.width;
    layer.iVideoHeight            = m_settings.height;
    layer.fFrameRate              = m_settings.fps;
    layer.iSpatialBitrate         = m_settings.bitrate;
    layer.uiProfileIdc            = PRO_BASELINE;
    layer.uiLevelIdc              = LEVEL_3_1;
    layer.iDLayerQp               = 0;
    layer.sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;

    return m_enc->InitializeExt(&param) == 0;
}

void VideoEncoderWorker::setTarget(QSize size, float fps, int bitrate)
{
    if (!m_enc) return;

    const int w = size.isValid() ? size.width()  : m_settings.width;
    const int h = size.isValid() ? size.height() : m_settings.height;
    if (fps > 0.0f)  m_settings.fps     = fps;
    if (bitrate > 0) m_settings.bitrate = bitrate;

    if ((w & ~1) != m_settings.width || (h & ~1) != m_settings.height) {
        // OpenH264 cannot resize a live encoder; a full re-init is the only way.
        m_enc->Uninitialize();
        applyDimensions(w, h);
        int fmt = videoFormatI420;
        m_enc->SetOption(ENCODER_OPTION_DATAFORMAT, &fmt);
    } else {
        SBitrateInfo info;
        std::memset(&info, 0, sizeof(info));
        info.iLayer   = SPATIAL_LAYER_ALL;
        info.iBitrate = m_settings.bitrate;
        m_enc->SetOption(ENCODER_OPTION_BITRATE, &info);

        float rate = m_settings.fps;
        m_enc->SetOption(ENCODER_OPTION_FRAME_RATE, &rate);
    }

    m_forceKeyframe = true;
}

void VideoEncoderWorker::forceKeyframe()
{
    m_forceKeyframe = true;
}

void VideoEncoderWorker::shutdown()
{
    if (m_enc) {
        m_enc->Uninitialize();
        WelsDestroySVCEncoder(m_enc);
        m_enc = nullptr;
    }
}

QByteArray VideoEncoderWorker::imageToI420(const QImage& source, int& w, int& h)
{
    w = h = 0;
    if (source.isNull()) return {};

    QImage img = source.convertToFormat(QImage::Format_ARGB32);
    w = img.width()  & ~1;
    h = img.height() & ~1;
    if (w <= 0 || h <= 0) return {};
    if (img.width() != w || img.height() != h) img = img.copy(0, 0, w, h);

    const int ySize  = w * h;
    const int uvSize = ySize / 4;
    QByteArray i420(ySize + uvSize * 2, Qt::Uninitialized);

    auto* dstY = reinterpret_cast<uint8_t*>(i420.data());
    auto* dstU = dstY + ySize;
    auto* dstV = dstU + uvSize;

    libyuv::ARGBToI420(img.constBits(), static_cast<int>(img.bytesPerLine()),
                       dstY, w,
                       dstU, w / 2,
                       dstV, w / 2,
                       w, h);
    return i420;
}

QByteArray VideoEncoderWorker::scaleI420(const QByteArray& src, int srcW, int srcH,
                                         int dstW, int dstH)
{
    if (src.isEmpty() || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return {};
    if (srcW == dstW && srcH == dstH) return src;

    const int srcYSize  = srcW * srcH;
    const int srcUvSize = srcYSize / 4;
    if (src.size() < srcYSize + srcUvSize * 2) return {};

    const int dstYSize  = dstW * dstH;
    const int dstUvSize = dstYSize / 4;
    QByteArray dst(dstYSize + dstUvSize * 2, Qt::Uninitialized);

    const auto* srcY = reinterpret_cast<const uint8_t*>(src.constData());
    const auto* srcU = srcY + srcYSize;
    const auto* srcV = srcU + srcUvSize;
    auto* dstY = reinterpret_cast<uint8_t*>(dst.data());
    auto* dstU = dstY + dstYSize;
    auto* dstV = dstU + dstUvSize;

    const int rc = libyuv::I420Scale(srcY, srcW, srcU, srcW / 2, srcV, srcW / 2, srcW, srcH,
                                     dstY, dstW, dstU, dstW / 2, dstV, dstW / 2, dstW, dstH,
                                     libyuv::kFilterBox);
    return rc == 0 ? dst : QByteArray{};
}

void VideoEncoderWorker::encodeI420Frame(const QByteArray& sourceI420, int sourceW, int sourceH)
{
    if (!m_enc || sourceI420.isEmpty()) return;

    const int outW = m_settings.width;
    const int outH = m_settings.height;
    const QByteArray i420 = scaleI420(sourceI420, sourceW, sourceH, outW, outH);
    if (i420.isEmpty()) return;

    if (m_forceKeyframe) {
        m_enc->ForceIntraFrame(true);
        m_forceKeyframe = false;
    }

    const auto* yPtr = reinterpret_cast<const uint8_t*>(i420.constData());
    const auto* uPtr = yPtr + outW * outH;
    const auto* vPtr = uPtr + (outW / 2) * (outH / 2);

    SSourcePicture pic;
    std::memset(&pic, 0, sizeof(pic));
    pic.iPicWidth    = outW;
    pic.iPicHeight   = outH;
    pic.iColorFormat = videoFormatI420;
    pic.iStride[0]   = outW;
    pic.iStride[1]   = outW / 2;
    pic.iStride[2]   = outW / 2;
    pic.pData[0]     = const_cast<uint8_t*>(yPtr);
    pic.pData[1]     = const_cast<uint8_t*>(uPtr);
    pic.pData[2]     = const_cast<uint8_t*>(vPtr);

    SFrameBSInfo info;
    std::memset(&info, 0, sizeof(info));

    if (m_enc->EncodeFrame(&pic, &info) != 0) return;
    if (info.eFrameType == videoFrameTypeSkip || info.eFrameType == videoFrameTypeInvalid) return;

    const bool keyframe = (info.eFrameType == videoFrameTypeIDR);

    // One picture goes out as a single Annex-B blob. Splitting layers across
    // packets would let the peer decode a partial access unit.
    QByteArray accessUnit;
    for (int i = 0; i < info.iLayerNum; ++i) {
        const SLayerBSInfo& layer = info.sLayerInfo[i];
        int totalLen = 0;
        for (int j = 0; j < layer.iNalCount; ++j) totalLen += layer.pNalLengthInByte[j];
        if (totalLen <= 0) continue;
        accessUnit.append(reinterpret_cast<const char*>(layer.pBsBuf), totalLen);
    }
    if (!accessUnit.isEmpty()) emit encodedNalu(accessUnit, keyframe);
}

void VideoEncoderWorker::encodeImage(QImage image)
{
    if (!m_enc || image.isNull()) return;
    int w = 0, h = 0;
    const QByteArray i420 = imageToI420(image, w, h);
    encodeI420Frame(i420, w, h);
}

// ── Video decoder ────────────────────────────────────────────────────────────

VideoDecoderWorker::VideoDecoderWorker(QObject* parent) : QObject(parent) {}

VideoDecoderWorker::~VideoDecoderWorker()
{
    shutdown();
}

bool VideoDecoderWorker::init()
{
    if (WelsCreateDecoder(&m_dec) != 0 || !m_dec) return false;

    SDecodingParam dParam;
    std::memset(&dParam, 0, sizeof(dParam));
    dParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
    dParam.bParseOnly                  = false;

    if (m_dec->Initialize(&dParam) != 0) {
        WelsDestroyDecoder(m_dec);
        m_dec = nullptr;
        return false;
    }
    return true;
}

void VideoDecoderWorker::shutdown()
{
    if (m_dec) {
        m_dec->Uninitialize();
        WelsDestroyDecoder(m_dec);
        m_dec = nullptr;
    }
}

void VideoDecoderWorker::decodeNalu(QByteArray data, bool keyframe)
{
    if (!m_dec || data.isEmpty()) return;

    // Feeding inter frames to a decoder that has never seen an IDR yields
    // nothing but errors, so ask for a keyframe instead of decoding garbage.
    if (!m_haveKeyframe && !keyframe) {
        if (++m_framesSinceRequest > 10) {
            m_framesSinceRequest = 0;
            emit keyframeNeeded();
        }
        return;
    }
    if (keyframe) m_haveKeyframe = true;

    unsigned char* pData[3] = {nullptr, nullptr, nullptr};
    SBufferInfo    bufInfo;
    std::memset(&bufInfo, 0, sizeof(bufInfo));

    const int result = m_dec->DecodeFrameNoDelay(
        reinterpret_cast<const unsigned char*>(data.constData()),
        data.size(),
        pData,
        &bufInfo);

    if (result != 0) {
        // The stream is broken; stop trusting our reference frames.
        m_haveKeyframe = false;
        emit keyframeNeeded();
        return;
    }
    if (bufInfo.iBufferStatus != 1) return;

    const int w       = bufInfo.UsrData.sSystemBuffer.iWidth;
    const int h       = bufInfo.UsrData.sSystemBuffer.iHeight;
    const int strideY = bufInfo.UsrData.sSystemBuffer.iStride[0];
    const int strideU = bufInfo.UsrData.sSystemBuffer.iStride[1];
    if (w <= 0 || h <= 0 || !pData[0] || !pData[1] || !pData[2]) return;

    QImage image(w, h, QImage::Format_ARGB32);
    libyuv::I420ToARGB(pData[0], strideY,
                       pData[1], strideU,
                       pData[2], strideU,
                       image.bits(), static_cast<int>(image.bytesPerLine()),
                       w, h);

    emit decodedImage(image);
}

#endif  // HAS_MEDIA_VIDEO

// ── Pipeline ─────────────────────────────────────────────────────────────────

MediaPipeline::MediaPipeline(const EncoderSettings& settings, QObject* parent)
    : QObject(parent), m_settings(settings)
{
    m_settings.width  = qMax(2, m_settings.width  & ~1);
    m_settings.height = qMax(2, m_settings.height & ~1);

    m_captureSink = new QVideoSink(this);
    connect(m_captureSink, &QVideoSink::videoFrameChanged, this, &MediaPipeline::onVideoFrame);

    m_screenTimer = new QTimer(this);
    m_screenTimer->setTimerType(Qt::PreciseTimer);
    connect(m_screenTimer, &QTimer::timeout, this, &MediaPipeline::onScreenFrameTimer);

    m_frameClock.start();
}

MediaPipeline::~MediaPipeline()
{
    stopCapture();
}

bool MediaPipeline::hasVideo() const noexcept
{
#ifdef HAS_MEDIA_VIDEO
    return m_videoReady;
#else
    return false;
#endif
}

QString MediaPipeline::audioCodecName() const
{
    return m_audioReady ? QStringLiteral("Opus 48k") : QStringLiteral("none");
}

QString MediaPipeline::videoCodecName() const
{
    return hasVideo() ? QStringLiteral("H.264") : QStringLiteral("none");
}

bool MediaPipeline::startCapture()
{
    if (m_capturing) return true;
    m_capturing = true;

    m_audioReady = initAudioCapture() && initAudioPlayback();
    if (!m_audioReady) cleanupAudio();

    if (m_settings.videoEnabled) {
        // Video failing is not fatal: a video call with a broken encoder is
        // still a usable voice call.
        m_videoReady = startVideoWorkers();
        if (m_videoReady) {
            if (m_screenSharing) startScreenCapture();
            else                 startCameraCapture();
        }
    }

    if (!m_audioReady && !m_videoReady) {
        stopCapture();
        return false;
    }
    return true;
}

void MediaPipeline::stopCapture()
{
    if (!m_capturing) return;
    m_capturing = false;

    stopScreenCapture();
    stopCameraCapture();
    cleanupAudio();
    stopVideoWorkers();

    m_audioReady = false;
    m_videoReady = false;
}

bool MediaPipeline::startVideoWorkers()
{
#ifdef HAS_MEDIA_VIDEO
    m_encoderThread = new QThread(this);
    m_videoEncoder  = new VideoEncoderWorker(m_settings);
    if (!m_videoEncoder->init()) {
        delete m_videoEncoder;
        m_videoEncoder = nullptr;
        delete m_encoderThread;
        m_encoderThread = nullptr;
        return false;
    }
    m_videoEncoder->moveToThread(m_encoderThread);
    connect(m_encoderThread, &QThread::finished, m_videoEncoder, &QObject::deleteLater);
    connect(this, &MediaPipeline::frameToEncode,
            m_videoEncoder, &VideoEncoderWorker::encodeImage, Qt::QueuedConnection);
    connect(this, &MediaPipeline::videoTargetChanged,
            m_videoEncoder, &VideoEncoderWorker::setTarget, Qt::QueuedConnection);
    connect(m_videoEncoder, &VideoEncoderWorker::encodedNalu,
            this, &MediaPipeline::encodedVideoFrame, Qt::QueuedConnection);
    m_encoderThread->start();

    m_decoderThread = new QThread(this);
    m_videoDecoder  = new VideoDecoderWorker();
    if (!m_videoDecoder->init()) {
        delete m_videoDecoder;
        m_videoDecoder = nullptr;
        delete m_decoderThread;
        m_decoderThread = nullptr;
        stopVideoWorkers();
        return false;
    }
    m_videoDecoder->moveToThread(m_decoderThread);
    connect(m_decoderThread, &QThread::finished, m_videoDecoder, &QObject::deleteLater);
    connect(m_videoDecoder, &VideoDecoderWorker::decodedImage, this,
            [this](QImage image) {
                m_remoteSize = image.size();
                emit remoteVideoImage(image);
            }, Qt::QueuedConnection);
    connect(m_videoDecoder, &VideoDecoderWorker::keyframeNeeded,
            this, &MediaPipeline::keyframeRequestNeeded, Qt::QueuedConnection);
    m_decoderThread->start();

    return true;
#else
    return false;
#endif
}

void MediaPipeline::stopVideoWorkers()
{
#ifdef HAS_MEDIA_VIDEO
    if (m_encoderThread) {
        m_encoderThread->quit();
        m_encoderThread->wait(3000);
        delete m_encoderThread;      // deletes the worker via finished/deleteLater
        m_encoderThread = nullptr;
        m_videoEncoder  = nullptr;
    }
    if (m_decoderThread) {
        m_decoderThread->quit();
        m_decoderThread->wait(3000);
        delete m_decoderThread;
        m_decoderThread = nullptr;
        m_videoDecoder  = nullptr;
    }
#endif
}

void MediaPipeline::startCameraCapture()
{
    if (!m_settings.videoEnabled || !m_cameraEnabled || m_screenSharing || m_camera) return;

    const QCameraDevice cameraDevice = QMediaDevices::defaultVideoInput();
    if (cameraDevice.isNull()) return;

    m_camera         = new QCamera(cameraDevice, this);
    m_captureSession = new QMediaCaptureSession(this);
    m_captureSession->setCamera(m_camera);
    m_captureSession->setVideoSink(m_captureSink);
    m_camera->start();
}

void MediaPipeline::stopCameraCapture()
{
    if (m_camera) {
        m_camera->stop();
        m_camera->deleteLater();
        m_camera = nullptr;
    }
    if (m_captureSession) {
        m_captureSession->setVideoSink(nullptr);
        m_captureSession->deleteLater();
        m_captureSession = nullptr;
    }
}

void MediaPipeline::startScreenCapture()
{
    if (!m_settings.videoEnabled || !m_screenSharing || !m_screenTimer) return;
    stopCameraCapture();
    const int intervalMs = qMax(16, static_cast<int>(1000.0f / qMax(1.0f, m_settings.fps)));
    m_screenTimer->start(intervalMs);
}

void MediaPipeline::stopScreenCapture()
{
    if (m_screenTimer && m_screenTimer->isActive()) m_screenTimer->stop();
}

void MediaPipeline::setMuted(bool muted)
{
    m_muted = muted;
}

void MediaPipeline::setCameraEnabled(bool enabled)
{
    m_cameraEnabled = enabled;
    if (!m_settings.videoEnabled || m_screenSharing) return;
    if (enabled) startCameraCapture();
    else         stopCameraCapture();
}

void MediaPipeline::setScreenSharing(bool enabled)
{
    if (!m_settings.videoEnabled || m_screenSharing == enabled) return;
    m_screenSharing = enabled;
    if (!m_capturing) return;

    if (enabled) {
        startScreenCapture();
    } else {
        stopScreenCapture();
        if (m_cameraEnabled) startCameraCapture();
    }
}

void MediaPipeline::setScreenAudioEnabled(bool enabled)
{
    m_screenAudioEnabled = enabled;
}

void MediaPipeline::setVideoTarget(const QSize& size, float fps, int bitrate)
{
    if (size.isValid()) {
        m_settings.width  = qMax(2, size.width()  & ~1);
        m_settings.height = qMax(2, size.height() & ~1);
    }
    if (fps > 0.0f)  m_settings.fps     = fps;
    if (bitrate > 0) m_settings.bitrate = bitrate;

    emit videoTargetChanged(QSize(m_settings.width, m_settings.height),
                            m_settings.fps, m_settings.bitrate);

    if (m_screenTimer && m_screenTimer->isActive()) {
        const int intervalMs = qMax(16, static_cast<int>(1000.0f / qMax(1.0f, m_settings.fps)));
        m_screenTimer->start(intervalMs);
    }
}

void MediaPipeline::onKeyframeRequested()
{
#ifdef HAS_MEDIA_VIDEO
    if (m_videoEncoder)
        QMetaObject::invokeMethod(m_videoEncoder, "forceKeyframe", Qt::QueuedConnection);
#endif
}

bool MediaPipeline::shouldEmitFrameNow()
{
    // Cameras deliver at their own rate, which is usually above the negotiated
    // target. Encoding every frame just burns CPU and bandwidth.
    const qint64 now    = m_frameClock.elapsed();
    const qint64 minGap = static_cast<qint64>(1000.0f / qMax(1.0f, m_settings.fps)) - 2;
    if (m_lastFrameMs >= 0 && now - m_lastFrameMs < minGap) return false;
    m_lastFrameMs = now;
    return true;
}

void MediaPipeline::onVideoFrame(const QVideoFrame& frame)
{
    if (!m_settings.videoEnabled || m_screenSharing || !m_videoReady) return;
    if (!shouldEmitFrameNow()) return;

    // Only the copy out of the frame buffer happens here. Colour conversion,
    // scaling and H.264 encoding run on the encoder thread — doing them inline
    // in this callback is what used to stall the whole UI at 30 fps.
    QImage image = frame.toImage();
    if (image.isNull()) return;

    emit localVideoImage(image);
    emit frameToEncode(image);
}

void MediaPipeline::onScreenFrameTimer()
{
    if (!m_settings.videoEnabled || !m_screenSharing || !m_videoReady) return;

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    // grabWindow has to stay on the GUI thread; everything after it does not.
    QImage image = screen->grabWindow(0).toImage();
    if (image.isNull()) return;

    emit localVideoImage(image);
    emit frameToEncode(image);
}

// ── Audio ────────────────────────────────────────────────────────────────────

bool MediaPipeline::initAudioCapture()
{
    const QAudioDevice inDevice = QMediaDevices::defaultAudioInput();
    if (inDevice.isNull()) return false;

    // Prefer the encoder's own format so no resampling is needed at all, then
    // fall back through the rates a typical device will actually open.
    QAudioFormat fmt;
    const int rates[]    = {ENCODER_SAMPLE_RATE, 44100, 24000, 16000};
    const int channels[] = {1, 2};
    for (int rate : rates) {
        for (int ch : channels) {
            QAudioFormat candidate;
            candidate.setSampleRate(rate);
            candidate.setChannelCount(ch);
            candidate.setSampleFormat(QAudioFormat::Int16);
            if (inDevice.isFormatSupported(candidate)) { fmt = candidate; break; }
        }
        if (fmt.isValid()) break;
    }
    if (!fmt.isValid()) {
        QAudioFormat preferred = inDevice.preferredFormat();
        if (preferred.isValid()) {
            preferred.setSampleFormat(QAudioFormat::Int16);
            if (inDevice.isFormatSupported(preferred)) fmt = preferred;
        }
    }
    if (!fmt.isValid()) return false;

    m_captureFormat = fmt;

    {
        QMutexLocker lk(&m_audioEncMutex);
        int err = OPUS_OK;
        m_opusEnc = opus_encoder_create(ENCODER_SAMPLE_RATE, ENCODER_CHANNELS,
                                        OPUS_APPLICATION_VOIP, &err);
        if (err != OPUS_OK || !m_opusEnc) {
            m_opusEnc = nullptr;
            return false;
        }
        opus_encoder_ctl(m_opusEnc, OPUS_SET_BITRATE(MediaSettings::OpusBitrate));
        // Inband FEC lets the peer rebuild a lost packet from the next one,
        // which is worth far more on a real network than the few kbit/s it costs.
        opus_encoder_ctl(m_opusEnc, OPUS_SET_INBAND_FEC(1));
        opus_encoder_ctl(m_opusEnc, OPUS_SET_PACKET_LOSS_PERC(10));
        opus_encoder_ctl(m_opusEnc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(m_opusEnc, OPUS_SET_COMPLEXITY(5));
    }

    m_audioSrc = new QAudioSource(inDevice, fmt, this);
    // A short buffer keeps capture latency down; 100 ms still survives a
    // scheduling hiccup without becoming audible delay.
    m_audioSrc->setBufferSize(qMax<qsizetype>(4096, fmt.bytesForDuration(100 * 1000)));
    m_audioDevice = m_audioSrc->start();
    if (!m_audioDevice) {
        m_audioSrc->deleteLater();
        m_audioSrc = nullptr;
        return false;
    }
    connect(m_audioDevice, &QIODevice::readyRead, this, &MediaPipeline::onAudioData);
    return true;
}

bool MediaPipeline::initAudioPlayback()
{
    const QAudioDevice outDevice = QMediaDevices::defaultAudioOutput();
    if (outDevice.isNull()) return false;

    QAudioFormat fmt;
    fmt.setSampleRate(ENCODER_SAMPLE_RATE);
    fmt.setChannelCount(ENCODER_CHANNELS);
    fmt.setSampleFormat(QAudioFormat::Int16);
    if (!outDevice.isFormatSupported(fmt)) {
        // Qt resamples internally when the device cannot take 48 kHz mono, so
        // keep going rather than dropping playback entirely.
        qWarning("MediaPipeline: output device does not natively support 48 kHz mono");
    }

    m_jitter = new JitterBuffer(this);
    if (!m_jitter->configure(ENCODER_SAMPLE_RATE, ENCODER_CHANNELS)) {
        m_jitter->deleteLater();
        m_jitter = nullptr;
        return false;
    }
    m_jitter->open(QIODevice::ReadOnly);

    m_audioSink = new QAudioSink(outDevice, fmt, this);
    m_audioSink->setBufferSize(fmt.bytesForDuration(80 * 1000));
    // Pull mode: the sink asks the jitter buffer for audio on the sound card's
    // clock, so playback timing no longer depends on when packets happen to land.
    m_audioSink->start(m_jitter);
    return true;
}

void MediaPipeline::cleanupAudio()
{
    if (m_audioSrc) {
        m_audioSrc->stop();
        m_audioSrc->deleteLater();
        m_audioSrc = nullptr;
    }
    m_audioDevice = nullptr;

    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink->deleteLater();
        m_audioSink = nullptr;
    }
    if (m_jitter) {
        m_jitter->close();
        m_jitter->deleteLater();
        m_jitter = nullptr;
    }

    m_captureBuffer.clear();
    m_encodeBuffer.clear();

    QMutexLocker lk(&m_audioEncMutex);
    if (m_opusEnc) {
        opus_encoder_destroy(m_opusEnc);
        m_opusEnc = nullptr;
    }
}

QByteArray MediaPipeline::toEncoderFormat(const QByteArray& devicePcm) const
{
    const int srcRate     = m_captureFormat.sampleRate();
    const int srcChannels = qMax(1, m_captureFormat.channelCount());

    if (srcRate == ENCODER_SAMPLE_RATE && srcChannels == ENCODER_CHANNELS)
        return devicePcm;

    const auto* src = reinterpret_cast<const qint16*>(devicePcm.constData());
    const int srcFrames = static_cast<int>(devicePcm.size()) / (2 * srcChannels);
    if (srcFrames <= 0) return {};

    // Downmix to mono first, then resample. Linear interpolation is plenty for
    // voice and costs almost nothing per frame.
    QByteArray mono(srcFrames * 2, Qt::Uninitialized);
    auto* monoOut = reinterpret_cast<qint16*>(mono.data());
    for (int i = 0; i < srcFrames; ++i) {
        int sum = 0;
        for (int c = 0; c < srcChannels; ++c) sum += src[i * srcChannels + c];
        monoOut[i] = static_cast<qint16>(sum / srcChannels);
    }
    if (srcRate == ENCODER_SAMPLE_RATE) return mono;

    const int dstFrames = static_cast<int>(
        static_cast<qint64>(srcFrames) * ENCODER_SAMPLE_RATE / srcRate);
    if (dstFrames <= 0) return {};

    QByteArray out(dstFrames * 2, Qt::Uninitialized);
    auto* dst = reinterpret_cast<qint16*>(out.data());
    const double ratio = static_cast<double>(srcRate) / ENCODER_SAMPLE_RATE;
    for (int i = 0; i < dstFrames; ++i) {
        const double pos  = i * ratio;
        const int    idx  = static_cast<int>(pos);
        const double frac = pos - idx;
        const qint16 a = monoOut[qMin(idx,     srcFrames - 1)];
        const qint16 b = monoOut[qMin(idx + 1, srcFrames - 1)];
        dst[i] = static_cast<qint16>(a + (b - a) * frac);
    }
    return out;
}

void MediaPipeline::onAudioData()
{
    if (!m_audioDevice || !m_captureFormat.isValid()) return;

    m_captureBuffer.append(m_audioDevice->readAll());
    if (m_captureBuffer.isEmpty()) return;

    // Convert whole device frames only, so the resampler never splits a sample.
    const int srcFrameBytes = 2 * qMax(1, m_captureFormat.channelCount());
    const int usable = (static_cast<int>(m_captureBuffer.size()) / srcFrameBytes) * srcFrameBytes;
    if (usable <= 0) return;

    const QByteArray chunk = m_captureBuffer.left(usable);
    m_captureBuffer.remove(0, usable);
    m_encodeBuffer.append(toEncoderFormat(chunk));

    const bool silent     = m_muted || (m_screenSharing && !m_screenAudioEnabled);
    const int  frameBytes = encoderFrameBytes();

    QMutexLocker lk(&m_audioEncMutex);
    if (!m_opusEnc) return;

    while (m_encodeBuffer.size() >= frameBytes) {
        const QByteArray pcm = m_encodeBuffer.left(frameBytes);
        m_encodeBuffer.remove(0, frameBytes);

        if (silent) {
            // A marked empty frame keeps the peer's sequence contiguous at no
            // bandwidth cost, so the jitter buffer never mistakes mute for loss.
            emit encodedAudioFrame(QByteArray(), true);
            continue;
        }

        QByteArray packet(OPUS_MAX_PACKET, Qt::Uninitialized);
        const int written = opus_encode(
            m_opusEnc,
            reinterpret_cast<const opus_int16*>(pcm.constData()),
            encoderFrameSamples(),
            reinterpret_cast<unsigned char*>(packet.data()),
            OPUS_MAX_PACKET);
        if (written <= 0) continue;

        packet.resize(written);
        emit encodedAudioFrame(packet, false);
    }
}

void MediaPipeline::onRemoteVideoFrame(const QByteArray& nalu, bool keyframe)
{
#ifdef HAS_MEDIA_VIDEO
    if (!m_videoDecoder) return;
    QMetaObject::invokeMethod(m_videoDecoder, "decodeNalu", Qt::QueuedConnection,
                              Q_ARG(QByteArray, nalu), Q_ARG(bool, keyframe));
#else
    Q_UNUSED(nalu);
    Q_UNUSED(keyframe);
#endif
}

void MediaPipeline::onRemoteAudioFrame(quint32 seq, const QByteArray& opusPacket, bool silence)
{
    if (m_jitter) m_jitter->pushPacket(seq, opusPacket, silence);
}
