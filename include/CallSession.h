#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QImage>
#include <QSize>
#include <QString>

#include "CallStats.h"
#include "CallTypes.h"
#include "MediaPacket.h"

class MediaTransport;
class MediaPipeline;
class QTimer;

/// Everything a call does except draw itself.
///
/// The session owns the media pipeline and the transport, picks between UDP and
/// TCP, and translates encoded frames into LCM3 packets and back. Keeping this
/// out of CallWindow means the call can be reasoned about (and its transport
/// swapped mid-call) without touching a single widget.
class CallSession : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Connecting, Connected, Reconnecting, Ended };
    Q_ENUM(State)

    CallSession(QString peerIp, CallMode mode, QString localId, QObject* parent = nullptr);
    ~CallSession() override;

    bool start();
    void stop();

    State   state()          const noexcept { return m_state; }
    QString transportName()  const;
    CallStats stats()        const { return m_stats; }
    qint64  elapsedMs()      const;

    void setMuted(bool muted);
    void setCameraEnabled(bool enabled);
    void setScreenSharing(bool enabled);
    void setScreenAudioEnabled(bool enabled);
    /// A size of {0,0} means "keep the current resolution".
    void setVideoTarget(const QSize& size, float fps, int bitrate);

signals:
    void stateChanged(CallSession::State state);
    void transportChanged(QString name);
    void statsUpdated(CallStats stats);
    void remoteFrame(QImage frame);
    void localFrame(QImage frame);
    void failed(QString reason);

private slots:
    void onPacket(int tag, quint8 flags, quint32 seq, quint32 timestampMs, QByteArray payload);
    void onTransportStats(CallStats transportStats);
    void onProbeTick();
    void onProbeTimeout();

private:
    void setState(State state);
    void attachTransport(MediaTransport* transport);
    void switchToTcp(const QString& reason);
    void sendHello();
    void requestKeyframe();
    void applyAdaptiveBitrate(int lossPercent);

    QString  m_peerIp;
    CallMode m_mode;
    QString  m_localId;

    MediaPipeline*  m_pipeline{nullptr};
    MediaTransport* m_transport{nullptr};

    QTimer* m_probeTimer{nullptr};     // repeats Hello until the peer answers
    QTimer* m_probeDeadline{nullptr};  // gives up on UDP and falls back to TCP

    State  m_state{State::Idle};
    bool   m_triedTcp{false};
    bool   m_peerHelloSeen{false};
    bool   m_wantVideo{false};
    bool   m_peerCanReceiveVideo{false};

    CallStats     m_stats;
    QElapsedTimer m_callClock;
    qint64        m_lastKeyframeRequestMs{-1};
    int           m_videoBitrate{800'000};
    int           m_baseVideoBitrate{800'000};
    QSize         m_videoSize{640, 360};
    float         m_videoFps{30.0f};
    qint64        m_lastBitrateChangeMs{-1};
};
