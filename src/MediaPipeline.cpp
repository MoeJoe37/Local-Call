#include "MediaPipeline.h"
#include <QCamera>
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
#include <QMutexLocker>
#include <cstring>

#include <libyuv.h>

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
    layer.uiProfileIdc            = PRO_CONSTRAINED_BASELINE;
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
    } else if (fmt == Fmt::Format_BGRA8888 || fmt == Fmt::Format_BGR32) {
        const uint8_t* src = frame.bits(0);
        libyuv::ARGBToI420(src, frame.bytesPerLine(0),
                            dstY, w,
                            dstU, w / 2,
                            dstV, w / 2,
                            w, h);
    } else if (fmt == Fmt::Format_RGBA8888 || fmt == Fmt::Format_RGB32) {
        const uint8_t* src = frame.bits(0);
        libyuv::ABGRToI420(src, frame.bytesPerLine(0),
                            dstY, w,
                            dstU, w / 2,
                            dstV, w / 2,
                            w, h);
    } else {
        QImage img = frame.toImage().convertToFormat(QImage::Format_ARGB32);
        libyuv::ARGBToI420(img.constBits(), img.bytesPerLine(),
                            dstY, w,
                            dstU, w / 2,
                            dstV, w / 2,
                            w, h);
    }

    return i420;
}

void VideoEncoderWorker::encodeFrame(const QVideoFrame& inFrame)
{
    if (!m_enc) return;

    QVideoFrame frame(inFrame);
    if (!frame.map(QVideoFrame::ReadOnly)) return;

    const int w = frame.width();
    const int h = frame.height();

    if (w != m_settings.width || h != m_settings.height) {
        frame.unmap();
        return;
    }

    const QByteArray i420 = convertToI420(frame);
    frame.unmap();

    const uint8_t* yPtr = reinterpret_cast<const uint8_t*>(i420.constData());
    const uint8_t* uPtr = yPtr + w * h;
    const uint8_t* vPtr = uPtr + (w / 2) * (h / 2);

    SSourcePicture pic;
    std::memset(&pic, 0, sizeof(pic));
    pic.iPicWidth      = w;
    pic.iPicHeight     = h;
    pic.iColorFormat   = videoFormatI420;
    pic.iStride[0]     = w;
    pic.iStride[1]     = w / 2;
    pic.iStride[2]     = w / 2;
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
    dParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AUTO;
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

    QMutexLocker lk(&m_sinkMutex);
    if (!m_sink) return;

    QVideoFrameFormat fmt(QSize(w, h), QVideoFrameFormat::Format_YUV420P);
    QVideoFrame frame(fmt);

    if (!frame.map(QVideoFrame::WriteOnly)) return;

    uint8_t* dstY = frame.bits(0);
    uint8_t* dstU = frame.bits(1);
    uint8_t* dstV = frame.bits(2);

    libyuv::I420Copy(pData[0], strideY,
                     pData[1], strideU,
                     pData[2], strideU,
                     dstY, frame.bytesPerLine(0),
                     dstU, frame.bytesPerLine(1),
                     dstV, frame.bytesPerLine(2),
                     w, h);
    frame.unmap();

    m_sink->setVideoFrame(frame);
}

MediaPipeline::MediaPipeline(const EncoderSettings& settings, QObject* parent)
    : QObject(parent), m_settings(settings)
{
    m_captureSink = new QVideoSink(this);
    connect(m_captureSink, &QVideoSink::videoFrameChanged,
            this,          &MediaPipeline::onVideoFrame);
}

MediaPipeline::~MediaPipeline()
{
    stopCapture();
}

bool MediaPipeline::startCapture()
{
    if (m_capturing) return true;

    m_encoderThread = new QThread(this);
    m_decoderThread = new QThread(this);

    m_videoEncoder = new VideoEncoderWorker(m_settings);
    m_videoDecoder = new VideoDecoderWorker();

    m_videoEncoder->moveToThread(m_encoderThread);
    m_videoDecoder->moveToThread(m_decoderThread);

    connect(m_encoderThread, &QThread::started,
            m_videoEncoder,  [this]{ m_videoEncoder->init(); });
    connect(m_decoderThread, &QThread::started,
            m_videoDecoder,  [this]{
                m_videoDecoder->init();
                QMutexLocker lk(&m_audioDecMutex);
                m_videoDecoder->setOutputSink(m_videoDecoder ? nullptr : nullptr);
            });

    connect(m_videoEncoder, &VideoEncoderWorker::encodedNalu,
            this,           &MediaPipeline::encodedVideoFrame,
            Qt::QueuedConnection);

    m_encoderThread->start();
    m_decoderThread->start();

    if (m_localSink) {
        m_videoDecoder->setOutputSink(nullptr);
    }

    m_camera = new QCamera(QMediaDevices::defaultVideoInput(), this);
    m_captureSession = new QMediaCaptureSession(this);
    m_captureSession->setCamera(m_camera);
    m_captureSession->setVideoSink(m_captureSink);

    if (m_localSink)
        m_captureSession->setVideoSink(m_localSink);

    m_camera->start();

    if (!initAudioEncoder()) return false;

    m_capturing = true;
    return true;
}

