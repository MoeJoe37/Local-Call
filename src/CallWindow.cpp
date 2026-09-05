#include "CallWindow.h"

#include "MediaSettings.h"
#include "UiTheme.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {

/// Display order for the resolution picker. MediaSettings::Resolutions is a
/// std::map, so iterating it would sort "1080p" before "144p".
const char* const kResolutionOrder[] = {"144p", "240p", "360p", "480p", "720p", "1080p", "Source"};

QString formatDuration(qint64 ms)
{
    const qint64 total = ms / 1000;
    const qint64 h = total / 3600;
    const qint64 m = (total / 60) % 60;
    const qint64 s = total % 60;
    return h > 0 ? QString::asprintf("%lld:%02lld:%02lld", h, m, s)
                 : QString::asprintf("%02lld:%02lld", m, s);
}

/// Rough bitrate budget for a picture size and frame rate. Keeps 360p30 near
/// 600 kbit/s and 720p30 near 2 Mbit/s, which a LAN link handles comfortably
/// while leaving headroom for audio.
int bitrateFor(const QSize& size, float fps)
{
    const qint64 pixels = qint64(size.width()) * size.height();
    const qint64 raw    = qint64(pixels * qMax(1.0f, fps)) / 12;
    return int(qBound<qint64>(200000LL, raw, 4000000LL));
}

}  // namespace

// ── VideoSurface ─────────────────────────────────────────────────────────────

VideoSurface::VideoSurface(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(160, 90);
}

void VideoSurface::setFrame(const QImage& frame)
{
    m_frame = frame;
    update();
}

void VideoSurface::clearFrame()
{
    m_frame = QImage();
    update();
}

void VideoSurface::setPlaceholder(const QString& text)
{
    m_placeholder = text;
    update();
}

void VideoSurface::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x0B, 0x0B, 0x12));

    if (m_frame.isNull()) {
        if (m_placeholder.isEmpty()) return;
        p.setPen(QColor(0x6C, 0x70, 0x86));
        QFont f = p.font();
        f.setPointSizeF(qMax(9.0, height() / 26.0));
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, m_placeholder);
        return;
    }

    // Scale only to the visible size, and only when we actually paint. The
    // self-view is small and updated constantly, so it uses fast scaling.
    p.setRenderHint(QPainter::SmoothPixmapTransform, !m_fastScaling);
    const QSize scaled = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - scaled.width()) / 2,
                              (height() - scaled.height()) / 2), scaled);
    p.drawImage(target, m_frame);
}

void VideoSurface::mouseDoubleClickEvent(QMouseEvent* e)
{
    emit doubleClicked();
    QWidget::mouseDoubleClickEvent(e);
}

// ── CallWindow ───────────────────────────────────────────────────────────────

CallWindow::CallWindow(const QString& peerIp, const QString& peerName,
                       CallMode mode, const QString& myId, const QString& myName,
                       bool initiator, QWidget* parent)
    : QDialog(parent)
    , m_peerIp(peerIp)
    , m_peerName(peerName)
    , m_mode(mode)
    , m_myId(myId)
    , m_myName(myName)
    , m_initiator(initiator)
{
    setObjectName("callWindow");
    setWindowTitle(tr("Call — %1").arg(peerName));
    setWindowFlag(Qt::Window, true);
    setMinimumSize(560, 420);
    resize(m_mode == CallMode::Voice ? QSize(560, 420) : QSize(960, 640));
    setMouseTracking(true);

    // A voice call has no camera to begin with. The old window started with
    // m_cameraOn = true regardless of mode, so the button lied about its state.
    m_cameraOn = (m_mode == CallMode::VideoCamera);
    m_screenOn = (m_mode == CallMode::VideoScreen);

    buildUi();
    startSession();
}

CallWindow::~CallWindow() = default;

void CallWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header ───────────────────────────────────────────────────────────────
    m_header = new QWidget(this);
    m_header->setObjectName("callHeader");
    auto* headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(16, 10, 16, 10);
    headerLayout->setSpacing(10);

    m_titleLabel = new QLabel(m_peerName, m_header);
    m_titleLabel->setObjectName("callPeerName");

    m_secureLabel = new QLabel(tr("Encrypted"), m_header);
    m_secureLabel->setObjectName("secureBadge");
    m_secureLabel->setToolTip(tr("Signalling is signed with your Ed25519 device key."));

    m_transportBadge = new QLabel(tr("connecting"), m_header);
    m_transportBadge->setObjectName("transportBadge");

    m_timerLabel = new QLabel(QStringLiteral("00:00"), m_header);
    m_timerLabel->setObjectName("callTimer");

    headerLayout->addWidget(m_titleLabel);
    headerLayout->addWidget(m_secureLabel);
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_transportBadge);
    headerLayout->addWidget(m_timerLabel);
    root->addWidget(m_header);

    // ── Video stage ──────────────────────────────────────────────────────────
    m_remoteView = new VideoSurface(this);
    m_remoteView->setObjectName("videoStage");
    m_remoteView->setMouseTracking(true);
    m_remoteView->setPlaceholder(m_mode == CallMode::Voice
                                     ? tr("%1\nVoice call").arg(m_peerName)
                                     : tr("Waiting for video from %1…").arg(m_peerName));
    connect(m_remoteView, &VideoSurface::doubleClicked, this,
            [this] { setFullScreenMode(!m_fullScreen); });
    root->addWidget(m_remoteView, 1);

    m_selfView = new VideoSurface(m_remoteView);
    m_selfView->setFastScaling(true);
    m_selfView->setMouseTracking(true);
    m_selfView->setVisible(m_mode != CallMode::Voice);

    m_statsOverlay = new QLabel(m_remoteView);
    m_statsOverlay->setObjectName("statsOverlay");
    m_statsOverlay->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_statsOverlay->setVisible(false);

    m_statusLabel = new QLabel(tr("Connecting…"), m_remoteView);
    m_statusLabel->setObjectName("callStatus");
    m_statusLabel->setAlignment(Qt::AlignCenter);

    // ── Controls ─────────────────────────────────────────────────────────────
    m_controls = new QWidget(this);
    m_controls->setObjectName("callControls");
    auto* controlsLayout = new QHBoxLayout(m_controls);
    controlsLayout->setContentsMargins(16, 12, 16, 12);
    controlsLayout->setSpacing(12);

    m_qualityPanel = new QWidget(m_controls);
    m_qualityPanel->setObjectName("qualityPanel");
    auto* qualityLayout = new QHBoxLayout(m_qualityPanel);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    qualityLayout->setSpacing(6);

    m_cmbRes = new QComboBox(m_qualityPanel);
    for (const char* name : kResolutionOrder) m_cmbRes->addItem(QString::fromLatin1(name));
    m_cmbRes->setCurrentText(m_mode == CallMode::VideoScreen ? QStringLiteral("720p")
                                                             : QStringLiteral("360p"));

    m_cmbFps = new QComboBox(m_qualityPanel);
    for (const std::string& fps : MediaSettings::FpsOptions)
        m_cmbFps->addItem(QString::fromStdString(fps));
    m_cmbFps->setCurrentText(QStringLiteral("30"));

    qualityLayout->addWidget(new QLabel(tr("Quality"), m_qualityPanel));
    qualityLayout->addWidget(m_cmbRes);
    qualityLayout->addWidget(m_cmbFps);
    m_qualityPanel->setVisible(m_mode != CallMode::Voice);

    m_chkScreenAudio = new QCheckBox(tr("Share audio"), m_controls);
    m_chkScreenAudio->setChecked(true);

    auto makeButton = [this](const QString& objectName, const QString& iconName,
                             const QString& tip, bool checkable) {
        auto* button = new QPushButton(m_controls);
        button->setObjectName(objectName);
        button->setToolTip(tip);
        button->setCheckable(checkable);
        button->setCursor(Qt::PointingHandCursor);
        UiTheme::setClass(button, QStringLiteral("callBtn"));
        UiTheme::applyIcon(button, iconName, 20);
        return button;
    };

    m_btnMute   = makeButton("btnMute",   "voice",  tr("Mute microphone"),   true);
    m_btnCamera = makeButton("btnCamera", "video",  tr("Turn camera off"),   true);
    m_btnScreen = makeButton("btnScreen", "screen", tr("Share your screen"), true);
    m_btnStats  = makeButton("btnStats",  "ping",   tr("Show call statistics"), true);

    m_btnHangup = new QPushButton(m_controls);
    m_btnHangup->setObjectName("btnHangup");
    m_btnHangup->setToolTip(tr("End call"));
    m_btnHangup->setCursor(Qt::PointingHandCursor);
    UiTheme::applyIcon(m_btnHangup, "hangup", 20);

    controlsLayout->addWidget(m_qualityPanel);
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(m_btnMute);
    controlsLayout->addWidget(m_btnCamera);
    controlsLayout->addWidget(m_btnScreen);
    controlsLayout->addWidget(m_btnStats);
    controlsLayout->addWidget(m_btnHangup);
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(m_chkScreenAudio);
    root->addWidget(m_controls);

    connect(m_btnMute,   &QPushButton::clicked, this, &CallWindow::onMute);
    connect(m_btnCamera, &QPushButton::clicked, this, &CallWindow::onCamera);
    connect(m_btnScreen, &QPushButton::clicked, this, &CallWindow::onScreen);
    connect(m_btnStats,  &QPushButton::clicked, this, &CallWindow::onToggleStats);
    connect(m_btnHangup, &QPushButton::clicked, this, &CallWindow::onHangup);
    connect(m_chkScreenAudio, &QCheckBox::toggled, this, &CallWindow::onScreenAudioChanged);
    connect(m_cmbRes, &QComboBox::currentTextChanged, this, &CallWindow::onQualityChanged);
    connect(m_cmbFps, &QComboBox::currentTextChanged, this, &CallWindow::onQualityChanged);

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(1000);
    connect(m_tickTimer, &QTimer::timeout, this, &CallWindow::onTick);
    m_tickTimer->start();

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(3000);
    connect(m_hideTimer, &QTimer::timeout, this, &CallWindow::onHideControls);

    updateButtonStates();
}

