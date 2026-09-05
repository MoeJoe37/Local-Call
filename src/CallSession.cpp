#include "CallSession.h"

#include "MediaPipeline.h"
#include "MediaSettings.h"
#include "MediaTransport.h"
#include "UdpMediaTransport.h"
#include "TcpMediaTransport.h"

#include <QTimer>
#include <QtGlobal>

namespace {
constexpr int MinVideoBitrate = 150'000;
constexpr int BitrateStepMs   = 3000;   // never react faster than this
}

CallSession::CallSession(QString peerIp, CallMode mode, QString localId, QObject* parent)
    : QObject(parent)
    , m_peerIp(std::move(peerIp))
    , m_mode(mode)
    , m_localId(std::move(localId))
{
    qRegisterMetaType<CallStats>("CallStats");

    m_wantVideo = (m_mode != CallMode::Voice);
    if (m_mode == CallMode::VideoScreen) {
        // Screen content is mostly static but needs detail, so trade frame rate
        // for resolution.
        m_videoSize = QSize(1280, 720);
        m_videoFps  = 15.0f;
        m_baseVideoBitrate = 1'500'000;
    }
    m_videoBitrate = m_baseVideoBitrate;

    m_stats.transport = QStringLiteral("—");
}

CallSession::~CallSession()
{
    stop();
}

qint64 CallSession::elapsedMs() const
{
    return m_callClock.isValid() ? m_callClock.elapsed() : 0;
}

QString CallSession::transportName() const
{
    return m_transport ? m_transport->name() : QString();
}

void CallSession::setState(State state)
{
    if (m_state == state) return;
    m_state = state;
    emit stateChanged(m_state);
}

bool CallSession::start()
{
    if (m_state != State::Idle) return true;
    setState(State::Connecting);

    EncoderSettings settings;
    settings.width        = m_videoSize.width();
    settings.height       = m_videoSize.height();
    settings.fps          = m_videoFps;
    settings.bitrate      = m_videoBitrate;
    settings.videoEnabled = m_wantVideo;

    m_pipeline = new MediaPipeline(settings, this);
    if (m_mode == CallMode::VideoScreen) m_pipeline->setScreenSharing(true);
    if (m_mode == CallMode::Voice)       m_pipeline->setCameraEnabled(false);

    if (!m_pipeline->startCapture()) {
        emit failed(tr("No microphone or camera could be opened."));
        setState(State::Ended);
        return false;
    }

    connect(m_pipeline, &MediaPipeline::encodedAudioFrame, this,
            [this](QByteArray packet, bool silence) {
                if (!m_transport) return;
                // Audio is never dropped for congestion — losing voice is far
                // worse than a few extra milliseconds of video latency.
                m_transport->sendPacket(MediaPacket::Tag::Audio,
                                        silence ? MediaPacket::FlagSilence : MediaPacket::FlagNone,
                                        packet, false);
            });

    connect(m_pipeline, &MediaPipeline::encodedVideoFrame, this,
            [this](QByteArray nalu, bool keyframe) {
                if (!m_transport || !m_peerCanReceiveVideo) return;
                m_transport->sendPacket(MediaPacket::Tag::Video,
                                        keyframe ? MediaPacket::FlagKeyframe : MediaPacket::FlagNone,
                                        nalu, !keyframe);
            });

    connect(m_pipeline, &MediaPipeline::remoteVideoImage, this, &CallSession::remoteFrame);
    connect(m_pipeline, &MediaPipeline::localVideoImage,  this, &CallSession::localFrame);
    connect(m_pipeline, &MediaPipeline::keyframeRequestNeeded, this, &CallSession::requestKeyframe);

    m_stats.audioCodec = m_pipeline->audioCodecName();
    m_stats.videoCodec = m_pipeline->videoCodecName();

    // UDP first: it is what a call actually wants. TCP only exists for networks
    // that block it.
    attachTransport(new UdpMediaTransport(m_peerIp, m_localId, this));
    if (!m_transport->start()) {
        switchToTcp(QStringLiteral("UDP socket unavailable"));
    } else {
        m_probeTimer = new QTimer(this);
        m_probeTimer->setInterval(MediaSettings::HelloIntervalMs);
        connect(m_probeTimer, &QTimer::timeout, this, &CallSession::onProbeTick);
        m_probeTimer->start();

        m_probeDeadline = new QTimer(this);
        m_probeDeadline->setSingleShot(true);
        connect(m_probeDeadline, &QTimer::timeout, this, &CallSession::onProbeTimeout);
        m_probeDeadline->start(MediaSettings::UdpProbeTimeoutMs);

        sendHello();
    }

    m_callClock.start();
    return true;
}

