#pragma once
#include <QDialog>
#include <QCloseEvent>
#include <QString>
#include <QList>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include "CallTypes.h"
#include "MediaWorker.h"

class QPushButton;
class QCheckBox;

class CallWindow : public QDialog {
    Q_OBJECT
public:
    CallWindow(const QString& peerIp, const QString& peerName,
               CallMode mode, const QString& myId, const QString& myName,
               QWidget* parent = nullptr);
    ~CallWindow();

    void doClose();

protected:
    void closeEvent(QCloseEvent* e) override;

signals:
    void hangupRequested();

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

    QString       m_peerIp;
    QString       m_peerName;
    CallMode      m_mode;
    QString       m_myId;
    QString       m_myName;

    QList<MediaWorker*> m_workers;
    MediaWorker*        m_audioSender = nullptr;
    MediaWorker*        m_videoSender = nullptr;

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

    // Quality controls (visible only when video is active)
    QWidget*   m_qualityPanel = nullptr;
    QComboBox* m_cmbRes       = nullptr;
    QComboBox* m_cmbFps       = nullptr;
    QSlider*   m_sldQuality   = nullptr;
    QLabel*    m_lblQuality   = nullptr;

    bool m_muted    = false;
    bool m_screenOn = false;
    bool m_cameraOn = true;
    bool m_closing  = false;
};
