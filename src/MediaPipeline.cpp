#include "MediaPipeline.h"
#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QImage>
#include <QDateTime>
#include <QMutexLocker>
#include <QTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QPixmap>
#include <QBuffer>
#include <QtEndian>
#include <cstring>

#include <libyuv.h>

namespace LocalLanCodec {
constexpr char VIDEO_MAGIC[] = {'L','C','J','1'};
constexpr char AUDIO_MAGIC[] = {'L','C','A','1'};

static void putU16(QByteArray& out, quint16 value)
{
    char bytes[2];
    qToLittleEndian<quint16>(value, bytes);
    out.append(bytes, 2);
}

static void putU32(QByteArray& out, quint32 value)
{
    char bytes[4];
    qToLittleEndian<quint32>(value, bytes);
    out.append(bytes, 4);
}

static quint16 getU16(const char* p)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(p));
}

static quint32 getU32(const char* p)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(p));
}
}


VideoEncoderWorker::VideoEncoderWorker(const EncoderSettings& s, QObject* parent)
    : QObject(parent), m_settings(s)
{}

VideoEncoderWorker::~VideoEncoderWorker()
{
    shutdown();
}

bool VideoEncoderWorker::init()
{
    if (WelsCreateSVCEncoder(&m_enc) != 0 || !m_enc) return false;

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

    SSpatialLayerConfig& layer   = param.sSpatialLayers[0];
    layer.iVideoWidth             = m_settings.width;
    layer.iVideoHeight            = m_settings.height;
    layer.fFrameRate              = m_settings.fps;
    layer.iSpatialBitrate         = m_settings.bitrate;
    layer.uiProfileIdc            = PRO_BASELINE;
    layer.uiLevelIdc              = LEVEL_3_1;
    layer.iDLayerQp               = 0;
    layer.sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;

    if (m_enc->InitializeExt(&param) != 0) {
        WelsDestroySVCEncoder(m_enc);
        m_enc = nullptr;
        return false;
    }

    int fmt = videoFormatI420;
    m_enc->SetOption(ENCODER_OPTION_DATAFORMAT, &fmt);

    const int sz = m_settings.width * m_settings.height;
    m_i420Buf.resize(sz + sz / 2);

    return true;
}

void VideoEncoderWorker::shutdown()
{
    if (m_enc) {
        m_enc->Uninitialize();
        WelsDestroySVCEncoder(m_enc);
        m_enc = nullptr;
    }
}

QByteArray VideoEncoderWorker::convertToI420(const QVideoFrame& frame)
{
    const int w = frame.width();
    const int h = frame.height();
    if (w <= 0 || h <= 0) return {};

    const int ySize  = w * h;
    const int uvSize = ySize / 4;
    QByteArray i420(ySize + uvSize * 2, Qt::Uninitialized);

    auto* dstY = reinterpret_cast<uint8_t*>(i420.data());
    auto* dstU = dstY + ySize;
    auto* dstV = dstU + uvSize;

    using Fmt = QVideoFrameFormat::PixelFormat;
    const Fmt fmt = frame.pixelFormat();

    if (fmt == Fmt::Format_YUV420P) {
        const uint8_t* srcY = frame.bits(0);
        const uint8_t* srcU = frame.bits(1);
        const uint8_t* srcV = frame.bits(2);
        libyuv::I420Copy(srcY, frame.bytesPerLine(0),
                         srcU, frame.bytesPerLine(1),
                         srcV, frame.bytesPerLine(2),
                         dstY, w,
                         dstU, w / 2,
                         dstV, w / 2,
                         w, h);
    } else if (fmt == Fmt::Format_NV12) {
        const uint8_t* srcY  = frame.bits(0);
        const uint8_t* srcUV = frame.bits(1);
        libyuv::NV12ToI420(srcY,  frame.bytesPerLine(0),
                            srcUV, frame.bytesPerLine(1),
                            dstY, w,
                            dstU, w / 2,
                            dstV, w / 2,
                            w, h);
    } else {
        QImage img = frame.toImage();
        int iw = 0;
        int ih = 0;
        return imageToI420(img, iw, ih);
    }

    return i420;
}

