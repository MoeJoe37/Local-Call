#pragma once
#include <QObject>
#include <QImage>
#include <QThread>
#include <QString>
#include <optional>
#include <atomic>
#include <utility>
#include "MediaSettings.h"

enum class MediaMode { Camera, Screen, Audio };

class MediaWorker : public QObject {
    Q_OBJECT
public:
    explicit MediaWorker(MediaMode mode, const QString& targetIp, int port,
                         bool isReceiver = false, QObject* parent = nullptr);
    ~MediaWorker();

    void start();
    void stop();

    std::atomic<bool> muted{false};
    std::atomic<bool> muteAudioOnScreen{false};
    std::atomic<int>  jpegQuality{65};  // 1-100, used by camera and screen sender
    std::optional<std::pair<int,int>> targetRes = std::make_pair(640, 360);
    int targetFps = 30;

signals:
    void frameReceived(QImage frame);
    void connected();

private:
    void runSender();
    void runReceiver();
    void sendAudio();
    void sendCamera();
    void sendScreen();
    void recvAudio();
    void recvVideo();

    MediaMode m_mode;
    QString   m_targetIp;
    int       m_port;
    bool      m_isReceiver;

    QThread*          m_thread = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connectedFired{false};
};
