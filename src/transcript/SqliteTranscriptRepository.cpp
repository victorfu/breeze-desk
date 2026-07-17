#include "breezedesk/transcript/SqliteTranscriptRepository.h"

#include "breezedesk/core/TimeUtils.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/DatabaseSearchService.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>

namespace BreezeDesk {
namespace {
UserFacingError queryError(const QString& message, const QSqlQuery& query) {
    return UserFacingError::database(ErrorCode::DatabaseQueryFailed, message, query.lastError().text(), true);
}
TranscriptSegment readSegment(QSqlQuery& query) {
    TranscriptSegment segment;
    segment.id = query.value(QStringLiteral("id")).toString();
    segment.recordingId = query.value(QStringLiteral("recording_id")).toString();
    segment.jobId = query.value(QStringLiteral("job_id")).toString();
    segment.chunkId = query.value(QStringLiteral("chunk_id")).toString();
    segment.ordinal = query.value(QStringLiteral("ordinal")).toInt();
    segment.startMs = query.value(QStringLiteral("start_ms")).toLongLong();
    segment.endMs = query.value(QStringLiteral("end_ms")).toLongLong();
    segment.originalText = query.value(QStringLiteral("original_text")).toString();
    segment.editedText = query.value(QStringLiteral("edited_text")).toString();
    segment.averageProbability = query.value(QStringLiteral("average_probability")).toDouble();
    segment.minimumProbability = query.value(QStringLiteral("minimum_probability")).toDouble();
    segment.noSpeechProbability = query.value(QStringLiteral("no_speech_probability")).toDouble();
    segment.lowConfidence = query.value(QStringLiteral("low_confidence")).toBool();
    segment.reviewed = query.value(QStringLiteral("reviewed")).toBool();
    segment.replacementAudit =
        QJsonDocument::fromJson(query.value(QStringLiteral("replacement_audit_json")).toByteArray()).array();
    segment.provisional = query.value(QStringLiteral("is_provisional")).toBool();
    segment.attempt = query.value(QStringLiteral("attempt")).toInt();
    segment.createdAt = TimeUtils::fromStorageString(query.value(QStringLiteral("created_at")).toString());
    segment.updatedAt = TimeUtils::fromStorageString(query.value(QStringLiteral("updated_at")).toString());
    return segment;
}
Result<void> validateSegments(const QList<TranscriptSegment>& segments, const bool allowOverlap = false) {
    qint64 previousEnd = -1;
    for (int i = 0; i < segments.size(); ++i) {
        const auto& segment = segments.at(i);
        if (segment.startMs < 0 || segment.endMs <= segment.startMs ||
            (!allowOverlap && segment.startMs < previousEnd)) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidArgument,
                QStringLiteral("Transcript segment %1 has an invalid or overlapping time range.")
                    .arg(i + 1)));
        }
        previousEnd = segment.endMs;
    }
    return Result<void>::success();
}
QString nonNull(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value;
}
} // namespace

SqliteTranscriptRepository::SqliteTranscriptRepository(DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager) {}

Result<QList<TranscriptSegment>>
SqliteTranscriptRepository::segmentsForJob(const QString& jobId, const bool includeProvisional) const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<QList<TranscriptSegment>>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(
        includeProvisional
            ? QStringLiteral("SELECT * FROM transcript_segments WHERE job_id=? ORDER BY ordinal")
            : QStringLiteral(
                  "SELECT * FROM transcript_segments WHERE job_id=? AND is_provisional=0 ORDER BY ordinal"));
    query.addBindValue(jobId);
    if (!query.exec())
        return Result<QList<TranscriptSegment>>::failure(
            queryError(QStringLiteral("Transcript segments could not be loaded."), query));
    QList<TranscriptSegment> segments;
    while (query.next())
        segments.append(readSegment(query));
    return Result<QList<TranscriptSegment>>::success(segments);
}

Result<std::optional<TranscriptSegment>> SqliteTranscriptRepository::segment(const QString& segmentId) const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<std::optional<TranscriptSegment>>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral("SELECT * FROM transcript_segments WHERE id=?"));
    query.addBindValue(segmentId);
    if (!query.exec())
        return Result<std::optional<TranscriptSegment>>::failure(
            queryError(QStringLiteral("The transcript segment could not be loaded."), query));
    if (!query.next())
        return Result<std::optional<TranscriptSegment>>::success(std::nullopt);
    return Result<std::optional<TranscriptSegment>>::success(readSegment(query));
}

