#include "breezedesk/ui/JobListModel.h"

#include <QUuid>
#include <QVariantMap>

namespace BreezeDesk {
namespace {
constexpr qsizetype MaximumTimelineEvents = 50;

bool isTerminalState(const QString& state) {
    return state == QLatin1String("Completed") || state == QLatin1String("Cancelled") ||
           state == QLatin1String("Failed") || state == QLatin1String("Interrupted");
}

bool isRunningState(const QString& state) {
    return state != QLatin1String("Queued") && state != QLatin1String("Interrupted") &&
           !isTerminalState(state);
}
} // namespace

JobListModel::JobListModel(QObject* parent) : QAbstractListModel(parent) {}

int JobListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_jobs.size());
}

QVariant JobListModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size()) {
        return {};
    }
    const Job& job = m_jobs.at(index.row());
    const bool terminal = isTerminalState(job.state);
    const int queuedPosition = queuePositionForRow(index.row());
    switch (role) {
    case IdRole:
        return job.id;
    case RecordingIdRole:
        return job.recordingId;
    case TitleRole:
        return job.title;
    case StateRole:
        return job.state;
    case StageRole:
        return job.stage;
    case ProgressRole:
        return job.progress;
    case ErrorRole:
        return job.error;
    case CanCancelRole:
        return !terminal && job.state != QLatin1String("Cancelling");
    case CanRetryRole:
        return job.state == QLatin1String("Failed") || job.state == QLatin1String("Cancelled");
    case CanResumeRole:
        return job.state == QLatin1String("Interrupted");
    case CanRemoveRole:
    case CanHideRole:
        return terminal;
    case IsRunningNowRole:
        return !m_runningJobId.isEmpty() && job.id == m_runningJobId;
    case QueuePositionRole:
    case WaitingAheadRole:
        return queuedPosition;
    case CurrentChunkRole:
        return job.currentChunk;
    case TotalChunksRole:
        return job.totalChunks;
    case LatestPartialTextRole:
        return job.latestPartialText;
    case EventTimelineRole:
        return job.eventTimeline;
    case CanMoveUpRole:
        return canMoveQueued(index.row(), -1);
    case CanMoveDownRole:
        return canMoveQueued(index.row(), 1);
    default:
        return {};
    }
}

QHash<int, QByteArray> JobListModel::roleNames() const {
    return {{IdRole, "jobId"},
            {RecordingIdRole, "recordingId"},
            {TitleRole, "title"},
            {StateRole, "jobState"},
            {StageRole, "stage"},
            {ProgressRole, "progress"},
            {ErrorRole, "errorMessage"},
            {CanCancelRole, "canCancel"},
            {CanRetryRole, "canRetry"},
            {CanResumeRole, "canResume"},
            {CanRemoveRole, "canRemove"},
            {IsRunningNowRole, "isRunningNow"},
            {QueuePositionRole, "queuePosition"},
            {WaitingAheadRole, "waitingAhead"},
            {CurrentChunkRole, "currentChunk"},
            {TotalChunksRole, "totalChunks"},
            {LatestPartialTextRole, "latestPartialText"},
            {EventTimelineRole, "eventTimeline"},
            {CanMoveUpRole, "canMoveUp"},
            {CanMoveDownRole, "canMoveDown"},
            {CanHideRole, "canHide"}};
}

QString JobListModel::enqueue(const QString& recordingId, const QString& title) {
    Job job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString id = job.id;
    job.recordingId = recordingId;
    job.title = title;
    const int insertionRow = static_cast<int>(m_jobs.size());
    beginInsertRows({}, insertionRow, insertionRow);
    m_jobs.append(std::move(job));
    endInsertRows();
    emitQueueMetadataChanged();
    return id;
}

