#pragma once

#include "breezedesk/ipc/AsrWorkerClient.h"

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QTimer>

namespace BreezeDesk {

class WorkerProcessManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

  public:
    explicit WorkerProcessManager(QObject* parent = nullptr);
    ~WorkerProcessManager() override;

    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool forcedCancellationPending() const noexcept;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] Ipc::AsrWorkerClient* client();
    void setPreferredBackend(const QString& backend);
    [[nodiscard]] bool start();
    void stop();
    void abortImmediately();
    void forceCancelAfterGrace(const QString& jobId, const QString& requestId);

  signals:
    void readyChanged();
    void lastErrorChanged();
    void workerInterrupted(const QString& reason);
    void forcedCancellationSettled();
    void automaticRestartStopped();

  private:
    [[nodiscard]] QStringList workerExecutables() const;
    void connectClientWithRetry(quint64 processGeneration, int attempt = 0);
    void recoverAuthenticatedChannelFailure(quint64 processGeneration, const QString& reason);
    void handleUnexpectedExit(int exitCode, QProcess::ExitStatus status);
    void scheduleForcedCancellationRecovery();
    void settleForcedCancellation();
    void setLastError(const QString& error);

    QProcess m_process;
    Ipc::AsrWorkerClient m_client;
    QByteArray m_sessionToken;
    QString m_serverName;
    QString m_lastError;
    QString m_preferredBackend{QStringLiteral("Auto")};
    QString m_currentExecutable;
    QSet<QString> m_failedExecutables;
    int m_candidateCount{0};
    QQueue<qint64> m_restartTimes;
    QQueue<QString> m_terminalRequestOrder;
    QSet<QString> m_terminalRequests;
    QTimer m_forcedCancellationRecoveryTimer;
    quint64 m_forcedCancellationTicket = 0;
    quint64 m_processGeneration = 0;
    quint64 m_authenticatedProcessGeneration = 0;
    QString m_forcedCancellationJobId;
    QString m_forcedCancellationRequestId;
    bool m_forcedCancellationPending = false;
    bool m_forcedCancellationAwaitingRestart = false;
    bool m_stopping = false;
};

} // namespace BreezeDesk
