#include "UdpMediaTransport.h"
#include "MediaSettings.h"

#include <QUdpSocket>
#include <QNetworkDatagram>

UdpMediaTransport::UdpMediaTransport(const QString& peerIp, const QString& localId, QObject* parent)
    : MediaTransport(peerIp, localId, parent), m_peerAddress(peerIp)
{}

UdpMediaTransport::~UdpMediaTransport() { stop(); }

bool UdpMediaTransport::start()
{
    if (m_running) return true;
    if (m_peerAddress.isNull()) {
        emit failed(QStringLiteral("Invalid peer IP address"));
        return false;
    }

    m_socket = new QUdpSocket(this);
    const auto flags = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
    if (!m_socket->bind(QHostAddress::AnyIPv4, MediaSettings::MediaUdpPort, flags)) {
        const QString reason = QStringLiteral("Could not bind LAN media UDP port %1: %2")
            .arg(MediaSettings::MediaUdpPort).arg(m_socket->errorString());
        stop();
        emit failed(reason);
        return false;
    }

    connect(m_socket, &QUdpSocket::readyRead, this, &UdpMediaTransport::onReadyRead);
    m_running = true;
    return true;
}

void UdpMediaTransport::stop()
{
    m_running = false;
    if (m_socket) {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

bool UdpMediaTransport::writeChunk(const QByteArray& chunk, bool /*dropIfCongested*/)
{
    if (!m_socket) return false;
    // UDP never queues, so backpressure does not apply: a datagram either goes
    // on the wire now or is lost, which is the correct behaviour for realtime
    // media.
    return m_socket->writeDatagram(chunk, m_peerAddress, MediaSettings::MediaUdpPort) == chunk.size();
}

void UdpMediaTransport::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram();
        const QHostAddress sender = datagram.senderAddress();
        if (!sender.isNull() && !m_peerAddress.isNull() &&
            sender.toIPv4Address() != m_peerAddress.toIPv4Address())
            continue;
        ingest(datagram.data());
    }
}