void CallSession::stop()
{
    if (m_state == State::Ended && !m_transport && !m_pipeline) return;

    if (m_probeTimer)    { m_probeTimer->stop();    m_probeTimer->deleteLater();    m_probeTimer = nullptr; }
    if (m_probeDeadline) { m_probeDeadline->stop(); m_probeDeadline->deleteLater(); m_probeDeadline = nullptr; }

    if (m_transport) {
        m_transport->stop();
        m_transport->deleteLater();
        m_transport = nullptr;
    }
    if (m_pipeline) {
        m_pipeline->stopCapture();
        m_pipeline->deleteLater();
        m_pipeline = nullptr;
    }
    setState(State::Ended);
}

void CallSession::attachTransport(MediaTransport* transport)
{
    m_transport = transport;
    connect(transport, &MediaTransport::packetReceived, this, &CallSession::onPacket);
    connect(transport, &MediaTransport::statsUpdated,   this, &CallSession::onTransportStats);
    connect(transport, &MediaTransport::failed, this, [this](const QString& reason) {
        if (m_triedTcp) {
            emit failed(reason);
            setState(State::Reconnecting);
        } else {
            switchToTcp(reason);
        }
    });
    m_stats.transport = transport->name();
    emit transportChanged(transport->name());
}

void CallSession::switchToTcp(const QString& reason)
{
    if (m_triedTcp) {
        emit failed(reason);
        setState(State::Reconnecting);
        return;
    }
    m_triedTcp = true;

    if (m_probeTimer)    { m_probeTimer->stop();    m_probeTimer->deleteLater();    m_probeTimer = nullptr; }
    if (m_probeDeadline) { m_probeDeadline->stop(); m_probeDeadline->deleteLater(); m_probeDeadline = nullptr; }

    if (m_transport) {
        m_transport->stop();
        m_transport->deleteLater();
        m_transport = nullptr;
    }

    attachTransport(new TcpMediaTransport(m_peerIp, m_localId, this));
    if (!m_transport->start()) {
        emit failed(tr("Could not open a media connection to %1.").arg(m_peerIp));
        setState(State::Reconnecting);
        return;
    }
    sendHello();
}

void CallSession::onProbeTick()
{
    sendHello();
}

void CallSession::onProbeTimeout()
{
    if (m_peerHelloSeen) return;
    switchToTcp(tr("No UDP media reply from %1").arg(m_peerIp));
}

void CallSession::sendHello()
{
    if (!m_transport) return;

    MediaPacket::Hello hello;
    hello.audioCodec      = MediaPacket::AudioCodec::Opus;
    hello.videoCodec      = (m_pipeline && m_pipeline->hasVideo())
                                ? MediaPacket::VideoCodec::H264
                                : MediaPacket::VideoCodec::None;
    hello.sampleRate      = MediaSettings::OpusSampleRate;
    hello.channels        = 1;
    // Voice calls still decode nothing, so tell the peer not to waste uplink.
    hello.canReceiveVideo = (m_pipeline && m_pipeline->hasVideo()) ? 1 : 0;

    m_transport->sendPacket(MediaPacket::Tag::Hello, MediaPacket::FlagNone,
                            MediaPacket::encodeHello(hello), false);
}

void CallSession::requestKeyframe()
{
    if (!m_transport) return;
    const qint64 now = m_callClock.isValid() ? m_callClock.elapsed() : 0;
    if (m_lastKeyframeRequestMs >= 0 &&
        now - m_lastKeyframeRequestMs < MediaSettings::KeyframeRequestCooldownMs) {
        return;
    }
    m_lastKeyframeRequestMs = now;
    m_transport->sendPacket(MediaPacket::Tag::KeyframeRequest, MediaPacket::FlagNone,
                            QByteArray(), false);
}

