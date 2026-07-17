#include "breezedesk/database/SqliteRecordingRepository.h"

#include "breezedesk/core/TimeUtils.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/DatabaseSearchService.h"

#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace BreezeDesk {
namespace {

UserFacingError queryError(const QString& message, const QSqlQuery& query) {
    return UserFacingError::database(ErrorCode::DatabaseQueryFailed, message, query.lastError().text(), true);
}

Recording readRecording(QSqlQuery& query) {
    Recording value;
    value.id = query.value(QStringLiteral("id")).toString();
    value.title = query.value(QStringLiteral("title")).toString();
    value.sourcePath = query.value(QStringLiteral("source_path")).toString();
    value.managedMediaPath = query.value(QStringLiteral("managed_media_path")).toString();
    value.normalizedPcmPath = query.value(QStringLiteral("normalized_pcm_path")).toString();
    value.sourceHash = query.value(QStringLiteral("source_hash")).toString();
    value.mediaType = query.value(QStringLiteral("media_type")).toString();
    value.durationMs = query.value(QStringLiteral("duration_ms")).toLongLong();
    value.sampleRate = query.value(QStringLiteral("sample_rate")).toInt();
    value.channelCount = query.value(QStringLiteral("channel_count")).toInt();
    value.waveformPath = query.value(QStringLiteral("waveform_path")).toString();
    value.createdAt = TimeUtils::fromStorageString(query.value(QStringLiteral("created_at")).toString());
    value.updatedAt = TimeUtils::fromStorageString(query.value(QStringLiteral("updated_at")).toString());
    value.deletedAt = TimeUtils::fromStorageString(query.value(QStringLiteral("deleted_at")).toString());
    value.notes = query.value(QStringLiteral("notes")).toString();
    value.reviewState = query.value(QStringLiteral("review_state")).toString();
    value.activeJobId = query.value(QStringLiteral("active_job_id")).toString();
    value.latestJobState = query.value(QStringLiteral("latest_job_state")).toString();
    value.latestJobModelId = query.value(QStringLiteral("latest_job_model_id")).toString();
    value.latestJobProgress = query.value(QStringLiteral("latest_job_progress")).toDouble();
    return value;
}

QString recordingSelection(const QString& alias) {
    return QStringLiteral("%1.*,"
                          "COALESCE((SELECT j.state FROM transcription_jobs j WHERE j.recording_id=%1.id "
                          "ORDER BY j.created_at DESC,j.id DESC LIMIT 1),'') AS latest_job_state,"
                          "COALESCE((SELECT j.model_id FROM transcription_jobs j WHERE j.recording_id=%1.id "
                          "ORDER BY j.created_at DESC,j.id DESC LIMIT 1),'') AS latest_job_model_id,"
                          "COALESCE((SELECT j.progress FROM transcription_jobs j WHERE j.recording_id=%1.id "
                          "ORDER BY j.created_at DESC,j.id DESC LIMIT 1),0) AS latest_job_progress")
        .arg(alias);
}

Result<QStringList> loadTags(QSqlDatabase& database, const QString& recordingId) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT t.name FROM tags t JOIN recording_tags rt ON rt.tag_id=t.id "
                                 "WHERE rt.recording_id=? ORDER BY t.name COLLATE NOCASE"));
    query.addBindValue(recordingId);
    if (!query.exec())
        return Result<QStringList>::failure(queryError(QStringLiteral("Tags could not be loaded."), query));
    QStringList tags;
    while (query.next())
        tags.append(query.value(0).toString());
    return Result<QStringList>::success(tags);
}

Result<QHash<QString, QStringList>> loadTags(QSqlDatabase& database, const QStringList& recordingIds) {
    QHash<QString, QStringList> tags;
    if (recordingIds.isEmpty()) {
        return Result<QHash<QString, QStringList>>::success(tags);
    }
    constexpr int TagBatchSize = 500;
    const int idCount = static_cast<int>(recordingIds.size());
    for (int offset = 0; offset < idCount; offset += TagBatchSize) {
        const int batchSize = qMin(TagBatchSize, idCount - offset);
        QStringList placeholders;
        placeholders.fill(QStringLiteral("?"), batchSize);
        QSqlQuery query(database);
        query.prepare(QStringLiteral("SELECT rt.recording_id,t.name FROM recording_tags rt "
                                     "JOIN tags t ON t.id=rt.tag_id WHERE rt.recording_id IN (%1) "
                                     "ORDER BY rt.recording_id,t.name COLLATE NOCASE")
                          .arg(placeholders.join(QLatin1Char(','))));
        for (int index = 0; index < batchSize; ++index) {
            query.addBindValue(recordingIds.at(offset + index));
        }
        if (!query.exec()) {
            return Result<QHash<QString, QStringList>>::failure(
                queryError(QStringLiteral("Tags could not be loaded."), query));
        }
        while (query.next()) {
            tags[query.value(0).toString()].append(query.value(1).toString());
        }
    }
    return Result<QHash<QString, QStringList>>::success(tags);
}

