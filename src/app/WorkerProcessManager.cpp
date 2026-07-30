#include "breezedesk/app/WorkerProcessManager.h"

#include "breezedesk/app_config.h"
#include "breezedesk/ipc/LocalEndpoint.h"
#include "breezedesk/platform/WorkerRegistry.h"
#include "breezedesk/version.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRandomGenerator>

#include <utility>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace BreezeDesk {

namespace {
constexpr int MaximumConnectionAttempts = 100;
constexpr int ConnectionRetryMs = 50;
constexpr int CancellationGraceMs = 5000;
constexpr int RestartWindowSeconds = 60;
constexpr int MaximumRestartsInWindow = 3;
constexpr qsizetype MaximumRememberedTerminalRequests = 128;

QString operationKey(const QString& jobId, const QString& requestId) {
    return jobId + QChar::Null + requestId;
}

int forcedCancellationRecoveryDelayMs() {
    bool validOverride = false;
    const int overrideDelay =
        qEnvironmentVariableIntValue("BREEZEDESK_TEST_FORCED_CANCELLATION_RETRY_MS", &validOverride);
    return validOverride && overrideDelay >= 0 ? overrideDelay : RestartWindowSeconds * 1'000;
}
} // namespace

WorkerProcessManager::WorkerProcessManager(QObject* parent)
    : QObject(parent), m_client(QString::fromLatin1(BREEZEDESK_VERSION_STRING), this) {
#if defined(Q_OS_WIN)
    // Let QProcess report worker loader failures through the normal fallback path instead
    // of allowing Windows to block the application with a system error dialog.
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS);
#endif
    connect(&m_client, &Ipc::AsrWorkerClient::ready, this, [this] {
        m_authenticatedProcessGeneration = m_processGeneration;
        setLastError({});
        emit readyChanged();
        if (m_forcedCancellationPending && m_forcedCancellationAwaitingRestart) {
            settleForcedCancellation();
        }
    });
    connect(&m_client, &Ipc::AsrWorkerClient::disconnected, this, [this] {
        const quint64 processGeneration = m_processGeneration;
        const bool authenticated = m_authenticatedProcessGeneration == processGeneration;
        emit readyChanged();
        if (authenticated) {
            recoverAuthenticatedChannelFailure(
                processGeneration, QStringLiteral("The ASR worker disconnected unexpectedly."));
        }
    });
    connect(&m_client, &Ipc::AsrWorkerClient::protocolError, this,
            [this](const Ipc::ProtocolError& error) {
                const quint64 processGeneration = m_processGeneration;
                const bool authenticated = m_client.isReady() &&
                                           m_authenticatedProcessGeneration == processGeneration;
                setLastError(error.detail);
                if (authenticated) {
                    recoverAuthenticatedChannelFailure(processGeneration, error.detail);
                }
            });
    connect(&m_client, &Ipc::AsrWorkerClient::envelopeReceived, this, [this](const Ipc::Envelope& envelope) {
        if (envelope.type == Ipc::MessageType::ChunkCompleted ||
            envelope.type == Ipc::MessageType::TranscriptionCompleted ||
            envelope.type == Ipc::MessageType::SpeechAnalysisCompleted ||
            envelope.type == Ipc::MessageType::ModelLoaded ||
            envelope.type == Ipc::MessageType::JobCancelled || envelope.type == Ipc::MessageType::Error) {
            if (!envelope.requestId.isEmpty()) {
                const QString key = operationKey(envelope.jobId, envelope.requestId);
                if (!m_terminalRequests.contains(key)) {
                    m_terminalRequests.insert(key);
                    m_terminalRequestOrder.enqueue(key);
                    while (m_terminalRequestOrder.size() > MaximumRememberedTerminalRequests) {
                        m_terminalRequests.remove(m_terminalRequestOrder.dequeue());
                    }
                }
            }
            if (m_forcedCancellationPending && envelope.jobId == m_forcedCancellationJobId &&
                envelope.requestId == m_forcedCancellationRequestId) {
                settleForcedCancellation();
            }
        }
    });
    m_forcedCancellationRecoveryTimer.setSingleShot(true);
    connect(&m_forcedCancellationRecoveryTimer, &QTimer::timeout, this, [this] {
        if (!m_forcedCancellationPending || m_stopping ||
            m_process.state() != QProcess::NotRunning) {
            return;
        }
        m_failedExecutables.clear();
        m_restartTimes.clear();
        if (!start()) {
            scheduleForcedCancellationRecovery();
        }
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            &WorkerProcessManager::handleUnexpectedExit);
}

