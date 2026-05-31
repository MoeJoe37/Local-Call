#include "CallWindow.h"
#include "MediaSettings.h"
#include "Helpers.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QTimer>
#include <QPixmap>
#include <QByteArray>
#include <QSizePolicy>
#include <QMessageBox>
#include <QIcon>
#include <QTimer>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QSharedPointer>
#include <QtGlobal>

#ifdef HAS_WEBRTC
#include <QProcessEnvironment>
#endif

namespace {
static QIcon lcIcon(const QString& name) {
    return QIcon(QStringLiteral(":/icons/") + name + QStringLiteral(".png"));
}

static void applyIcon(QPushButton* button, const QString& iconName, int px = 18) {
    if (!button) return;
    button->setIcon(lcIcon(iconName));
    button->setIconSize(QSize(px, px));
}

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
    auto* vl = new QGridLayout(videoStack);
    vl->setContentsMargins(0,0,0,0);
    vl->setSpacing(0);

    m_remoteVideo = new QLabel(videoStack);
    m_remoteVideo->setObjectName("remote");
    m_remoteVideo->setAlignment(Qt::AlignCenter);
    m_remoteVideo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    vl->addWidget(m_remoteVideo, 0, 0);

    m_overlayLabel = new QLabel("Starting LAN media…", videoStack);
    m_overlayLabel->setObjectName("overlay");
    m_overlayLabel->setAlignment(Qt::AlignCenter);
    vl->addWidget(m_overlayLabel, 0, 0, Qt::AlignCenter);

    m_localVideo = new QLabel(videoStack);
    m_localVideo->setObjectName("local");
    m_localVideo->setFixedSize(160, 120);
    m_localVideo->setAlignment(Qt::AlignCenter);
    m_localVideo->setText("Local preview");
    m_localVideo->setStyleSheet("background:#11111B;border:1px solid #45475A;border-radius:8px;color:#6C7086;font-size:11px;");
    vl->addWidget(m_localVideo, 0, 0, Qt::AlignRight | Qt::AlignBottom);

    if (mode == CallMode::Voice) {
        m_remoteVideo->setVisible(false);
        m_localVideo->setVisible(false);
        m_overlayLabel->setText("Voice call connecting…");
    }
    root->addWidget(videoStack);

    auto* infoBar = new QHBoxLayout();
    infoBar->setContentsMargins(12,8,12,4);
    auto* callIcon = new QLabel(this);
    callIcon->setPixmap(lcIcon("lock").pixmap(16, 16));
    auto* callWith = new QLabel(peerName, this);
    callWith->setStyleSheet("font-size:14px;font-weight:bold;color:#CDD6F4;");
    m_statusLabel = new QLabel("Media connecting…", this);
    m_statusLabel->setObjectName("status");
    m_pingLabel = new QLabel("Ping --", this);
    m_pingLabel->setStyleSheet("color:#A6ADC8;font-size:11px;background:#181825;border:1px solid #313244;border-radius:10px;padding:3px 8px;");
    infoBar->addWidget(callIcon);
    infoBar->addWidget(callWith);
    infoBar->addStretch();
    infoBar->addWidget(m_pingLabel);
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
    m_cmbQuality = new QComboBox(m_qualityPanel);
    m_cmbQuality->setStyleSheet("background:#313244;color:#CDD6F4;border:none;border-radius:3px;padding:2px 6px;font-size:11px;");
    for (int q = 10; q <= 100; q += 10)
        m_cmbQuality->addItem(QString::number(q), q);
    m_cmbQuality->setCurrentText("60");
    m_lblQuality = new QLabel("60", m_qualityPanel);
    m_lblQuality->setStyleSheet("color:#CDD6F4;font-size:11px;min-width:24px;");

    qRow->addWidget(lblRes);
    qRow->addWidget(m_cmbRes);
    qRow->addWidget(lblFps);
    qRow->addWidget(m_cmbFps);
    qRow->addWidget(lblQ);
    qRow->addWidget(m_cmbQuality);
    qRow->addWidget(m_lblQuality);
    qRow->addStretch();
    m_qualityPanel->setVisible(mode != CallMode::Voice);
    root->addWidget(m_qualityPanel);

    auto* ctrlBar = new QHBoxLayout();
    ctrlBar->setContentsMargins(12,8,12,12);
    ctrlBar->setSpacing(8);