Result<void> SqliteTranscriptRepository::insertSegments(QSqlDatabase& database, const QString& recordingId,
                                                        const QString& jobId,
                                                        QList<TranscriptSegment> segments,
                                                        const bool allowOverlap) const {
    auto validation = validateSegments(segments, allowOverlap);
    if (!validation)
        return validation;
    const QString now = TimeUtils::nowStorageString();
    for (int index = 0; index < segments.size(); ++index) {
        TranscriptSegment& segment = segments[index];
        if (segment.id.isEmpty())
            segment.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "INSERT INTO transcript_segments(id,recording_id,job_id,chunk_id,ordinal,start_ms,end_ms,"
            "original_text,edited_text,average_probability,minimum_probability,no_speech_probability,"
            "low_confidence,replacement_audit_json,is_provisional,attempt,created_at,updated_at,reviewed) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        query.addBindValue(segment.id);
        query.addBindValue(recordingId);
        query.addBindValue(jobId);
        query.addBindValue(segment.chunkId.isEmpty() ? QVariant() : QVariant(segment.chunkId));
        query.addBindValue(index);
        query.addBindValue(segment.startMs);
        query.addBindValue(segment.endMs);
        query.addBindValue(nonNull(segment.originalText));
        query.addBindValue(nonNull(segment.editedText));
        query.addBindValue(segment.averageProbability);
        query.addBindValue(segment.minimumProbability);
        query.addBindValue(segment.noSpeechProbability);
        query.addBindValue(segment.lowConfidence);
        query.addBindValue(
            QString::fromUtf8(QJsonDocument(segment.replacementAudit).toJson(QJsonDocument::Compact)));
        query.addBindValue(segment.provisional);
        query.addBindValue(qMax(1, segment.attempt));
        query.addBindValue(segment.createdAt.isValid() ? TimeUtils::toStorageString(segment.createdAt) : now);
        query.addBindValue(now);
        query.addBindValue(segment.reviewed);
        if (!query.exec())
            return Result<void>::failure(
                queryError(QStringLiteral("A transcript segment could not be saved."), query));
    }
    return Result<void>::success();
}

Result<void> SqliteTranscriptRepository::replaceRevision(const QString& recordingId, const QString& jobId,
                                                         QList<TranscriptSegment> segments) {
    auto result = m_databaseManager.transaction([&](QSqlDatabase& database) {
        QSqlQuery edited(database);
        edited.prepare(QStringLiteral("SELECT 1 FROM transcript_segments WHERE job_id=? AND edited_text<>'' "
                                      "AND edited_text<>original_text LIMIT 1"));
        edited.addBindValue(jobId);
        if (!edited.exec())
            return Result<void>::failure(
                queryError(QStringLiteral("Existing transcript edits could not be checked."), edited));
        if (edited.next())
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidStateTransition,
                QStringLiteral("A transcript revision with manual edits cannot be overwritten. Create a new "
                               "revision instead.")));
        QSqlQuery remove(database);
        remove.prepare(QStringLiteral("DELETE FROM transcript_segments WHERE job_id=?"));
        remove.addBindValue(jobId);
        if (!remove.exec())
            return Result<void>::failure(
                queryError(QStringLiteral("The old transcript revision could not be replaced."), remove));
        return insertSegments(database, recordingId, jobId, std::move(segments));
    });
    if (!result)
        return result;
    return DatabaseSearchService(m_databaseManager).rebuildRecording(recordingId);
}

Result<void> SqliteTranscriptRepository::saveEditedRevision(const QString& recordingId, const QString& jobId,
                                                            QList<TranscriptSegment> segments) {
    const auto validation = validateSegments(segments);
    if (!validation) {
        return validation;
    }
    auto result = m_databaseManager.transaction([&](QSqlDatabase& database) {
        QSqlQuery remove(database);
        remove.prepare(QStringLiteral("DELETE FROM transcript_segments WHERE job_id=?"));
        remove.addBindValue(jobId);
        if (!remove.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The transcript revision could not be saved."), remove));
        }
        return insertSegments(database, recordingId, jobId, std::move(segments));
    });
    if (!result) {
        return result;
    }
    return DatabaseSearchService(m_databaseManager).rebuildRecording(recordingId);
}

