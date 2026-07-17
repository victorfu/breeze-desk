#include "breezedesk/ui/JobListModel.h"

#include <QUuid>

namespace BreezeDesk {

JobListModel::JobListModel(QObject* parent) : QAbstractListModel(parent) {}

int JobListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_jobs.size());
}

QVariant JobListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size()) {
        return {};
    }
    const Job& job = m_jobs.at(index.row());
    const bool terminal = job.state == QLatin1String("Completed") ||
                          job.state == QLatin1String("Cancelled") || job.state == QLatin1String("Failed");
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
        return terminal;
    default:
        return {};
    }
}

QHash<int, QByteArray> JobListModel::roleNames() const {
    return {{IdRole, "jobId"},           {RecordingIdRole, "recordingId"},
            {TitleRole, "title"},        {StateRole, "jobState"},
            {StageRole, "stage"},        {ProgressRole, "progress"},
            {ErrorRole, "errorMessage"}, {CanCancelRole, "canCancel"},
            {CanRetryRole, "canRetry"},  {CanResumeRole, "canResume"},
            {CanRemoveRole, "canRemove"}};
}

QString JobListModel::enqueue(const QString& recordingId, const QString& title) {
    Job job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.recordingId = recordingId;
    job.title = title;
    const int insertionRow = static_cast<int>(m_jobs.size());
    beginInsertRows({}, insertionRow, insertionRow);
    m_jobs.append(job);
    endInsertRows();
    return job.id;
}

void JobListModel::upsert(const QString& id, const QString& recordingId, const QString& title,
                          const QString& state, const QString& stage, qreal progress, const QString& error) {
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
        return;
    }
    Job& job = m_jobs[existing];
    job.recordingId = recordingId;
    job.title = title;
    job.state = state;
    job.stage = stage;
    job.progress = qBound(0.0, progress, 1.0);
    job.error = error;
    emitRowChanged(existing);
}

bool JobListModel::cancel(const QString& id) {
    const int row = indexOf(id);
    if (row < 0 || !data(index(row), CanCancelRole).toBool()) {
        return false;
    }
    Job& job = m_jobs[row];
    job.state =
        job.state == QLatin1String("Queued") ? QStringLiteral("Cancelled") : QStringLiteral("Cancelling");
    emitRowChanged(row);
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
    emitRowChanged(row);
    return true;
}

bool JobListModel::resume(const QString& id) {
    const int row = indexOf(id);
    if (row < 0 || !data(index(row), CanResumeRole).toBool()) {
        return false;
    }
    m_jobs[row].state = QStringLiteral("Queued");
    emitRowChanged(row);
    return true;
}

bool JobListModel::remove(const QString& id) {
    const int row = indexOf(id);
    if (row < 0 || !data(index(row), CanRemoveRole).toBool()) {
        return false;
    }
    beginRemoveRows({}, row, row);
    m_jobs.removeAt(row);
    endRemoveRows();
    return true;
}

bool JobListModel::move(const QString& id, int destination) {
    const int row = indexOf(id);
    const int bounded = qBound(0, destination, static_cast<int>(m_jobs.size()) - 1);
    if (row < 0 || row == bounded || m_jobs.at(row).state != QLatin1String("Queued")) {
        return false;
    }
    const int destinationChild = bounded > row ? bounded + 1 : bounded;
    if (!beginMoveRows({}, row, row, {}, destinationChild)) {
        return false;
    }
    m_jobs.move(row, bounded);
    endMoveRows();
    return true;
}

void JobListModel::clearCompleted() {
    for (int row = static_cast<int>(m_jobs.size()) - 1; row >= 0; --row) {
        if (m_jobs.at(row).state == QLatin1String("Completed")) {
            beginRemoveRows({}, row, row);
            m_jobs.removeAt(row);
            endRemoveRows();
        }
    }
}

int JobListModel::activeCount() const {
    int count = 0;
    for (const Job& job : m_jobs) {
        if (job.state != QLatin1String("Completed") && job.state != QLatin1String("Cancelled") &&
            job.state != QLatin1String("Failed")) {
            ++count;
        }
    }
    return count;
}

bool JobListModel::isWritingTranscript(const QString& id) const {
    const int row = indexOf(id);
    if (row < 0) {
        return false;
    }
    const QString& state = m_jobs.at(row).state;
    return state != QLatin1String("Queued") && state != QLatin1String("Completed") &&
           state != QLatin1String("Cancelled") && state != QLatin1String("Failed") &&
           state != QLatin1String("Interrupted");
}

int JobListModel::indexOf(const QString& id) const {
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

void JobListModel::emitRowChanged(int row) {
    emit dataChanged(index(row), index(row));
}

} // namespace BreezeDesk