void CallSession::onPacket(int tag, quint8 flags, quint32 seq, quint32 timestampMs,
                           QByteArray payload)
{
    Q_UNUSED(timestampMs);
    if (!m_pipeline) return;

    switch (static_cast<MediaPacket::Tag>(tag)) {
    case MediaPacket::Tag::Audio:
        m_pipeline->onRemoteAudioFrame(seq, payload, (flags & MediaPacket::FlagSilence) != 0);
        break;

    case MediaPacket::Tag::Video:
        m_pipeline->onRemoteVideoFrame(payload, (flags & MediaPacket::FlagKeyframe) != 0);
        break;

    case MediaPacket::Tag::Hello: {
        MediaPacket::Hello hello;
        if (!MediaPacket::decodeHello(payload, hello)) break;

        const bool first = !m_peerHelloSeen;
        m_peerHelloSeen  = true;
        m_peerCanReceiveVideo = (hello.canReceiveVideo != 0);

        if (first) {
            if (m_probeTimer)    { m_probeTimer->stop();    m_probeTimer->deleteLater();    m_probeTimer = nullptr; }
            if (m_probeDeadline) { m_probeDeadline->stop(); m_probeDeadline->deleteLater(); m_probeDeadline = nullptr; }
            // Answer once so the peer's own probe stops too, then start sending
            // video with an IDR so its decoder has something to lock onto.
            sendHello();
            m_pipeline->onKeyframeRequested();
            setState(State::Connected);
        }
        break;
    }

    case MediaPacket::Tag::KeyframeRequest:
        m_pipeline->onKeyframeRequested();
        break;

    case MediaPacket::Tag::Ping:
    case MediaPacket::Tag::Pong:
        break;   // handled inside MediaTransport
    }
}

void CallSession::onTransportStats(CallStats transportStats)
{
    m_stats = transportStats;
    if (m_pipeline) {
        m_stats.audioCodec = m_pipeline->audioCodecName();
        m_stats.videoCodec = m_pipeline->videoCodecName();
        const QSize remote  = m_pipeline->remoteVideoSize();
        m_stats.videoWidth  = remote.width();
        m_stats.videoHeight = remote.height();
    }
    if (m_transport) m_stats.transport = m_transport->name();

    applyAdaptiveBitrate(m_stats.lossPercent);
    emit statsUpdated(m_stats);
}

void CallSession::applyAdaptiveBitrate(int lossPercent)
{
    if (!m_pipeline || !m_wantVideo || !m_pipeline->hasVideo()) return;

    const qint64 now = m_callClock.isValid() ? m_callClock.elapsed() : 0;
    if (m_lastBitrateChangeMs >= 0 && now - m_lastBitrateChangeMs < BitrateStepMs) return;

    int target = m_videoBitrate;
    if (lossPercent > 5) {
        // Back off quickly: sustained loss means the link is already saturated.
        target = qMax(MinVideoBitrate, m_videoBitrate * 2 / 3);
    } else if (lossPercent < 2 && m_videoBitrate < m_baseVideoBitrate) {
        // Recover slowly so the stream does not oscillate.
        target = qMin(m_baseVideoBitrate, m_videoBitrate + m_baseVideoBitrate / 8);
    }
    if (target == m_videoBitrate) return;

    m_videoBitrate = target;
    m_lastBitrateChangeMs = now;
    m_pipeline->setVideoTarget(QSize(), m_videoFps, m_videoBitrate);
}

void CallSession::setMuted(bool muted)
{
    if (m_pipeline) m_pipeline->setMuted(muted);
}

void CallSession::setCameraEnabled(bool enabled)
{
    if (m_pipeline) m_pipeline->setCameraEnabled(enabled);
}

void CallSession::setScreenSharing(bool enabled)
{
    if (!m_pipeline) return;
    m_pipeline->setScreenSharing(enabled);
    // The scene changes completely, so the peer needs a fresh IDR immediately.
    m_pipeline->onKeyframeRequested();
}

void CallSession::setScreenAudioEnabled(bool enabled)
{
    if (m_pipeline) m_pipeline->setScreenAudioEnabled(enabled);
}

void CallSession::setVideoTarget(const QSize& size, float fps, int bitrate)
{
    if (size.isValid() && !size.isEmpty()) m_videoSize = size;
    if (fps > 0.0f)  m_videoFps = fps;
    if (bitrate > 0) {
        m_baseVideoBitrate = bitrate;
        m_videoBitrate     = bitrate;
    }
    if (m_pipeline) m_pipeline->setVideoTarget(m_videoSize, m_videoFps, m_videoBitrate);
}