Result<void> SqliteTranscriptRepository::replaceChunk(const QString& recordingId, const QString& jobId,
                                                      const QString& chunkId,
                                                      QList<TranscriptSegment> segments,
                                                      const bool provisional, const int attempt) {
    auto existingResult = segmentsForJob(jobId, true);
    if (!existingResult)
        return Result<void>::failure(existingResult.error());
    QList<TranscriptSegment> combined;
    for (const TranscriptSegment& existing : existingResult.value()) {
        if (existing.chunkId == chunkId && existing.isEdited()) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidStateTransition,
                QStringLiteral("A chunk containing manual edits cannot be overwritten.")));
        }
        if (existing.chunkId != chunkId)
            combined.append(existing);
    }
    for (TranscriptSegment& segment : segments) {
        segment.chunkId = chunkId;
        segment.provisional = provisional;
        segment.attempt = attempt;
        combined.append(segment);
    }
    std::stable_sort(combined.begin(), combined.end(),
                     [](const TranscriptSegment& left, const TranscriptSegment& right) {
                         if (left.startMs != right.startMs)
                             return left.startMs < right.startMs;
                         return left.ordinal < right.ordinal;
                     });
    auto result = m_databaseManager.transaction([&](QSqlDatabase& database) {
        QSqlQuery remove(database);
        remove.prepare(QStringLiteral("DELETE FROM transcript_segments WHERE job_id=?"));
        remove.addBindValue(jobId);
        if (!remove.exec())
            return Result<void>::failure(
                queryError(QStringLiteral("The chunk transcript could not be replaced."), remove));
        return insertSegments(database, recordingId, jobId, combined, provisional);
    });
    if (!result)
        return result;
    if (provisional)
        return Result<void>::success();
    return DatabaseSearchService(m_databaseManager).rebuildRecording(recordingId);
}

Result<void> SqliteTranscriptRepository::saveEditedSegment(const TranscriptSegment& segment) {
    if (segment.startMs < 0 || segment.endMs <= segment.startMs)
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("The segment time range is invalid.")));
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery overlap(connectionResult.value());
    overlap.prepare(QStringLiteral(
        "SELECT 1 FROM transcript_segments WHERE job_id=? AND id<>? AND start_ms<? AND end_ms>? LIMIT 1"));
    overlap.addBindValue(segment.jobId);
    overlap.addBindValue(segment.id);
    overlap.addBindValue(segment.endMs);
    overlap.addBindValue(segment.startMs);
    if (!overlap.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The segment time range could not be checked."), overlap));
    if (overlap.next())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("Transcript segments cannot overlap.")));
    QSqlQuery query(connectionResult.value());
    query.prepare(
        QStringLiteral("UPDATE transcript_segments SET "
                       "start_ms=?,end_ms=?,edited_text=?,replacement_audit_json=?,reviewed=?,updated_at=? "
                       "WHERE id=?"));
    query.addBindValue(segment.startMs);
    query.addBindValue(segment.endMs);
    query.addBindValue(nonNull(segment.editedText));
    query.addBindValue(
        QString::fromUtf8(QJsonDocument(segment.replacementAudit).toJson(QJsonDocument::Compact)));
    query.addBindValue(segment.reviewed);
    query.addBindValue(TimeUtils::nowStorageString());
    query.addBindValue(segment.id);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The transcript edit could not be saved."), query));
    if (query.numRowsAffected() == 0)
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The transcript segment no longer exists.")));
    return DatabaseSearchService(m_databaseManager).rebuildRecording(segment.recordingId);
}

Result<void> SqliteTranscriptRepository::deleteSegment(const QString& segmentId) {
    auto current = segment(segmentId);
    if (!current)
        return Result<void>::failure(current.error());
    if (!current.value())
        return Result<void>::success();
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral("DELETE FROM transcript_segments WHERE id=?"));
    query.addBindValue(segmentId);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The transcript segment could not be deleted."), query));
    return DatabaseSearchService(m_databaseManager).rebuildRecording(current.value()->recordingId);
}

} // namespace BreezeDesk