void CallWindow::startSession()
{
    m_session = new CallSession(m_peerIp, m_mode, m_myId, this);

    connect(m_session, &CallSession::remoteFrame,  this, &CallWindow::onRemoteFrame);
    connect(m_session, &CallSession::localFrame,   this, &CallWindow::onLocalFrame);
    connect(m_session, &CallSession::statsUpdated, this, &CallWindow::onStats);
    connect(m_session, &CallSession::stateChanged, this, &CallWindow::onSessionState);
    connect(m_session, &CallSession::transportChanged, this, [this](const QString& name) {
        if (m_transportBadge) m_transportBadge->setText(name);
    });
    connect(m_session, &CallSession::failed, this, [this](const QString& reason) {
        if (m_statusLabel) {
            m_statusLabel->setVisible(true);
            m_statusLabel->setText(reason);
        }
    });

    if (!m_session->start()) {
        if (m_statusLabel)
            m_statusLabel->setText(tr("Could not open your microphone or camera."));
        return;
    }

    m_session->setScreenAudioEnabled(m_chkScreenAudio->isChecked());
    if (m_mode != CallMode::Voice) onQualityChanged();
}

void CallWindow::updateButtonStates()
{
    const bool videoCall = (m_mode != CallMode::Voice);

    m_btnMute->setChecked(m_muted);
    m_btnMute->setToolTip(m_muted ? tr("Unmute microphone") : tr("Mute microphone"));

    m_btnCamera->setEnabled(videoCall);
    // Checked reads as "off" for mute and camera, which the theme paints red.
    m_btnCamera->setChecked(videoCall && !m_cameraOn);
    m_btnCamera->setToolTip(m_cameraOn ? tr("Turn camera off") : tr("Turn camera on"));

    m_btnScreen->setEnabled(videoCall);
    m_btnScreen->setChecked(m_screenOn);
    m_btnScreen->setToolTip(m_screenOn ? tr("Stop sharing your screen")
                                       : tr("Share your screen"));

    m_chkScreenAudio->setVisible(m_screenOn);
    if (m_selfView) m_selfView->setVisible(videoCall && (m_cameraOn || m_screenOn));
}

