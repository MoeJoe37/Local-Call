#pragma once

#include <QObject>
#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>

#include "CallStats.h"
#include "MediaPacket.h"

class QTimer;

/// Common behaviour for every media transport: chunking on send, reassembly on
/// receive, loopback rejection, RTT probing and throughput accounting.
///
/// Subclasses only implement the wire I/O — writeDatagram() for one chunk and
/// start/stop for the sockets.  The chunking and reassembly logic is shared so
/// UDP and TCP cannot drift apart, which is what happened when they carried
/// separate LCU1 and LCM2 headers.
class MediaTransport : public QObject {
    Q_OBJECT
public:
    explicit MediaTransport(const QString& peerIp, const QString& localId, QObject* parent = nullptr);
    ~MediaTransport() override;

    virtual bool start() = 0;
    virtual void stop()  = 0;
    virtual QString name() const = 0;

    bool isRunning()   const noexcept { return m_running; }
    bool isConnected() const noexcept { return m_connectedEmitted; }

    /// Fragments payload into LCM3 chunks and hands each to writeChunk().
    /// dropIfCongested is honoured by transports that can queue (TCP) and is
    /// used to shed video rather than let latency grow without bound.
    void sendPacket(MediaPacket::Tag tag, quint8 flags, const QByteArray& payload,
                    bool dropIfCongested = false);

    CallStats stats() const;

public slots:
    void sendPing();

signals:
    void packetReceived(int tag, quint8 flags, quint32 seq, quint32 timestampMs, QByteArray payload);
    void connected();
    void failed(QString reason);
    void statsUpdated(CallStats stats);

protected:
    /// Implemented by subclasses. Returns false when the chunk could not be
    /// queued at all (disconnected socket, congested queue).
    virtual bool writeChunk(const QByteArray& chunk, bool dropIfCongested) = 0;

    /// Subclasses call this for every complete inbound datagram / framed blob.
    void ingest(const QByteArray& packet);

    void markConnected();
    void countBytesOut(int bytes) { m_bytesOut += bytes; }

    quint32 localToken() const noexcept { return m_localToken; }

    QString m_peerIpText;
    bool    m_running{false};

private slots:
    void onStatsTick();

private:
    struct Assembly {
        quint16    expectedChunks{0};
        quint16    receivedChunks{0};
        int        totalBytes{0};
        quint8     flags{0};
        quint32    timestampMs{0};
        QVector<QByteArray> chunks;
    };

    bool tryAssemble(const MediaPacket::Header& h, const QByteArray& payload, QByteArray& frameOut);
    void pruneAssemblies(QMap<quint32, Assembly>& map, quint32 newestSeq);

    quint32 m_localToken{0};
    quint32 m_audioSeq{1};
    quint32 m_videoSeq{1};

    QMap<quint32, Assembly> m_audioAssembly;
    QMap<quint32, Assembly> m_videoAssembly;

    // Loss accounting: highest sequence seen vs. frames actually delivered.
    quint32 m_highestAudioSeq{0};
    quint32 m_deliveredAudio{0};
    quint32 m_windowAudioBase{0};

    qint64  m_bytesIn{0};
    qint64  m_bytesOut{0};
    int     m_framesIn{0};
    int     m_framesOut{0};
    int     m_droppedOut{0};

    QTimer*       m_statsTimer{nullptr};
    QElapsedTimer m_clock;
    qint64        m_lastPingSentMs{-1};
    int           m_rttMs{-1};

    mutable CallStats m_stats;
    bool          m_connectedEmitted{false};
};
