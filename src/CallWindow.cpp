#include "CallWindow.h"
#include "MediaSettings.h"
#include "Helpers.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QCloseEvent>
#include <QTimer>
#include <QPixmap>
#include <QByteArray>
#include <QSizePolicy>
#include <QMessageBox>

#ifdef HAS_WEBRTC
#include <QProcessEnvironment>
#endif

namespace {
#ifdef HAS_WEBRTC
std::vector<std::string> iceServersFromEnvironment()
{
    // Comma-separated, for example:
    // LOCALCALL_ICE_SERVERS="stun:stun.l.google.com:19302,turn:user:pass@turn.example.com:3478"
    std::vector<std::string> out;
    const QByteArray raw = qgetenv("LOCALCALL_ICE_SERVERS");
    if (!raw.trimmed().isEmpty()) {
        for (const QByteArray& item : raw.split(',')) {
            const QByteArray v = item.trimmed();
            if (!v.isEmpty()) out.emplace_back(v.constData());
        }
    }
    if (out.empty()) out.emplace_back("stun:stun.l.google.com:19302");
    return out;
}
#endif
}

CallWindow::CallWindow(const QString& peerIp, const QString& peerName,
                       CallMode mode, const QString& myId, const QString& myName,
                       bool initiator,
                       QWidget* parent)
    : QDialog(parent, Qt::Window), m_peerIp(peerIp), m_peerName(peerName),
      m_mode(mode), m_myId(myId), m_myName(myName), m_initiator(initiator),
      m_rtcSessionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    setWindowTitle(QString("Secure call with %1").arg(peerName));
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

    auto* videoStack = new QWidget(this);
    videoStack->setMinimumHeight(280);
    auto* vl = new QVBoxLayout(videoStack);
    vl->setContentsMargins(0,0,0,0);

    m_remoteVideo = new QLabel(videoStack);
    m_remoteVideo->setObjectName("remote");
    m_remoteVideo->setAlignment(Qt::AlignCenter);
    m_remoteVideo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    vl->addWidget(m_remoteVideo);

    m_overlayLabel = new QLabel("Negotiating secure low-latency RTC…", videoStack);
    m_overlayLabel->setObjectName("overlay");
    m_overlayLabel->setAlignment(Qt::AlignCenter);
    vl->addWidget(m_overlayLabel, 0, Qt::AlignCenter);

    m_localVideo = new QLabel(videoStack);
    m_localVideo->setObjectName("local");
    m_localVideo->setFixedSize(160, 120);
    m_localVideo->setAlignment(Qt::AlignCenter);

    if (mode == CallMode::Voice) {
        m_remoteVideo->setVisible(false);
        m_localVideo->setVisible(false);
    }
    root->addWidget(videoStack);

    auto* infoBar = new QHBoxLayout();
    infoBar->setContentsMargins(12,8,12,4);
    auto* callWith = new QLabel(QString("🔒  %1").arg(peerName), this);
    callWith->setStyleSheet("font-size:14px;font-weight:bold;color:#CDD6F4;");
    m_statusLabel = new QLabel("Secure RTC connecting…", this);
    m_statusLabel->setObjectName("status");
    infoBar->addWidget(callWith);
    infoBar->addStretch();
    infoBar->addWidget(m_statusLabel);
    root->addLayout(infoBar);

    m_screenAudioPanel = new QWidget(this);
    auto* saRow = new QHBoxLayout(m_screenAudioPanel);
    saRow->setContentsMargins(12,0,12,0);
    m_chkScreenAudio = new QCheckBox("Include microphone audio", this);
    m_chkScreenAudio->setChecked(true);
    saRow->addWidget(m_chkScreenAudio);
    saRow->addStretch();
    m_screenAudioPanel->setVisible(mode == CallMode::VideoScreen);
    root->addWidget(m_screenAudioPanel);

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

    auto* lblQ = new QLabel("Bitrate bias:", m_qualityPanel);
    lblQ->setStyleSheet("color:#A6ADC8;font-size:11px;");
    m_sldQuality = new QSlider(Qt::Horizontal, m_qualityPanel);
    m_sldQuality->setRange(10, 100);
    m_sldQuality->setValue(55);
    m_sldQuality->setFixedWidth(90);
    m_lblQuality = new QLabel("55", m_qualityPanel);
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

    auto* ctrlBar = new QHBoxLayout();
    ctrlBar->setContentsMargins(12,8,12,12);
    ctrlBar->setSpacing(8);

    m_btnMute   = new QPushButton("🎤 Mute",   this);
    m_btnCamera = new QPushButton("📷 Camera", this);
    m_btnScreen = new QPushButton("🖥 Screen", this);
    auto* btnHangup = new QPushButton("📵 Hang up", this);
    btnHangup->setObjectName("hangup");

    m_btnCamera->setEnabled(mode != CallMode::Voice);
    m_btnScreen->setEnabled(mode != CallMode::Voice);

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

