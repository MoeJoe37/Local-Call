#pragma once

#include <QDialog>
#include <QImage>
#include <QString>
#include <QWidget>

#include "CallSession.h"
#include "CallStats.h"
#include "CallTypes.h"
#include "SigMsg.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;

/// Video output that paints its frame directly.
///
/// The old call window pushed every frame through QLabel::setPixmap with a
/// smooth-scaled QPixmap, which allocated a full-size pixmap 30 times a second.
/// Painting in paintEvent scales only what is actually shown, and only when the
/// widget is actually visible.
class VideoSurface : public QWidget {
    Q_OBJECT
public:
    explicit VideoSurface(QWidget* parent = nullptr);

    void setFrame(const QImage& frame);
    void clearFrame();
    void setPlaceholder(const QString& text);
    void setFastScaling(bool fast) { m_fastScaling = fast; }
    bool hasFrame() const { return !m_frame.isNull(); }

signals:
    void doubleClicked();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    QImage  m_frame;
    QString m_placeholder;
    bool    m_fastScaling{false};
};

/// The call UI. All media work lives in CallSession; this class only shows it.
class CallWindow : public QDialog {
    Q_OBJECT
public:
    CallWindow(const QString& peerIp, const QString& peerName,
               CallMode mode, const QString& myId, const QString& myName,
               bool initiator = false,
               QWidget* parent = nullptr);
    ~CallWindow() override;

    void doClose();
    /// Kept so signalling stays source-compatible; the LAN media path does not
    /// use ICE, and WebRTC signalling is ignored unless it is compiled in.
    void handleRtcSignal(const SigMsg& msg);

protected:
    void closeEvent(QCloseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

signals:
    void hangupRequested();
    void rtcSignalReady(SigMsg msg);

private slots:
    void onMute();
    void onCamera();
    void onScreen();
    void onHangup();
    void onToggleStats();
    void onScreenAudioChanged(bool on);
    void onQualityChanged();
    void onSessionState(CallSession::State state);
    void onStats(CallStats stats);
    void onRemoteFrame(QImage frame);
    void onLocalFrame(QImage frame);
    void onTick();
    void onHideControls();

private:
    void buildUi();
    void startSession();
    void layoutSelfView();
    void setFullScreenMode(bool on);
    void showControls();
    void updateButtonStates();

    QString  m_peerIp;
    QString  m_peerName;
    CallMode m_mode;
    QString  m_myId;
    QString  m_myName;
    bool     m_initiator{false};

    CallSession* m_session{nullptr};

    VideoSurface* m_remoteView{nullptr};
    VideoSurface* m_selfView{nullptr};

    QWidget* m_header{nullptr};
    QLabel*  m_titleLabel{nullptr};
    QLabel*  m_secureLabel{nullptr};
    QLabel*  m_timerLabel{nullptr};
    QLabel*  m_transportBadge{nullptr};
    QLabel*  m_statusLabel{nullptr};
    QLabel*  m_statsOverlay{nullptr};

    QWidget*     m_controls{nullptr};
    QPushButton* m_btnMute{nullptr};
    QPushButton* m_btnCamera{nullptr};
    QPushButton* m_btnScreen{nullptr};
    QPushButton* m_btnStats{nullptr};
    QPushButton* m_btnHangup{nullptr};
    QCheckBox*   m_chkScreenAudio{nullptr};
    QComboBox*   m_cmbRes{nullptr};
    QComboBox*   m_cmbFps{nullptr};
    QWidget*     m_qualityPanel{nullptr};

    QTimer* m_tickTimer{nullptr};
    QTimer* m_hideTimer{nullptr};

    bool m_muted{false};
    bool m_cameraOn{false};
    bool m_screenOn{false};
    bool m_closing{false};
    bool m_fullScreen{false};
};