void MediaPipeline::stopCapture()
{
    if (!m_capturing) return;
    m_capturing = false;

    if (m_camera) { m_camera->stop(); }

    cleanupAudio();

    if (m_encoderThread) {
        m_encoderThread->quit();
        m_encoderThread->wait(3000);
        m_videoEncoder->deleteLater();
        m_encoderThread->deleteLater();
        m_videoEncoder  = nullptr;
        m_encoderThread = nullptr;
    }

    if (m_decoderThread) {
        m_decoderThread->quit();
        m_decoderThread->wait(3000);
        m_videoDecoder->deleteLater();
        m_decoderThread->deleteLater();
        m_videoDecoder  = nullptr;
        m_decoderThread = nullptr;
    }
}

void MediaPipeline::setLocalVideoSink(QVideoSink* sink)
{
    m_localSink = sink;
    if (m_captureSession)
        m_captureSession->setVideoSink(sink ? sink : m_captureSink);
}

void MediaPipeline::setRemoteVideoSink(QVideoSink* sink)
{
    if (m_videoDecoder)
        m_videoDecoder->setOutputSink(sink);
}

bool MediaPipeline::isCapturing() const noexcept { return m_capturing; }

bool MediaPipeline::initAudioEncoder()
{
    QAudioFormat fmt;
    fmt.setSampleRate(m_settings.sampleRate);
    fmt.setChannelCount(m_settings.channels);
    fmt.setSampleFormat(QAudioFormat::Int16);

    m_audioSrc = new QAudioSource(QMediaDevices::defaultAudioInput(), fmt, this);
    m_audioDevice = m_audioSrc->start();
    if (!m_audioDevice) return false;

    connect(m_audioDevice, &QIODevice::readyRead, this, &MediaPipeline::onAudioData);

    int err = 0;
    m_opusEnc = opus_encoder_create(
        m_settings.sampleRate, m_settings.channels, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !m_opusEnc) return false;

    opus_encoder_ctl(m_opusEnc, OPUS_SET_BITRATE(m_settings.opusBitrate));
    opus_encoder_ctl(m_opusEnc, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(m_opusEnc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

    int decErr = 0;
    m_opusDec = opus_decoder_create(m_settings.sampleRate, m_settings.channels, &decErr);

    QAudioFormat outFmt = fmt;
    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), outFmt, this);
    m_audioOut  = m_audioSink->start();

    return decErr == OPUS_OK && m_opusDec;
}

void MediaPipeline::cleanupAudio()
{
    if (m_audioSrc) { m_audioSrc->stop(); m_audioSrc = nullptr; }
    m_audioDevice = nullptr;

    if (m_audioSink) { m_audioSink->stop(); m_audioSink = nullptr; }
    m_audioOut = nullptr;

    {
        QMutexLocker lk(&m_audioEncMutex);
        if (m_opusEnc) { opus_encoder_destroy(m_opusEnc); m_opusEnc = nullptr; }
    }
    {
        QMutexLocker lk(&m_audioDecMutex);
        if (m_opusDec) { opus_decoder_destroy(m_opusDec); m_opusDec = nullptr; }
    }
}

void MediaPipeline::onVideoFrame(const QVideoFrame& frame)
{
    if (!m_videoEncoder || !m_encoderThread) return;
    QMetaObject::invokeMethod(m_videoEncoder,
        [this, frame]{ m_videoEncoder->encodeFrame(frame); },
        Qt::QueuedConnection);
}

void MediaPipeline::onAudioData()
{
    if (!m_audioDevice) return;

    m_audioCaptureBuffer.append(m_audioDevice->readAll());

    const int frameBytes = OPUS_FRAME_SAMPLES * m_settings.channels * 2;

    while (m_audioCaptureBuffer.size() >= frameBytes) {
        const QByteArray chunk = m_audioCaptureBuffer.left(frameBytes);
        m_audioCaptureBuffer.remove(0, frameBytes);

        QMutexLocker lk(&m_audioEncMutex);
        if (!m_opusEnc) break;

        unsigned char encoded[4096];
        const opus_int16* pcm =
            reinterpret_cast<const opus_int16*>(chunk.constData());

        const int bytes = opus_encode(
            m_opusEnc, pcm, OPUS_FRAME_SAMPLES,
            encoded, static_cast<opus_int32>(sizeof(encoded)));

        if (bytes > 0)
            emit encodedAudioFrame(QByteArray(reinterpret_cast<const char*>(encoded), bytes));
    }
}

void MediaPipeline::onRemoteVideoFrame(const QByteArray& h264AnnexB)
{
    if (!m_videoDecoder || !m_decoderThread) return;
    QMetaObject::invokeMethod(m_videoDecoder,
        [this, h264AnnexB]{ m_videoDecoder->decodeNalu(h264AnnexB); },
        Qt::QueuedConnection);
}

void MediaPipeline::onRemoteAudioFrame(const QByteArray& opusPacket)
{
    QMutexLocker lk(&m_audioDecMutex);
    if (!m_opusDec || !m_audioOut) return;

    QByteArray pcmBuf(OPUS_FRAME_SAMPLES * m_settings.channels * 2, Qt::Uninitialized);
    const int samples = opus_decode(
        m_opusDec,
        reinterpret_cast<const unsigned char*>(opusPacket.constData()),
        opusPacket.size(),
        reinterpret_cast<opus_int16*>(pcmBuf.data()),
        OPUS_FRAME_SAMPLES,
        0);

    if (samples > 0) {
        pcmBuf.resize(samples * m_settings.channels * 2);
        m_audioOut->write(pcmBuf);
    }
}
