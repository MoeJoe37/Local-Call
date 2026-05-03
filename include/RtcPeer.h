#pragma once
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QMutex>
#include <memory>
#include <vector>
#include <string>

#include <rtc/rtc.hpp>
#include <rtc/h264rtpdepacketizer.hpp>
#include <rtc/h264rtppacketizer.hpp>
#include <rtc/rtpdepacketizer.hpp>
#include <rtc/rtppacketizer.hpp>

struct RtcConfig {
    bool   localNetworkOnly{false};
    std::vector<std::string> iceServers;
    quint32 videoSsrc{1};
    quint32 audioSsrc{2};
    int     videoPt{96};
    int     audioPt{111};
};

class RtcPeer : public QObject {
    Q_OBJECT
public:
    explicit RtcPeer(const QString& localId,
                      const QString& remoteId,
                      const RtcConfig& cfg = {},
                      QObject* parent = nullptr);
    ~RtcPeer() override;

    void createOffer();
    void setRemoteDescription(const QString& type, const QString& sdp);
    void addRemoteCandidate(const QString& candidate, const QString& mid, int mlineIndex);
    void close();

    bool isConnected() const noexcept;
    QString localId()  const noexcept;
    QString remoteId() const noexcept;

public slots:
    void sendVideoFrame(const QByteArray& h264AnnexB);
    void sendAudioFrame(const QByteArray& opusPacket);

signals:
    void localDescriptionReady(QString type, QString sdp);
    void localCandidateReady(QString candidate, QString mid, int mlineIndex);
    void remoteVideoFrame(QByteArray h264AnnexB);
    void remoteAudioFrame(QByteArray opusPacket);
    void connected();
    void disconnected();
    void failed();

private:
    void setupCallbacks();
    void setupTracks();

    QString   m_localId;
    QString   m_remoteId;
    RtcConfig m_cfg;

    std::shared_ptr<rtc::PeerConnection>  m_pc;
    std::shared_ptr<rtc::Track>           m_videoTrack;
    std::shared_ptr<rtc::Track>           m_audioTrack;

    mutable QMutex m_sendMutex;
    bool           m_connected{false};
};
