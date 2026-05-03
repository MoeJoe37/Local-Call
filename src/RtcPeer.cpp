#include "RtcPeer.h"
#include <QMutexLocker>
#include <QPointer>
#include <QByteArray>

RtcPeer::RtcPeer(const QString&   localId,
                  const QString&   remoteId,
                  const RtcConfig& cfg,
                  QObject*         parent)
    : QObject(parent), m_localId(localId), m_remoteId(remoteId), m_cfg(cfg)
{
    rtc::Configuration config;
    if (!cfg.localNetworkOnly) {
        if (cfg.iceServers.empty()) {
            config.iceServers.emplace_back("stun:stun.l.google.com:19302");
        } else {
            for (const auto& server : cfg.iceServers)
                config.iceServers.emplace_back(server);
        }
    }

    m_pc = std::make_shared<rtc::PeerConnection>(config);
    setupCallbacks();
    setupTracks();
}

RtcPeer::~RtcPeer()
{
    close();
}

void RtcPeer::setupCallbacks()
{
    QPointer<RtcPeer> safeThis = this;

    m_pc->onLocalDescription([safeThis](rtc::Description desc) {
        if (!safeThis) return;
        QString type = QString::fromStdString(desc.typeString());
        QString sdp  = QString::fromStdString(std::string(desc));
        QMetaObject::invokeMethod(safeThis, [safeThis, type, sdp]() {
            if (safeThis) emit safeThis->localDescriptionReady(type, sdp);
        }, Qt::QueuedConnection);
    });

    m_pc->onLocalCandidate([safeThis](rtc::Candidate cand) {
        if (!safeThis) return;
        QString candidate = QString::fromStdString(std::string(cand));
        QString mid       = QString::fromStdString(cand.mid());
        QMetaObject::invokeMethod(safeThis, [safeThis, candidate, mid]() {
            if (safeThis) emit safeThis->localCandidateReady(candidate, mid, 0);
        }, Qt::QueuedConnection);
    });

    m_pc->onStateChange([safeThis](rtc::PeerConnection::State state) {
        if (!safeThis) return;
        QMetaObject::invokeMethod(safeThis, [safeThis, state]() {
            if (!safeThis) return;
            if (state == rtc::PeerConnection::State::Connected) {
                safeThis->m_connected = true;
                emit safeThis->connected();
            } else if (state == rtc::PeerConnection::State::Disconnected ||
                       state == rtc::PeerConnection::State::Closed) {
                safeThis->m_connected = false;
                emit safeThis->disconnected();
            } else if (state == rtc::PeerConnection::State::Failed) {
                safeThis->m_connected = false;
                emit safeThis->failed();
            }
        }, Qt::QueuedConnection);
    });

    m_pc->onTrack([safeThis](std::shared_ptr<rtc::Track> track) {
        if (!safeThis) return;

        const std::string mid = track->description().mid();
        const bool isVideo    = (mid == "video");
        const bool isAudio    = (mid == "audio");

        if (!isVideo && !isAudio) return;

        if (isVideo) {
            auto depack = std::make_shared<rtc::H264RtpDepacketizer>(
                rtc::H264RtpDepacketizer::Separator::StartSequence);
            track->setMediaHandler(depack);
        } else {
            auto depack = std::make_shared<rtc::RtpDepacketizer>();
            track->setMediaHandler(depack);
        }

        track->onMessage([safeThis, isVideo](rtc::binary data) {
            if (!safeThis) return;
            QByteArray bytes(reinterpret_cast<const char*>(data.data()),
                             static_cast<int>(data.size()));
            QMetaObject::invokeMethod(safeThis, [safeThis, bytes, isVideo]() {
                if (!safeThis) return;
                if (isVideo)
                    emit safeThis->remoteVideoFrame(bytes);
                else
                    emit safeThis->remoteAudioFrame(bytes);
            }, Qt::QueuedConnection);
        });
    });
}

void RtcPeer::setupTracks()
{
    {
        rtc::Description::Video video("video", rtc::Description::Direction::SendRecv);
        video.addH264Codec(m_cfg.videoPt,
                            std::nullopt,
                            "profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1");
        m_videoTrack = m_pc->addTrack(video);

        auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
            m_cfg.videoSsrc, "H264",
            m_cfg.videoPt,
            rtc::H264RtpPacketizer::defaultClockRate);

        auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
            rtc::H264RtpPacketizer::Separator::StartSequence, rtpConfig);

        m_videoTrack->setMediaHandler(packetizer);
    }

    {
        rtc::Description::Audio audio("audio", rtc::Description::Direction::SendRecv);
        audio.addOpusCodec(m_cfg.audioPt);
        m_audioTrack = m_pc->addTrack(audio);

        auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
            m_cfg.audioSsrc, "OPUS",
            m_cfg.audioPt,
            48000, 2);

        auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtpConfig);
        m_audioTrack->setMediaHandler(packetizer);
    }
}

void RtcPeer::createOffer()
{
    m_pc->setLocalDescription(rtc::Description::Type::Offer);
}

void RtcPeer::setRemoteDescription(const QString& type, const QString& sdp)
{
    rtc::Description::Type descType = rtc::Description::Type::Unspec;
    if (type == QLatin1String("offer"))  descType = rtc::Description::Type::Offer;
    if (type == QLatin1String("answer")) descType = rtc::Description::Type::Answer;

    m_pc->setRemoteDescription(rtc::Description(sdp.toStdString(), descType));

    if (descType == rtc::Description::Type::Offer)
        m_pc->setLocalDescription(rtc::Description::Type::Answer);
}

void RtcPeer::addRemoteCandidate(const QString& candidate,
                                   const QString& mid,
                                   int            /*mlineIndex*/)
{
    try {
        m_pc->addRemoteCandidate(
            rtc::Candidate(candidate.toStdString(), mid.toStdString()));
    } catch (...) {}
}

void RtcPeer::close()
{
    if (m_pc) {
        try { m_pc->close(); } catch (...) {}
        m_pc.reset();
    }
    m_videoTrack.reset();
    m_audioTrack.reset();
    m_connected = false;
}

bool RtcPeer::isConnected() const noexcept  { return m_connected; }
QString RtcPeer::localId()  const noexcept  { return m_localId;   }
QString RtcPeer::remoteId() const noexcept  { return m_remoteId;  }

void RtcPeer::sendVideoFrame(const QByteArray& h264AnnexB)
{
    if (!m_videoTrack || !m_connected) return;
    QMutexLocker lk(&m_sendMutex);
    try {
        m_videoTrack->send(
            rtc::binary(
                reinterpret_cast<const std::byte*>(h264AnnexB.constData()),
                reinterpret_cast<const std::byte*>(h264AnnexB.constData() + h264AnnexB.size())));
    } catch (...) {}
}

void RtcPeer::sendAudioFrame(const QByteArray& opusPacket)
{
    if (!m_audioTrack || !m_connected) return;
    QMutexLocker lk(&m_sendMutex);
    try {
        m_audioTrack->send(
            rtc::binary(
                reinterpret_cast<const std::byte*>(opusPacket.constData()),
                reinterpret_cast<const std::byte*>(opusPacket.constData() + opusPacket.size())));
    } catch (...) {}
}
