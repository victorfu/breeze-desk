#include "breezedesk/ui/JobQueueViewModel.h"

namespace BreezeDesk {

JobQueueViewModel::JobQueueViewModel(QObject* parent) : QObject(parent) {
    connect(&m_jobs, &QAbstractItemModel::rowsInserted, this, &JobQueueViewModel::activeCountChanged);
    connect(&m_jobs, &QAbstractItemModel::rowsRemoved, this, &JobQueueViewModel::activeCountChanged);
    connect(&m_jobs, &QAbstractItemModel::dataChanged, this, &JobQueueViewModel::activeCountChanged);
}

QAbstractItemModel* JobQueueViewModel::jobs() noexcept {
    return &m_jobs;
}
bool JobQueueViewModel::pauseAfterCurrent() const noexcept {
    return m_pauseAfterCurrent;
}
int JobQueueViewModel::activeCount() const {
    return m_jobs.activeCount();
}
bool JobQueueViewModel::empty() const {
    return m_jobs.rowCount() == 0;
}
bool JobQueueViewModel::isWritingTranscript(const QString& jobId) const {
    return m_jobs.isWritingTranscript(jobId);
}

QString JobQueueViewModel::enqueue(const QString& recordingId, const QString& title) {
    return m_jobs.enqueue(recordingId, title);
}

void JobQueueViewModel::cancel(const QString& jobId) {
    if (m_jobs.cancel(jobId)) {
        emit cancelRequested(jobId);
    }
}

void JobQueueViewModel::retry(const QString& jobId) {
    if (m_jobs.retry(jobId)) {
        emit retryRequested(jobId);
    }
}

void JobQueueViewModel::resume(const QString& jobId) {
    if (m_jobs.resume(jobId)) {
        emit resumeRequested(jobId);
    }
}

void JobQueueViewModel::remove(const QString& jobId) {
    if (m_jobs.remove(jobId)) {
        emit removeRequested(jobId);
    }
}

void JobQueueViewModel::reorder(const QString& jobId, int destination) {
    if (m_jobs.move(jobId, destination)) {
        emit reorderRequested(jobId, destination);
    }
}
void JobQueueViewModel::clearCompleted() {
    m_jobs.clearCompleted();
    emit clearCompletedRequested();
}

void JobQueueViewModel::updateJob(const QString& id, const QString& recordingId, const QString& title,
                                  const QString& state, const QString& stage, qreal progress,
                                  const QString& error) {
    m_jobs.upsert(id, recordingId, title, state, stage, progress, error);
}

void JobQueueViewModel::setPauseAfterCurrent(bool enabled) {
    if (m_pauseAfterCurrent == enabled) {
        return;
    }
    m_pauseAfterCurrent = enabled;
    emit pauseAfterCurrentChanged();
}

} // namespace BreezeDesk
