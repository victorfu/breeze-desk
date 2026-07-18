#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/jobs/TranscriptionJob.h"

#include <optional>

namespace BreezeDesk {

class IJobRepository {
  public:
    virtual ~IJobRepository() = default;

    [[nodiscard]] virtual Result<void> create(TranscriptionJob job) = 0;
    [[nodiscard]] virtual Result<TranscriptionJob> createQueued(TranscriptionJob job) = 0;
    [[nodiscard]] virtual Result<std::optional<TranscriptionJob>> findById(const QString& id) const = 0;
    [[nodiscard]] virtual Result<QList<TranscriptionJob>> list(bool includeCompleted = true) const = 0;
    [[nodiscard]] virtual Result<QList<TranscriptRevisionSummary>>
    listForRecording(const QString& recordingId) const = 0;
    [[nodiscard]] virtual Result<std::optional<TranscriptRevisionSummary>>
    latestForRecording(const QString& recordingId) const = 0;
    [[nodiscard]] virtual Result<void> setActiveRevision(const QString& recordingId,
                                                         const QString& jobId) = 0;
    [[nodiscard]] virtual Result<RevisionDeletionResult> deleteRevision(const QString& recordingId,
                                                                        const QString& jobId) = 0;
    [[nodiscard]] virtual Result<std::optional<TranscriptSegment>>
    latestSegmentForJob(const QString& jobId, bool includeProvisional = true) const = 0;
    [[nodiscard]] virtual Result<JobEvent> appendEvent(JobEvent event) = 0;
    [[nodiscard]] virtual Result<QList<JobEvent>> eventsForJob(const QString& jobId, qint64 afterId = 0,
                                                               int limit = 200) const = 0;
    [[nodiscard]] virtual Result<JobClaimResult> claimNextQueued(const QString& ownerToken,
                                                                 qint64 leaseDurationMs = 15'000) = 0;
    [[nodiscard]] virtual Result<JobClaimResult> claimQueued(const QString& jobId, const QString& ownerToken,
                                                             qint64 leaseDurationMs = 15'000) = 0;
    [[nodiscard]] virtual Result<AsrExecutionLease>
    renewLease(const QString& jobId, const QString& ownerToken, qint64 leaseDurationMs = 15'000) = 0;
    [[nodiscard]] virtual Result<void> releaseLease(const QString& jobId, const QString& ownerToken) = 0;
    [[nodiscard]] virtual Result<std::optional<AsrExecutionLease>> activeLease() const = 0;
    [[nodiscard]] virtual Result<void> completeAndActivate(const QString& recordingId, const QString& jobId,
                                                           const QString& ownerToken = {}) = 0;
    [[nodiscard]] virtual Result<void> transition(const QString& id, JobState state,
                                                  const QString& errorCode = {},
                                                  const QString& errorMessage = {}) = 0;
    [[nodiscard]] virtual Result<void> updateProgress(const QString& id, JobStage stage, double progress,
                                                      int lastCompletedChunk) = 0;
    [[nodiscard]] virtual Result<void> updateRuntimeInfo(const QString& id, const QString& actualBackend,
                                                         const QString& engineVersion,
                                                         const QString& workerVersion,
                                                         const QJsonObject& diagnostics) = 0;
    [[nodiscard]] virtual Result<void> replaceChunks(const QString& jobId, const QList<JobChunk>& chunks) = 0;
    [[nodiscard]] virtual Result<QList<JobChunk>> chunks(const QString& jobId) const = 0;
    [[nodiscard]] virtual Result<void> updateChunk(const JobChunk& chunk) = 0;
    [[nodiscard]] virtual Result<void> reorder(const QStringList& orderedJobIds) = 0;
    [[nodiscard]] virtual Result<int> markRunningJobsInterrupted(const QString& reason) = 0;
    [[nodiscard]] virtual Result<void> deleteTerminalJob(const QString& id) = 0;
    [[nodiscard]] virtual Result<int> clearCompleted() = 0;
};

} // namespace BreezeDesk
