#include <breezedesk/ipc/ApplicationCommand.h>

#include <breezedesk/app_config.h>
#include <breezedesk/ipc/FrameCodec.h>
#include <breezedesk/ipc/LocalEndpoint.h>

#include <QtCore/QCborArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QThread>
#include <QtCore/QUuid>
#include <QtNetwork/QLocalSocket>

namespace BreezeDesk::Ipc {
namespace {

constexpr qsizetype MaximumArgumentCount = 256;
constexpr qsizetype MaximumArgumentBytes = 32 * 1024;
constexpr qsizetype MaximumCombinedArgumentBytes = 512 * 1024;
constexpr qsizetype MaximumCombinedOutputBytes = 8 * 1024 * 1024;
constexpr int ConnectionAttemptMs = 50;
constexpr int MaximumUnavailableAttempts = 3;
constexpr int RetryDelayMs = 20;
constexpr int MinimumRetryRoundTripBudgetMs = ConnectionAttemptMs;

QString unavailableGuiMessage() {
    return QStringLiteral("No running %1 GUI was found.").arg(QString::fromLatin1(AppConfig::ProductName));
}

ApplicationCommandForwardResult failed(ApplicationCommandForwardStatus status, const QString& message) {
    ApplicationCommandForwardResult result;
    result.status = status;
    result.error = message;
    return result;
}

bool argumentsAreValid(const QStringList& arguments, QString* error) {
    if (arguments.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("The forwarded command must not be empty.");
        }
        return false;
    }
    if (arguments.size() > MaximumArgumentCount) {
        if (error != nullptr) {
            *error = QStringLiteral("The forwarded command contains too many arguments.");
        }
        return false;
    }
    qsizetype combinedBytes = 0;
    for (const QString& argument : arguments) {
        const qsizetype bytes = argument.toUtf8().size();
        if (bytes > MaximumArgumentBytes) {
            if (error != nullptr) {
                *error = QStringLiteral("A forwarded command argument is too large.");
            }
            return false;
        }
        combinedBytes += bytes;
        if (combinedBytes > MaximumCombinedArgumentBytes) {
            if (error != nullptr) {
                *error = QStringLiteral("The forwarded command is too large.");
            }
            return false;
        }
    }
    return true;
}

} // namespace

