#include "breezedesk/jobs/JobQueue.h"

#include "breezedesk/jobs/IJobRepository.h"
#include "breezedesk/jobs/JobStateMachine.h"

#include <QUuid>

namespace BreezeDesk {

JobQueue::JobQueue(IJobRepository& repository) : m_repository(repository) {}

Result<QString> JobQueue::enqueue(TranscriptionJob job) {
    if (job.id.isEmpty())
        job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.state = JobState::Queued;
    job.stage = JobStage::Preparing;
    job.progress = 0.0;
    const auto result = m_repository.createQueued(std::move(job));
    return result ? Result<QString>::success(result.value().id)
                  : Result<QString>::failure(result.error());
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
    const JobState state = result.value()->state;
    if (state != JobState::Failed && state != JobState::Cancelled) {
        return Result<QString>::failure(
            UserFacingError::validation(ErrorCode::InvalidStateTransition,
                                        QStringLiteral("Only failed or cancelled jobs can be retried.")));
    }
    const auto transition = m_repository.transition(jobId, JobState::Queued);
    return transition ? Result<QString>::success(jobId)
                      : Result<QString>::failure(transition.error());
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