void CallWindow::onMute()
{
    m_muted = !m_muted;
    if (m_session) m_session->setMuted(m_muted);
    updateButtonStates();
}

void CallWindow::onCamera()
{
    if (m_mode == CallMode::Voice) return;
    m_cameraOn = !m_cameraOn;
    if (m_session) m_session->setCameraEnabled(m_cameraOn);
    if (!m_cameraOn && m_selfView) m_selfView->clearFrame();
    updateButtonStates();
}

void CallWindow::onScreen()
{
    if (m_mode == CallMode::Voice) return;
    m_screenOn = !m_screenOn;
    if (m_session) m_session->setScreenSharing(m_screenOn);
    if (m_selfView) m_selfView->clearFrame();
    updateButtonStates();
}

void CallWindow::onToggleStats()
{
    if (m_statsOverlay) m_statsOverlay->setVisible(m_btnStats->isChecked());
}

void CallWindow::onScreenAudioChanged(bool on)
{
    if (m_session) m_session->setScreenAudioEnabled(on);
}

void CallWindow::onQualityChanged()
{
    if (!m_session || m_mode == CallMode::Voice) return;

    // "Source" has no fixed size, so it maps to 720p — the encoder needs
    // concrete dimensions and the camera's native size is not known up front.
    QSize size(1280, 720);
    const auto it = MediaSettings::Resolutions.find(m_cmbRes->currentText().toStdString());
    if (it != MediaSettings::Resolutions.end() && it->second.has_value())
        size = QSize(it->second->w, it->second->h);

    float fps = m_cmbFps->currentText().toFloat();
    if (fps <= 0.0f) fps = 30.0f;

    m_session->setVideoTarget(size, fps, bitrateFor(size, fps));
}

void CallWindow::onSessionState(CallSession::State state)
{
    if (!m_statusLabel) return;
    switch (state) {
    case CallSession::State::Idle:
    case CallSession::State::Connecting:
        m_statusLabel->setVisible(true);
        m_statusLabel->setText(tr("Connecting…"));
        break;
    case CallSession::State::Connected:
        m_statusLabel->setVisible(false);
        m_statusLabel->setText(QString());
        break;
    case CallSession::State::Reconnecting:
        m_statusLabel->setVisible(true);
        m_statusLabel->setText(tr("Reconnecting…"));
        break;
    case CallSession::State::Ended:
        m_statusLabel->setVisible(true);
        m_statusLabel->setText(tr("Call ended"));
        break;
    }
}

void CallWindow::onStats(CallStats stats)
{
    if (!m_statsOverlay || !m_statsOverlay->isVisible()) return;

    const QString rtt = stats.rttMs >= 0 ? QString::number(stats.rttMs) + QStringLiteral(" ms")
                                         : QStringLiteral("—");
    const QString res = stats.videoWidth > 0
                            ? QStringLiteral("%1x%2").arg(stats.videoWidth).arg(stats.videoHeight)
                            : QStringLiteral("—");

    m_statsOverlay->setText(
        tr("transport  %1\n"
           "rtt        %2\n"
           "up / down  %3 / %4 kbit/s\n"
           "loss       %5 %\n"
           "jitter     %6 ms\n"
           "video in   %7 @ %8 fps\n"
           "codecs     %9 / %10\n"
           "dropped    %11")
            .arg(stats.transport, rtt)
            .arg(stats.kbpsUp).arg(stats.kbpsDown)
            .arg(stats.lossPercent).arg(stats.jitterMs)
            .arg(res).arg(stats.fpsIn)
            .arg(stats.audioCodec, stats.videoCodec)
            .arg(stats.droppedFrames));
    m_statsOverlay->adjustSize();
    layoutSelfView();
}

