#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/jobs/TranscriptionJob.h"

#include <optional>

namespace BreezeDesk {

class IJobRepository {
  public:
    virtual ~IJobRepository() = default;

    [[nodiscard]] virtual Result<void> create(TranscriptionJob job) = 0;
    [[nodiscard]] virtual Result<std::optional<TranscriptionJob>> findById(const QString& id) const = 0;
    [[nodiscard]] virtual Result<QList<TranscriptionJob>> list(bool includeCompleted = true) const = 0;
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
    [[nodiscard]] virtual Result<int> clearCompleted() = 0;
};

} // namespace BreezeDesk
