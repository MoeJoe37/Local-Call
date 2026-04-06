#include "nlohmann/json.hpp"
using json = nlohmann::json;
#include "SignalingClient.h"
#include "MediaSettings.h"
#include <QTcpSocket>
#include <QThread>
#include <QtConcurrent>

namespace SignalingClient {

// ── Mirrors C# TrySendOnceAsync exactly ──────────────────────────────────────
// 1. Connect (4 s timeout)
// 2. Write 4-byte big-endian length prefix
// 3. Write JSON body
// 4. Flush (waitForBytesWritten) — puts data into the OS send buffer
// 5. Graceful shutdown: disconnectFromHost() sends TCP FIN so the server's
//    ReadFully loop sees EOF cleanly instead of an abrupt RST.
//    Without this, the socket destructor calls abort() which sends RST on
//    some platforms, causing the server's read() to fail mid-message.
static bool trySendOnce(const QString& ip, const SigMsg& msg)
{
    try {
        QTcpSocket sock;
        sock.connectToHost(ip, MediaSettings::SignalingPort);
        if (!sock.waitForConnected(4000)) return false;

        auto enc = SigMsgEncode(msg);   // 4-byte header + JSON body, already framed
        sock.write(reinterpret_cast<const char*>(enc.data()),
                   static_cast<qint64>(enc.size()));

        if (!sock.waitForBytesWritten(4000)) return false;

        // Graceful close: send FIN so server ReadFully sees a clean EOF
        sock.disconnectFromHost();
        if (sock.state() != QAbstractSocket::UnconnectedState)
            (void)sock.waitForDisconnected(2000);

        return true;
    } catch (...) { return false; }
}

// ── Public API — mirrors C# SignalingClient.Send / SendReliable ──────────────

void send(const QString& ip, const SigMsg& msg)
{
    QString ipCopy  = ip;
    SigMsg  msgCopy = msg;
    (void)QtConcurrent::run([ipCopy, msgCopy]() { trySendOnce(ipCopy, msgCopy); });
}

void sendReliable(const QString& ip, const SigMsg& msg)
{
    QString ipCopy  = ip;
    SigMsg  msgCopy = msg;
    (void)QtConcurrent::run([ipCopy, msgCopy]() {
        for (int i = 0; i < 3; ++i) {
            if (trySendOnce(ipCopy, msgCopy)) return;
            if (i < 2) QThread::msleep(600);
        }
    });
}

} // namespace SignalingClient