void CallWindow::onRemoteFrame(QImage frame)
{
    if (m_remoteView) m_remoteView->setFrame(frame);
}

void CallWindow::onLocalFrame(QImage frame)
{
    if (m_selfView && m_selfView->isVisible()) m_selfView->setFrame(frame);
}

void CallWindow::onTick()
{
    if (!m_timerLabel || !m_session) return;
    if (m_session->state() == CallSession::State::Connected ||
        m_session->state() == CallSession::State::Reconnecting) {
        m_timerLabel->setText(formatDuration(m_session->elapsedMs()));
    }
}

// ── Layout / chrome ──────────────────────────────────────────────────────────

void CallWindow::layoutSelfView()
{
    if (!m_remoteView) return;
    const QRect stage = m_remoteView->rect();

    if (m_selfView) {
        const int w = qBound(140, stage.width() / 5, 320);
        const int h = w * 9 / 16;
        m_selfView->setGeometry(stage.right() - w - 16, stage.bottom() - h - 16, w, h);
        m_selfView->raise();
    }
    if (m_statsOverlay) {
        m_statsOverlay->move(16, 16);
        m_statsOverlay->raise();
    }
    if (m_statusLabel) {
        m_statusLabel->setGeometry(0, qMax(0, stage.height() - 40), stage.width(), 24);
        m_statusLabel->raise();
    }
}

void CallWindow::resizeEvent(QResizeEvent* e)
{
    QDialog::resizeEvent(e);
    // The stage is resized by the layout after this returns, so lay the
    // overlays out again on the next event-loop pass.
    layoutSelfView();
    QTimer::singleShot(0, this, [this] { layoutSelfView(); });
}

void CallWindow::setFullScreenMode(bool on)
{
    if (m_fullScreen == on) return;
    m_fullScreen = on;

    if (on) {
        showFullScreen();
        m_hideTimer->start();
    } else {
        showNormal();
        m_hideTimer->stop();
        showControls();
    }
}

void CallWindow::showControls()
{
    if (m_header)   m_header->setVisible(true);
    if (m_controls) m_controls->setVisible(true);
    unsetCursor();
}

void CallWindow::onHideControls()
{
    if (!m_fullScreen) return;
    if (m_header)   m_header->setVisible(false);
    if (m_controls) m_controls->setVisible(false);
    setCursor(Qt::BlankCursor);
}

void CallWindow::mouseMoveEvent(QMouseEvent* e)
{
    if (m_fullScreen) {
        showControls();
        m_hideTimer->start();
    }
    QDialog::mouseMoveEvent(e);
}

void CallWindow::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape && m_fullScreen) {
        setFullScreenMode(false);
        return;
    }
    if (e->key() == Qt::Key_F11) {
        setFullScreenMode(!m_fullScreen);
        return;
    }
    if (e->key() == Qt::Key_M) {
        onMute();
        return;
    }
    QDialog::keyPressEvent(e);
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void CallWindow::handleRtcSignal(const SigMsg& msg)
{
    // LAN calls negotiate over LCM3 Hello packets rather than ICE, so there is
    // nothing to do here. The hook stays so signalling remains compatible with
    // peers that still send WebRTC offers.
    Q_UNUSED(msg);
}

void CallWindow::onHangup()
{
    if (m_closing) return;
    m_closing = true;
    if (m_session) m_session->stop();
    emit hangupRequested();
    close();
}

void CallWindow::doClose()
{
    if (m_closing) return;
    m_closing = true;
    if (m_session) m_session->stop();
    close();
}

void CallWindow::closeEvent(QCloseEvent* e)
{
    if (!m_closing) {
        m_closing = true;
        if (m_session) m_session->stop();
        emit hangupRequested();
    }
    if (m_tickTimer) m_tickTimer->stop();
    if (m_hideTimer) m_hideTimer->stop();
    e->accept();
    deleteLater();
}