    m_btnMute   = new QPushButton("Mute",   this);
    m_btnCamera = new QPushButton("Camera", this);
    m_btnScreen = new QPushButton("Screen", this);
    auto* btnHangup = new QPushButton("Hang up", this);
    applyIcon(m_btnMute, "voice", 18);
    applyIcon(m_btnCamera, "video", 18);
    applyIcon(m_btnScreen, "screen", 18);
    applyIcon(btnHangup, "hangup", 18);
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
    connect(m_cmbQuality, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){
        if (m_lblQuality) m_lblQuality->setText(m_cmbQuality->currentText());
        onQualityChanged();
    });

    startMedia();

    m_pingTimer = new QTimer(this);
    m_pingTimer->setInterval(2000);
    connect(m_pingTimer, &QTimer::timeout, this, &CallWindow::pollPing);
    m_pingTimer->start();
    QTimer::singleShot(250, this, &CallWindow::pollPing);
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
    if (m_lblQuality && m_cmbQuality) m_lblQuality->setText(m_cmbQuality->currentText());

    QSize targetSize;
    QString resName = m_cmbRes ? m_cmbRes->currentText() : QStringLiteral("360p");
    auto it = MediaSettings::Resolutions.find(resName.toStdString());
    if (it != MediaSettings::Resolutions.end() && it->second.has_value())
        targetSize = QSize(it->second->w, it->second->h);

    QString fpsStr = m_cmbFps ? m_cmbFps->currentText() : QStringLiteral("30");
    float fps = (fpsStr == "Source") ? 30.0f : qMax(1, fpsStr.toInt());

    const int quality = m_cmbQuality ? m_cmbQuality->currentData().toInt() : 60;
    const int bitrate = qMax(128000, quality * 16000);

#ifdef HAS_WEBRTC
    if (m_pipeline)
        m_pipeline->setVideoTarget(targetSize, fps, bitrate);
#endif

#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
    if (!m_videoSender) return;
    if (it != MediaSettings::Resolutions.end()) {
        if (it->second.has_value())
            m_videoSender->targetRes = std::make_pair(it->second->w, it->second->h);
        else
            m_videoSender->targetRes = std::nullopt;
    }
    if (fpsStr == "Source") m_videoSender->targetFps = 999;
    else                    m_videoSender->targetFps = fpsStr.toInt();
    m_videoSender->jpegQuality.store(quality);
#endif
}

