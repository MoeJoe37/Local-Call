#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QMutex>
#include <QHash>
#include <QVector>
#include <memory>
#include <vector>
#include <string>
#include <cstdint>

#include <rtc/rtc.hpp>

struct RtcConfig {
    bool   localNetworkOnly{false};
    std::vector<std::string> iceServers;
    quint32 videoSsrc{1};              // Kept for protocol/backward config compatibility.
    quint32 audioSsrc{2};              // Kept for protocol/backward config compatibility.
    int     videoPt{96};               // Kept for protocol/backward config compatibility.
    int     audioPt{111};              // Kept for protocol/backward config compatibility.
    int     videoFps{30};
    int     audioFrameSamples{480};    // 10 ms at 48 kHz for low latency.
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
    struct FrameAssembly {
        quint16 expectedChunks{0};
        quint16 receivedChunks{0};
        int totalBytes{0};
        QVector<QByteArray> chunks;
    };

    void setupCallbacks();
    void ensureOutgoingChannels();
    void configureDataChannel(const std::shared_ptr<rtc::DataChannel>& channel, bool isVideo);
    void sendFrameOnChannel(const std::shared_ptr<rtc::DataChannel>& channel,
                            const QByteArray& frame,
                            quint32& sequence,
                            char mediaTag);
    void handleChannelMessage(bool isVideo, const rtc::binary& payload);
    bool tryAssembleFrame(bool isVideo, const QByteArray& packet, QByteArray& frameOut);
    void applyRemoteCandidate(const QString& candidate, const QString& mid, int mlineIndex);
    void flushPendingCandidates();
    void clearAssemblers();

    QString   m_localId;
    QString   m_remoteId;
    RtcConfig m_cfg;

    std::shared_ptr<rtc::PeerConnection>  m_pc;
    std::shared_ptr<rtc::DataChannel>     m_videoChannel;
    std::shared_ptr<rtc::DataChannel>     m_audioChannel;

    mutable QMutex m_sendMutex;
    mutable QMutex m_recvMutex;
    struct PendingCandidate {
        QString candidate;
        QString mid;
        int     mlineIndex{0};
    };

    bool           m_connected{false};
    bool           m_videoOpen{false};
    bool           m_audioOpen{false};
    bool           m_remoteDescriptionSet{false};
    QVector<PendingCandidate> m_pendingCandidates;
    quint32        m_videoSeq{1};
    quint32        m_audioSeq{1};
    QHash<quint32, FrameAssembly> m_videoFrames;
    QHash<quint32, FrameAssembly> m_audioFrames;
};