WorkerProcessManager::~WorkerProcessManager() {
    stop();
}

bool WorkerProcessManager::isReady() const {
    return m_client.isReady();
}
bool WorkerProcessManager::forcedCancellationPending() const noexcept {
    return m_forcedCancellationPending;
}
QString WorkerProcessManager::lastError() const {
    return m_lastError;
}
Ipc::AsrWorkerClient* WorkerProcessManager::client() {
    return &m_client;
}

void WorkerProcessManager::setPreferredBackend(const QString& backend) {
    const QString normalized = backend.trimmed().isEmpty() ? QStringLiteral("Auto") : backend.trimmed();
    if (m_preferredBackend == normalized) {
        return;
    }
    m_preferredBackend = normalized;
    m_failedExecutables.clear();
}

QStringList WorkerProcessManager::workerExecutables() const {
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    const QString overridePath = qEnvironmentVariable("BREEZEDESK_ASR_WORKER_PATH");
#ifdef BREEZEDESK_DEV_WORKER_PATH
    const QString developmentPath = QString::fromUtf8(BREEZEDESK_DEV_WORKER_PATH);
#else
    const QString developmentPath;
#endif
    const QStringList candidates =
        qEnvironmentVariableIntValue("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY") == 1 &&
                !overridePath.trimmed().isEmpty()
            ? QStringList{overridePath}
            : WorkerRegistry::executableCandidates(applicationDirectory, m_preferredBackend,
                                                   overridePath, developmentPath);
    QStringList available;
    QSet<QString> inspected;
    for (const QString& candidate : std::as_const(candidates)) {
        const QString absolutePath = QFileInfo(candidate).absoluteFilePath();
        if (inspected.contains(absolutePath)) {
            continue;
        }
        inspected.insert(absolutePath);
        const QFileInfo info(absolutePath);
        if (info.isFile() && info.isExecutable()) {
            available.append(info.canonicalFilePath().isEmpty() ? info.absoluteFilePath()
                                                                : info.canonicalFilePath());
        }
    }
    return available;
}

bool WorkerProcessManager::start() {
    if (m_process.state() != QProcess::NotRunning) {
        return true;
    }
    const QStringList executables = workerExecutables();
    m_candidateCount = static_cast<int>(executables.size());
    if (executables.isEmpty()) {
        setLastError(QStringLiteral("The native ASR worker is missing. Reinstall %1 or set "
                                    "BREEZEDESK_ASR_WORKER_PATH for a development build.")
                         .arg(QString::fromLatin1(AppConfig::ProductName)));
        scheduleForcedCancellationRecovery();
        return false;
    }
    QString executable;
    for (const QString& candidate : executables) {
        if (!m_failedExecutables.contains(candidate)) {
            executable = candidate;
            break;
        }
    }
    if (executable.isEmpty()) {
        setLastError(QStringLiteral("All available ASR worker variants failed to start."));
        emit automaticRestartStopped();
        scheduleForcedCancellationRecovery();
        return false;
    }
    m_stopping = false;
    m_sessionToken.resize(32);
    for (char& byte : m_sessionToken) {
        byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xFFU);
    }
    const QByteArray tokenDigest =
        QCryptographicHash::hash(m_sessionToken, QCryptographicHash::Sha256).toHex().left(16);
    const QString workerChannel = QStringLiteral("asr-worker-%1-%2")
                                      .arg(QCoreApplication::applicationPid())
                                      .arg(QString::fromLatin1(tokenDigest));
    m_serverName =
        Ipc::LocalEndpoint::userScopedName(QString::fromLatin1(AppConfig::BundleId), workerChannel);
    const QStringList arguments{QStringLiteral("--server"),
                                m_serverName,
                                QStringLiteral("--session-token"),
                                QString::fromLatin1(m_sessionToken.toBase64(QByteArray::Base64UrlEncoding)),
                                QStringLiteral("--worker-version"),
                                QString::fromLatin1(BREEZEDESK_VERSION_STRING)};
    QProcessEnvironment processEnvironment = QProcessEnvironment::systemEnvironment();
    const QString pathKey = QStringLiteral("PATH");
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    const QString inheritedPath = processEnvironment.value(pathKey);
    processEnvironment.insert(pathKey, inheritedPath.isEmpty()
                                           ? applicationDirectory
                                           : applicationDirectory + QDir::listSeparator() + inheritedPath);
    m_process.setProcessEnvironment(processEnvironment);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    ++m_processGeneration;
    m_authenticatedProcessGeneration = 0;
    m_terminalRequests.clear();
    m_terminalRequestOrder.clear();
    m_process.start(executable, arguments, QIODevice::ReadOnly);
    if (!m_process.waitForStarted(3000)) {
        m_failedExecutables.insert(executable);
        setLastError(
            QStringLiteral("The native ASR worker variant could not start: %1").arg(m_process.errorString()));
        return start();
    }
    m_currentExecutable = executable;
    connectClientWithRetry(m_processGeneration);
    return true;
}

