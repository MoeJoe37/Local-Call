#include "MediaWorker.h"
#include <QUdpSocket>
#include <QThread>

#ifdef HAS_MULTIMEDIA
#  include <QAudioSource>
#  include <QAudioSink>
#  include <QMediaDevices>
#  include <QAudioDevice>
#  include <QBuffer>
#endif

#ifdef HAS_OPENCV
#  include <QScreen>
#  include <QGuiApplication>
#  include <QPixmap>
#  include <opencv2/opencv.hpp>
#endif

MediaWorker::MediaWorker(MediaMode mode, const QString& targetIp, int port,
                         bool isReceiver, QObject* parent)
    : QObject(parent), m_mode(mode), m_targetIp(targetIp),
      m_port(port), m_isReceiver(isReceiver)
{}

MediaWorker::~MediaWorker() { stop(); }

void MediaWorker::start()
{
    m_running = true;
    m_thread  = QThread::create([this]() {
        if (m_isReceiver) runReceiver();
        else              runSender();
    });
    m_thread->start();
}

void MediaWorker::stop()
{
    m_running = false;
    if (m_thread) { m_thread->wait(3000); m_thread->deleteLater(); m_thread = nullptr; }
}

// ── Sender ────────────────────────────────────────────────────────────────────
void MediaWorker::runSender()
{
    switch (m_mode) {
        case MediaMode::Audio:  sendAudio();  break;
        case MediaMode::Camera: sendCamera(); break;
        case MediaMode::Screen: sendScreen(); break;
    }
}

void MediaWorker::sendAudio()
{
#ifdef HAS_MULTIMEDIA
    QAudioFormat fmt;
    fmt.setSampleRate(44100);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);

    auto device = QMediaDevices::defaultAudioInput();
    QAudioSource src(device, fmt);
    QBuffer buf;
    buf.open(QIODevice::ReadWrite);
    src.start(&buf);

    QUdpSocket sock;
    while (m_running) {
        QThread::msleep(20);
        buf.seek(0);
        QByteArray chunk = buf.readAll();
        buf.buffer().clear();
        buf.seek(0);
        if (chunk.isEmpty()) continue;
        if (muted || muteAudioOnScreen) chunk.fill('\0');
        sock.writeDatagram(chunk, QHostAddress(m_targetIp), m_port);
    }
    src.stop();
#endif
}

void MediaWorker::sendCamera()
{
#ifdef HAS_OPENCV
    cv::VideoCapture cap(0);
    if (!cap.isOpened()) return;

    QUdpSocket sock;
    cv::Mat frame;

    while (m_running) {
        auto start = std::chrono::steady_clock::now();
        if (!cap.read(frame) || frame.empty()) { QThread::msleep(10); continue; }

        cv::Mat toSend = frame;
        cv::Mat resized;
        if (targetRes) {
            cv::resize(frame, resized, cv::Size(targetRes->first, targetRes->second));
            toSend = resized;
        }

        std::vector<uchar> jpg;
        cv::imencode(".jpg", toSend, jpg, {cv::IMWRITE_JPEG_QUALITY, jpegQuality.load()});

        const int maxChunk = 60000;
        for (size_t i = 0; i < jpg.size(); i += maxChunk) {
            size_t end = std::min(i + (size_t)maxChunk, jpg.size());
            bool isLast = (end >= jpg.size());
            QByteArray pkt(8 + (int)(end - i), '\0');
            pkt[0] = isLast ? 1 : 0;
            uint32_t cl = end - i;
            pkt[4]=(cl>>24)&0xFF; pkt[5]=(cl>>16)&0xFF; pkt[6]=(cl>>8)&0xFF; pkt[7]=cl&0xFF;
            memcpy(pkt.data()+8, jpg.data()+i, end-i);
            sock.writeDatagram(pkt, QHostAddress(m_targetIp), m_port);
        }
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        double delay = (1.0/targetFps) - elapsed;
        if (delay > 0) QThread::msleep((int)(delay*1000));
    }
#endif
}

