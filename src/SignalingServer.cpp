#include "nlohmann/json.hpp"
using json = nlohmann::json;
#include "SignalingServer.h"
#include "MediaSettings.h"
#include "Helpers.h"
#include <QHostAddress>

SignalingServer::SignalingServer(QObject* parent) : QObject(parent) {}
SignalingServer::~SignalingServer() { stop(); }

void SignalingServer::setIdentity(const QString& id, const QString& name)
{
    m_myId = id; m_myName = name;
}

void SignalingServer::start()
{
    if (m_server && m_server->isListening()) return;
    if (m_server) { m_server->deleteLater(); m_server = nullptr; }
    m_server = new SigTcpServer(this);
    m_server->setMaxPendingConnections(64);
    if (!m_server->listen(QHostAddress::Any, MediaSettings::SignalingPort)) {
        qWarning("SignalingServer: cannot listen on port %d", MediaSettings::SignalingPort);
        return;
    }
    connect(m_server, &SigTcpServer::newDescriptor,
            this,     &SignalingServer::spawnWorker);
}

void SignalingServer::stop()
{
    if (!m_server) return;
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
}

void SignalingServer::spawnWorker(qintptr fd)
{
    // Create a worker that handles the connection event-driven (readyRead signal)
    // on a dedicated thread with a running event loop.
    //
    // Qt documentation explicitly warns that waitForReadyRead() "may fail randomly
    // on Windows" — so we use the event-driven approach instead.
    auto* thread = new QThread(this);
    auto* worker = new SocketWorker(fd, m_myId, m_myName);
    worker->moveToThread(thread);

    // Forward the parsed message back to the main thread
    connect(worker, &SocketWorker::messageReady,
            this,   &SignalingServer::messageReceived,
            Qt::QueuedConnection);

    // When the worker signals it's done, clean everything up
    connect(worker, &SocketWorker::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished,      worker, &QObject::deleteLater);
    connect(thread, &QThread::finished,      thread, &QThread::deleteLater);

    // Call worker->init() once the thread's event loop is running.
    // AutoConnection: sender (thread) and receiver (worker) are on different
    // threads at connect-time, so Qt uses QueuedConnection — init() is called
    // as an event once exec() has started. This is the correct Qt pattern.
    connect(thread, &QThread::started, worker, &SocketWorker::init);

    thread->start();
}
