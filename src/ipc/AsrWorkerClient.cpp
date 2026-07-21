#include <breezedesk/ipc/AsrWorkerClient.h>

#include <QtCore/QUuid>

namespace BreezeDesk::Ipc {

AsrWorkerClient::AsrWorkerClient(QString clientVersion, QObject* parent)
    : IAsrWorkerClient(parent), m_clientVersion(std::move(clientVersion)) {
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        Envelope hello;
        hello.type = MessageType::Hello;
        hello.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        hello.workerVersion = m_clientVersion;
        hello.payload.insert(QStringLiteral("clientVersion"), m_clientVersion);
        sendEnvelope(std::move(hello));
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &AsrWorkerClient::readAvailable);
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        const bool wasConnected = m_ready;
        m_ready = false;
        m_heartbeatTimer.stop();
        if (wasConnected || m_socket.error() == QLocalSocket::UnknownSocketError) {
            emit disconnected();
        }
    });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error) {
        if (error != QLocalSocket::PeerClosedError) {
            fail(ProtocolErrorCode::SocketError, m_socket.errorString());
        }
    });
    m_heartbeatTimer.setInterval(kHeartbeatIntervalMs);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &AsrWorkerClient::heartbeat);
}

void AsrWorkerClient::connectToWorker(const QString& serverName, const QByteArray& sessionToken) {
    disconnectFromWorker();
    m_decoder.reset();
    m_sessionToken = sessionToken;
    m_socket.connectToServer(serverName, QIODevice::ReadWrite);
}

void AsrWorkerClient::disconnectFromWorker() {
    m_heartbeatTimer.stop();
    m_ready = false;
    if (m_socket.state() != QLocalSocket::UnconnectedState) {
        m_socket.abort();
    }
}

bool AsrWorkerClient::isReady() const noexcept {
    return m_ready;
}

QString AsrWorkerClient::sendRequest(MessageType type, const QString& jobId, const QCborMap& payload) {
    if (!m_ready) {
        fail(ProtocolErrorCode::HandshakeRequired, QStringLiteral("Worker connection is not ready"));
        return {};
    }
    Envelope envelope;
    envelope.type = type;
    envelope.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    envelope.jobId = jobId;
    envelope.payload = payload;
    const QString requestId = envelope.requestId;
    sendEnvelope(std::move(envelope));
    return requestId;
}

void AsrWorkerClient::sendEnvelope(Envelope envelope) {
    envelope.protocolVersion = kProtocolVersion;
    envelope.sessionToken = m_sessionToken;
    envelope.workerVersion = m_clientVersion;
    ProtocolError error;
    const QByteArray frame = FrameCodec::encode(envelope, &error);
    if (frame.isEmpty()) {
        emit protocolError(error);
        return;
    }
    if (m_socket.write(frame) != frame.size()) {
        fail(ProtocolErrorCode::SocketError, m_socket.errorString());
        return;
    }
    // flush() is only a best-effort, non-blocking nudge. A false result means
    // that no bytes were written immediately, not that the queued frame failed.
    // The event loop will continue draining the buffer.
    (void)m_socket.flush();
}

void AsrWorkerClient::readAvailable() {
    const auto parsed = m_decoder.append(m_socket.readAll());
    if (parsed.error.isError()) {
        fail(parsed.error.code, parsed.error.detail);
        return;
    }
    for (const auto& envelope : parsed.envelopes) {
        processEnvelope(envelope);
    }
}

void AsrWorkerClient::processEnvelope(const Envelope& envelope) {
    if (envelope.protocolVersion != kProtocolVersion) {
        fail(ProtocolErrorCode::ProtocolMismatch, QStringLiteral("Expected protocol %1 but worker sent %2")
                                                      .arg(kProtocolVersion)
                                                      .arg(envelope.protocolVersion));
        return;
    }
    if (envelope.sessionToken != m_sessionToken) {
        fail(ProtocolErrorCode::AuthenticationFailed, QStringLiteral("Worker session token mismatch"));
        return;
    }
    if (!m_ready) {
        if (envelope.type != MessageType::HelloAck) {
            fail(ProtocolErrorCode::HandshakeRequired, QStringLiteral("Expected HelloAck from worker"));
            return;
        }
        m_ready = true;
        m_lastPong.start();
        m_heartbeatTimer.start();
        emit ready();
        return;
    }
    if (envelope.type == MessageType::Pong) {
        m_lastPong.restart();
        return;
    }
    emit envelopeReceived(envelope);
}

void AsrWorkerClient::fail(ProtocolErrorCode code, const QString& detail) {
    emit protocolError({code, detail});
    m_ready = false;
    m_heartbeatTimer.stop();
    if (m_socket.state() != QLocalSocket::UnconnectedState) {
        m_socket.abort();
    }
}

void AsrWorkerClient::heartbeat() {
    if (!m_ready) {
        return;
    }
    if (m_lastPong.isValid() && m_lastPong.elapsed() > kHeartbeatTimeoutMs) {
        fail(ProtocolErrorCode::HeartbeatTimeout, QStringLiteral("Worker heartbeat timed out"));
        return;
    }
    Envelope ping;
    ping.type = MessageType::Ping;
    ping.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    sendEnvelope(std::move(ping));
}

} // namespace BreezeDesk::Ipc
