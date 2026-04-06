#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QString>
#include <QByteArray>
#include "SigMsg.h"
#include "Helpers.h"

// ── SocketWorker ──────────────────────────────────────────────────────────────
// Owns a QTcpSocket and lives on a dedicated QThread that runs exec().
// Uses the event-driven readyRead signal to accumulate bytes into a buffer,
// then parses the message once the full framed payload has arrived.
//
// Why not waitForReadyRead():
//   Qt documentation explicitly warns: "This function may fail randomly on
//   Windows." (QAbstractSocket::waitForReadyRead).  Event-driven I/O via
//   readyRead + a running event loop is the correct, reliable Qt approach on
//   all platforms.
class SocketWorker : public QObject {
    Q_OBJECT
public:
    SocketWorker(qintptr fd, const QString& myId, const QString& myName,
                 QObject* parent = nullptr)
        : QObject(parent), m_fd(fd), m_myId(myId), m_myName(myName) {}

signals:
    void messageReady(SigMsg msg, QString ip);
    void finished();

public slots:
    void init()   // called once the worker is running on its thread
    {
        m_socket = new QTcpSocket(this);
        if (!m_socket->setSocketDescriptor(m_fd)) {
            emit finished();
            return;
        }
        m_ip = m_socket->peerAddress().toString().replace("::ffff:", "");

        connect(m_socket, &QTcpSocket::readyRead,
                this,     &SocketWorker::onReadyRead);
        connect(m_socket, &QTcpSocket::disconnected,
                this,     &SocketWorker::onDisconnected);
        connect(m_socket,
                static_cast<void(QTcpSocket::*)(QAbstractSocket::SocketError)>(
                    &QTcpSocket::errorOccurred),
                this, [this](QAbstractSocket::SocketError){
                    emit finished();
                });

        // Guard against the client having already sent everything and closed
        // before we connected the signal — drain what's already buffered.
        if (m_socket->bytesAvailable() > 0)
            onReadyRead();
    }

private slots:
    void onReadyRead()
    {
        m_buf += m_socket->readAll();
        tryParse();
    }

    void onDisconnected()
    {
        // Client closed — drain any remaining buffered bytes then finish
        if (m_socket->bytesAvailable() > 0)
            m_buf += m_socket->readAll();
        tryParse();
        emit finished();
    }

private:
    void tryParse()
    {
        // Need at least a 4-byte length header
        if (m_buf.size() < 4) return;

        uint32_t len = ((uint8_t)m_buf[0] << 24) | ((uint8_t)m_buf[1] << 16)
                     | ((uint8_t)m_buf[2] <<  8) |  (uint8_t)m_buf[3];

        if (len == 0 || len > 60u * 1024 * 1024) { emit finished(); return; }
        if ((uint32_t)m_buf.size() < 4 + len) return; // not yet complete

        QByteArray body = m_buf.mid(4, (int)len);

        try {
            using json = nlohmann::json;
            auto msg = json::parse(body.toStdString()).get<SigMsg>();

            if (msg.type == SigType::DiscProbe) {
                // Reply inline
                SigMsg resp;
                resp.type      = SigType::DiscResp;
                resp.from_id   = m_myId.toStdString();
                resp.from_name = m_myName.toStdString();
                resp.ts        = Helpers::nowMs();
                auto enc = SigMsgEncode(resp);
                m_socket->write(reinterpret_cast<const char*>(enc.data()), enc.size());
                m_socket->flush();
            } else {
                emit messageReady(msg, m_ip);
            }
        } catch (...) {}

        emit finished();
    }

    qintptr    m_fd;
    QString    m_myId;
    QString    m_myName;
    QString    m_ip;
    QTcpSocket* m_socket = nullptr;
    QByteArray  m_buf;
};

// ── SigTcpServer ─────────────────────────────────────────────────────────────
class SigTcpServer : public QTcpServer {
    Q_OBJECT
public:
    explicit SigTcpServer(QObject* parent = nullptr) : QTcpServer(parent) {}
signals:
    void newDescriptor(qintptr fd);
protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        emit newDescriptor(socketDescriptor);
    }
};

// ── SignalingServer ───────────────────────────────────────────────────────────
class SignalingServer : public QObject {
    Q_OBJECT
public:
    explicit SignalingServer(QObject* parent = nullptr);
    ~SignalingServer();

    void setIdentity(const QString& id, const QString& name);
    void start();
    void stop();

signals:
    void messageReceived(SigMsg msg, QString ip);

private slots:
    void spawnWorker(qintptr fd);

private:
    SigTcpServer* m_server = nullptr;
    QString       m_myId;
    QString       m_myName;
};
