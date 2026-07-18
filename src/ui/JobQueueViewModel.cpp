#include "breezedesk/ui/JobQueueViewModel.h"

#include <QUuid>

namespace BreezeDesk {

JobQueueViewModel::JobQueueViewModel(QObject* parent) : QObject(parent) {
    connect(&m_jobs, &QAbstractItemModel::rowsInserted, this, [this] {
        emit activeCountChanged();
        emit emptyChanged();
    });
    connect(&m_jobs, &QAbstractItemModel::rowsRemoved, this, [this] {
        emit activeCountChanged();
        emit emptyChanged();
    });
    connect(&m_jobs, &QAbstractItemModel::dataChanged, this, &JobQueueViewModel::activeCountChanged);
    connect(&m_jobs, &JobListModel::runningJobIdChanged, this,
            &JobQueueViewModel::runningJobIdChanged);
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
QString JobQueueViewModel::runningJobId() const {
    return m_jobs.runningJobId();
}
bool JobQueueViewModel::isWritingTranscript(const QString& jobId) const {
    return m_jobs.isWritingTranscript(jobId);
}

bool JobQueueViewModel::containsJob(const QString& jobId) const {
    return m_jobs.contains(jobId);
}

QString JobQueueViewModel::allocateJobId() const {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
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

void JobQueueViewModel::hide(const QString& jobId) {
    if (m_jobs.hide(jobId)) {
        // The persistence layer retains the durable job and only hides its queue row.
        emit removeRequested(jobId);
    }
}

void JobQueueViewModel::remove(const QString& jobId) {
    hide(jobId);
}

void JobQueueViewModel::reorder(const QString& jobId, int destination) {
    if (m_jobs.moveQueued(jobId, destination)) {
        emit reorderRequested(jobId, destination);
    }
}

void JobQueueViewModel::moveUp(const QString& jobId) {
    const int position = m_jobs.queuePosition(jobId);
    if (position > 0) {
        reorder(jobId, position - 1);
    }
}

void JobQueueViewModel::moveDown(const QString& jobId) {
    const int position = m_jobs.queuePosition(jobId);
    if (position >= 0) {
        reorder(jobId, position + 1);
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

void JobQueueViewModel::setRunningJobId(const QString& jobId) {
    m_jobs.setRunningJobId(jobId);
}

void JobQueueViewModel::updateJobTelemetry(const QString& jobId, const int currentChunk,
                                           const int totalChunks, const QString& latestPartialText) {
    m_jobs.updateTelemetry(jobId, currentChunk, totalChunks, latestPartialText);
}

void JobQueueViewModel::appendJobEvent(const QString& jobId, const QString& title,
                                       const QString& detail, const QString& severity,
                                       const QDateTime& occurredAt) {
    m_jobs.appendEvent(jobId, title, detail, severity, occurredAt);
}

} // namespace BreezeDesk