QString safeSortColumn(const QString& requested) {
    static const QSet<QString> columns = {QStringLiteral("title"), QStringLiteral("created_at"),
                                          QStringLiteral("updated_at"), QStringLiteral("duration_ms"),
                                          QStringLiteral("review_state")};
    return columns.contains(requested) ? requested : QStringLiteral("updated_at");
}

QString nonNull(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value;
}

} // namespace

SqliteRecordingRepository::SqliteRecordingRepository(DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager) {}

Result<void> SqliteRecordingRepository::create(Recording recording) {
    if (recording.title.trimmed().isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A recording title is required.")));
    }
    if (recording.id.isEmpty())
        recording.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!recording.createdAt.isValid())
        recording.createdAt = now;
    recording.updatedAt = now;
    auto result = m_databaseManager.transaction([&recording](QSqlDatabase& database) {
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "INSERT INTO recordings(id,title,source_path,managed_media_path,normalized_pcm_path,"
            "source_hash,media_type,duration_ms,sample_rate,channel_count,waveform_path,created_at,"
            "updated_at,deleted_at,notes,review_state,active_job_id) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        query.addBindValue(recording.id);
        query.addBindValue(recording.title.trimmed());
        query.addBindValue(nonNull(recording.sourcePath));
        query.addBindValue(nonNull(recording.managedMediaPath));
        query.addBindValue(nonNull(recording.normalizedPcmPath));
        query.addBindValue(nonNull(recording.sourceHash));
        query.addBindValue(nonNull(recording.mediaType));
        query.addBindValue(recording.durationMs);
        query.addBindValue(recording.sampleRate);
        query.addBindValue(recording.channelCount);
        query.addBindValue(nonNull(recording.waveformPath));
        query.addBindValue(TimeUtils::toStorageString(recording.createdAt));
        query.addBindValue(TimeUtils::toStorageString(recording.updatedAt));
        query.addBindValue(QVariant());
        query.addBindValue(nonNull(recording.notes));
        query.addBindValue(nonNull(recording.reviewState));
        query.addBindValue(recording.activeJobId.isEmpty() ? QVariant() : QVariant(recording.activeJobId));
        if (!query.exec())
            return Result<void>::failure(
                queryError(QStringLiteral("The recording could not be created."), query));
        return Result<void>::success();
    });
    if (!result)
        return result;
    if (!recording.tags.isEmpty()) {
        auto tagResult = setTags(recording.id, recording.tags);
        if (!tagResult)
            return tagResult;
    }
    return DatabaseSearchService(m_databaseManager).rebuildRecording(recording.id);
}

Result<void> SqliteRecordingRepository::update(const Recording& recording) {
    if (recording.id.isEmpty() || recording.title.trimmed().isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("Recording ID and title are required.")));
    }
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral(
        "UPDATE recordings SET title=?,source_path=?,managed_media_path=?,normalized_pcm_path=?,"
        "source_hash=?,media_type=?,duration_ms=?,sample_rate=?,channel_count=?,waveform_path=?,"
        "updated_at=?,notes=?,review_state=? WHERE id=?"));
    query.addBindValue(recording.title.trimmed());
    query.addBindValue(nonNull(recording.sourcePath));
    query.addBindValue(nonNull(recording.managedMediaPath));
    query.addBindValue(nonNull(recording.normalizedPcmPath));
    query.addBindValue(nonNull(recording.sourceHash));
    query.addBindValue(nonNull(recording.mediaType));
    query.addBindValue(qMax<qint64>(0, recording.durationMs));
    query.addBindValue(qMax(0, recording.sampleRate));
    query.addBindValue(qMax(0, recording.channelCount));
    query.addBindValue(nonNull(recording.waveformPath));
    query.addBindValue(TimeUtils::nowStorageString());
    query.addBindValue(nonNull(recording.notes));
    query.addBindValue(nonNull(recording.reviewState));
    query.addBindValue(recording.id);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The recording could not be updated."), query));
    if (query.numRowsAffected() == 0) {
        return Result<void>::failure(UserFacingError::database(
            ErrorCode::NotFound, QStringLiteral("The recording no longer exists."), recording.id));
    }
    if (!recording.tags.isEmpty()) {
        auto tagResult = setTags(recording.id, recording.tags);
        if (!tagResult)
            return tagResult;
    }
    return DatabaseSearchService(m_databaseManager).rebuildRecording(recording.id);
}