void WorkerProcessManager::connectClientWithRetry(const quint64 processGeneration, const int attempt) {
    if (processGeneration != m_processGeneration ||
        m_process.state() == QProcess::NotRunning || m_stopping) {
        return;
    }
    if (attempt >= MaximumConnectionAttempts) {
        setLastError(QStringLiteral("The ASR worker did not open its local endpoint in time."));
        if (processGeneration == m_processGeneration &&
            m_process.state() != QProcess::NotRunning) {
            m_process.kill();
        }
        return;
    }
    if (!m_client.isReady()) {
        m_client.connectToWorker(m_serverName, m_sessionToken);
        QTimer::singleShot(ConnectionRetryMs, this, [this, processGeneration, attempt] {
            if (processGeneration == m_processGeneration && !m_client.isReady()) {
                connectClientWithRetry(processGeneration, attempt + 1);
            }
        });
    }
}

void WorkerProcessManager::recoverAuthenticatedChannelFailure(const quint64 processGeneration,
                                                               const QString& reason) {
    if (processGeneration != m_processGeneration ||
        m_authenticatedProcessGeneration != processGeneration || m_stopping ||
        m_process.state() == QProcess::NotRunning) {
        return;
    }

    // Once an authenticated channel becomes unusable, reconnecting to the same
    // process is unsafe: it may still be running the request whose terminal
    // reply was lost. Invalidate every callback for this generation and let the
    // QProcess::finished path emit the single interruption and start a clean
    // worker generation.
    ++m_processGeneration;
    m_authenticatedProcessGeneration = 0;
    m_client.disconnectFromWorker();
    emit readyChanged();
    if (!reason.trimmed().isEmpty()) {
        setLastError(reason);
    }
    m_process.kill();
}

void WorkerProcessManager::stop() {
    m_stopping = true;
    m_forcedCancellationRecoveryTimer.stop();
    ++m_processGeneration;
    m_authenticatedProcessGeneration = 0;
    ++m_forcedCancellationTicket;
    if (m_client.isReady()) {
        m_client.sendRequest(Ipc::MessageType::Shutdown, {}, {});
    }
    if (m_process.state() != QProcess::NotRunning && !m_process.waitForFinished(5000)) {
        m_process.kill();
        m_process.waitForFinished(2000);
    }
    m_client.disconnectFromWorker();
    // A pending terminal checkpoint may release the cross-process execution
    // lease as soon as this signal is emitted. Do not settle it until the old
    // worker has actually exited (or has been killed) and disconnected.
    if (m_process.state() == QProcess::NotRunning) {
        settleForcedCancellation();
    }
}

