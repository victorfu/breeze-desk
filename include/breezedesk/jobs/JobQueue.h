#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/jobs/TranscriptionJob.h"

namespace BreezeDesk {

class IJobRepository;

class JobQueue final {
  public:
    explicit JobQueue(IJobRepository& repository);

    [[nodiscard]] Result<QString> enqueue(TranscriptionJob job);
    [[nodiscard]] Result<void> cancel(const QString& jobId);
    [[nodiscard]] Result<QString> retry(const QString& jobId);
    [[nodiscard]] Result<void> resume(const QString& jobId);
    [[nodiscard]] Result<void> reorder(const QStringList& orderedJobIds);
    [[nodiscard]] Result<void> remove(const QString& jobId);
    [[nodiscard]] Result<int> clearCompleted();

  private:
    IJobRepository& m_repository;
};

} // namespace BreezeDesk
