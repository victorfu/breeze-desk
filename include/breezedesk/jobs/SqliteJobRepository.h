#pragma once

#include "breezedesk/jobs/IJobRepository.h"

namespace BreezeDesk {

class DatabaseManager;

class SqliteJobRepository final : public IJobRepository {
  public:
    explicit SqliteJobRepository(DatabaseManager& databaseManager);

    [[nodiscard]] Result<void> create(TranscriptionJob job) override;
    [[nodiscard]] Result<TranscriptionJob> createQueued(TranscriptionJob job) override;
    [[nodiscard]] Result<std::optional<TranscriptionJob>> findById(const QString& id) const override;
    [[nodiscard]] Result<QList<TranscriptionJob>> list(bool includeCompleted = true) const override;
    [[nodiscard]] Result<QList<TranscriptRevisionSummary>>
    listForRecording(const QString& recordingId) const override;
    [[nodiscard]] Result<std::optional<TranscriptRevisionSummary>>
    latestForRecording(const QString& recordingId) const override;
    [[nodiscard]] Result<void> setActiveRevision(const QString& recordingId, const QString& jobId) override;
    [[nodiscard]] Result<RevisionDeletionResult> deleteRevision(const QString& recordingId,
                                                                const QString& jobId) override;
    [[nodiscard]] Result<std::optional<TranscriptSegment>>
    latestSegmentForJob(const QString& jobId, bool includeProvisional = true) const override;
    [[nodiscard]] Result<JobEvent> appendEvent(JobEvent event) override;
    [[nodiscard]] Result<QList<JobEvent>> eventsForJob(const QString& jobId, qint64 afterId = 0,
                                                       int limit = 200) const override;
    [[nodiscard]] Result<JobClaimResult> claimNextQueued(const QString& ownerToken,
                                                         qint64 leaseDurationMs = 15'000) override;
    [[nodiscard]] Result<JobClaimResult> claimQueued(const QString& jobId, const QString& ownerToken,
                                                     qint64 leaseDurationMs = 15'000) override;
    [[nodiscard]] Result<AsrExecutionLease> renewLease(const QString& jobId, const QString& ownerToken,
                                                       qint64 leaseDurationMs = 15'000) override;
    [[nodiscard]] Result<void> releaseLease(const QString& jobId, const QString& ownerToken) override;
    [[nodiscard]] Result<std::optional<AsrExecutionLease>> activeLease() const override;
    [[nodiscard]] Result<void> completeAndActivate(const QString& recordingId, const QString& jobId,
                                                   const QString& ownerToken = {}) override;
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
    [[nodiscard]] Result<void> removeFromQueue(const QString& id) override;
    [[nodiscard]] Result<int> clearCompleted() override;

  private:
    DatabaseManager& m_databaseManager;
};

} // namespace BreezeDesk
