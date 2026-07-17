#include "breezedesk/jobs/JobQueue.h"

#include "breezedesk/jobs/IJobRepository.h"
#include "breezedesk/jobs/JobStateMachine.h"

#include <QUuid>

#include <limits>

namespace BreezeDesk {

JobQueue::JobQueue(IJobRepository& repository) : m_repository(repository) {}

Result<QString> JobQueue::enqueue(TranscriptionJob job) {
    if (job.id.isEmpty())
        job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.state = JobState::Queued;
    job.stage = JobStage::Preparing;
    job.progress = 0.0;
    auto jobsResult = m_repository.list(false);
    if (!jobsResult)
        return Result<QString>::failure(jobsResult.error());
    job.queuePosition =
        static_cast<int>(qMin<qsizetype>(jobsResult.value().size(), std::numeric_limits<int>::max()));
    auto result = m_repository.create(job);
    return result ? Result<QString>::success(job.id) : Result<QString>::failure(result.error());
}

Result<void> JobQueue::cancel(const QString& jobId) {
    auto result = m_repository.findById(jobId);
    if (!result)
        return Result<void>::failure(result.error());
    if (!result.value())
        return Result<void>::failure(
            UserFacingError::validation(ErrorCode::NotFound, QStringLiteral("The job does not exist.")));
    const JobState state = result.value()->state;
    if (state == JobState::Queued || state == JobState::Interrupted) {
        return m_repository.transition(jobId, JobState::Cancelled, QStringLiteral("JobCancelled"),
                                       QStringLiteral("Cancelled by the user."));
    }
    if (JobStateMachine::isRunning(state))
        return m_repository.transition(jobId, JobState::Cancelling);
    return Result<void>::failure(UserFacingError::validation(
        ErrorCode::InvalidStateTransition, QStringLiteral("This job can no longer be cancelled.")));
}

Result<QString> JobQueue::retry(const QString& jobId) {
    auto result = m_repository.findById(jobId);
    if (!result)
        return Result<QString>::failure(result.error());
    if (!result.value())
        return Result<QString>::failure(
            UserFacingError::validation(ErrorCode::NotFound, QStringLiteral("The job does not exist.")));
    TranscriptionJob copy = *result.value();
    if (copy.state != JobState::Failed && copy.state != JobState::Cancelled) {
        return Result<QString>::failure(
            UserFacingError::validation(ErrorCode::InvalidStateTransition,
                                        QStringLiteral("Only failed or cancelled jobs can be retried.")));
    }
    copy.id.clear();
    copy.revisionNumber = 0;
    copy.retryCount += 1;
    copy.createdAt = {};
    copy.startedAt = {};
    copy.completedAt = {};
    copy.interruptedAt = {};
    copy.errorCode.clear();
    copy.errorMessage.clear();
    copy.lastCompletedChunk = -1;
    return enqueue(copy);
}

Result<void> JobQueue::resume(const QString& jobId) {
    auto result = m_repository.findById(jobId);
    if (!result)
        return Result<void>::failure(result.error());
    if (!result.value() || result.value()->state != JobState::Interrupted) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidStateTransition, QStringLiteral("Only interrupted jobs can be resumed.")));
    }
    return m_repository.transition(jobId, JobState::Queued);
}

Result<void> JobQueue::reorder(const QStringList& orderedJobIds) {
    return m_repository.reorder(orderedJobIds);
}

Result<void> JobQueue::remove(const QString& jobId) {
    return m_repository.removeFromQueue(jobId);
}

Result<int> JobQueue::clearCompleted() {
    return m_repository.clearCompleted();
}

} // namespace BreezeDesk
