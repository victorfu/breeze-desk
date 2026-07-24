#include "breezedesk/ui/LibraryViewModel.h"

#include "breezedesk/audio/MediaFileSupport.h"
#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/database/IRecordingRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QUrl>
#include <QUuid>

#include <optional>

namespace {

QString absoluteCleanPath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool isStrictChildPath(const QString& path, const QString& directory) {
    const QString absolutePath = absoluteCleanPath(path);
    const QString absoluteDirectory = absoluteCleanPath(directory);
    const QString relativePath = QDir(absoluteDirectory).relativeFilePath(absolutePath);
    return !relativePath.isEmpty() && relativePath != QLatin1String(".") &&
           relativePath != QLatin1String("..") && !relativePath.startsWith(QLatin1String("../")) &&
           !relativePath.startsWith(QLatin1String("..\\")) && !QDir::isAbsolutePath(relativePath);
}

bool isFileWithin(const QString& path, const QString& allowedDirectory) {
    if (path.isEmpty() || !isStrictChildPath(path, allowedDirectory)) {
        return false;
    }

    const QFileInfo fileInfo(path);
    if (fileInfo.isSymLink()) {
        // QFile::remove() removes the link itself. The lexical boundary check above ensures the
        // directory entry belongs to BreezeDesk even when its target does not.
        return true;
    }
    if (fileInfo.isDir()) {
        return false;
    }

    const QString canonicalPath = fileInfo.canonicalFilePath();
    const QString canonicalDirectory = QFileInfo(allowedDirectory).canonicalFilePath();
    return canonicalPath.isEmpty() || canonicalDirectory.isEmpty() ||
           isStrictChildPath(canonicalPath, canonicalDirectory);
}

bool removeFileWithin(const QString& path, const QString& allowedDirectory) {
    if (!isFileWithin(path, allowedDirectory)) {
        return true;
    }
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() && !fileInfo.isSymLink()) {
        return true;
    }
    return QFile::remove(fileInfo.absoluteFilePath());
}

} // namespace

