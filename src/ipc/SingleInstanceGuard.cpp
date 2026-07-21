#include <breezedesk/ipc/SingleInstanceGuard.h>

#include <breezedesk/ipc/LocalEndpoint.h>

#include <QtCore/QCborArray>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtCore/QUuid>
#include <QtNetwork/QLocalSocket>

namespace BreezeDesk::Ipc {
namespace {

constexpr qsizetype MaximumArgumentCount = 256;
constexpr qsizetype MaximumArgumentBytes = 32 * 1024;
constexpr qsizetype MaximumCombinedArgumentBytes = 512 * 1024;
constexpr qsizetype MaximumCombinedOutputBytes = 8 * 1024 * 1024;
constexpr int CommandInternalFailureExitCode = 12;

QString lockPath(const QString& applicationId) {
    QString directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    QDir().mkpath(directory);
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(applicationId.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
    return QDir(directory).filePath(QStringLiteral("breezedesk-%1.lock").arg(digest));
}

void writeEnvelopeAndDisconnect(QLocalSocket* socket, const Envelope& envelope) {
    const QByteArray frame = FrameCodec::encode(envelope);
    QObject::connect(socket, &QLocalSocket::bytesWritten, socket, [socket](qint64) {
        if (socket->bytesToWrite() == 0) {
            socket->disconnectFromServer();
        }
    });
    if (frame.isEmpty() || socket->write(frame) != frame.size()) {
        socket->abort();
    } else if (socket->bytesToWrite() == 0) {
        socket->disconnectFromServer();
    }
}

bool decodeCommandArguments(const QCborMap& payload, QStringList* arguments) {
    const QCborValue value = payload.value(QStringLiteral("arguments"));
    if (arguments == nullptr || !value.isArray()) {
        return false;
    }
    const QCborArray encoded = value.toArray();
    if (encoded.isEmpty() || encoded.size() > MaximumArgumentCount) {
        return false;
    }
    qsizetype combinedBytes = 0;
    for (const QCborValue& argument : encoded) {
        if (!argument.isString()) {
            return false;
        }
        const QString decoded = argument.toString();
        const qsizetype bytes = decoded.toUtf8().size();
        combinedBytes += bytes;
        if (bytes > MaximumArgumentBytes || combinedBytes > MaximumCombinedArgumentBytes) {
            return false;
        }
        arguments->append(decoded);
    }
    return true;
}

} // namespace

SingleInstanceGuard::SingleInstanceGuard(QString applicationId, QObject* parent)
    : QObject(parent), m_applicationId(std::move(applicationId)),
      m_endpointName(LocalEndpoint::userScopedName(m_applicationId, QStringLiteral("application"))),
      m_lockFile(std::make_unique<QLockFile>(lockPath(m_applicationId))) {
    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstanceGuard::acceptConnections);
}

SingleInstanceGuard::~SingleInstanceGuard() {
    m_server.close();
    if (m_primary) {
        QLocalServer::removeServer(m_endpointName);
        m_lockFile->unlock();
    }
}

SingleInstanceGuard::AcquireResult SingleInstanceGuard::acquire(const QStringList& filePaths,
                                                                int forwardingTimeoutMs) {
    if (m_applicationId.trimmed().isEmpty()) {
        m_error = QStringLiteral("Application ID must not be empty");
        return AcquireResult::Error;
    }
    if (m_primary) {
        return AcquireResult::Primary;
    }

    if (m_lockFile->tryLock(0)) {
        if (!startPrimary()) {
            m_lockFile->unlock();
            return AcquireResult::Error;
        }
        m_primary = true;
        return AcquireResult::Primary;
    }

    if (forwardToPrimary(filePaths, qMax(1, forwardingTimeoutMs))) {
        return AcquireResult::Forwarded;
    }
    return AcquireResult::Error;
}

bool SingleInstanceGuard::isPrimary() const noexcept {
    return m_primary;
}

QString SingleInstanceGuard::errorString() const {
    return m_error;
}

QString SingleInstanceGuard::endpointName() const {
    return m_endpointName;
}

void SingleInstanceGuard::setCommandHandler(CommandHandler handler) {
    m_commandHandler = std::move(handler);
}

bool SingleInstanceGuard::startPrimary() {
    // Owning the lock makes stale endpoint cleanup safe against another live primary.
    QLocalServer::removeServer(m_endpointName);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(m_endpointName)) {
        m_error = m_server.errorString();
        return false;
    }
    return true;
}