SigMsg CallWindow::makeRtcSignal(const std::string& type) const
{
    SigMsg sig;
    sig.protocol = LocalCallProtocol::Name;
    sig.schema = LocalCallProtocol::Schema;
#ifdef LOCALCALL_VERSION
    sig.app_version = LOCALCALL_VERSION;
#endif
#if defined(Q_OS_WIN)
    sig.platform = "windows";
#elif defined(Q_OS_MACOS)
    sig.platform = "macos";
#elif defined(Q_OS_LINUX)
    sig.platform = "linux";
#elif defined(Q_OS_ANDROID)
    sig.platform = "android";
#else
    sig.platform = "unknown";
#endif
    sig.type = type;
    sig.from_id = m_myId.toStdString();
    sig.from_name = m_myName.toStdString();
    sig.rtc_session_id = m_rtcSessionId.toStdString();
    sig.transport = "webrtc-dtls-srtp";
    sig.ts = Helpers::nowMs();
    return sig;
}

void CallWindow::onQualityChanged()
{
    if (m_lblQuality) m_lblQuality->setText(QString::number(m_sldQuality->value()));
#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
    if (!m_videoSender) return;

    QString resName = m_cmbRes->currentText();
    auto it = MediaSettings::Resolutions.find(resName.toStdString());
    if (it != MediaSettings::Resolutions.end()) {
        if (it->second.has_value())
            m_videoSender->targetRes = std::make_pair(it->second->w, it->second->h);
        else
            m_videoSender->targetRes = std::nullopt;
    }

    QString fpsStr = m_cmbFps->currentText();
    if (fpsStr == "Source") m_videoSender->targetFps = 999;
    else                    m_videoSender->targetFps = fpsStr.toInt();

    m_videoSender->jpegQuality.store(m_sldQuality->value());
#endif
}

void CallWindow::startMedia()
{
#ifdef HAS_WEBRTC
    RtcConfig cfg;
    cfg.localNetworkOnly = false;
    cfg.iceServers = iceServersFromEnvironment();

    m_rtcPeer = new RtcPeer(m_myId, m_peerName, cfg, this);
    connect(m_rtcPeer, &RtcPeer::localDescriptionReady, this, [this](QString type, QString sdp) {
        SigMsg sig = makeRtcSignal(type == "answer" ? SigType::RtcAnswer : SigType::RtcOffer);
        sig.sdp_type = type.toStdString();
        sig.sdp = sdp.toStdString();
        emit rtcSignalReady(sig);
    });
    connect(m_rtcPeer, &RtcPeer::localCandidateReady, this, [this](QString candidate, QString mid, int mline) {
        SigMsg sig = makeRtcSignal(SigType::RtcIce);
        sig.candidate = candidate.toStdString();
        sig.candidate_mid = mid.toStdString();
        sig.candidate_mline = mline;
        emit rtcSignalReady(sig);
    });
    connect(m_rtcPeer, &RtcPeer::connected, this, &CallWindow::onMediaConnected);
    connect(m_rtcPeer, &RtcPeer::failed, this, [this]() {
        m_statusLabel->setText("RTC failed");
        m_statusLabel->setStyleSheet("color:#F38BA8;font-size:13px;");
        m_overlayLabel->setText("Secure RTC failed. Check STUN/TURN or peer reachability.");
        m_overlayLabel->setVisible(true);
    });

    EncoderSettings settings;
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30.0f;
    settings.bitrate = 800000;
    settings.opusBitrate = 32000;
    if (m_mode == CallMode::Voice) settings.bitrate = 1;

    m_pipeline = new MediaPipeline(settings, this);
    connect(m_pipeline, &MediaPipeline::encodedVideoFrame, m_rtcPeer, &RtcPeer::sendVideoFrame, Qt::QueuedConnection);
    connect(m_pipeline, &MediaPipeline::encodedAudioFrame, m_rtcPeer, &RtcPeer::sendAudioFrame, Qt::QueuedConnection);
    connect(m_rtcPeer, &RtcPeer::remoteVideoFrame, m_pipeline, &MediaPipeline::onRemoteVideoFrame, Qt::QueuedConnection);
    connect(m_rtcPeer, &RtcPeer::remoteAudioFrame, m_pipeline, &MediaPipeline::onRemoteAudioFrame, Qt::QueuedConnection);
    connect(m_pipeline, &MediaPipeline::remoteVideoImage, this, &CallWindow::onRemoteFrame, Qt::QueuedConnection);

    if (!m_pipeline->startCapture()) {
        m_overlayLabel->setText("Could not start microphone/camera capture.");
        m_statusLabel->setText("Capture failed");
    }

    if (m_initiator) {
        QTimer::singleShot(150, this, [this]() {
            if (m_rtcPeer) m_rtcPeer->createOffer();
        });
    }
    return;
#endif

#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
#ifdef HAS_MULTIMEDIA
    auto* aSend = new MediaWorker(MediaMode::Audio, m_peerIp,
                                   MediaSettings::MediaAudioPort, false, this);
    auto* aRecv = new MediaWorker(MediaMode::Audio, {},
                                   MediaSettings::MediaAudioPort, true, this);
    connect(aRecv, &MediaWorker::connected, this, &CallWindow::onMediaConnected);
    m_workers << aSend << aRecv;
    m_audioSender = aSend;
#endif

#ifdef HAS_OPENCV
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
#endif

#ifdef HAS_MULTIMEDIA
    if (m_audioSender && m_mode == CallMode::VideoScreen)
        m_audioSender->muteAudioOnScreen = !m_chkScreenAudio->isChecked();
#endif

    if (m_workers.isEmpty()) {
        m_overlayLabel->setText("Media support is not built in this package.");
        QTimer::singleShot(500, this, &CallWindow::onMediaConnected);
        return;
    }

    for (auto* w : m_workers) w->start();
#else
    m_overlayLabel->setText("Media support is not built in this package.");
    QTimer::singleShot(500, this, &CallWindow::onMediaConnected);
#endif
}

