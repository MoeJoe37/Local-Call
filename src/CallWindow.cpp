#include "CallWindow.h"
#include "MediaSettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QCloseEvent>
#include <QTimer>

CallWindow::CallWindow(const QString& peerIp, const QString& peerName,
                       CallMode mode, const QString& myId, const QString& myName,
                       QWidget* parent)
    : QDialog(parent, Qt::Window), m_peerIp(peerIp), m_peerName(peerName),
      m_mode(mode), m_myId(myId), m_myName(myName)
{
    setWindowTitle(QString("Call with %1").arg(peerName));
    setMinimumSize(520, 420);
    setAttribute(Qt::WA_DeleteOnClose);

    setStyleSheet(R"(
        QDialog { background: #1E1E2E; }
        QLabel  { color: #CDD6F4; }
        QLabel#status { color: #A6E3A1; font-size: 13px; }
        QLabel#overlay { color: #CDD6F4; font-size: 16px; background: rgba(0,0,0,160);
                         padding: 12px 20px; border-radius: 8px; }
        QLabel#remote { background: #181825; border-radius: 6px; }
        QLabel#local  { background: #1E1E2E; border-radius: 4px; }
        QPushButton {
            background: #313244; color: #CDD6F4; border: none;
            border-radius: 6px; padding: 8px 18px; font-size: 13px;
        }
        QPushButton:hover { background: #45475A; }
        QPushButton#hangup { background: #E64553; color: white; }
        QPushButton#hangup:hover { background: #C73C48; }
        QCheckBox { color: #CDD6F4; font-size: 12px; }
    )");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(0);

    // Video area
    auto* videoStack = new QWidget(this);
    videoStack->setMinimumHeight(280);
    auto* vl = new QVBoxLayout(videoStack);
    vl->setContentsMargins(0,0,0,0);

    m_remoteVideo = new QLabel(videoStack);
    m_remoteVideo->setObjectName("remote");
    m_remoteVideo->setAlignment(Qt::AlignCenter);
    m_remoteVideo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    vl->addWidget(m_remoteVideo);

    m_overlayLabel = new QLabel("Connecting…", videoStack);
    m_overlayLabel->setObjectName("overlay");
    m_overlayLabel->setAlignment(Qt::AlignCenter);
    vl->addWidget(m_overlayLabel, 0, Qt::AlignCenter);

    m_localVideo = new QLabel(videoStack);
    m_localVideo->setObjectName("local");
    m_localVideo->setFixedSize(160, 120);
    m_localVideo->setAlignment(Qt::AlignCenter);

    bool hasMedia = false;
#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
    hasMedia = true;
#endif
    if (!hasMedia || mode == CallMode::Voice) {
        m_remoteVideo->setVisible(false);
        m_localVideo->setVisible(false);
    }
    root->addWidget(videoStack);

    // Info row
    auto* infoBar = new QHBoxLayout();
    infoBar->setContentsMargins(12,8,12,4);
    auto* callWith = new QLabel(QString("📞  %1").arg(peerName), this);
    callWith->setStyleSheet("font-size:14px;font-weight:bold;color:#CDD6F4;");
    m_statusLabel = new QLabel("Connecting…", this);
    m_statusLabel->setObjectName("status");
    infoBar->addWidget(callWith);
    infoBar->addStretch();
    infoBar->addWidget(m_statusLabel);
    root->addLayout(infoBar);

    // Screen audio
    m_screenAudioPanel = new QWidget(this);
    auto* saRow = new QHBoxLayout(m_screenAudioPanel);
    saRow->setContentsMargins(12,0,12,0);
    m_chkScreenAudio = new QCheckBox("Include microphone audio", this);
    m_chkScreenAudio->setChecked(true);
    saRow->addWidget(m_chkScreenAudio);
    saRow->addStretch();
    m_screenAudioPanel->setVisible(mode == CallMode::VideoScreen);
    root->addWidget(m_screenAudioPanel);

    // Quality controls (resolution / FPS / JPEG quality) — video modes only
    m_qualityPanel = new QWidget(this);
    m_qualityPanel->setStyleSheet("background:#1E1E2E;border-top:1px solid #313244;padding:4px 0;");
    auto* qRow = new QHBoxLayout(m_qualityPanel);
    qRow->setContentsMargins(12,6,12,6);
    qRow->setSpacing(12);

    auto* lblRes = new QLabel("Res:", m_qualityPanel);
    lblRes->setStyleSheet("color:#A6ADC8;font-size:11px;");
    m_cmbRes = new QComboBox(m_qualityPanel);
    m_cmbRes->setStyleSheet("background:#313244;color:#CDD6F4;border:none;border-radius:3px;padding:2px 6px;font-size:11px;");
    for (const auto& [name, _] : MediaSettings::Resolutions)
        m_cmbRes->addItem(QString::fromStdString(name));
    m_cmbRes->setCurrentText("360p");

    auto* lblFps = new QLabel("FPS:", m_qualityPanel);
    lblFps->setStyleSheet("color:#A6ADC8;font-size:11px;");
    m_cmbFps = new QComboBox(m_qualityPanel);
    m_cmbFps->setStyleSheet("background:#313244;color:#CDD6F4;border:none;border-radius:3px;padding:2px 6px;font-size:11px;");
    for (const auto& f : MediaSettings::FpsOptions)
        m_cmbFps->addItem(QString::fromStdString(f));
    m_cmbFps->setCurrentText("30");

    auto* lblQ = new QLabel("Quality:", m_qualityPanel);
    lblQ->setStyleSheet("color:#A6ADC8;font-size:11px;");
    m_sldQuality = new QSlider(Qt::Horizontal, m_qualityPanel);
    m_sldQuality->setRange(10, 100);
    m_sldQuality->setValue(65);
    m_sldQuality->setFixedWidth(90);
    m_sldQuality->setStyleSheet("QSlider::groove:horizontal{background:#313244;height:4px;border-radius:2px;}"
                                "QSlider::handle:horizontal{background:#CBA6F7;width:12px;height:12px;border-radius:6px;margin:-4px 0;}");
    m_lblQuality = new QLabel("65", m_qualityPanel);
    m_lblQuality->setStyleSheet("color:#CDD6F4;font-size:11px;min-width:24px;");

    qRow->addWidget(lblRes);
    qRow->addWidget(m_cmbRes);
    qRow->addWidget(lblFps);
    qRow->addWidget(m_cmbFps);
    qRow->addWidget(lblQ);
    qRow->addWidget(m_sldQuality);
    qRow->addWidget(m_lblQuality);
    qRow->addStretch();
    m_qualityPanel->setVisible(mode != CallMode::Voice);
    root->addWidget(m_qualityPanel);

    // Controls
    auto* ctrlBar = new QHBoxLayout();
    ctrlBar->setContentsMargins(12,8,12,12);
    ctrlBar->setSpacing(8);

    m_btnMute   = new QPushButton("🎤 Mute",   this);
    m_btnCamera = new QPushButton("📷 Camera", this);
    m_btnScreen = new QPushButton("🖥 Screen", this);
    auto* btnHangup = new QPushButton("📵 Hang up", this);
    btnHangup->setObjectName("hangup");

    m_btnCamera->setEnabled(hasMedia && mode != CallMode::Voice);
    m_btnScreen->setEnabled(hasMedia && mode != CallMode::Voice);
    m_btnMute->setEnabled(hasMedia);

    ctrlBar->addWidget(m_btnMute);
    ctrlBar->addWidget(m_btnCamera);
    ctrlBar->addWidget(m_btnScreen);
    ctrlBar->addStretch();
    ctrlBar->addWidget(btnHangup);
    root->addLayout(ctrlBar);

    connect(m_btnMute,        &QPushButton::clicked,     this, &CallWindow::onMute);
    connect(m_btnCamera,      &QPushButton::clicked,     this, &CallWindow::onCamera);
    connect(m_btnScreen,      &QPushButton::clicked,     this, &CallWindow::onScreen);
    connect(btnHangup,        &QPushButton::clicked,     this, &CallWindow::onHangup);
    connect(m_chkScreenAudio, &QCheckBox::stateChanged,  this, &CallWindow::onScreenAudioChanged);
    connect(m_cmbRes,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CallWindow::onQualityChanged);
    connect(m_cmbFps,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CallWindow::onQualityChanged);
    connect(m_sldQuality, &QSlider::valueChanged, this, [this](int v){
        m_lblQuality->setText(QString::number(v));
        onQualityChanged();
    });

    startMedia();
}

CallWindow::~CallWindow() { stopMedia(); }

void CallWindow::onQualityChanged()
{
    if (!m_videoSender) return;

    // Resolution
    QString resName = m_cmbRes->currentText();
    auto it = MediaSettings::Resolutions.find(resName.toStdString());
    if (it != MediaSettings::Resolutions.end()) {
        if (it->second.has_value())
            m_videoSender->targetRes = std::make_pair(it->second->w, it->second->h);
        else
            m_videoSender->targetRes = std::nullopt;  // "Source" = native
    }

    // FPS
    QString fpsStr = m_cmbFps->currentText();
    if (fpsStr == "Source") m_videoSender->targetFps = 999;
    else                    m_videoSender->targetFps = fpsStr.toInt();

    // JPEG quality
    m_videoSender->jpegQuality.store(m_sldQuality->value());
}

void CallWindow::startMedia()
{
#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
    // Audio
    auto* aSend = new MediaWorker(MediaMode::Audio, m_peerIp,
                                   MediaSettings::MediaAudioPort, false, this);
    auto* aRecv = new MediaWorker(MediaMode::Audio, {},
                                   MediaSettings::MediaAudioPort, true, this);
    connect(aRecv, &MediaWorker::connected, this, &CallWindow::onMediaConnected);
    m_workers << aSend << aRecv;
    m_audioSender = aSend;

    if (m_mode != CallMode::Voice) {
        auto vMode = (m_mode == CallMode::VideoScreen) ? MediaMode::Screen : MediaMode::Camera;
        auto* vSend = new MediaWorker(vMode, m_peerIp,
                                       MediaSettings::MediaVideoPort, false, this);
        auto* vRecv = new MediaWorker(MediaMode::Camera, {},
                                       MediaSettings::MediaVideoPort, true, this);
        connect(vRecv, &MediaWorker::frameReceived, this, &CallWindow::onRemoteFrame);
        connect(vRecv, &MediaWorker::connected,     this, &CallWindow::onMediaConnected);
        m_workers << vSend << vRecv;
        m_videoSender = vSend;
    }
    if (m_audioSender && m_mode == CallMode::VideoScreen)
        m_audioSender->muteAudioOnScreen = !m_chkScreenAudio->isChecked();

    for (auto* w : m_workers) w->start();
#else
    // No media deps — show a simple "connected" state immediately
    QTimer::singleShot(500, this, &CallWindow::onMediaConnected);
#endif
}

void CallWindow::stopMedia()
{
    for (auto* w : m_workers) { w->stop(); delete w; }
    m_workers.clear();
    m_audioSender = nullptr;
    m_videoSender = nullptr;
}

void CallWindow::onMediaConnected()
{
    m_overlayLabel->setVisible(false);
    m_statusLabel->setText("● Connected");
    m_statusLabel->setStyleSheet("color:#A6E3A1;font-size:13px;");
}

void CallWindow::onRemoteFrame(QImage frame)
{
    m_overlayLabel->setVisible(false);
    m_remoteVideo->setPixmap(
        QPixmap::fromImage(frame).scaled(m_remoteVideo->size(),
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void CallWindow::onLocalFrame(QImage frame)
{
    m_localVideo->setPixmap(
        QPixmap::fromImage(frame).scaled(m_localVideo->size(),
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void CallWindow::onMute()
{
    m_muted = !m_muted;
    if (m_audioSender) m_audioSender->muted = m_muted;
    m_btnMute->setText(m_muted ? "🔇 Unmute" : "🎤 Mute");
}

void CallWindow::onCamera()
{
    m_cameraOn = !m_cameraOn;
    m_btnCamera->setText(m_cameraOn ? "📷 Camera" : "📷 Cam Off");
    if (m_videoSender) { m_videoSender->stop(); if (m_cameraOn) m_videoSender->start(); }
}

void CallWindow::onScreen()
{
    m_screenOn = !m_screenOn;
    m_btnScreen->setText(m_screenOn ? "🖥 Stop" : "🖥 Screen");
    m_screenAudioPanel->setVisible(m_screenOn);

    if (m_videoSender) {
        m_videoSender->stop();
        m_workers.removeOne(m_videoSender);
        delete m_videoSender;
        auto vMode = m_screenOn ? MediaMode::Screen : MediaMode::Camera;
        m_videoSender = new MediaWorker(vMode, m_peerIp,
                                         MediaSettings::MediaVideoPort, false, this);
        m_workers << m_videoSender;
        m_videoSender->start();
    }
    if (m_audioSender)
        m_audioSender->muteAudioOnScreen = m_screenOn && !m_chkScreenAudio->isChecked();
}

void CallWindow::onScreenAudioChanged(int)
{
    if (m_audioSender)
        m_audioSender->muteAudioOnScreen = m_screenOn && !m_chkScreenAudio->isChecked();
}

void CallWindow::onHangup() { emit hangupRequested(); doClose(); }

void CallWindow::doClose()
{
    if (m_closing) return;
    m_closing = true;
    stopMedia();
    QTimer::singleShot(0, this, &QDialog::close);
}

void CallWindow::closeEvent(QCloseEvent* e)
{
    if (!m_closing) emit hangupRequested();
    stopMedia();
    e->accept();
}
