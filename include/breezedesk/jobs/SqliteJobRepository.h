#pragma once

#include "breezedesk/jobs/IJobRepository.h"

namespace BreezeDesk {

class DatabaseManager;

class SqliteJobRepository final : public IJobRepository {
  public:
    explicit SqliteJobRepository(DatabaseManager& databaseManager);

    [[nodiscard]] Result<void> create(TranscriptionJob job) override;
    [[nodiscard]] Result<std::optional<TranscriptionJob>> findById(const QString& id) const override;
    [[nodiscard]] Result<QList<TranscriptionJob>> list(bool includeCompleted = true) const override;
    [[nodiscard]] Result<void> transition(const QString& id, JobState state, const QString& errorCode = {},
                                          const QString& errorMessage = {}) override;
    [[nodiscard]] Result<void> updateProgress(const QString& id, JobStage stage, double progress,
                                              int lastCompletedChunk) override;
    [[nodiscard]] Result<void> updateRuntimeInfo(const QString& id, const QString& actualBackend,
                                                 const QString& engineVersion, const QString& workerVersion,
                                                 const QJsonObject& diagnostics) override;
    [[nodiscard]] Result<void> replaceChunks(const QString& jobId, const QList<JobChunk>& chunks) override;
    [[nodiscard]] Result<QList<JobChunk>> chunks(const QString& jobId) const override;
    [[nodiscard]] Result<void> updateChunk(const JobChunk& chunk) override;
    [[nodiscard]] Result<void> reorder(const QStringList& orderedJobIds) override;
    [[nodiscard]] Result<int> markRunningJobsInterrupted(const QString& reason) override;
    [[nodiscard]] Result<int> clearCompleted() override;

  private:
    DatabaseManager& m_databaseManager;
};

} // namespace BreezeDesk