void JobListModel::upsert(const QString& id, const QString& recordingId, const QString& title,
                          const QString& state, const QString& stage, const qreal progress,
                          const QString& error) {
    const int existing = indexOf(id);
    if (existing < 0) {
        Job job;
        job.id = id;
        job.recordingId = recordingId;
        job.title = title;
        job.state = state;
        job.stage = stage;
        job.progress = qBound(0.0, progress, 1.0);
        job.error = error;
        const int insertionRow = static_cast<int>(m_jobs.size());
        beginInsertRows({}, insertionRow, insertionRow);
        m_jobs.append(std::move(job));
        endInsertRows();
        emitQueueMetadataChanged();
        if (isRunningState(state)) {
            setRunningJobId(id);
        }
        return;
    }

    Job& job = m_jobs[existing];
    const bool stateChanged = job.state != state;
    job.recordingId = recordingId;
    job.title = title;
    job.state = state;
    job.stage = stage;
    job.progress = qBound(0.0, progress, 1.0);
    job.error = error;
    emitRowChanged(existing);
    if (stateChanged) {
        emitQueueMetadataChanged();
    }
    if (isRunningState(state)) {
        setRunningJobId(id);
    } else if (m_runningJobId == id) {
        setRunningJobId({});
    }
}

bool JobListModel::cancel(const QString& id) {
    const int row = indexOf(id);
    if (row < 0 || !data(index(row), CanCancelRole).toBool()) {
        return false;
    }
    Job& job = m_jobs[row];
    const bool wasQueued = job.state == QLatin1String("Queued");
    job.state = wasQueued ? QStringLiteral("Cancelled") : QStringLiteral("Cancelling");
    emitRowChanged(row);
    if (wasQueued) {
        emitQueueMetadataChanged();
    }
    return true;
}

bool JobListModel::retry(const QString& id) {
    const int row = indexOf(id);
    if (row < 0 || !data(index(row), CanRetryRole).toBool()) {
        return false;
    }
    Job& job = m_jobs[row];
    job.state = QStringLiteral("Queued");
    job.stage = QStringLiteral("Preparing");
    job.progress = 0.0;
    job.error.clear();
    job.currentChunk = 0;
    job.latestPartialText.clear();
    emitRowChanged(row);
    emitQueueMetadataChanged();
    return true;
}

bool JobListModel::resume(const QString& id) {
    const int row = indexOf(id);
    if (row < 0 || !data(index(row), CanResumeRole).toBool()) {
        return false;
    }
    Job& job = m_jobs[row];
    job.state = QStringLiteral("Queued");
    emitRowChanged(row);
    emitQueueMetadataChanged();
    return true;
}

bool JobListModel::hide(const QString& id) {
    const int row = indexOf(id);
    if (row < 0 || !data(index(row), CanHideRole).toBool()) {
        return false;
    }
    beginRemoveRows({}, row, row);
    m_jobs.removeAt(row);
    endRemoveRows();
    emitQueueMetadataChanged();
    return true;
}

bool JobListModel::remove(const QString& id) {
    return hide(id);
}

bool JobListModel::moveQueued(const QString& id, const int destination) {
    const int sourceRow = indexOf(id);
    const int sourcePosition = queuePositionForRow(sourceRow);
    const int count = queuedCount();
    if (sourceRow < 0 || sourcePosition < 0 || destination < 0 || destination >= count ||
        sourcePosition == destination) {
        return false;
    }

    int targetRow = -1;
    int queuedPosition = 0;
    for (int row = 0; row < m_jobs.size(); ++row) {
        if (m_jobs.at(row).state != QLatin1String("Queued")) {
            continue;
        }
        if (queuedPosition == destination) {
            targetRow = row;
            break;
        }
        ++queuedPosition;
    }
    if (targetRow < 0) {
        return false;
    }

    const int destinationChild = targetRow > sourceRow ? targetRow + 1 : targetRow;
    if (!beginMoveRows({}, sourceRow, sourceRow, {}, destinationChild)) {
        return false;
    }
    m_jobs.move(sourceRow, targetRow);
    endMoveRows();
    emitQueueMetadataChanged();
    return true;
}

void JobListModel::clearCompleted() {
    for (int row = static_cast<int>(m_jobs.size()) - 1; row >= 0; --row) {
        const QString& state = m_jobs.at(row).state;
        if (state == QLatin1String("Completed") || state == QLatin1String("Cancelled")) {
            beginRemoveRows({}, row, row);
            m_jobs.removeAt(row);
            endRemoveRows();
        }
    }
    emitQueueMetadataChanged();
}

int JobListModel::activeCount() const {
    int count = 0;
    for (const Job& job : m_jobs) {
        if (!isTerminalState(job.state)) {
            ++count;
        }
    }
    return count;
}

