#pragma once

#include <QDialog>
#include <QCloseEvent>
#include <QString>
#include <QList>
#include <QComboBox>
#include <QLabel>
#include <QImage>
#include <QUuid>
#include "CallTypes.h"
#include "SigMsg.h"

#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
#include "MediaWorker.h"
#endif
#ifdef HAS_WEBRTC
#include "RtcPeer.h"
#include "MediaPipeline.h"
#endif

class QPushButton;
class QCheckBox;

class CallWindow : public QDialog {
    Q_OBJECT
public:
    CallWindow(const QString& peerIp, const QString& peerName,
               CallMode mode, const QString& myId, const QString& myName,
               bool initiator = false,
               QWidget* parent = nullptr);
    ~CallWindow() override;

    void doClose();
    void handleRtcSignal(const SigMsg& msg);

protected:
    void closeEvent(QCloseEvent* e) override;

signals:
    void hangupRequested();
    void rtcSignalReady(SigMsg msg);

private slots:
    void onMute();
    void onCamera();
    void onScreen();
    void onHangup();
    void onScreenAudioChanged(int state);
    void onQualityChanged();
    void onMediaConnected();
    void onRemoteFrame(QImage frame);
    void onLocalFrame(QImage frame);

private:
    void startMedia();
    void stopMedia();
    SigMsg makeRtcSignal(const std::string& type) const;

    QString       m_peerIp;
    QString       m_peerName;
    CallMode      m_mode;
    QString       m_myId;
    QString       m_myName;
    bool          m_initiator = false;
    QString       m_rtcSessionId;

#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
    QList<MediaWorker*> m_workers;
    MediaWorker*        m_audioSender = nullptr;
    MediaWorker*        m_videoSender = nullptr;
#endif

#ifdef HAS_WEBRTC
    RtcPeer*       m_rtcPeer = nullptr;
    MediaPipeline* m_pipeline = nullptr;
#endif

    // UI elements
    QLabel*      m_remoteVideo    = nullptr;
    QLabel*      m_localVideo     = nullptr;
    QLabel*      m_statusLabel    = nullptr;
    QLabel*      m_overlayLabel   = nullptr;
    QPushButton* m_btnMute        = nullptr;
    QPushButton* m_btnCamera      = nullptr;
    QPushButton* m_btnScreen      = nullptr;
    QCheckBox*   m_chkScreenAudio = nullptr;
    QWidget*     m_screenAudioPanel = nullptr;

    QWidget*   m_qualityPanel = nullptr;
    QComboBox* m_cmbRes       = nullptr;
    QComboBox* m_cmbFps       = nullptr;
    QComboBox* m_cmbQuality   = nullptr;
    QLabel*    m_lblQuality   = nullptr;

    bool m_muted    = false;
    bool m_screenOn = false;
    bool m_cameraOn = true;
    bool m_closing  = false;
};
