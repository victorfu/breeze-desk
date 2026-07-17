#include <breezedesk/ipc/WorkerServer.h>

#include <QtCore/QElapsedTimer>
#include <QtNetwork/QLocalSocket>

namespace BreezeDesk::Ipc {

struct WorkerServer::ClientState {
    QLocalSocket* socket = nullptr;
    FrameDecoder decoder;
    QElapsedTimer lastActivity;
    bool ready = false;
};

WorkerServer::WorkerServer(QObject* parent) : QObject(parent) {
    qRegisterMetaType<Envelope>();
    qRegisterMetaType<ProtocolError>();
    connect(&m_server, &QLocalServer::newConnection, this, &WorkerServer::acceptConnections);
    m_watchdog.setInterval(kHeartbeatIntervalMs);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        const auto clientIds = m_clients.keys();
        for (const quint64 clientId : clientIds) {
            const auto state = m_clients.value(clientId);
            if (state && state->lastActivity.isValid() &&
                state->lastActivity.elapsed() > kHeartbeatTimeoutMs) {
                disconnectClient(clientId, {ProtocolErrorCode::HeartbeatTimeout,
                                            QStringLiteral("Client heartbeat timed out")});
            }
        }
    });
}

WorkerServer::~WorkerServer() {
    close();
}

bool WorkerServer::listen(const QString& serverName, const QByteArray& sessionToken) {
    close();
    if (serverName.isEmpty() || sessionToken.size() < 16) {
        return false;
    }
    m_sessionToken = sessionToken;
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(serverName) && m_server.serverError() == QAbstractSocket::AddressInUseError) {
        QLocalSocket probe;
        probe.connectToServer(serverName, QIODevice::ReadWrite);
        if (!probe.waitForConnected(150)) {
            QLocalServer::removeServer(serverName);
            m_server.listen(serverName);
        }
    }
    if (m_server.isListening()) {
        m_watchdog.start();
    }
    return m_server.isListening();
}

void WorkerServer::close() {
    m_watchdog.stop();
    const auto clientIds = m_clients.keys();
    for (const quint64 clientId : clientIds) {
        disconnectClient(clientId);
    }
    m_server.close();
    m_sessionToken.clear();
}

bool WorkerServer::isListening() const noexcept {
    return m_server.isListening();
}

QString WorkerServer::errorString() const {
    return m_server.errorString();
}

QList<quint64> WorkerServer::clients() const {
    return m_clients.keys();
}

bool WorkerServer::send(quint64 clientId, Envelope envelope) {
    const auto state = m_clients.value(clientId);
    if (!state || state->socket == nullptr || state->socket->state() != QLocalSocket::ConnectedState) {
        return false;
    }
    envelope.protocolVersion = kProtocolVersion;
    envelope.sessionToken = m_sessionToken;
    ProtocolError error;
    const QByteArray bytes = FrameCodec::encode(envelope, &error);
    if (bytes.isEmpty()) {
        emit protocolError(clientId, error);
        return false;
    }
    return state->socket->write(bytes) == bytes.size();
}

void WorkerServer::broadcast(Envelope envelope) {
    for (const quint64 clientId : m_clients.keys()) {
        send(clientId, envelope);
    }
}

void WorkerServer::acceptConnections() {
    while (m_server.hasPendingConnections()) {
        auto* socket = m_server.nextPendingConnection();
        if (socket == nullptr) {
            continue;
        }
        const quint64 clientId = m_nextClientId++;
        auto state = std::make_shared<ClientState>();
        state->socket = socket;
        state->lastActivity.start();
        m_clients.insert(clientId, state);
        connect(socket, &QLocalSocket::readyRead, this, [this, clientId] { readClient(clientId); });
        connect(socket, &QLocalSocket::disconnected, this, [this, clientId] { disconnectClient(clientId); });
        connect(socket, &QLocalSocket::errorOccurred, this, [this, clientId](QLocalSocket::LocalSocketError) {
            const auto current = m_clients.value(clientId);
            if (current && current->socket != nullptr) {
                emit protocolError(clientId,
                                   {ProtocolErrorCode::SocketError, current->socket->errorString()});
            }
        });
    }
}

void WorkerServer::readClient(quint64 clientId) {
    const auto state = m_clients.value(clientId);
    if (!state || state->socket == nullptr) {
        return;
    }
    state->lastActivity.restart();
    const auto parsed = state->decoder.append(state->socket->readAll());
    if (parsed.error.isError()) {
        disconnectClient(clientId, parsed.error);
        return;
    }
    for (const Envelope& envelope : parsed.envelopes) {
        if (!processEnvelope(clientId, envelope)) {
            return;
        }
    }
}

void WorkerServer::disconnectClient(quint64 clientId, const ProtocolError& error) {
    const auto state = m_clients.take(clientId);
    if (!state) {
        return;
    }
    if (error.isError()) {
        emit protocolError(clientId, error);
    }
    if (state->socket != nullptr) {
        state->socket->disconnect(this);
        state->socket->abort();
        state->socket->deleteLater();
    }
    emit clientDisconnected(clientId);
}

bool WorkerServer::processEnvelope(quint64 clientId, const Envelope& envelope) {
    const auto state = m_clients.value(clientId);
    if (!state) {
        return false;
    }
    if (envelope.protocolVersion != kProtocolVersion) {
        disconnectClient(clientId, {ProtocolErrorCode::ProtocolMismatch,
                                    QStringLiteral("Worker protocol %1 cannot accept protocol %2")
                                        .arg(kProtocolVersion)
                                        .arg(envelope.protocolVersion)});
        return false;
    }
    if (envelope.sessionToken != m_sessionToken) {
        disconnectClient(clientId,
                         {ProtocolErrorCode::AuthenticationFailed, QStringLiteral("Session token mismatch")});
        return false;
    }
    if (!state->ready) {
        if (envelope.type != MessageType::Hello) {
            disconnectClient(clientId, {ProtocolErrorCode::HandshakeRequired,
                                        QStringLiteral("Hello must be the first message")});
            return false;
        }
        state->ready = true;
        Envelope reply;
        reply.type = MessageType::HelloAck;
        reply.requestId = envelope.requestId;
        reply.payload.insert(QStringLiteral("protocolVersion"), kProtocolVersion);
        send(clientId, reply);
        emit clientReady(clientId);
        return true;
    }
    if (envelope.type == MessageType::Ping) {
        Envelope pong;
        pong.type = MessageType::Pong;
        pong.requestId = envelope.requestId;
        send(clientId, pong);
        return true;
    }
    emit envelopeReceived(clientId, envelope);
    return true;
}

} // namespace BreezeDesk::Ipc
