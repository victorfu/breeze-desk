#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

namespace BreezeDesk {

class DatabaseManager;
class UpdateCoordinator;

namespace Ipc {
class IAsrWorkerClient;
struct Envelope;
} // namespace Ipc

struct MaintenancePaths {
    QString dataRoot;
    QString cacheDirectory;
    QString logDirectory;
    QString exportDirectory;
};

struct MaintenanceDependencies {
    DatabaseManager* database{nullptr};
    Ipc::IAsrWorkerClient* workerClient{nullptr};
    UpdateCoordinator* updateCoordinator{nullptr};
    MaintenancePaths paths;
};

class MaintenanceController final : public QObject {
    Q_OBJECT

  public:
    explicit MaintenanceController(MaintenanceDependencies dependencies, QObject* parent = nullptr);
    ~MaintenanceController() override;

    MaintenanceController(const MaintenanceController&) = delete;
    MaintenanceController& operator=(const MaintenanceController&) = delete;

    [[nodiscard]] QJsonObject diagnosticsSnapshot() const;

  public slots:
    void clearCache();
    void backupDatabase();
    void backupDatabaseTo(const QString& destinationPath);
    void backupDatabaseToUrl(const QUrl& destinationUrl);
    void refreshDiagnostics();
    void exportDiagnostics(bool includePaths = false);
    void exportDiagnosticsTo(const QString& destinationPath, bool includePaths = false);
    void exportDiagnosticsToUrl(const QUrl& destinationUrl, bool includePaths = false);
    void checkForUpdates();
    void setCacheBusy(bool busy);
    void setSelectedBackend(const QString& backend);
    void setActualBackend(const QString& backend);
    void setSanitizedSettings(const QJsonObject& settings);

  signals:
    void operationStarted(const QString& operation);
    void operationSucceeded(const QString& message);
    void operationFailed(const QString& operation, const QString& message, const QString& technicalDetails);
    void cacheCleared(qint64 releasedBytes);
    void databaseBackupCreated(const QString& path);
    void ffmpegVersionDetected(const QString& version);
    void whisperVersionDetected(const QString& version);
    void backendDetected(const QString& selectedBackend, const QString& actualBackend);
    void diagnosticsChanged(const QJsonObject& snapshot);
    void diagnosticsExported(const QString& path);
    void updateAvailabilityChanged(bool available);
    void updateAvailable(const QString& version, const QString& releaseNotes);
    void noUpdateAvailable();
    void updateError(const QString& message);

  private:
    void handleWorkerEnvelope(const Ipc::Envelope& envelope);
    void finishDiagnosticsRefreshIfReady();
    void publishDiagnostics();
    [[nodiscard]] QString uniqueExportPath(const QString& baseName, const QString& suffix) const;
    [[nodiscard]] QJsonObject diagnosticsForExport(bool includePaths) const;

    DatabaseManager* m_database{nullptr};
    QPointer<Ipc::IAsrWorkerClient> m_workerClient;
    QPointer<UpdateCoordinator> m_updateCoordinator;
    MaintenancePaths m_paths;
    QThreadPool m_threadPool;
    QTimer m_workerTimeout;
    QJsonObject m_diagnostics;
    QJsonObject m_settings;
    QString m_capabilitiesRequestId;
    bool m_cacheBusy{false};
    bool m_cacheOperationRunning{false};
    bool m_backupOperationRunning{false};
    bool m_exportOperationRunning{false};
    bool m_diagnosticsRefreshRunning{false};
    bool m_ffmpegRefreshPending{false};
    bool m_workerRefreshPending{false};
};

} // namespace BreezeDesk