void MediaWorker::sendScreen()
{
#ifdef HAS_OPENCV
    QUdpSocket sock;
    while (m_running) {
        auto start = std::chrono::steady_clock::now();

        QScreen* screen = QGuiApplication::primaryScreen();
        if (!screen) { QThread::msleep(33); continue; }

        QImage img = screen->grabWindow(0).toImage().convertToFormat(QImage::Format_RGB888);
        cv::Mat mat(img.height(), img.width(), CV_8UC3,
                    const_cast<uchar*>(img.bits()), img.bytesPerLine());
        cv::Mat bgr;
        cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);

        if (targetRes) {
            cv::Mat r;
            cv::resize(bgr, r, cv::Size(targetRes->first, targetRes->second));
            bgr = r;
        }

        std::vector<uchar> jpg;
        cv::imencode(".jpg", bgr, jpg, {cv::IMWRITE_JPEG_QUALITY, jpegQuality.load()});

        const int maxChunk = 60000;
        for (size_t i = 0; i < jpg.size(); i += maxChunk) {
            size_t end = std::min(i+(size_t)maxChunk, jpg.size());
            bool isLast = (end >= jpg.size());
            QByteArray pkt(8+(int)(end-i), '\0');
            pkt[0]=isLast?1:0;
            uint32_t cl=end-i;
            pkt[4]=(cl>>24)&0xFF; pkt[5]=(cl>>16)&0xFF; pkt[6]=(cl>>8)&0xFF; pkt[7]=cl&0xFF;
            memcpy(pkt.data()+8, jpg.data()+i, end-i);
            sock.writeDatagram(pkt, QHostAddress(m_targetIp), m_port);
        }
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        double delay = (1.0/targetFps) - elapsed;
        if (delay > 0) QThread::msleep((int)(delay*1000));
    }
#endif
}

// ── Receiver ──────────────────────────────────────────────────────────────────
void MediaWorker::runReceiver()
{
    if (m_mode == MediaMode::Audio) recvAudio();
    else                             recvVideo();
}

void MediaWorker::recvAudio()
{
#ifdef HAS_MULTIMEDIA
    QUdpSocket sock;
    sock.bind(QHostAddress::AnyIPv4, m_port,
              QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    QAudioFormat fmt;
    fmt.setSampleRate(44100);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);

    QAudioSink sink(QMediaDevices::defaultAudioOutput(), fmt);
    QBuffer audioBuf;
    audioBuf.open(QIODevice::ReadWrite);
    sink.start(&audioBuf);

    QHostAddress sender;
    quint16 senderPort;
    while (m_running) {
        if (!sock.waitForReadyRead(500)) continue;
        while (sock.hasPendingDatagrams()) {
            QByteArray data(sock.pendingDatagramSize(), '\0');
            sock.readDatagram(data.data(), data.size(), &sender, &senderPort);
            if (!m_connectedFired.exchange(true)) emit connected();
            qint64 pos = audioBuf.pos();
            audioBuf.seek(audioBuf.size());
            audioBuf.write(data);
            audioBuf.seek(pos);
        }
    }
    sink.stop();
#endif
}

void MediaWorker::recvVideo()
{
#ifdef HAS_OPENCV
    QUdpSocket sock;
    sock.bind(QHostAddress::AnyIPv4, m_port,
              QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    QByteArray frameData;
    QHostAddress sender;
    quint16 senderPort;

    while (m_running) {
        if (!sock.waitForReadyRead(500)) continue;
        while (sock.hasPendingDatagrams()) {
            QByteArray pkt(sock.pendingDatagramSize(), '\0');
            sock.readDatagram(pkt.data(), pkt.size(), &sender, &senderPort);
            if (pkt.size() < 8) continue;
            if (!m_connectedFired.exchange(true)) emit connected();

            bool isLast = (pkt[0] != 0);
            frameData.append(pkt.data()+8, pkt.size()-8);
            if (!isLast) continue;

            std::vector<uchar> jpg(frameData.begin(), frameData.end());
            cv::Mat mat = cv::imdecode(jpg, cv::IMREAD_COLOR);
            frameData.clear();
            if (mat.empty()) continue;

            cv::Mat rgb;
            cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
            QImage img(rgb.data, rgb.cols, rgb.rows, (int)rgb.step, QImage::Format_RGB888);
            emit frameReceived(img.copy());
        }
    }
#endif
}