bool JobListModel::isWritingTranscript(const QString& id) const {
    const int row = indexOf(id);
    return row >= 0 && isRunningState(m_jobs.at(row).state);
}

bool JobListModel::contains(const QString& id) const {
    return indexOf(id) >= 0;
}

QString JobListModel::runningJobId() const {
    return m_runningJobId;
}

int JobListModel::queuePosition(const QString& id) const {
    return queuePositionForRow(indexOf(id));
}

void JobListModel::setRunningJobId(const QString& id) {
    if (m_runningJobId == id) {
        return;
    }
    const int previousRow = indexOf(m_runningJobId);
    m_runningJobId = id;
    const int nextRow = indexOf(m_runningJobId);
    if (previousRow >= 0) {
        emitRowChanged(previousRow, {IsRunningNowRole});
    }
    if (nextRow >= 0 && nextRow != previousRow) {
        emitRowChanged(nextRow, {IsRunningNowRole});
    }
    emit runningJobIdChanged();
}

void JobListModel::updateTelemetry(const QString& id, const int currentChunk, const int totalChunks,
                                   const QString& latestPartialText) {
    const int row = indexOf(id);
    if (row < 0) {
        return;
    }
    Job& job = m_jobs[row];
    const int boundedTotal = qMax(0, totalChunks);
    const int boundedCurrent = boundedTotal > 0 ? qBound(0, currentChunk, boundedTotal) : qMax(0, currentChunk);
    if (job.currentChunk == boundedCurrent && job.totalChunks == boundedTotal &&
        job.latestPartialText == latestPartialText) {
        return;
    }
    job.currentChunk = boundedCurrent;
    job.totalChunks = boundedTotal;
    job.latestPartialText = latestPartialText;
    emitRowChanged(row, {CurrentChunkRole, TotalChunksRole, LatestPartialTextRole});
}

void JobListModel::appendEvent(const QString& id, const QString& title, const QString& detail,
                               const QString& severity, const QDateTime& occurredAt) {
    const int row = indexOf(id);
    if (row < 0) {
        return;
    }
    appendEvent(m_jobs[row], title, detail, severity, occurredAt);
    emitRowChanged(row, {EventTimelineRole});
}

int JobListModel::indexOf(const QString& id) const {
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

int JobListModel::queuePositionForRow(const int row) const {
    if (row < 0 || row >= m_jobs.size() || m_jobs.at(row).state != QLatin1String("Queued")) {
        return -1;
    }
    int position = 0;
    for (int candidate = 0; candidate < row; ++candidate) {
        if (m_jobs.at(candidate).state == QLatin1String("Queued")) {
            ++position;
        }
    }
    return position;
}

int JobListModel::queuedCount() const {
    int count = 0;
    for (const Job& job : m_jobs) {
        if (job.state == QLatin1String("Queued")) {
            ++count;
        }
    }
    return count;
}

bool JobListModel::canMoveQueued(const int row, const int delta) const {
    const int position = queuePositionForRow(row);
    return position >= 0 && position + delta >= 0 && position + delta < queuedCount();
}

void JobListModel::appendEvent(Job& job, const QString& title, const QString& detail,
                               const QString& severity, const QDateTime& occurredAt) {
    QVariantMap event;
    event.insert(QStringLiteral("timestamp"),
                 occurredAt.isValid() ? occurredAt : QDateTime::currentDateTimeUtc());
    event.insert(QStringLiteral("title"), title);
    event.insert(QStringLiteral("detail"), detail);
    event.insert(QStringLiteral("severity"), severity.isEmpty() ? QStringLiteral("info") : severity);
    job.eventTimeline.append(event);
    while (job.eventTimeline.size() > MaximumTimelineEvents) {
        job.eventTimeline.removeFirst();
    }
}

void JobListModel::emitRowChanged(const int row, const QList<int>& roles) {
    emit dataChanged(index(row), index(row), roles);
}

void JobListModel::emitQueueMetadataChanged() {
    if (m_jobs.isEmpty()) {
        return;
    }
    emit dataChanged(index(0), index(static_cast<int>(m_jobs.size()) - 1),
                     {QueuePositionRole, WaitingAheadRole, CanMoveUpRole, CanMoveDownRole});
}

} // namespace BreezeDesk