void CallWindow::startMedia()
{
#ifdef HAS_WEBRTC
    // Default to the deterministic LAN media path. It uses the already-opened
    // firewall fixed UDP ports and avoids the Windows/libdatachannel ICE cases
    // that made calls connect on one PC but carry no media on another. WebRTC
    // can still be enabled explicitly for testing with LOCALCALL_ENABLE_WEBRTC_MEDIA=1.
    m_useRtcMedia = qEnvironmentVariableIsSet("LOCALCALL_ENABLE_WEBRTC_MEDIA");

    if (m_useRtcMedia) {
        RtcConfig cfg;
        const auto envIce = iceServersFromEnvironment();
        cfg.localNetworkOnly = qgetenv("LOCALCALL_ICE_SERVERS").trimmed().isEmpty();
        if (!cfg.localNetworkOnly) cfg.iceServers = envIce;

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
            m_overlayLabel->setText("RTC failed. LAN media is recommended on local networks.");
            m_overlayLabel->setVisible(true);
        });
    }

    EncoderSettings settings;
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30.0f;
    settings.bitrate = 800000;
    settings.opusBitrate = 32000;
    if (m_mode == CallMode::Voice) {
        settings.bitrate = 1;
        settings.videoEnabled = false;
    }

    m_pipeline = new MediaPipeline(settings, this);

    // Default media path: a persistent TCP media channel. It uses one fixed,
    // firewall-opened port and avoids the packet loss / NAT loopback problems
    // that made the old UDP fallback appear connected while carrying no audio
    // or video on some Windows 11 machines.  UDP remains available only for
    // explicit testing with LOCALCALL_ENABLE_UDP_MEDIA=1.
    const bool useUdpMedia = qEnvironmentVariableIsSet("LOCALCALL_ENABLE_UDP_MEDIA") && !m_useRtcMedia;

    if (m_rtcPeer && m_useRtcMedia) {
        connect(m_pipeline, &MediaPipeline::encodedVideoFrame, m_rtcPeer, &RtcPeer::sendVideoFrame, Qt::QueuedConnection);
        connect(m_pipeline, &MediaPipeline::encodedAudioFrame, m_rtcPeer, &RtcPeer::sendAudioFrame, Qt::QueuedConnection);
        connect(m_rtcPeer, &RtcPeer::remoteVideoFrame, m_pipeline, &MediaPipeline::onRemoteVideoFrame, Qt::QueuedConnection);
        connect(m_rtcPeer, &RtcPeer::remoteAudioFrame, m_pipeline, &MediaPipeline::onRemoteAudioFrame, Qt::QueuedConnection);
    } else if (useUdpMedia) {
        m_udpPeer = new UdpMediaPeer(m_peerIp, m_myId, this);
        connect(m_pipeline, &MediaPipeline::encodedVideoFrame, m_udpPeer, &UdpMediaPeer::sendVideoFrame, Qt::QueuedConnection);
        connect(m_pipeline, &MediaPipeline::encodedAudioFrame, m_udpPeer, &UdpMediaPeer::sendAudioFrame, Qt::QueuedConnection);
        connect(m_udpPeer, &UdpMediaPeer::remoteVideoFrame, m_pipeline, &MediaPipeline::onRemoteVideoFrame, Qt::QueuedConnection);
        connect(m_udpPeer, &UdpMediaPeer::remoteAudioFrame, m_pipeline, &MediaPipeline::onRemoteAudioFrame, Qt::QueuedConnection);
        connect(m_udpPeer, &UdpMediaPeer::connected, this, &CallWindow::onMediaConnected, Qt::QueuedConnection);
        connect(m_udpPeer, &UdpMediaPeer::failed, this, [this](const QString& reason) {
            m_statusLabel->setText("UDP media failed");
            m_statusLabel->setStyleSheet("color:#F38BA8;font-size:13px;");
            m_overlayLabel->setText(reason);
            m_overlayLabel->setVisible(true);
        });
        if (!m_udpPeer->start()) {
            m_overlayLabel->setText("Could not start UDP media sockets.");
            m_statusLabel->setText("Media failed");
        }
    } else {
        m_tcpPeer = new MediaTcpPeer(m_peerIp, this);
        connect(m_pipeline, &MediaPipeline::encodedVideoFrame, m_tcpPeer, &MediaTcpPeer::sendVideoFrame, Qt::QueuedConnection);
        connect(m_pipeline, &MediaPipeline::encodedAudioFrame, m_tcpPeer, &MediaTcpPeer::sendAudioFrame, Qt::QueuedConnection);
        connect(m_tcpPeer, &MediaTcpPeer::remoteVideoFrame, m_pipeline, &MediaPipeline::onRemoteVideoFrame, Qt::QueuedConnection);
        connect(m_tcpPeer, &MediaTcpPeer::remoteAudioFrame, m_pipeline, &MediaPipeline::onRemoteAudioFrame, Qt::QueuedConnection);
        connect(m_tcpPeer, &MediaTcpPeer::connected, this, &CallWindow::onMediaConnected, Qt::QueuedConnection);
        connect(m_tcpPeer, &MediaTcpPeer::failed, this, [this](const QString& reason) {
            m_statusLabel->setText("TCP media failed");
            m_statusLabel->setStyleSheet("color:#F38BA8;font-size:13px;");
            m_overlayLabel->setText(reason);
            m_overlayLabel->setVisible(true);
        });
        if (!m_tcpPeer->start()) {
            m_overlayLabel->setText("Could not start TCP media channel.");
            m_statusLabel->setText("Media failed");
        }
    }

    connect(m_pipeline, &MediaPipeline::remoteVideoImage, this, &CallWindow::onRemoteFrame, Qt::QueuedConnection);
    connect(m_pipeline, &MediaPipeline::localVideoImage,  this, &CallWindow::onLocalFrame,  Qt::QueuedConnection);
    m_pipeline->setMuted(m_muted);
    m_pipeline->setScreenAudioEnabled(m_chkScreenAudio ? m_chkScreenAudio->isChecked() : true);
    if (m_mode == CallMode::VideoScreen) {
        m_screenOn = true;
        if (m_btnScreen) { m_btnScreen->setText("Stop"); applyIcon(m_btnScreen, "stop", 18); }
        m_pipeline->setScreenSharing(true);
    }

    if (!m_pipeline->startCapture()) {
        m_overlayLabel->setText(m_mode == CallMode::Voice
                                ? "Could not start microphone capture. Check Windows microphone permission."
                                : "Could not start microphone/camera capture. Check Windows camera/microphone permission.");
        m_statusLabel->setText("Capture failed");
    } else if (!m_useRtcMedia) {
        m_statusLabel->setText("LAN media ready");
        m_statusLabel->setStyleSheet("color:#A6E3A1;font-size:13px;");
        if (m_mode == CallMode::Voice)
            m_overlayLabel->setText("Voice call connected. Waiting for audio…");
        else
            m_overlayLabel->setText("Call connected. Waiting for remote video…");
    }

    if (m_rtcPeer && m_useRtcMedia && m_initiator) {
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
    if (m_tcpPeer) { m_tcpPeer->stop(); m_tcpPeer->deleteLater(); m_tcpPeer = nullptr; }
    if (m_udpPeer) { m_udpPeer->stop(); m_udpPeer->deleteLater(); m_udpPeer = nullptr; }
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

void CallWindow::pollPing()
{
    if (m_pingInFlight || m_peerIp.isEmpty()) return;
    m_pingInFlight = true;

    auto* socket = new QTcpSocket(this);
    auto timer = QSharedPointer<QElapsedTimer>::create();
    auto done = QSharedPointer<bool>::create(false);
    timer->start();

    auto finish = [this, socket, timer, done](int ms) {
        if (*done) return;
        *done = true;
        m_pingInFlight = false;
        if (m_pingLabel) {
            if (ms >= 0) {
                m_pingLabel->setText(QString("%1 ms").arg(ms));
                m_pingLabel->setStyleSheet("color:#A6E3A1;font-size:11px;background:#181825;border:1px solid #313244;border-radius:10px;padding:3px 8px;");
            } else {
                m_pingLabel->setText("Ping timeout");
                m_pingLabel->setStyleSheet("color:#F38BA8;font-size:11px;background:#181825;border:1px solid #313244;border-radius:10px;padding:3px 8px;");
            }
        }
        socket->abort();
        socket->deleteLater();
    };

    connect(socket, &QTcpSocket::connected, this, [finish, timer]() mutable {
        finish(static_cast<int>(timer->elapsed()));
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [finish](QAbstractSocket::SocketError) mutable {
        finish(-1);
    });
    QTimer::singleShot(900, this, [finish]() mutable { finish(-1); });
    socket->connectToHost(m_peerIp, MediaSettings::SignalingPort);
}

void CallWindow::onMediaConnected()
{
    m_overlayLabel->setVisible(false);
    m_statusLabel->setText("● LAN media connected");
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
#ifdef HAS_WEBRTC
    if (m_pipeline) m_pipeline->setMuted(m_muted);
#endif
#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
    if (m_audioSender) m_audioSender->muted = m_muted;
#endif
    m_btnMute->setText(m_muted ? "Unmute" : "Mute");
    applyIcon(m_btnMute, m_muted ? "stop" : "voice", 18);
}

void CallWindow::onCamera()
{
    m_cameraOn = !m_cameraOn;
    m_btnCamera->setText(m_cameraOn ? "Camera" : "Cam off");
    applyIcon(m_btnCamera, "video", 18);
#ifdef HAS_WEBRTC
    if (m_pipeline) m_pipeline->setCameraEnabled(m_cameraOn);
#endif
#if defined(HAS_MULTIMEDIA) || defined(HAS_OPENCV)
    if (m_videoSender) { m_videoSender->stop(); if (m_cameraOn) m_videoSender->start(); }
#endif
}

void CallWindow::onScreen()
{
    m_screenOn = !m_screenOn;
    m_btnScreen->setText(m_screenOn ? "Stop" : "Screen");
    applyIcon(m_btnScreen, m_screenOn ? "stop" : "screen", 18);
    m_screenAudioPanel->setVisible(m_screenOn);

#ifdef HAS_WEBRTC
    if (m_pipeline) {
        m_pipeline->setScreenSharing(m_screenOn);
        m_pipeline->setScreenAudioEnabled(!m_screenOn || (m_chkScreenAudio && m_chkScreenAudio->isChecked()));
    }
#endif
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
#ifdef HAS_WEBRTC
    if (m_pipeline)
        m_pipeline->setScreenAudioEnabled(!m_screenOn || (m_chkScreenAudio && m_chkScreenAudio->isChecked()));
#endif
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
