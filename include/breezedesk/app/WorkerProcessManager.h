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
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] Ipc::AsrWorkerClient* client();
    void setPreferredBackend(const QString& backend);
    [[nodiscard]] bool start();
    void stop();
    void abortImmediately();
    void forceCancelAfterGrace(const QString& jobId);

  signals:
    void readyChanged();
    void lastErrorChanged();
    void workerInterrupted(const QString& reason);
    void automaticRestartStopped();

  private:
    [[nodiscard]] QStringList workerExecutables() const;
    void connectClientWithRetry(int attempt = 0);
    void handleUnexpectedExit(int exitCode, QProcess::ExitStatus status);
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
    quint64 m_cancellationGeneration = 0;
    bool m_stopping = false;
};

} // namespace BreezeDesk