ApplicationCommandForwardResult ApplicationCommandClient::forward(const QString& applicationId,
                                                                  const QStringList& arguments,
                                                                  const int timeoutMs) {
    if (applicationId.trimmed().isEmpty()) {
        return failed(ApplicationCommandForwardStatus::ProtocolError,
                      QStringLiteral("Application ID must not be empty."));
    }
    QString validationError;
    if (!argumentsAreValid(arguments, &validationError)) {
        return failed(ApplicationCommandForwardStatus::ProtocolError, validationError);
    }

    const int boundedTimeout = qMax(1, timeoutMs);
    const QString endpoint = LocalEndpoint::userScopedName(applicationId, QStringLiteral("application"));
    QElapsedTimer timer;
    timer.start();
    bool connectedAtLeastOnce = false;
    bool primaryExplicitlyNotReady = false;
    int unavailableAttempts = 0;

    while (timer.elapsed() < boundedTimeout) {
        QLocalSocket socket;
        socket.connectToServer(endpoint, QIODevice::ReadWrite);
        const int connectBudget =
            qMin(ConnectionAttemptMs, boundedTimeout - static_cast<int>(timer.elapsed()));
        if (!socket.waitForConnected(qMax(1, connectBudget))) {
            ++unavailableAttempts;
            if (unavailableAttempts >= MaximumUnavailableAttempts) {
                return failed(ApplicationCommandForwardStatus::Unavailable, unavailableGuiMessage());
            }
            if (timer.elapsed() < boundedTimeout) {
                QThread::msleep(RetryDelayMs);
            }
            continue;
        }
        connectedAtLeastOnce = true;

        Envelope command;
        command.type = MessageType::ApplicationCommand;
        command.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QCborArray encodedArguments;
        for (const QString& argument : arguments) {
            encodedArguments.append(argument);
        }
        command.payload.insert(QStringLiteral("arguments"), encodedArguments);
        const QByteArray frame = FrameCodec::encode(command);
        const int writeBudget = boundedTimeout - static_cast<int>(timer.elapsed());
        if (socket.write(frame) != frame.size()) {
            return failed(ApplicationCommandForwardStatus::Indeterminate,
                          QStringLiteral("The GUI command may have been received, but its result "
                                         "could not be confirmed: %1")
                              .arg(socket.errorString()));
        }
        // A fast GUI can consume the request and close its pipe before this wait observes a
        // bytes-written signal. Only fail while Qt still has request bytes queued; the matching
        // reply below remains the success condition.
        if (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(qMax(1, writeBudget)) &&
            socket.bytesToWrite() > 0) {
            return failed(ApplicationCommandForwardStatus::Indeterminate,
                          QStringLiteral("The GUI command may have been received, but its result "
                                         "could not be confirmed: %1")
                              .arg(socket.errorString()));
        }

        FrameDecoder decoder;
        bool retryWhenReady = false;
        while (timer.elapsed() < boundedTimeout) {
            if (socket.bytesAvailable() == 0) {
                const int readBudget =
                    qMin(ConnectionAttemptMs, boundedTimeout - static_cast<int>(timer.elapsed()));
                if (!socket.waitForReadyRead(qMax(1, readBudget))) {
                    // QLocalSocket can report the peer disconnect at the same time as the final
                    // response becomes readable. Always drain those buffered bytes before treating
                    // the disconnect as an unconfirmed delivery.
                    if (socket.bytesAvailable() == 0) {
                        if (socket.state() == QLocalSocket::UnconnectedState) {
                            return failed(ApplicationCommandForwardStatus::Indeterminate,
                                          QStringLiteral("The GUI disconnected before confirming the "
                                                         "forwarded command."));
                        }
                        continue;
                    }
                }
            }
            const FrameParseResult parsed = decoder.append(socket.readAll());
            if (parsed.error.isError()) {
                return failed(ApplicationCommandForwardStatus::ProtocolError, parsed.error.detail);
            }
            if (!parsed.envelopes.isEmpty()) {
                const Envelope& reply = parsed.envelopes.constFirst();
                if (reply.protocolVersion != kProtocolVersion ||
                    reply.type != MessageType::ApplicationCommandResult ||
                    reply.requestId != command.requestId) {
                    return failed(ApplicationCommandForwardStatus::ProtocolError,
                                  QStringLiteral("The GUI returned an invalid command response."));
                }
                const QCborValue handledValue = reply.payload.value(QStringLiteral("handled"));
                const QCborValue retryableValue = reply.payload.value(QStringLiteral("retryable"));
                const QCborValue exitCodeValue = reply.payload.value(QStringLiteral("exitCode"));
                const QCborValue outputValue = reply.payload.value(QStringLiteral("stdout"));
                const QCborValue errorValue = reply.payload.value(QStringLiteral("stderr"));
                if (!handledValue.isBool() || !retryableValue.isBool() || !exitCodeValue.isInteger() ||
                    !outputValue.isByteArray() || !errorValue.isByteArray()) {
                    return failed(ApplicationCommandForwardStatus::ProtocolError,
                                  QStringLiteral("The GUI command response has invalid fields."));
                }
                const qint64 encodedExitCode = exitCodeValue.toInteger();
                const QByteArray standardOutput = outputValue.toByteArray();
                const QByteArray standardError = errorValue.toByteArray();
                if (encodedExitCode < 0 || encodedExitCode > 255 ||
                    standardOutput.size() + standardError.size() > MaximumCombinedOutputBytes) {
                    return failed(ApplicationCommandForwardStatus::ProtocolError,
                                  QStringLiteral("The GUI command response exceeds protocol limits."));
                }
                if (retryableValue.toBool()) {
                    primaryExplicitlyNotReady = true;
                    retryWhenReady = true;
                } else {
                    ApplicationCommandForwardResult result;
                    result.status = handledValue.toBool() ? ApplicationCommandForwardStatus::Completed
                                                          : ApplicationCommandForwardStatus::Declined;
                    result.exitCode = static_cast<int>(encodedExitCode);
                    result.standardOutput = standardOutput;
                    result.standardError = standardError;
                    return result;
                }
            }
            if (retryWhenReady) {
                break;
            }
        }
        if (!retryWhenReady) {
            return failed(ApplicationCommandForwardStatus::Indeterminate,
                          QStringLiteral("Timed out while waiting for the GUI command result."));
        }
        const int remainingBudget = boundedTimeout - static_cast<int>(timer.elapsed());
        // Once the primary has explicitly reported that its handler is not ready, do not send a
        // final retry that cannot leave enough time to receive its response. Doing so would turn a
        // safe local fallback into an indeterminate delivery at the timeout boundary.
        if (remainingBudget <= RetryDelayMs + MinimumRetryRoundTripBudgetMs) {
            break;
        }
        QThread::msleep(RetryDelayMs);
    }

    if (primaryExplicitlyNotReady) {
        return failed(ApplicationCommandForwardStatus::Declined,
                      QStringLiteral("The GUI did not become ready to accept commands in time."));
    }
    if (connectedAtLeastOnce) {
        return failed(ApplicationCommandForwardStatus::Indeterminate,
                      QStringLiteral("The GUI did not confirm the forwarded command."));
    }
    return failed(ApplicationCommandForwardStatus::Unavailable, unavailableGuiMessage());
}

} // namespace BreezeDesk::Ipc