bool SingleInstanceGuard::forwardToPrimary(const QStringList& filePaths, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QLocalSocket socket;
        socket.connectToServer(m_endpointName, QIODevice::ReadWrite);
        const int connectBudget = qMin(100, timeoutMs - static_cast<int>(timer.elapsed()));
        if (!socket.waitForConnected(qMax(1, connectBudget))) {
            QThread::msleep(20);
            continue;
        }

        Envelope activation;
        activation.type = MessageType::ActivateApplication;
        activation.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QCborArray paths;
        for (const QString& filePath : filePaths) {
            paths.append(filePath);
        }
        activation.payload.insert(QStringLiteral("filePaths"), paths);
        const QByteArray frame = FrameCodec::encode(activation);
        if (socket.write(frame) != frame.size()) {
            m_error = socket.errorString();
            return false;
        }
        // A fast primary can consume the request and close its pipe before this wait observes a
        // bytes-written signal. Only fail while Qt still has request bytes queued; the matching
        // reply below remains the success condition.
        if (socket.bytesToWrite() > 0 &&
            !socket.waitForBytesWritten(qMax(1, timeoutMs - static_cast<int>(timer.elapsed()))) &&
            socket.bytesToWrite() > 0) {
            m_error = socket.errorString();
            return false;
        }

        FrameDecoder decoder;
        while (timer.elapsed() < timeoutMs) {
            if (socket.bytesAvailable() == 0 &&
                !socket.waitForReadyRead(qMin(100, timeoutMs - static_cast<int>(timer.elapsed())))) {
                continue;
            }
            const auto parsed = decoder.append(socket.readAll());
            if (parsed.error.isError()) {
                m_error = parsed.error.detail;
                return false;
            }
            for (const auto& reply : parsed.envelopes) {
                if (reply.type == MessageType::ActivationAccepted &&
                    reply.requestId == activation.requestId) {
                    return true;
                }
            }
        }
    }
    m_error = QStringLiteral("Timed out while forwarding activation to the primary instance");
    return false;
}

void SingleInstanceGuard::acceptConnections() {
    while (m_server.hasPendingConnections()) {
        QLocalSocket* socket = m_server.nextPendingConnection();
        if (socket == nullptr) {
            continue;
        }
        m_decoders.insert(socket, std::make_shared<FrameDecoder>());
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readSocket(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_decoders.remove(socket);
            socket->deleteLater();
        });
    }
}

void SingleInstanceGuard::readSocket(QLocalSocket* socket) {
    const auto decoder = m_decoders.value(socket);
    if (!decoder) {
        return;
    }
    const auto parsed = decoder->append(socket->readAll());
    if (parsed.error.isError()) {
        socket->abort();
        return;
    }
    if (!parsed.envelopes.isEmpty()) {
        const auto& envelope = parsed.envelopes.constFirst();
        if (envelope.protocolVersion != kProtocolVersion || envelope.requestId.isEmpty()) {
            socket->abort();
            return;
        }
        if (envelope.type == MessageType::ActivateApplication) {
            QStringList paths;
            const auto pathsValue = envelope.payload.value(QStringLiteral("filePaths"));
            if (!pathsValue.isArray()) {
                socket->abort();
                return;
            }
            for (const auto& path : pathsValue.toArray()) {
                if (!path.isString()) {
                    socket->abort();
                    return;
                }
                paths.append(path.toString());
            }
            emit activationRequested(paths);
            Envelope reply;
            reply.type = MessageType::ActivationAccepted;
            reply.requestId = envelope.requestId;
            writeEnvelopeAndDisconnect(socket, reply);
            return;
        }
        if (envelope.type == MessageType::ApplicationCommand) {
            QStringList arguments;
            if (!decodeCommandArguments(envelope.payload, &arguments)) {
                socket->abort();
                return;
            }
            ApplicationCommandReply commandReply;
            const bool ready = static_cast<bool>(m_commandHandler);
            if (ready) {
                commandReply = m_commandHandler(arguments);
            }
            if (commandReply.standardOutput.size() + commandReply.standardError.size() >
                MaximumCombinedOutputBytes) {
                commandReply.handled = true;
                commandReply.exitCode = CommandInternalFailureExitCode;
                commandReply.standardOutput.clear();
                commandReply.standardError =
                    QByteArrayLiteral("The GUI command output exceeded the local IPC limit.\n");
            }
            Envelope reply;
            reply.type = MessageType::ApplicationCommandResult;
            reply.requestId = envelope.requestId;
            reply.payload.insert(QStringLiteral("handled"), commandReply.handled);
            reply.payload.insert(QStringLiteral("retryable"), !ready);
            reply.payload.insert(QStringLiteral("exitCode"), qBound(0, commandReply.exitCode, 255));
            reply.payload.insert(QStringLiteral("stdout"), commandReply.standardOutput);
            reply.payload.insert(QStringLiteral("stderr"), commandReply.standardError);
            writeEnvelopeAndDisconnect(socket, reply);
            return;
        }
        socket->abort();
        return;
    }
}

} // namespace BreezeDesk::Ipc
