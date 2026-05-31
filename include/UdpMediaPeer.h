#pragma once

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QVector>
#include <QMutex>
#include <QHostAddress>
#include <QString>
#include <cstdint>

class QUdpSocket;

class UdpMediaPeer : public QObject {
    Q_OBJECT
public:
    explicit UdpMediaPeer(const QString& peerIp, const QString& localId = QString(), QObject* parent = nullptr);
    ~UdpMediaPeer() override;

    bool start();
    void stop();
    bool isRunning() const noexcept { return m_running; }

public slots:
    void sendVideoFrame(const QByteArray& h264AnnexB);
    void sendAudioFrame(const QByteArray& opusPacket);

signals:
    void remoteVideoFrame(QByteArray h264AnnexB);
    void remoteAudioFrame(QByteArray opusPacket);
    void connected();
    void failed(QString reason);

private slots:
    void onAudioReadyRead();
    void onVideoReadyRead();

private:
    struct FrameAssembly {
        quint16 expectedChunks{0};
        quint16 receivedChunks{0};
        int totalBytes{0};
        QVector<QByteArray> chunks;
    };

    void sendFrame(QUdpSocket* socket, quint16 port, const QByteArray& frame,
                   quint32& sequence, char mediaTag);
    void drainSocket(QUdpSocket* socket, bool isVideo);
    bool tryAssemble(bool isVideo, const QByteArray& packet, QByteArray& frameOut);
    void clearAssemblers();

    QString      m_peerIpText;
    QHostAddress m_peerAddress;
    quint32      m_localToken{0};
    QUdpSocket*  m_audioSocket{nullptr};
    QUdpSocket*  m_videoSocket{nullptr};
    bool         m_running{false};
    bool         m_connectedEmitted{false};
    quint32      m_videoSeq{1};
    quint32      m_audioSeq{1};
    QHash<quint32, FrameAssembly> m_videoFrames;
    QHash<quint32, FrameAssembly> m_audioFrames;
    mutable QMutex m_sendMutex;
    mutable QMutex m_recvMutex;
};