Result<std::optional<Recording>> SqliteRecordingRepository::findById(const QString& id) const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<std::optional<Recording>>::failure(connectionResult.error());
    QSqlDatabase database = connectionResult.value();
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT %1 FROM recordings r WHERE r.id=?")
                      .arg(recordingSelection(QStringLiteral("r"))));
    query.addBindValue(id);
    if (!query.exec())
        return Result<std::optional<Recording>>::failure(
            queryError(QStringLiteral("The recording could not be loaded."), query));
    if (!query.next())
        return Result<std::optional<Recording>>::success(std::nullopt);
    Recording recording = readRecording(query);
    auto tags = loadTags(database, id);
    if (!tags)
        return Result<std::optional<Recording>>::failure(tags.error());
    recording.tags = tags.value();
    return Result<std::optional<Recording>>::success(recording);
}

Result<std::optional<Recording>>
SqliteRecordingRepository::findBySourcePath(const QString& sourcePath) const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<std::optional<Recording>>::failure(connectionResult.error());
    QSqlDatabase database = connectionResult.value();
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT %1 FROM recordings r WHERE r.source_path=? AND r.deleted_at IS "
                                 "NULL ORDER BY r.updated_at DESC LIMIT 1")
                      .arg(recordingSelection(QStringLiteral("r"))));
    query.addBindValue(nonNull(sourcePath));
    if (!query.exec())
        return Result<std::optional<Recording>>::failure(
            queryError(QStringLiteral("The recording source could not be looked up."), query));
    if (!query.next())
        return Result<std::optional<Recording>>::success(std::nullopt);
    Recording recording = readRecording(query);
    auto tags = loadTags(database, recording.id);
    if (!tags)
        return Result<std::optional<Recording>>::failure(tags.error());
    recording.tags = tags.value();
    return Result<std::optional<Recording>>::success(recording);
}

Result<RecordingPage> SqliteRecordingRepository::list(const RecordingQuery& request) const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<RecordingPage>::failure(connectionResult.error());
    QSqlDatabase database = connectionResult.value();
    constexpr int MaximumPageSize = 1'000;
    const int limit = qBound(1, request.limit, MaximumPageSize);
    const int offset = qMax(0, request.offset);
    QStringList where;
    QVariantList binds;
    if (request.deletedOnly)
        where.append(QStringLiteral("r.deleted_at IS NOT NULL"));
    else if (!request.includeDeleted)
        where.append(QStringLiteral("r.deleted_at IS NULL"));
    if (!request.status.isEmpty()) {
        where.append(QStringLiteral("r.review_state=?"));
        binds.append(request.status);
    }
    if (!request.tag.isEmpty()) {
        where.append(QStringLiteral("EXISTS(SELECT 1 FROM recording_tags rt JOIN tags t ON t.id=rt.tag_id "
                                    "WHERE rt.recording_id=r.id AND t.name=? COLLATE NOCASE)"));
        binds.append(request.tag);
    }
    if (!request.searchText.trimmed().isEmpty()) {
        where.append(
            QStringLiteral("(r.title LIKE ? ESCAPE '\\' OR r.notes LIKE ? ESCAPE '\\' OR "
                           "EXISTS(SELECT 1 FROM recording_tags srt JOIN tags st ON st.id=srt.tag_id "
                           "WHERE srt.recording_id=r.id AND st.name LIKE ? ESCAPE '\\') OR "
                           "EXISTS(SELECT 1 FROM transcript_segments s WHERE s.recording_id=r.id "
                           "AND (s.edited_text LIKE ? ESCAPE '\\' OR s.original_text LIKE ? ESCAPE '\\')))"));
        QString pattern = request.searchText;
        pattern.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        pattern.replace(QLatin1Char('%'), QStringLiteral("\\%"));
        pattern.replace(QLatin1Char('_'), QStringLiteral("\\_"));
        pattern = QLatin1Char('%') + pattern + QLatin1Char('%');
        for (int i = 0; i < 5; ++i)
            binds.append(pattern);
    }
    const QString clause =
        where.isEmpty() ? QString() : QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    QSqlQuery count(database);
    count.prepare(QStringLiteral("SELECT COUNT(*) FROM recordings r") + clause);
    for (const QVariant& bind : binds)
        count.addBindValue(bind);
    if (!count.exec() || !count.next())
        return Result<RecordingPage>::failure(
            queryError(QStringLiteral("The recording count could not be loaded."), count));
    RecordingPage page;
    page.totalCount = count.value(0).toInt();
    page.offset = offset;
    page.limit = limit;
    const QString direction =
        request.sortOrder == Qt::AscendingOrder ? QStringLiteral("ASC") : QStringLiteral("DESC");
    QSqlQuery rows(database);
    rows.prepare(QStringLiteral("SELECT %1 FROM recordings r").arg(recordingSelection(QStringLiteral("r"))) +
                 clause + QStringLiteral(" ORDER BY r.") + safeSortColumn(request.sortColumn) +
                 QLatin1Char(' ') + direction + QStringLiteral(", r.id ASC LIMIT ? OFFSET ?"));
    for (const QVariant& bind : binds)
        rows.addBindValue(bind);
    rows.addBindValue(limit);
    rows.addBindValue(offset);
    if (!rows.exec())
        return Result<RecordingPage>::failure(
            queryError(QStringLiteral("The recordings could not be loaded."), rows));
    QStringList recordingIds;
    while (rows.next()) {
        page.items.append(readRecording(rows));
        recordingIds.append(page.items.constLast().id);
    }
    const auto tags = loadTags(database, recordingIds);
    if (!tags) {
        return Result<RecordingPage>::failure(tags.error());
    }
    for (Recording& recording : page.items) {
        recording.tags = tags.value().value(recording.id);
    }
    return Result<RecordingPage>::success(page);
}