QByteArray VideoEncoderWorker::imageToI420(const QImage& source, int& w, int& h)
{
    if (source.isNull()) return {};
    QImage img = source.convertToFormat(QImage::Format_ARGB32);
    w = img.width() & ~1;
    h = img.height() & ~1;
    if (w <= 0 || h <= 0) return {};
    if (img.width() != w || img.height() != h)
        img = img.copy(0, 0, w, h);

    const int ySize  = w * h;
    const int uvSize = ySize / 4;
    QByteArray i420(ySize + uvSize * 2, Qt::Uninitialized);

    auto* dstY = reinterpret_cast<uint8_t*>(i420.data());
    auto* dstU = dstY + ySize;
    auto* dstV = dstU + uvSize;

    libyuv::ARGBToI420(img.constBits(), img.bytesPerLine(),
                        dstY, w,
                        dstU, w / 2,
                        dstV, w / 2,
                        w, h);
    return i420;
}

QByteArray VideoEncoderWorker::scaleI420(const QByteArray& src, int srcW, int srcH, int dstW, int dstH)
{
    if (src.isEmpty() || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return {};
    if (srcW == dstW && srcH == dstH) return src;

    const int srcYSize = srcW * srcH;
    const int srcUvSize = srcYSize / 4;
    if (src.size() < srcYSize + srcUvSize * 2) return {};

    const int dstYSize = dstW * dstH;
    const int dstUvSize = dstYSize / 4;
    QByteArray dst(dstYSize + dstUvSize * 2, Qt::Uninitialized);

    const auto* srcY = reinterpret_cast<const uint8_t*>(src.constData());
    const auto* srcU = srcY + srcYSize;
    const auto* srcV = srcU + srcUvSize;
    auto* dstY = reinterpret_cast<uint8_t*>(dst.data());
    auto* dstU = dstY + dstYSize;
    auto* dstV = dstU + dstUvSize;

    const int rc = libyuv::I420Scale(srcY, srcW,
                                      srcU, srcW / 2,
                                      srcV, srcW / 2,
                                      srcW, srcH,
                                      dstY, dstW,
                                      dstU, dstW / 2,
                                      dstV, dstW / 2,
                                      dstW, dstH,
                                      libyuv::kFilterBox);
    return rc == 0 ? dst : QByteArray{};
}

void VideoEncoderWorker::encodeI420Frame(const QByteArray& sourceI420, int sourceW, int sourceH)
{
    if (!m_enc || sourceI420.isEmpty()) return;

    const int outW = m_settings.width;
    const int outH = m_settings.height;
    QByteArray i420 = scaleI420(sourceI420, sourceW, sourceH, outW, outH);
    if (i420.isEmpty()) return;

    const uint8_t* yPtr = reinterpret_cast<const uint8_t*>(i420.constData());
    const uint8_t* uPtr = yPtr + outW * outH;
    const uint8_t* vPtr = uPtr + (outW / 2) * (outH / 2);

    SSourcePicture pic;
    std::memset(&pic, 0, sizeof(pic));
    pic.iPicWidth      = outW;
    pic.iPicHeight     = outH;
    pic.iColorFormat   = videoFormatI420;
    pic.iStride[0]     = outW;
    pic.iStride[1]     = outW / 2;
    pic.iStride[2]     = outW / 2;
    pic.pData[0]       = const_cast<uint8_t*>(yPtr);
    pic.pData[1]       = const_cast<uint8_t*>(uPtr);
    pic.pData[2]       = const_cast<uint8_t*>(vPtr);
    pic.uiTimeStamp    = QDateTime::currentMSecsSinceEpoch();

    SFrameBSInfo info;
    std::memset(&info, 0, sizeof(info));

    if (m_enc->EncodeFrame(&pic, &info) != 0) return;
    if (info.eFrameType == videoFrameTypeSkip) return;

    for (int i = 0; i < info.iLayerNum; ++i) {
        const SLayerBSInfo& layer = info.sLayerInfo[i];
        int totalLen = 0;
        for (int j = 0; j < layer.iNalCount; ++j)
            totalLen += layer.pNalLengthInByte[j];

        if (totalLen <= 0) continue;

        QByteArray nalu(reinterpret_cast<const char*>(layer.pBsBuf), totalLen);
        emit encodedNalu(nalu);
    }
}

void VideoEncoderWorker::encodeFrame(const QVideoFrame& inFrame)
{
    if (!m_enc) return;

    QVideoFrame frame(inFrame);
    if (!frame.map(QVideoFrame::ReadOnly)) return;

    const int w = frame.width();
    const int h = frame.height();
    const QByteArray i420 = convertToI420(frame);
    frame.unmap();

    // Cameras rarely produce exactly the negotiated size. Older builds dropped
    // every frame whose size was not 640x360, which made video calls look dead
    // on most Windows webcams.  Encode after scaling instead.
    encodeI420Frame(i420, w, h);
}

void VideoEncoderWorker::encodeImage(QImage image)
{
    if (!m_enc || image.isNull()) return;
    int w = 0;
    int h = 0;
    const QByteArray i420 = imageToI420(image, w, h);
    encodeI420Frame(i420, w, h);
}

VideoDecoderWorker::VideoDecoderWorker(QObject* parent)
    : QObject(parent)
{}

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

void VideoDecoderWorker::setOutputSink(QVideoSink* sink)
{
    QMutexLocker lk(&m_sinkMutex);
    m_sink = sink;
}

void VideoDecoderWorker::decodeNalu(QByteArray data)
{
    if (!m_dec) return;

    unsigned char* pData[3] = {nullptr, nullptr, nullptr};
    SBufferInfo    bufInfo;
    std::memset(&bufInfo, 0, sizeof(bufInfo));

    const int result = m_dec->DecodeFrameNoDelay(
        reinterpret_cast<const unsigned char*>(data.constData()),
        data.size(),
        pData,
        &bufInfo);

    if (result != 0 || bufInfo.iBufferStatus != 1) return;

    const int w      = bufInfo.UsrData.sSystemBuffer.iWidth;
    const int h      = bufInfo.UsrData.sSystemBuffer.iHeight;
    const int strideY = bufInfo.UsrData.sSystemBuffer.iStride[0];
    const int strideU = bufInfo.UsrData.sSystemBuffer.iStride[1];

    if (w <= 0 || h <= 0 || !pData[0] || !pData[1] || !pData[2]) return;

    QImage image(w, h, QImage::Format_ARGB32);
    libyuv::I420ToARGB(pData[0], strideY,
                       pData[1], strideU,
                       pData[2], strideU,
                       image.bits(), image.bytesPerLine(),
                       w, h);

    emit decodedImage(image.copy());

    // The QLabel-based call UI consumes decoded QImage frames directly.
    // QVideoSink support is kept as a no-op extension point to avoid forcing
    // a second conversion path on latency-sensitive builds.
}

MediaPipeline::MediaPipeline(const EncoderSettings& settings, QObject* parent)
    : QObject(parent), m_settings(settings)
{
    // H.264 and I420 require even dimensions. Keep presets safe if a caller
    // passes an odd custom size.
    m_settings.width  = qMax(2, m_settings.width  & ~1);
    m_settings.height = qMax(2, m_settings.height & ~1);

    m_captureSink = new QVideoSink(this);
    connect(m_captureSink, &QVideoSink::videoFrameChanged,
            this,          &MediaPipeline::onVideoFrame);

    m_screenTimer = new QTimer(this);
    m_screenTimer->setTimerType(Qt::PreciseTimer);
    connect(m_screenTimer, &QTimer::timeout,
            this,          &MediaPipeline::onScreenFrameTimer);
}

MediaPipeline::~MediaPipeline()
{
    stopCapture();
}

bool MediaPipeline::startCapture()
{
    if (m_capturing) return true;

    // Reliable default path: capture with Qt Multimedia, send JPEG video and
    // PCM audio packets over the LAN UDP transport.  This intentionally avoids
    // H.264/Opus/WebRTC as the default runtime path because the previous stack
    // could connect but silently fail on clean Windows machines when a codec,
    // device format, or ICE state did not line up.
    m_capturing = true;

    if (m_settings.videoEnabled) {
        if (m_screenSharing) startScreenCapture();
        else                 startCameraCapture();
    }

    if (!initAudioEncoder()) {
        // Voice-only calls must have a microphone.  Video and screen-share calls
        // should still work as video-only instead of failing the whole call just
        // because Windows microphone privacy or a missing input device blocked
        // QAudioSource.  The remote side will simply receive no audio packets.
        if (!m_settings.videoEnabled) {
            stopCapture();
            return false;
        }
    }

    return true;
}

void MediaPipeline::stopCapture()
{
    if (!m_capturing && !m_camera && !m_screenTimer) return;
    m_capturing = false;

    stopScreenCapture();
    stopCameraCapture();

    cleanupAudio();

    if (m_encoderThread) {
        m_encoderThread->quit();
        m_encoderThread->wait(3000);
        m_encoderThread->deleteLater();
        m_videoEncoder  = nullptr;
        m_encoderThread = nullptr;
    }

    if (m_decoderThread) {
        m_decoderThread->quit();
        m_decoderThread->wait(3000);
        m_decoderThread->deleteLater();
        m_videoDecoder  = nullptr;
        m_decoderThread = nullptr;
    }
}

void MediaPipeline::startCameraCapture()
{
    if (!m_settings.videoEnabled || !m_cameraEnabled || m_screenSharing || m_camera) return;

    const QCameraDevice cameraDevice = QMediaDevices::defaultVideoInput();
    if (cameraDevice.isNull()) return;

    m_camera = new QCamera(cameraDevice, this);
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
    if (!m_screenTimer->isActive()) m_screenTimer->start(intervalMs);
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
    if (!m_settings.videoEnabled) return;
    if (m_screenSharing == enabled) return;
    m_screenSharing = enabled;
    if (!m_capturing) return;
    if (enabled) startScreenCapture();
    else {
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
    if (fps > 0.0f) m_settings.fps = fps;
    if (bitrate > 0) m_settings.bitrate = bitrate;

    if (m_screenTimer && m_screenTimer->isActive()) {
        const int intervalMs = qMax(16, static_cast<int>(1000.0f / qMax(1.0f, m_settings.fps)));
        m_screenTimer->start(intervalMs);
    }
}

void MediaPipeline::setLocalVideoSink(QVideoSink* sink)
{
    // Kept for API compatibility. The QLabel call UI uses localVideoImage.
    m_localSink = sink;
    Q_UNUSED(m_localSink);
}

void MediaPipeline::setRemoteVideoSink(QVideoSink* sink)
{
    if (m_videoDecoder)
        m_videoDecoder->setOutputSink(sink);
}

bool MediaPipeline::isCapturing() const noexcept { return m_capturing; }

bool MediaPipeline::initAudioEncoder()
{
    const QAudioDevice inDevice = QMediaDevices::defaultAudioInput();
    if (inDevice.isNull()) return false;

    QAudioFormat fmt;
    const int rates[] = {48000, 44100, 24000, 16000};
    const int channels[] = {1, 2};
    for (int rate : rates) {
        for (int ch : channels) {
            QAudioFormat candidate;
            candidate.setSampleRate(rate);
            candidate.setChannelCount(ch);
            candidate.setSampleFormat(QAudioFormat::Int16);
            if (inDevice.isFormatSupported(candidate)) {
                fmt = candidate;
                break;
            }
        }
        if (fmt.isValid()) break;
    }

    if (!fmt.isValid()) {
        QAudioFormat preferred = inDevice.preferredFormat();
        if (preferred.isValid()) {
            preferred.setSampleFormat(QAudioFormat::Int16);
            fmt = preferred;
        }
    }
    if (!fmt.isValid()) return false;

    m_audioFormat = fmt;
    m_settings.sampleRate = fmt.sampleRate();
    m_settings.channels   = fmt.channelCount();

    m_audioSrc = new QAudioSource(inDevice, fmt, this);
    m_audioSrc->setBufferSize(qMax<qsizetype>(4096, fmt.bytesForDuration(200 * 1000)));
    m_audioDevice = m_audioSrc->start();
    if (!m_audioDevice) {
        m_audioSrc->deleteLater();
        m_audioSrc = nullptr;
        return false;
    }

    connect(m_audioDevice, &QIODevice::readyRead, this, &MediaPipeline::onAudioData);
    return true;
}

bool MediaPipeline::ensureRemoteAudioSink(const QAudioFormat& fmt)
{
    if (!fmt.isValid()) return false;
    if (m_audioSink && m_audioOut &&
        m_remoteAudioFormat.sampleRate() == fmt.sampleRate() &&
        m_remoteAudioFormat.channelCount() == fmt.channelCount() &&
        m_remoteAudioFormat.sampleFormat() == fmt.sampleFormat())
        return true;

    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink->deleteLater();
        m_audioSink = nullptr;
        m_audioOut = nullptr;
    }

    const QAudioDevice outDevice = QMediaDevices::defaultAudioOutput();
    if (outDevice.isNull()) return false;

    m_remoteAudioFormat = fmt;
    m_audioSink = new QAudioSink(outDevice, fmt, this);
    m_audioOut = m_audioSink->start();
    return m_audioOut != nullptr;
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
    m_audioOut = nullptr;
    m_audioCaptureBuffer.clear();

    {
        QMutexLocker lk(&m_audioEncMutex);
        if (m_opusEnc) { opus_encoder_destroy(m_opusEnc); m_opusEnc = nullptr; }
    }
    {
        QMutexLocker lk(&m_audioDecMutex);
        if (m_opusDec) { opus_decoder_destroy(m_opusDec); m_opusDec = nullptr; }
    }
}

QByteArray MediaPipeline::encodeVideoImage(const QImage& source) const
{
    if (source.isNull()) return {};

    QImage image = source.convertToFormat(QImage::Format_RGB888);
    if (m_settings.width > 0 && m_settings.height > 0) {
        image = image.scaled(m_settings.width, m_settings.height,
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    const int quality = qBound(35, m_settings.bitrate / 16000, 85);
    image.save(&buffer, "JPG", quality);
    if (jpeg.isEmpty()) return {};

    QByteArray packet;
    packet.reserve(4 + jpeg.size());
    packet.append(LocalLanCodec::VIDEO_MAGIC, 4);
    packet.append(jpeg);
    return packet;
}

void MediaPipeline::onVideoFrame(const QVideoFrame& frame)
{
    if (!m_settings.videoEnabled || m_screenSharing) return;

    QImage preview = frame.toImage();
    if (preview.isNull()) return;

    QImage image = preview.convertToFormat(QImage::Format_ARGB32);
    emit localVideoImage(image.copy());

    QByteArray packet = encodeVideoImage(image);
    if (!packet.isEmpty()) emit encodedVideoFrame(packet);
}

void MediaPipeline::onScreenFrameTimer()
{
    if (!m_settings.videoEnabled || !m_screenSharing) return;

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    QImage image = screen->grabWindow(0).toImage().convertToFormat(QImage::Format_ARGB32);
    if (image.isNull()) return;

    QImage preview = image.scaled(m_settings.width, m_settings.height,
                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
    emit localVideoImage(preview.copy());

    QByteArray packet = encodeVideoImage(image);
    if (!packet.isEmpty()) emit encodedVideoFrame(packet);
}

void MediaPipeline::onAudioData()
{
    if (!m_audioDevice || !m_audioFormat.isValid()) return;

    m_audioCaptureBuffer.append(m_audioDevice->readAll());

    const int bytesPerSample = 2;
    const int frameSamples = qMax(160, m_audioFormat.sampleRate() / 50); // 20 ms
    const int frameBytes = frameSamples * qMax(1, m_audioFormat.channelCount()) * bytesPerSample;

    while (m_audioCaptureBuffer.size() >= frameBytes) {
        QByteArray pcm = m_audioCaptureBuffer.left(frameBytes);
        m_audioCaptureBuffer.remove(0, frameBytes);

        if (m_muted || (m_screenSharing && !m_screenAudioEnabled))
            pcm.fill('\0');

        QByteArray packet;
        packet.reserve(4 + 4 + 2 + 1 + pcm.size());
        packet.append(LocalLanCodec::AUDIO_MAGIC, 4);
        LocalLanCodec::putU32(packet, static_cast<quint32>(m_audioFormat.sampleRate()));
        LocalLanCodec::putU16(packet, static_cast<quint16>(m_audioFormat.channelCount()));
        packet.append(char(1)); // Int16 little-endian PCM
        packet.append(pcm);
        emit encodedAudioFrame(packet);
    }
}

void MediaPipeline::onRemoteVideoFrame(const QByteArray& videoPacket)
{
    if (videoPacket.size() >= 4 &&
        std::memcmp(videoPacket.constData(), LocalLanCodec::VIDEO_MAGIC, 4) == 0) {
        QImage img;
        img.loadFromData(reinterpret_cast<const uchar*>(videoPacket.constData() + 4),
                         videoPacket.size() - 4, "JPG");
        if (!img.isNull()) emit remoteVideoImage(img.convertToFormat(QImage::Format_ARGB32));
        return;
    }

    // Compatibility with older H.264 packets if both sides were built from an
    // older release. New builds use JPEG/PCM packets by default.
    if (!m_videoDecoder || !m_decoderThread) return;
    QMetaObject::invokeMethod(m_videoDecoder,
        [this, videoPacket]{ if (m_videoDecoder) m_videoDecoder->decodeNalu(videoPacket); },
        Qt::QueuedConnection);
}

void MediaPipeline::onRemoteAudioFrame(const QByteArray& audioPacket)
{
    if (audioPacket.size() >= 11 &&
        std::memcmp(audioPacket.constData(), LocalLanCodec::AUDIO_MAGIC, 4) == 0) {
        const char* p = audioPacket.constData();
        const int sampleRate = static_cast<int>(LocalLanCodec::getU32(p + 4));
        const int channels   = static_cast<int>(LocalLanCodec::getU16(p + 8));
        const int formatTag  = static_cast<unsigned char>(p[10]);
        if (sampleRate <= 0 || channels <= 0 || formatTag != 1) return;

        QAudioFormat fmt;
        fmt.setSampleRate(sampleRate);
        fmt.setChannelCount(channels);
        fmt.setSampleFormat(QAudioFormat::Int16);
        if (!ensureRemoteAudioSink(fmt) || !m_audioOut) return;

        m_audioOut->write(audioPacket.constData() + 11, audioPacket.size() - 11);
        return;
    }

    // Compatibility with older Opus packets.
    QMutexLocker lk(&m_audioDecMutex);
    if (!m_opusDec || !m_audioOut) return;

    const int frameSamples = qMax(80, m_settings.sampleRate / 100);
    QByteArray pcmBuf(frameSamples * m_settings.channels * 2, Qt::Uninitialized);
    const int samples = opus_decode(
        m_opusDec,
        reinterpret_cast<const unsigned char*>(audioPacket.constData()),
        audioPacket.size(),
        reinterpret_cast<opus_int16*>(pcmBuf.data()),
        frameSamples,
        0);

    if (samples > 0) {
        pcmBuf.resize(samples * m_settings.channels * 2);
        m_audioOut->write(pcmBuf);
    }
}
