#include "breezedesk/ui/RecordingListModel.h"

#include <QFileInfo>
#include <QUuid>

#include <algorithm>

namespace BreezeDesk {

RecordingListModel::RecordingListModel(QObject* parent) : QAbstractListModel(parent) {}

int RecordingListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_recordings.size());
}

QVariant RecordingListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_recordings.size()) {
        return {};
    }
    const Recording& item = m_recordings.at(index.row());
    switch (role) {
    case IdRole:
        return item.id;
    case TitleRole:
        return item.title;
    case SourceUrlRole:
        return item.sourceUrl;
    case DurationMsRole:
        return item.durationMs;
    case CreatedAtRole:
        return item.createdAt;
    case StatusRole:
        return item.status;
    case ModelRole:
        return item.model;
    case TagsRole:
        return item.tags;
    case ReviewStateRole:
        return item.reviewState;
    case ProgressRole:
        return item.progress;
    case DeletedRole:
        return item.deleted;
    case NotesRole:
        return item.notes;
    case SourceMissingRole: {
        const QString sourcePath =
            item.originalSourcePath.isEmpty() ? item.sourceUrl.toLocalFile() : item.originalSourcePath;
        return !QFileInfo(sourcePath).isFile();
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> RecordingListModel::roleNames() const {
    return {{IdRole, "recordingId"},
            {TitleRole, "title"},
            {SourceUrlRole, "sourceUrl"},
            {DurationMsRole, "durationMs"},
            {CreatedAtRole, "createdAt"},
            {StatusRole, "status"},
            {ModelRole, "modelName"},
            {TagsRole, "tags"},
            {ReviewStateRole, "reviewState"},
            {ProgressRole, "progress"},
            {DeletedRole, "deleted"},
            {NotesRole, "notes"},
            {SourceMissingRole, "sourceMissing"}};
}

QString RecordingListModel::addSource(const QUrl& source) {
    const QFileInfo info(source.toLocalFile());
    Recording item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.title =
        uniqueTitle(info.completeBaseName().trimmed().isEmpty() ? info.fileName() : info.completeBaseName());
    item.sourceUrl = source;
    item.createdAt = QDateTime::currentDateTimeUtc();
    item.status = QStringLiteral("Imported");
    item.reviewState = QStringLiteral("Unreviewed");
    const int insertionRow = static_cast<int>(m_recordings.size());
    beginInsertRows({}, insertionRow, insertionRow);
    m_recordings.append(item);
    endInsertRows();
    return item.id;
}

bool RecordingListModel::addRecording(const ::BreezeDesk::Recording& recording) {
    if (recording.id.isEmpty() || indexOf(recording.id) >= 0) {
        return false;
    }
    Recording item;
    item.id = recording.id;
    item.title = recording.title;
    item.originalSourcePath = recording.sourcePath;
    item.managedMediaPath = recording.managedMediaPath;
    const QString playbackPath =
        QFileInfo(recording.managedMediaPath).isFile() ? recording.managedMediaPath : recording.sourcePath;
    item.sourceUrl = QUrl::fromLocalFile(playbackPath);
    item.durationMs = recording.durationMs;
    item.createdAt = recording.createdAt;
    item.status =
        recording.latestJobState.isEmpty()
            ? (recording.activeJobId.isEmpty() ? QStringLiteral("Imported") : QStringLiteral("Completed"))
            : recording.latestJobState;
    item.model = recording.latestJobModelId;
    item.progress = qBound(0.0, recording.latestJobProgress, 1.0);
    item.tags = recording.tags;
    item.reviewState = recording.reviewState;
    item.deleted = recording.deletedAt.isValid();
    item.notes = recording.notes;
    item.activeJobId = recording.activeJobId;
    item.waveformPath = recording.waveformPath;
    const int insertionRow = static_cast<int>(m_recordings.size());
    beginInsertRows({}, insertionRow, insertionRow);
    m_recordings.append(std::move(item));
    endInsertRows();
    return true;
}

void RecordingListModel::replaceRecordings(const QList<::BreezeDesk::Recording>& recordings) {
    beginResetModel();
    m_recordings.clear();
    m_recordings.reserve(recordings.size());
    for (const auto& recording : recordings) {
        if (recording.id.isEmpty()) {
            continue;
        }
        Recording item;
        item.id = recording.id;
        item.title = recording.title;
        item.originalSourcePath = recording.sourcePath;
        item.managedMediaPath = recording.managedMediaPath;
        const QString playbackPath = QFileInfo(recording.managedMediaPath).isFile()
                                         ? recording.managedMediaPath
                                         : recording.sourcePath;
        item.sourceUrl = QUrl::fromLocalFile(playbackPath);
        item.durationMs = recording.durationMs;
        item.createdAt = recording.createdAt;
        item.status =
            recording.latestJobState.isEmpty()
                ? (recording.activeJobId.isEmpty() ? QStringLiteral("Imported") : QStringLiteral("Completed"))
                : recording.latestJobState;
        item.model = recording.latestJobModelId;
        item.progress = qBound(0.0, recording.latestJobProgress, 1.0);
        item.tags = recording.tags;
        item.reviewState = recording.reviewState;
        item.deleted = recording.deletedAt.isValid();
        item.notes = recording.notes;
        item.activeJobId = recording.activeJobId;
        item.waveformPath = recording.waveformPath;
        m_recordings.append(std::move(item));
    }
    endResetModel();
}

QString RecordingListModel::availableTitle(const QString& candidate, const QString& excludedId) const {
    return uniqueTitle(candidate, excludedId);
}

bool RecordingListModel::moveToTrash(const QString& id) {
    const int row = indexOf(id);
    if (row < 0 || m_recordings.at(row).deleted) {
        return false;
    }
    m_recordings[row].deleted = true;
    emit dataChanged(index(row), index(row), {DeletedRole});
    return true;
}

bool RecordingListModel::restore(const QString& id) {
    const int row = indexOf(id);
    if (row < 0 || !m_recordings.at(row).deleted) {
        return false;
    }
    m_recordings[row].deleted = false;
    emit dataChanged(index(row), index(row), {DeletedRole});
    return true;
}

bool RecordingListModel::removePermanently(const QString& id) {
    const int row = indexOf(id);
    if (row < 0 || !m_recordings.at(row).deleted) {
        return false;
    }
    beginRemoveRows({}, row, row);
    m_recordings.removeAt(row);
    endRemoveRows();
    return true;
}

bool RecordingListModel::rename(const QString& id, const QString& title) {
    const int row = indexOf(id);
    const QString trimmed = title.trimmed();
    if (row < 0 || trimmed.isEmpty()) {
        return false;
    }
    m_recordings[row].title = uniqueTitle(trimmed, id);
    emit dataChanged(index(row), index(row), {TitleRole});
    return true;
}

bool RecordingListModel::relinkSource(const QString& id, const QString& sourcePath) {
    const int row = indexOf(id);
    const QFileInfo source(sourcePath);
    if (row < 0 || !source.isFile()) {
        return false;
    }
    Recording& recording = m_recordings[row];
    recording.originalSourcePath = source.absoluteFilePath();
    if (!QFileInfo(recording.managedMediaPath).isFile()) {
        recording.sourceUrl = QUrl::fromLocalFile(recording.originalSourcePath);
    }
    emit dataChanged(index(row), index(row), {SourceUrlRole, SourceMissingRole});
    return true;
}

bool RecordingListModel::setTags(const QString& id, const QStringList& tags) {
    const int row = indexOf(id);
    if (row < 0) {
        return false;
    }
    m_recordings[row].tags = tags;
    emit dataChanged(index(row), index(row), {TagsRole});
    return true;
}

bool RecordingListModel::setNotes(const QString& id, const QString& notes) {
    const int row = indexOf(id);
    if (row < 0 || m_recordings.at(row).notes == notes) {
        return false;
    }
    m_recordings[row].notes = notes;
    emit dataChanged(index(row), index(row), {NotesRole});
    return true;
}

bool RecordingListModel::setReviewState(const QString& id, const QString& state) {
    const int row = indexOf(id);
    if (row < 0) {
        return false;
    }
    m_recordings[row].reviewState = state;
    emit dataChanged(index(row), index(row), {ReviewStateRole});
    return true;
}

QVariantMap RecordingListModel::recording(const QString& id) const {
    const int row = indexOf(id);
    if (row < 0) {
        return {};
    }
    const auto& item = m_recordings.at(row);
    return {{"id", item.id},
            {"title", item.title},
            {"sourcePath",
             item.originalSourcePath.isEmpty() ? item.sourceUrl.toLocalFile() : item.originalSourcePath},
            {"playbackPath", item.sourceUrl.toLocalFile()},
            {"managedMediaPath", item.managedMediaPath},
            {"durationMs", item.durationMs},
            {"createdAt", item.createdAt},
            {"status", item.status},
            {"model", item.model},
            {"tags", item.tags},
            {"reviewState", item.reviewState},
            {"progress", item.progress},
            {"deleted", item.deleted},
            {"sourceMissing", !QFileInfo(item.originalSourcePath.isEmpty() ? item.sourceUrl.toLocalFile()
                                                                           : item.originalSourcePath)
                                   .isFile()},
            {"notes", item.notes},
            {"activeJobId", item.activeJobId},
            {"waveformPath", item.waveformPath}};
}

int RecordingListModel::indexOf(const QString& id) const {
    for (int i = 0; i < m_recordings.size(); ++i) {
        if (m_recordings.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

QString RecordingListModel::uniqueTitle(const QString& candidate, const QString& excludedId) const {
    QString result = candidate;
    int suffix = 2;
    auto exists = [this, &excludedId](const QString& value) {
        return std::any_of(
            m_recordings.cbegin(), m_recordings.cend(), [&value, &excludedId](const Recording& item) {
                return item.id != excludedId && item.title.compare(value, Qt::CaseInsensitive) == 0;
            });
    };
    while (exists(result)) {
        result = QStringLiteral("%1 (%2)").arg(candidate).arg(suffix++);
    }
    return result;
}

} // namespace BreezeDesk
