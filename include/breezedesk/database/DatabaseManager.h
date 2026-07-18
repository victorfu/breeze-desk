#pragma once

#include "breezedesk/core/Result.h"

#include <QMutex>
#include <QSet>
#include <QSqlDatabase>
#include <QString>

#include <functional>

namespace BreezeDesk {

struct DatabaseOptions {
    QString filePath;
    int busyTimeoutMs = 5'000;
    bool enableWriteAheadLog = true;
    bool createMigrationBackup = true;
};

class DatabaseManager final {
  public:
    explicit DatabaseManager(DatabaseOptions options);
    ~DatabaseManager();

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    [[nodiscard]] Result<void> initialize();
    [[nodiscard]] Result<QSqlDatabase> connection() const;
    [[nodiscard]] Result<void> transaction(const std::function<Result<void>(QSqlDatabase&)>& operation) const;
    [[nodiscard]] Result<void>
    immediateTransaction(const std::function<Result<void>(QSqlDatabase&)>& operation) const;
    [[nodiscard]] Result<void> integrityCheck() const;
    [[nodiscard]] Result<QString> createBackup(const QString& destinationPath = {}) const;

    [[nodiscard]] QString filePath() const { return m_options.filePath; }
    [[nodiscard]] int schemaVersion() const;
    [[nodiscard]] bool hasFts5() const;

  private:
    [[nodiscard]] Result<QSqlDatabase> createThreadConnection() const;
    [[nodiscard]] Result<void> configureConnection(QSqlDatabase& database) const;
    [[nodiscard]] Result<void> applyMigrations(QSqlDatabase& database);
    [[nodiscard]] QString connectionNameForCurrentThread() const;

    DatabaseOptions m_options;
    QString m_instanceId;
    mutable QMutex m_mutex;
    mutable QSet<QString> m_connectionNames;
    bool m_initialized = false;
    bool m_hasFts5 = false;
    int m_schemaVersion = 0;
};

} // namespace BreezeDesk