void WorkerProcessManager::abortImmediately() {
    // This is used only after a bounded operation timeout. Treat the exit as
    // recoverable so the normal crash-isolation path can start a fresh worker.
    m_stopping = false;
    ++m_processGeneration;
    m_authenticatedProcessGeneration = 0;
    m_client.disconnectFromWorker();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
    } else {
        // There is no old process that can still touch the shared runtime. A
        // pending coordinator checkpoint is therefore safe to release now.
        settleForcedCancellation();
    }
}

void WorkerProcessManager::forceCancelAfterGrace(const QString& jobId, const QString& requestId) {
    if (requestId.isEmpty()) {
        return;
    }
    if (m_terminalRequests.contains(operationKey(jobId, requestId)) ||
        (m_forcedCancellationPending && jobId == m_forcedCancellationJobId &&
         requestId == m_forcedCancellationRequestId)) {
        return;
    }
    const quint64 cancellationTicket = ++m_forcedCancellationTicket;
    const quint64 processGeneration = m_processGeneration;
    m_forcedCancellationPending = true;
    m_forcedCancellationAwaitingRestart = !m_client.isReady();
    m_forcedCancellationJobId = jobId;
    m_forcedCancellationRequestId = requestId;
    if (!m_client.isReady()) {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
        } else if (!m_stopping) {
            QTimer::singleShot(0, this, [this] {
                if (m_forcedCancellationPending && m_process.state() == QProcess::NotRunning) {
                    (void)start();
                }
            });
        }
        return;
    }
    m_client.sendRequest(Ipc::MessageType::CancelJob, jobId, {});
    QTimer::singleShot(CancellationGraceMs, this,
                       [this, cancellationTicket, processGeneration] {
        if (cancellationTicket == m_forcedCancellationTicket &&
            processGeneration == m_processGeneration &&
            m_process.state() != QProcess::NotRunning) {
            m_process.kill();
        }
    });
}

void WorkerProcessManager::handleUnexpectedExit(int exitCode, QProcess::ExitStatus status) {
    ++m_processGeneration;
    m_authenticatedProcessGeneration = 0;
    if (m_forcedCancellationPending) {
        m_forcedCancellationAwaitingRestart = true;
    }
    m_client.disconnectFromWorker();
    emit readyChanged();
    if (m_stopping) {
        settleForcedCancellation();
        return;
    }
    const QString reason =
        status == QProcess::CrashExit
            ? QStringLiteral("ASR worker crashed (exit code %1).").arg(exitCode)
            : QStringLiteral("ASR worker exited unexpectedly (exit code %1).").arg(exitCode);
    setLastError(reason);
    emit workerInterrupted(reason);
    if (m_candidateCount > 1 && !m_currentExecutable.isEmpty()) {
        m_failedExecutables.insert(m_currentExecutable);
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    while (!m_restartTimes.isEmpty() && m_restartTimes.head() < now - RestartWindowSeconds) {
        m_restartTimes.dequeue();
    }
    if (m_restartTimes.size() >= MaximumRestartsInWindow) {
        emit automaticRestartStopped();
        scheduleForcedCancellationRecovery();
        return;
    }
    m_restartTimes.enqueue(now);
    QTimer::singleShot(250, this, [this] {
        if (!m_stopping && m_process.state() == QProcess::NotRunning) {
            const bool restarted = start();
            Q_UNUSED(restarted)
        }
    });
}

void WorkerProcessManager::scheduleForcedCancellationRecovery() {
    if (!m_forcedCancellationPending || m_stopping ||
        m_process.state() != QProcess::NotRunning || m_forcedCancellationRecoveryTimer.isActive()) {
        return;
    }
    m_forcedCancellationRecoveryTimer.start(forcedCancellationRecoveryDelayMs());
}

void WorkerProcessManager::settleForcedCancellation() {
    if (!m_forcedCancellationPending) {
        return;
    }
    m_forcedCancellationPending = false;
    m_forcedCancellationAwaitingRestart = false;
    m_forcedCancellationJobId.clear();
    m_forcedCancellationRequestId.clear();
    m_forcedCancellationRecoveryTimer.stop();
    ++m_forcedCancellationTicket;
    emit forcedCancellationSettled();
}

void WorkerProcessManager::setLastError(const QString& error) {
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    emit lastErrorChanged();
}

} // namespace BreezeDesk