Result<void> SqliteRecordingRepository::setTags(const QString& recordingId, const QStringList& tags) {
    QStringList unique;
    QSet<QString> seen;
    for (const QString& tag : tags) {
        const QString clean = tag.trimmed();
        const QString key = clean.toCaseFolded();
        if (!clean.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            unique.append(clean);
        }
    }
    auto result = m_databaseManager.transaction([&](QSqlDatabase& database) {
        QSqlQuery remove(database);
        remove.prepare(QStringLiteral("DELETE FROM recording_tags WHERE recording_id=?"));
        remove.addBindValue(recordingId);
        if (!remove.exec())
            return Result<void>::failure(
                queryError(QStringLiteral("Existing tags could not be replaced."), remove));
        for (const QString& tag : unique) {
            QSqlQuery insertTag(database);
            const QString tagId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            insertTag.prepare(QStringLiteral("INSERT OR IGNORE INTO tags(id,name,created_at) VALUES(?,?,?)"));
            insertTag.addBindValue(tagId);
            insertTag.addBindValue(tag);
            insertTag.addBindValue(TimeUtils::nowStorageString());
            if (!insertTag.exec())
                return Result<void>::failure(
                    queryError(QStringLiteral("A tag could not be created."), insertTag));
            QSqlQuery attach(database);
            attach.prepare(QStringLiteral("INSERT INTO recording_tags(recording_id,tag_id) SELECT ?,id FROM "
                                          "tags WHERE name=? COLLATE NOCASE"));
            attach.addBindValue(recordingId);
            attach.addBindValue(tag);
            if (!attach.exec())
                return Result<void>::failure(
                    queryError(QStringLiteral("A tag could not be attached."), attach));
        }
        return Result<void>::success();
    });
    if (!result)
        return result;
    return DatabaseSearchService(m_databaseManager).rebuildRecording(recordingId);
}

Result<void> SqliteRecordingRepository::moveToTrash(const QString& id) {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(
        QStringLiteral("UPDATE recordings SET deleted_at=?,updated_at=? WHERE id=? AND deleted_at IS NULL"));
    const QString now = TimeUtils::nowStorageString();
    query.addBindValue(now);
    query.addBindValue(now);
    query.addBindValue(id);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The recording could not be moved to Trash."), query));
    return Result<void>::success();
}

Result<void> SqliteRecordingRepository::restore(const QString& id) {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral(
        "UPDATE recordings SET deleted_at=NULL,updated_at=? WHERE id=? AND deleted_at IS NOT NULL"));
    query.addBindValue(TimeUtils::nowStorageString());
    query.addBindValue(id);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The recording could not be restored."), query));
    return Result<void>::success();
}

Result<void> SqliteRecordingRepository::permanentlyDelete(const QString& id) {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral("DELETE FROM recordings WHERE id=? AND deleted_at IS NOT NULL"));
    query.addBindValue(id);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The recording could not be permanently deleted."), query));
    if (query.numRowsAffected() == 0) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidStateTransition,
            QStringLiteral("Only recordings in Trash can be permanently deleted.")));
    }
    return Result<void>::success();
}

Result<void> SqliteRecordingRepository::setActiveTranscriptJob(const QString& recordingId,
                                                               const QString& jobId) {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral(
        "UPDATE recordings SET active_job_id=?,updated_at=? WHERE id=? AND "
        "EXISTS(SELECT 1 FROM transcription_jobs j WHERE j.id=? AND j.recording_id=recordings.id)"));
    query.addBindValue(jobId);
    query.addBindValue(TimeUtils::nowStorageString());
    query.addBindValue(recordingId);
    query.addBindValue(jobId);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The active transcript could not be changed."), query));
    if (query.numRowsAffected() == 0) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The requested transcript revision does not exist.")));
    }
    return Result<void>::success();
}

} // namespace BreezeDesk