namespace BreezeDesk {

RecordingFilterProxyModel::RecordingFilterProxyModel(bool deleted, QObject* parent)
    : QSortFilterProxyModel(parent), m_deleted(deleted) {
    setDynamicSortFilter(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
}

void RecordingFilterProxyModel::setQuery(const QString& query) {
    if (m_query == query) {
        return;
    }
    m_query = query;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
}

void RecordingFilterProxyModel::setReviewFilter(const QString& reviewFilter) {
    if (m_reviewFilter == reviewFilter) {
        return;
    }
    m_reviewFilter = reviewFilter;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
}

bool RecordingFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
    if (sourceModel()->data(sourceIndex, RecordingListModel::DeletedRole).toBool() != m_deleted) {
        return false;
    }
    if (!m_reviewFilter.isEmpty() && sourceModel()
                                             ->data(sourceIndex, RecordingListModel::ReviewStateRole)
                                             .toString()
                                             .compare(m_reviewFilter, Qt::CaseInsensitive) != 0) {
        return false;
    }
    if (m_query.isEmpty()) {
        return true;
    }
    const QString title = sourceModel()->data(sourceIndex, RecordingListModel::TitleRole).toString();
    const QString notes = sourceModel()->data(sourceIndex, RecordingListModel::NotesRole).toString();
    const QStringList tags = sourceModel()->data(sourceIndex, RecordingListModel::TagsRole).toStringList();
    return title.contains(m_query, Qt::CaseInsensitive) || notes.contains(m_query, Qt::CaseInsensitive) ||
           tags.join(QLatin1Char(' ')).contains(m_query, Qt::CaseInsensitive);
}

LibraryViewModel::LibraryViewModel(QObject* parent) : LibraryViewModel(nullptr, parent) {}

LibraryViewModel::LibraryViewModel(IRecordingRepository* repository, QObject* parent)
    : QObject(parent), m_libraryProxy(false, this), m_trashProxy(true, this), m_repository(repository) {
    m_libraryProxy.setSourceModel(&m_source);
    m_trashProxy.setSourceModel(&m_source);

    connect(&m_source, &QAbstractItemModel::rowsInserted, this, &LibraryViewModel::emptyChanged);
    connect(&m_source, &QAbstractItemModel::rowsRemoved, this, &LibraryViewModel::emptyChanged);
    connect(&m_source, &QAbstractItemModel::dataChanged, this, &LibraryViewModel::emptyChanged);
    m_searchDebounce.setSingleShot(true);
    m_searchDebounce.setInterval(180);
    connect(&m_searchDebounce, &QTimer::timeout, this, &LibraryViewModel::refresh);
    refresh();
}

QAbstractItemModel* LibraryViewModel::recordings() noexcept {
    return &m_libraryProxy;
}
QAbstractItemModel* LibraryViewModel::trash() noexcept {
    return &m_trashProxy;
}
QString LibraryViewModel::searchText() const {
    return m_searchText;
}
QString LibraryViewModel::sortMode() const {
    return m_sortMode;
}
QString LibraryViewModel::reviewFilter() const {
    return m_reviewFilter;
}
QString LibraryViewModel::selectedRecordingId() const {
    return m_selectedRecordingId;
}
bool LibraryViewModel::empty() const {
    return m_libraryProxy.rowCount() == 0;
}
bool LibraryViewModel::trashEmpty() const {
    return m_trashProxy.rowCount() == 0;
}

int LibraryViewModel::importUrls(const QVariantList& urls) {
    int imported = 0;
    for (const QVariant& value : urls) {
        const QUrl url = value.canConvert<QUrl>() ? value.toUrl() : QUrl::fromLocalFile(value.toString());
        const QFileInfo info(url.toLocalFile());
        if (!url.isLocalFile() || !info.exists() || !info.isFile()) {
            emit importRejected(url, tr("The selected file does not exist or is not a local file."));
            continue;
        }
        if (m_repository == nullptr) {
            const QString recordingId = m_source.addSource(url);
            emit recordingImported(recordingId, info.absoluteFilePath());
            ++imported;
            continue;
        }

        Recording recording;
        recording.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString candidate =
            info.completeBaseName().trimmed().isEmpty() ? info.fileName() : info.completeBaseName();
        recording.title = m_source.availableTitle(candidate);
        recording.sourcePath = info.absoluteFilePath();
        recording.mediaType = QStringLiteral("media");
        recording.createdAt = QDateTime::currentDateTimeUtc();
        recording.updatedAt = recording.createdAt;
        const auto result = m_repository->create(recording);
        if (!result) {
            emit importRejected(url, result.error().message);
            continue;
        }
        m_source.addRecording(recording);
        emit recordingImported(recording.id, recording.sourcePath);
        ++imported;
    }
    if (imported > 0) {
        emit filesImported(imported);
    }
    return imported;
}

QString LibraryViewModel::importManagedCopy(const QUrl& originalUrl, const QString& managedPath) {
    const QFileInfo original(originalUrl.toLocalFile());
    const QFileInfo managed(managedPath);
    if (m_repository == nullptr || !originalUrl.isLocalFile() || !original.isFile() || !managed.isFile() ||
        !isFileWithin(managed.absoluteFilePath(), StoragePaths::recordings())) {
        emit importRejected(originalUrl, tr("The managed media copy could not be imported."));
        return {};
    }
    Recording recording;
    recording.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString candidate =
        original.completeBaseName().trimmed().isEmpty() ? original.fileName() : original.completeBaseName();
    recording.title = m_source.availableTitle(candidate);
    recording.sourcePath = original.absoluteFilePath();
    recording.managedMediaPath = managed.absoluteFilePath();
    recording.mediaType = QStringLiteral("media");
    recording.createdAt = QDateTime::currentDateTimeUtc();
    recording.updatedAt = recording.createdAt;
    const auto result = m_repository->create(recording);
    if (!result) {
        emit importRejected(originalUrl, result.error().message);
        return {};
    }
    m_source.addRecording(recording);
    emit recordingImported(recording.id, recording.sourcePath);
    emit filesImported(1);
    return recording.id;
}

void LibraryViewModel::moveToTrash(const QString& id) {
    if (m_repository != nullptr) {
        const auto result = m_repository->moveToTrash(id);
        if (!result) {
            emit operationFailed(result.error().message);
            return;
        }
    }
    m_source.moveToTrash(id);
}
void LibraryViewModel::restore(const QString& id) {
    if (m_repository != nullptr) {
        const auto result = m_repository->restore(id);
        if (!result) {
            emit operationFailed(result.error().message);
            return;
        }
    }
    m_source.restore(id);
}
void LibraryViewModel::deletePermanently(const QString& id) {
    std::optional<Recording> storedRecording;
    if (m_repository != nullptr) {
        const auto existing = m_repository->findById(id);
        if (!existing || !existing.value().has_value()) {
            emit operationFailed(existing ? tr("The recording no longer exists.") : existing.error().message);
            return;
        }
        storedRecording = existing.value().value();
        if (storedRecording->deletedAt.isValid()) {
            emit recordingAboutToBePermanentlyDeleted(id);
        }
        const auto result = m_repository->permanentlyDelete(id);
        if (!result) {
            emit operationFailed(result.error().message);
            return;
        }
    }
    m_source.removePermanently(id);

    if (!storedRecording.has_value()) {
        return;
    }

    const QString sourcePath = absoluteCleanPath(storedRecording->sourcePath);
    const QString managedPath = absoluteCleanPath(storedRecording->managedMediaPath);
    QStringList removalFailures;
    const auto removeArtifact = [&removalFailures](const QString& path, const QString& directory) {
        if (path.isEmpty()) {
            return;
        }
        const QString absolutePath = absoluteCleanPath(path);
        if (!removeFileWithin(absolutePath, directory)) {
            removalFailures.append(QFileInfo(absolutePath).fileName());
        }
    };
    removeArtifact(storedRecording->managedMediaPath, StoragePaths::recordings());
    // Cache metadata must never turn an original source into a deletion target. A source is
    // removable only when it is also explicitly recorded as the managed media copy.
    if (absoluteCleanPath(storedRecording->normalizedPcmPath) != sourcePath ||
        absoluteCleanPath(storedRecording->normalizedPcmPath) == managedPath) {
        removeArtifact(storedRecording->normalizedPcmPath, StoragePaths::cache());
    }
    if (absoluteCleanPath(storedRecording->waveformPath) != sourcePath ||
        absoluteCleanPath(storedRecording->waveformPath) == managedPath) {
        removeArtifact(storedRecording->waveformPath, StoragePaths::cache());
    }
    if (!removalFailures.isEmpty()) {
        emit operationFailed(tr("The recording was deleted, but some managed files could not be removed: %1")
                                 .arg(removalFailures.join(QStringLiteral(", "))));
    }
    emit recordingPermanentlyDeleted(id);
}
void LibraryViewModel::rename(const QString& id, const QString& title) {
    const QString trimmedTitle = title.trimmed();
    if (trimmedTitle.isEmpty()) {
        emit operationFailed(tr("A recording title is required."));
        return;
    }
    if (m_repository == nullptr) {
        if (!m_source.rename(id, trimmedTitle)) {
            emit operationFailed(tr("The recording could not be renamed."));
            return;
        }
        emit recordingMetadataChanged(id);
        return;
    }
    const auto existing = m_repository->findById(id);
    if (!existing || !existing.value().has_value()) {
        emit operationFailed(existing ? tr("The recording no longer exists.") : existing.error().message);
        return;
    }
    Recording recording = existing.value().value();
    recording.title = m_source.availableTitle(trimmedTitle, id);
    const auto result = m_repository->update(recording);
    if (!result) {
        emit operationFailed(result.error().message);
        return;
    }
    if (!m_source.rename(id, recording.title)) {
        refresh();
    }
    emit recordingMetadataChanged(id);
}

void LibraryViewModel::relinkSource(const QString& id, const QUrl& source) {
    const QFileInfo sourceInfo(source.toLocalFile());
    if (!source.isLocalFile() || !sourceInfo.isFile()) {
        emit operationFailed(tr("Choose an existing local media file."));
        return;
    }
    if (!MediaFileSupport::isSupportedPath(sourceInfo.fileName())) {
        emit operationFailed(tr("The selected file is not a supported audio or video file."));
        return;
    }
    if (m_repository == nullptr) {
        if (!m_source.relinkSource(id, sourceInfo.absoluteFilePath())) {
            emit operationFailed(tr("The recording source could not be relinked."));
            return;
        }
        emit recordingMetadataChanged(id);
        return;
    }
    const auto existing = m_repository->findById(id);
    if (!existing || !existing.value().has_value()) {
        emit operationFailed(existing ? tr("The recording no longer exists.") : existing.error().message);
        return;
    }
    Recording recording = existing.value().value();
    recording.sourcePath = sourceInfo.absoluteFilePath();
    if (!QFileInfo(recording.managedMediaPath).isFile()) {
        // A relink can point at a different file. Never reuse derived artifacts whose provenance
        // cannot be verified against the newly selected source.
        recording.sourceHash.clear();
        recording.normalizedPcmPath.clear();
        recording.waveformPath.clear();
        recording.durationMs = 0;
        recording.sampleRate = 0;
        recording.channelCount = 0;
    }
    const auto result = m_repository->update(recording);
    if (!result) {
        emit operationFailed(result.error().message);
        return;
    }
    refresh();
    emit recordingMetadataChanged(id);
}
void LibraryViewModel::setTags(const QString& id, const QStringList& tags) {
    QStringList uniqueTags;
    QSet<QString> seen;
    for (const QString& tag : tags) {
        const QString clean = tag.trimmed();
        const QString key = clean.toCaseFolded();
        if (!clean.isEmpty() && !seen.contains(key)) {
            uniqueTags.append(clean);
            seen.insert(key);
        }
    }
    if (m_repository != nullptr) {
        const auto result = m_repository->setTags(id, uniqueTags);
        if (!result) {
            emit operationFailed(result.error().message);
            return;
        }
    }
    m_source.setTags(id, uniqueTags);
    emit recordingMetadataChanged(id);
}

void LibraryViewModel::setTagsText(const QString& id, const QString& tags) {
    QString normalized = tags;
    normalized.replace(QLatin1Char(';'), QLatin1Char(','));
    setTags(id, normalized.split(QLatin1Char(','), Qt::SkipEmptyParts));
}

void LibraryViewModel::setReviewState(const QString& id, const bool reviewed) {
    const QString state = reviewed ? QStringLiteral("reviewed") : QStringLiteral("unreviewed");
    if (m_repository != nullptr) {
        const auto existing = m_repository->findById(id);
        if (!existing || !existing.value().has_value()) {
            emit operationFailed(existing ? tr("The recording no longer exists.") : existing.error().message);
            return;
        }
        Recording recording = existing.value().value();
        if (recording.reviewState.compare(state, Qt::CaseInsensitive) == 0) {
            return;
        }
        recording.reviewState = state;
        const auto result = m_repository->update(recording);
        if (!result) {
            emit operationFailed(result.error().message);
            return;
        }
    }
    m_source.setReviewState(id, state);
    emit recordingMetadataChanged(id);
}

bool LibraryViewModel::setNotes(const QString& id, const QString& notes) {
    if (m_repository != nullptr) {
        const auto existing = m_repository->findById(id);
        if (!existing || !existing.value().has_value()) {
            emit operationFailed(existing ? tr("The recording no longer exists.") : existing.error().message);
            return false;
        }
        Recording recording = existing.value().value();
        if (recording.notes == notes) {
            return true;
        }
        recording.notes = notes;
        const auto result = m_repository->update(recording);
        if (!result) {
            emit operationFailed(result.error().message);
            return false;
        }
    }
    m_source.setNotes(id, notes);
    return true;
}

QVariantMap LibraryViewModel::details(const QString& id) const {
    return m_source.recording(id);
}

void LibraryViewModel::refresh() {
    if (m_repository == nullptr) {
        return;
    }
    RecordingQuery query;
    query.includeDeleted = true;
    query.searchText = m_searchText;
    constexpr int PageSize = 500;
    query.limit = PageSize;
    if (m_sortMode == QLatin1String("Oldest")) {
        query.sortColumn = QStringLiteral("created_at");
        query.sortOrder = Qt::AscendingOrder;
    } else if (m_sortMode == QLatin1String("TitleAZ")) {
        query.sortColumn = QStringLiteral("title");
        query.sortOrder = Qt::AscendingOrder;
    } else if (m_sortMode == QLatin1String("TitleZA")) {
        query.sortColumn = QStringLiteral("title");
        query.sortOrder = Qt::DescendingOrder;
    }
    if (m_reviewFilter != QLatin1String("All")) {
        query.status = m_reviewFilter.toLower();
    }
    QList<Recording> recordings;
    int totalCount = 0;
    do {
        query.offset = static_cast<int>(recordings.size());
        const auto result = m_repository->list(query);
        if (!result) {
            emit operationFailed(result.error().message);
            return;
        }
        totalCount = result.value().totalCount;
        if (result.value().items.isEmpty()) {
            break;
        }
        recordings.append(result.value().items);
    } while (recordings.size() < totalCount);
    m_source.replaceRecordings(recordings);
    emit emptyChanged();
}

void LibraryViewModel::setRepository(IRecordingRepository* repository) {
    if (m_repository == repository) {
        return;
    }
    m_repository = repository;
    refresh();
}

void LibraryViewModel::setSearchText(const QString& text) {
    if (m_searchText == text) {
        return;
    }
    m_searchText = text;
    if (m_repository == nullptr) {
        m_libraryProxy.setQuery(text);
    } else {
        m_libraryProxy.setQuery({});
        m_trashProxy.setQuery({});
        m_searchDebounce.start();
    }
    emit searchTextChanged();
    emit emptyChanged();
}

void LibraryViewModel::setSortMode(const QString& mode) {
    static const QStringList allowed{QStringLiteral("Newest"), QStringLiteral("Oldest"),
                                     QStringLiteral("TitleAZ"), QStringLiteral("TitleZA")};
    if (!allowed.contains(mode) || m_sortMode == mode) {
        return;
    }
    m_sortMode = mode;
    if (m_repository == nullptr) {
        if (mode == QLatin1String("Newest") || mode == QLatin1String("Oldest")) {
            m_libraryProxy.setSortRole(RecordingListModel::CreatedAtRole);
            m_trashProxy.setSortRole(RecordingListModel::CreatedAtRole);
        } else {
            m_libraryProxy.setSortRole(RecordingListModel::TitleRole);
            m_trashProxy.setSortRole(RecordingListModel::TitleRole);
        }
        const Qt::SortOrder order = mode == QLatin1String("Oldest") || mode == QLatin1String("TitleAZ")
                                        ? Qt::AscendingOrder
                                        : Qt::DescendingOrder;
        m_libraryProxy.sort(0, order);
        m_trashProxy.sort(0, order);
        emit emptyChanged();
    } else {
        refresh();
    }
    emit sortModeChanged();
}

void LibraryViewModel::setReviewFilter(const QString& filter) {
    static const QStringList allowed{QStringLiteral("All"), QStringLiteral("Reviewed"),
                                     QStringLiteral("Unreviewed")};
    if (!allowed.contains(filter) || m_reviewFilter == filter) {
        return;
    }
    m_reviewFilter = filter;
    if (m_repository == nullptr) {
        const QString value = filter == QLatin1String("All") ? QString() : filter;
        m_libraryProxy.setReviewFilter(value);
        m_trashProxy.setReviewFilter(value);
        emit emptyChanged();
    } else {
        refresh();
    }
    emit reviewFilterChanged();
}

void LibraryViewModel::setSelectedRecordingId(const QString& id) {
    if (m_selectedRecordingId == id) {
        return;
    }
    m_selectedRecordingId = id;
    emit selectedRecordingIdChanged();
}

void LibraryViewModel::activateRecording(const QString& id) {
    if (!id.isEmpty()) {
        emit recordingActivated(id);
    }
}

} // namespace BreezeDesk