void CallWindow::stopMedia()
{
#ifdef HAS_WEBRTC
    if (m_pipeline) { m_pipeline->stopCapture(); m_pipeline->deleteLater(); m_pipeline = nullptr; }
    if (m_rtcPeer) { m_rtcPeer->close(); m_rtcPeer->deleteLater(); m_rtcPeer = nullptr; }
#endif
#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
    for (auto* w : m_workers) { w->stop(); delete w; }
    m_workers.clear();
    m_audioSender = nullptr;
    m_videoSender = nullptr;
#endif
}

void CallWindow::handleRtcSignal(const SigMsg& msg)
{
#ifdef HAS_WEBRTC
    if (!m_rtcPeer) return;
    if (msg.type == SigType::RtcOffer || msg.type == SigType::RtcAnswer) {
        if (msg.sdp && msg.sdp_type)
            m_rtcPeer->setRemoteDescription(QString::fromStdString(*msg.sdp_type),
                                            QString::fromStdString(*msg.sdp));
    } else if (msg.type == SigType::RtcIce) {
        if (msg.candidate && msg.candidate_mid)
            m_rtcPeer->addRemoteCandidate(QString::fromStdString(*msg.candidate),
                                          QString::fromStdString(*msg.candidate_mid),
                                          msg.candidate_mline.value_or(0));
    }
#else
    Q_UNUSED(msg);
#endif
}

void CallWindow::onMediaConnected()
{
    m_overlayLabel->setVisible(false);
    m_statusLabel->setText("● Encrypted low-latency RTC connected");
    m_statusLabel->setStyleSheet("color:#A6E3A1;font-size:13px;");
}

void CallWindow::onRemoteFrame(QImage frame)
{
    if (!m_remoteVideo || frame.isNull()) return;
    m_overlayLabel->setVisible(false);
    m_remoteVideo->setPixmap(
        QPixmap::fromImage(frame).scaled(m_remoteVideo->size(),
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void CallWindow::onLocalFrame(QImage frame)
{
    if (!m_localVideo || frame.isNull()) return;
    m_localVideo->setPixmap(
        QPixmap::fromImage(frame).scaled(m_localVideo->size(),
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void CallWindow::onMute()
{
    m_muted = !m_muted;
#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
    if (m_audioSender) m_audioSender->muted = m_muted;
#endif
    m_btnMute->setText(m_muted ? "🔇 Unmute" : "🎤 Mute");
}

void CallWindow::onCamera()
{
    m_cameraOn = !m_cameraOn;
    m_btnCamera->setText(m_cameraOn ? "📷 Camera" : "📷 Cam Off");
#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
    if (m_videoSender) { m_videoSender->stop(); if (m_cameraOn) m_videoSender->start(); }
#endif
}

void CallWindow::onScreen()
{
    m_screenOn = !m_screenOn;
    m_btnScreen->setText(m_screenOn ? "🖥 Stop" : "🖥 Screen");
    m_screenAudioPanel->setVisible(m_screenOn);

#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
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
#endif
}

void CallWindow::onScreenAudioChanged(int)
{
#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
    if (m_audioSender)
        m_audioSender->muteAudioOnScreen = m_screenOn && !m_chkScreenAudio->isChecked();
#endif
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
