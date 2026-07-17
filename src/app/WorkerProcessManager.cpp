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

namespace BreezeDesk {

namespace {
constexpr int MaximumConnectionAttempts = 40;
constexpr int ConnectionRetryMs = 50;
constexpr int CancellationGraceMs = 5000;
constexpr int RestartWindowSeconds = 60;
constexpr int MaximumRestartsInWindow = 3;
} // namespace

WorkerProcessManager::WorkerProcessManager(QObject* parent)
    : QObject(parent), m_client(QString::fromLatin1(BREEZEDESK_VERSION_STRING), this) {
    connect(&m_client, &Ipc::AsrWorkerClient::ready, this, &WorkerProcessManager::readyChanged);
    connect(&m_client, &Ipc::AsrWorkerClient::disconnected, this, &WorkerProcessManager::readyChanged);
    connect(&m_client, &Ipc::AsrWorkerClient::protocolError, this,
            [this](const Ipc::ProtocolError& error) { setLastError(error.detail); });
    connect(&m_client, &Ipc::AsrWorkerClient::envelopeReceived, this, [this](const Ipc::Envelope& envelope) {
        if (envelope.type == Ipc::MessageType::ChunkCompleted ||
            envelope.type == Ipc::MessageType::TranscriptionCompleted ||
            envelope.type == Ipc::MessageType::SpeechAnalysisCompleted ||
            envelope.type == Ipc::MessageType::JobCancelled || envelope.type == Ipc::MessageType::Error) {
            ++m_cancellationGeneration;
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
    const QStringList candidates = WorkerRegistry::executableCandidates(
        applicationDirectory, m_preferredBackend, overridePath, developmentPath);
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
    m_process.start(executable, arguments, QIODevice::ReadOnly);
    if (!m_process.waitForStarted(3000)) {
        m_failedExecutables.insert(executable);
        setLastError(
            QStringLiteral("The native ASR worker variant could not start: %1").arg(m_process.errorString()));
        return start();
    }
    m_currentExecutable = executable;
    connectClientWithRetry();
    return true;
}

void WorkerProcessManager::connectClientWithRetry(int attempt) {
    if (m_process.state() == QProcess::NotRunning || m_stopping) {
        return;
    }
    if (attempt >= MaximumConnectionAttempts) {
        setLastError(QStringLiteral("The ASR worker did not open its local endpoint in time."));
        m_process.kill();
        return;
    }
    if (!m_client.isReady()) {
        m_client.connectToWorker(m_serverName, m_sessionToken);
        QTimer::singleShot(ConnectionRetryMs, this, [this, attempt] {
            if (!m_client.isReady()) {
                connectClientWithRetry(attempt + 1);
            }
        });
    }
}

void WorkerProcessManager::stop() {
    m_stopping = true;
    if (m_client.isReady()) {
        m_client.sendRequest(Ipc::MessageType::Shutdown, {}, {});
    }
    if (m_process.state() != QProcess::NotRunning && !m_process.waitForFinished(5000)) {
        m_process.kill();
        m_process.waitForFinished(2000);
    }
    m_client.disconnectFromWorker();
}

void WorkerProcessManager::abortImmediately() {
    // This is used only after a bounded operation timeout. Treat the exit as
    // recoverable so the normal crash-isolation path can start a fresh worker.
    m_stopping = false;
    m_client.disconnectFromWorker();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
    }
}

void WorkerProcessManager::forceCancelAfterGrace(const QString& jobId) {
    if (!m_client.isReady()) {
        return;
    }
    const quint64 cancellationGeneration = ++m_cancellationGeneration;
    m_client.sendRequest(Ipc::MessageType::CancelJob, jobId, {});
    QTimer::singleShot(CancellationGraceMs, this, [this, jobId, cancellationGeneration] {
        Q_UNUSED(jobId)
        if (cancellationGeneration == m_cancellationGeneration && m_process.state() != QProcess::NotRunning) {
            m_process.kill();
        }
    });
}

void WorkerProcessManager::handleUnexpectedExit(int exitCode, QProcess::ExitStatus status) {
    m_client.disconnectFromWorker();
    emit readyChanged();
    if (m_stopping) {
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
        return;
    }
    m_restartTimes.enqueue(now);
    QTimer::singleShot(250, this, [this] {
        const bool restarted = start();
        Q_UNUSED(restarted)
    });
}

void WorkerProcessManager::setLastError(const QString& error) {
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    emit lastErrorChanged();
}

} // namespace BreezeDesk
