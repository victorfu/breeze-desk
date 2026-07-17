#include "breezedesk/jobs/SqliteJobRepository.h"

#include "breezedesk/core/TimeUtils.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/jobs/JobStateMachine.h"

#include <QJsonDocument>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace BreezeDesk {
namespace {
UserFacingError queryError(const QString& message, const QSqlQuery& query) {
    return UserFacingError::database(ErrorCode::DatabaseQueryFailed, message, query.lastError().text(), true);
}
QJsonObject objectFromText(const QString& value) {
    return QJsonDocument::fromJson(value.toUtf8()).object();
}
QString textFromObject(const QJsonObject& value) {
    return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
}
TranscriptionJob readJob(QSqlQuery& query) {
    TranscriptionJob job;
    job.id = query.value(QStringLiteral("id")).toString();
    job.recordingId = query.value(QStringLiteral("recording_id")).toString();
    job.state = jobStateFromName(query.value(QStringLiteral("state")).toString());
    job.stage = jobStageFromName(query.value(QStringLiteral("stage")).toString());
    job.progress = query.value(QStringLiteral("progress")).toDouble();
    job.modelId = query.value(QStringLiteral("model_id")).toString();
    job.modelChecksum = query.value(QStringLiteral("model_checksum")).toString();
    job.engineVersion = query.value(QStringLiteral("engine_version")).toString();
    job.workerVersion = query.value(QStringLiteral("worker_version")).toString();
    job.backend = query.value(QStringLiteral("backend")).toString();
    job.language = query.value(QStringLiteral("language")).toString();
    job.preset = query.value(QStringLiteral("preset")).toString();
    job.glossaryProfileId = query.value(QStringLiteral("glossary_profile_id")).toString();
    job.meetingContext = query.value(QStringLiteral("meeting_context")).toString();
    job.vadEnabled = query.value(QStringLiteral("vad_enabled")).toBool();
    job.errorCode = query.value(QStringLiteral("error_code")).toString();
    job.errorMessage = query.value(QStringLiteral("error_message")).toString();
    job.diagnostics = objectFromText(query.value(QStringLiteral("diagnostics_json")).toString());
    job.parameters = objectFromText(query.value(QStringLiteral("parameters_json")).toString());
    job.queuePosition = query.value(QStringLiteral("queue_position")).toInt();
    job.revisionNumber = query.value(QStringLiteral("revision_number")).toInt();
    job.retryCount = query.value(QStringLiteral("retry_count")).toInt();
    job.createdAt = TimeUtils::fromStorageString(query.value(QStringLiteral("created_at")).toString());
    job.startedAt = TimeUtils::fromStorageString(query.value(QStringLiteral("started_at")).toString());
    job.completedAt = TimeUtils::fromStorageString(query.value(QStringLiteral("completed_at")).toString());
    job.interruptedAt =
        TimeUtils::fromStorageString(query.value(QStringLiteral("interrupted_at")).toString());
    job.lastCompletedChunk = query.value(QStringLiteral("last_completed_chunk")).toInt();
    return job;
}
JobChunk readChunk(QSqlQuery& query) {
    JobChunk chunk;
    chunk.id = query.value(QStringLiteral("id")).toString();
    chunk.jobId = query.value(QStringLiteral("job_id")).toString();
    chunk.ordinal = query.value(QStringLiteral("ordinal")).toInt();
    chunk.startMs = query.value(QStringLiteral("start_ms")).toLongLong();
    chunk.endMs = query.value(QStringLiteral("end_ms")).toLongLong();
    chunk.overlapBeforeMs = query.value(QStringLiteral("overlap_before_ms")).toLongLong();
    chunk.overlapAfterMs = query.value(QStringLiteral("overlap_after_ms")).toLongLong();
    chunk.state = chunkStateFromName(query.value(QStringLiteral("state")).toString());
    chunk.attempts = query.value(QStringLiteral("attempts")).toInt();
    chunk.startedAt = TimeUtils::fromStorageString(query.value(QStringLiteral("started_at")).toString());
    chunk.completedAt = TimeUtils::fromStorageString(query.value(QStringLiteral("completed_at")).toString());
    chunk.error = query.value(QStringLiteral("error")).toString();
    chunk.resultHash = query.value(QStringLiteral("result_hash")).toString();
    chunk.diagnostics = objectFromText(query.value(QStringLiteral("diagnostics_json")).toString());
    return chunk;
}
QString nonNull(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value;
}
} // namespace

SqliteJobRepository::SqliteJobRepository(DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager) {}

Result<void> SqliteJobRepository::create(TranscriptionJob job) {
    if (job.recordingId.isEmpty())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A recording is required for a transcription job.")));
    if (job.id.isEmpty())
        job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!job.createdAt.isValid())
        job.createdAt = QDateTime::currentDateTimeUtc();
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlDatabase database = connectionResult.value();
    if (job.revisionNumber <= 0) {
        QSqlQuery revision(database);
        revision.prepare(QStringLiteral(
            "SELECT COALESCE(MAX(revision_number),0)+1 FROM transcription_jobs WHERE recording_id=?"));
        revision.addBindValue(job.recordingId);
        if (!revision.exec() || !revision.next())
            return Result<void>::failure(
                queryError(QStringLiteral("The transcript revision could not be allocated."), revision));
        job.revisionNumber = revision.value(0).toInt();
    }
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO transcription_jobs(id,recording_id,state,stage,progress,model_id,model_checksum,"
        "engine_version,worker_version,backend,language,preset,glossary_profile_id,meeting_context,"
        "vad_enabled,error_code,error_message,diagnostics_json,parameters_json,queue_position,"
        "revision_number,retry_count,created_at,started_at,completed_at,interrupted_at,last_completed_chunk) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(job.id);
    query.addBindValue(job.recordingId);
    query.addBindValue(jobStateName(job.state));
    query.addBindValue(jobStageName(job.stage));
    query.addBindValue(qBound(0.0, job.progress, 1.0));
    query.addBindValue(nonNull(job.modelId));
    query.addBindValue(nonNull(job.modelChecksum));
    query.addBindValue(nonNull(job.engineVersion));
    query.addBindValue(nonNull(job.workerVersion));
    query.addBindValue(nonNull(job.backend));
    query.addBindValue(nonNull(job.language));
    query.addBindValue(nonNull(job.preset));
    query.addBindValue(job.glossaryProfileId.isEmpty() ? QVariant() : QVariant(job.glossaryProfileId));
    query.addBindValue(nonNull(job.meetingContext));
    query.addBindValue(job.vadEnabled);
    query.addBindValue(nonNull(job.errorCode));
    query.addBindValue(nonNull(job.errorMessage));
    query.addBindValue(textFromObject(job.diagnostics));
    query.addBindValue(textFromObject(job.parameters));
    query.addBindValue(job.queuePosition);
    query.addBindValue(job.revisionNumber);
    query.addBindValue(job.retryCount);
    query.addBindValue(TimeUtils::toStorageString(job.createdAt));
    query.addBindValue(job.startedAt.isValid() ? QVariant(TimeUtils::toStorageString(job.startedAt))
                                               : QVariant());
    query.addBindValue(job.completedAt.isValid() ? QVariant(TimeUtils::toStorageString(job.completedAt))
                                                 : QVariant());
    query.addBindValue(job.interruptedAt.isValid() ? QVariant(TimeUtils::toStorageString(job.interruptedAt))
                                                   : QVariant());
    query.addBindValue(job.lastCompletedChunk);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The transcription job could not be created."), query));
    return Result<void>::success();
}

Result<std::optional<TranscriptionJob>> SqliteJobRepository::findById(const QString& id) const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<std::optional<TranscriptionJob>>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral("SELECT * FROM transcription_jobs WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec())
        return Result<std::optional<TranscriptionJob>>::failure(
            queryError(QStringLiteral("The transcription job could not be loaded."), query));
    if (!query.next())
        return Result<std::optional<TranscriptionJob>>::success(std::nullopt);
    return Result<std::optional<TranscriptionJob>>::success(readJob(query));
}

Result<QList<TranscriptionJob>> SqliteJobRepository::list(const bool includeCompleted) const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<QList<TranscriptionJob>>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    const QString sql =
        includeCompleted
            ? QStringLiteral("SELECT * FROM transcription_jobs WHERE queue_hidden=0 ORDER BY CASE WHEN "
                             "state='Queued' THEN 0 ELSE 1 END, queue_position, created_at DESC")
            : QStringLiteral("SELECT * FROM transcription_jobs WHERE queue_hidden=0 AND state NOT IN "
                             "('Completed','Cancelled') ORDER BY queue_position, created_at");
    if (!query.exec(sql))
        return Result<QList<TranscriptionJob>>::failure(
            queryError(QStringLiteral("The job queue could not be loaded."), query));
    QList<TranscriptionJob> jobs;
    while (query.next())
        jobs.append(readJob(query));
    return Result<QList<TranscriptionJob>>::success(jobs);
}

Result<void> SqliteJobRepository::transition(const QString& id, const JobState state,
                                             const QString& errorCode, const QString& errorMessage) {
    auto currentResult = findById(id);
    if (!currentResult)
        return Result<void>::failure(currentResult.error());
    if (!currentResult.value())
        return Result<void>::failure(UserFacingError::database(
            ErrorCode::NotFound, QStringLiteral("The transcription job no longer exists."), id));
    const TranscriptionJob current = *currentResult.value();
    auto validation = JobStateMachine::validateTransition(current.state, state);
    if (!validation)
        return validation;
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QStringList updates = {QStringLiteral("state=?"), QStringLiteral("error_code=?"),
                           QStringLiteral("error_message=?")};
    QVariantList values = {jobStateName(state), nonNull(errorCode), nonNull(errorMessage)};
    const QString now = TimeUtils::nowStorageString();
    if (state == JobState::Preparing && !current.startedAt.isValid()) {
        updates.append(QStringLiteral("started_at=?"));
        values.append(now);
    }
    if (state == JobState::Completed) {
        updates.append(QStringLiteral("completed_at=?"));
        values.append(now);
        updates.append(QStringLiteral("progress=1"));
        updates.append(QStringLiteral("stage='Completed'"));
    }
    if (state == JobState::Interrupted) {
        updates.append(QStringLiteral("interrupted_at=?"));
        values.append(now);
    }
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral("UPDATE transcription_jobs SET ") + updates.join(QLatin1Char(',')) +
                  QStringLiteral(" WHERE id=?"));
    for (const QVariant& value : values)
        query.addBindValue(value);
    query.addBindValue(id);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The job state could not be changed."), query));
    return Result<void>::success();
}

Result<void> SqliteJobRepository::updateProgress(const QString& id, const JobStage stage,
                                                 const double progress, const int lastCompletedChunk) {
    auto current = findById(id);
    if (!current)
        return Result<void>::failure(current.error());
    if (!current.value())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The transcription job no longer exists.")));
    if (static_cast<int>(stage) < static_cast<int>(current.value()->stage)) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidStateTransition, QStringLiteral("Job progress stages cannot move backwards.")));
    }
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral("UPDATE transcription_jobs SET stage=?,progress=MAX(progress,?),"
                                 "last_completed_chunk=MAX(last_completed_chunk,?) WHERE id=?"));
    query.addBindValue(jobStageName(stage));
    query.addBindValue(qBound(0.0, progress, 1.0));
    query.addBindValue(lastCompletedChunk);
    query.addBindValue(id);
    if (!query.exec())
        return Result<void>::failure(queryError(QStringLiteral("Job progress could not be saved."), query));
    return Result<void>::success();
}

Result<void> SqliteJobRepository::updateRuntimeInfo(const QString& id, const QString& actualBackend,
                                                    const QString& engineVersion,
                                                    const QString& workerVersion,
                                                    const QJsonObject& diagnostics) {
    const auto current = findById(id);
    if (!current)
        return Result<void>::failure(current.error());
    if (!current.value())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The transcription job no longer exists.")));
    QJsonObject mergedDiagnostics = current.value()->diagnostics;
    for (auto iterator = diagnostics.constBegin(); iterator != diagnostics.constEnd(); ++iterator)
        mergedDiagnostics.insert(iterator.key(), iterator.value());
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral("UPDATE transcription_jobs SET backend=?,engine_version=?,"
                                 "worker_version=?,diagnostics_json=? WHERE id=?"));
    query.addBindValue(nonNull(actualBackend));
    query.addBindValue(nonNull(engineVersion));
    query.addBindValue(nonNull(workerVersion));
    query.addBindValue(textFromObject(mergedDiagnostics));
    query.addBindValue(id);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The ASR runtime diagnostics could not be saved."), query));
    return Result<void>::success();
}

Result<void> SqliteJobRepository::replaceChunks(const QString& jobId, const QList<JobChunk>& chunks) {
    for (int i = 0; i < chunks.size(); ++i) {
        if (chunks.at(i).ordinal != i || chunks.at(i).startMs < 0 ||
            chunks.at(i).endMs <= chunks.at(i).startMs) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidArgument,
                QStringLiteral("Job chunks must be ordered and have valid time ranges.")));
        }
    }
    return m_databaseManager.transaction([&](QSqlDatabase& database) {
        QSqlQuery remove(database);
        remove.prepare(QStringLiteral("DELETE FROM job_chunks WHERE job_id=?"));
        remove.addBindValue(jobId);
        if (!remove.exec())
            return Result<void>::failure(
                queryError(QStringLiteral("Existing job chunks could not be replaced."), remove));
        for (JobChunk chunk : chunks) {
            if (chunk.id.isEmpty())
                chunk.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            QSqlQuery insert(database);
            insert.prepare(QStringLiteral(
                "INSERT INTO job_chunks(id,job_id,ordinal,start_ms,end_ms,overlap_before_ms,overlap_after_ms,"
                "state,attempts,started_at,completed_at,error,result_hash,diagnostics_json) "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
            insert.addBindValue(chunk.id);
            insert.addBindValue(jobId);
            insert.addBindValue(chunk.ordinal);
            insert.addBindValue(chunk.startMs);
            insert.addBindValue(chunk.endMs);
            insert.addBindValue(chunk.overlapBeforeMs);
            insert.addBindValue(chunk.overlapAfterMs);
            insert.addBindValue(chunkStateName(chunk.state));
            insert.addBindValue(chunk.attempts);
            insert.addBindValue(chunk.startedAt.isValid()
                                    ? QVariant(TimeUtils::toStorageString(chunk.startedAt))
                                    : QVariant());
            insert.addBindValue(chunk.completedAt.isValid()
                                    ? QVariant(TimeUtils::toStorageString(chunk.completedAt))
                                    : QVariant());
            insert.addBindValue(nonNull(chunk.error));
            insert.addBindValue(nonNull(chunk.resultHash));
            insert.addBindValue(textFromObject(chunk.diagnostics));
            if (!insert.exec())
                return Result<void>::failure(
                    queryError(QStringLiteral("A job chunk could not be saved."), insert));
        }
        return Result<void>::success();
    });
}

Result<QList<JobChunk>> SqliteJobRepository::chunks(const QString& jobId) const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<QList<JobChunk>>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral("SELECT * FROM job_chunks WHERE job_id=? ORDER BY ordinal"));
    query.addBindValue(jobId);
    if (!query.exec())
        return Result<QList<JobChunk>>::failure(
            queryError(QStringLiteral("Job chunks could not be loaded."), query));
    QList<JobChunk> result;
    while (query.next())
        result.append(readChunk(query));
    return Result<QList<JobChunk>>::success(result);
}

Result<void> SqliteJobRepository::updateChunk(const JobChunk& chunk) {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral(
        "UPDATE job_chunks SET state=?,attempts=?,started_at=?,completed_at=?,error=?,result_hash=?,"
        "diagnostics_json=? WHERE id=? AND job_id=?"));
    query.addBindValue(chunkStateName(chunk.state));
    query.addBindValue(chunk.attempts);
    query.addBindValue(chunk.startedAt.isValid() ? QVariant(TimeUtils::toStorageString(chunk.startedAt))
                                                 : QVariant());
    query.addBindValue(chunk.completedAt.isValid() ? QVariant(TimeUtils::toStorageString(chunk.completedAt))
                                                   : QVariant());
    query.addBindValue(nonNull(chunk.error));
    query.addBindValue(nonNull(chunk.resultHash));
    query.addBindValue(textFromObject(chunk.diagnostics));
    query.addBindValue(chunk.id);
    query.addBindValue(chunk.jobId);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The job chunk could not be updated."), query));
    return Result<void>::success();
}

Result<void> SqliteJobRepository::reorder(const QStringList& orderedJobIds) {
    QSet<QString> uniqueIds(orderedJobIds.cbegin(), orderedJobIds.cend());
    if (uniqueIds.size() != orderedJobIds.size()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("The reordered queue contains duplicate job IDs.")));
    }
    return m_databaseManager.transaction([&](QSqlDatabase& database) {
        for (int i = 0; i < orderedJobIds.size(); ++i) {
            QSqlQuery query(database);
            query.prepare(QStringLiteral(
                "UPDATE transcription_jobs SET queue_position=? WHERE id=? AND state='Queued'"));
            query.addBindValue(i);
            query.addBindValue(orderedJobIds.at(i));
            if (!query.exec())
                return Result<void>::failure(
                    queryError(QStringLiteral("The job queue could not be reordered."), query));
            if (query.numRowsAffected() == 0)
                return Result<void>::failure(UserFacingError::validation(
                    ErrorCode::InvalidStateTransition, QStringLiteral("Only queued jobs can be reordered."),
                    orderedJobIds.at(i)));
        }
        return Result<void>::success();
    });
}

Result<int> SqliteJobRepository::markRunningJobsInterrupted(const QString& reason) {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<int>::failure(connectionResult.error());
    QSqlDatabase database = connectionResult.value();
    if (!database.transaction())
        return Result<int>::failure(UserFacingError::database(ErrorCode::DatabaseQueryFailed,
                                                              QStringLiteral("Job recovery could not start."),
                                                              database.lastError().text()));
    const QString now = TimeUtils::nowStorageString();
    QSqlQuery jobs(database);
    jobs.prepare(QStringLiteral(
        "UPDATE transcription_jobs SET state='Interrupted',interrupted_at=?,error_code='WorkerCrashed',"
        "error_message=? WHERE state IN ('Preparing','Normalizing','WaitingForModel','LoadingModel',"
        "'AnalyzingSpeech','Transcribing','Finalizing','Cancelling')"));
    jobs.addBindValue(now);
    jobs.addBindValue(reason);
    if (!jobs.exec()) {
        database.rollback();
        return Result<int>::failure(queryError(QStringLiteral("Running jobs could not be recovered."), jobs));
    }
    const int affected = jobs.numRowsAffected();
    QSqlQuery chunks(database);
    chunks.prepare(QStringLiteral("UPDATE job_chunks SET state='Interrupted',error=? WHERE state='Running'"));
    chunks.addBindValue(reason);
    if (!chunks.exec() || !database.commit()) {
        database.rollback();
        return Result<int>::failure(
            queryError(QStringLiteral("Running chunks could not be recovered."), chunks));
    }
    return Result<int>::success(affected);
}

Result<int> SqliteJobRepository::clearCompleted() {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<int>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    if (!query.exec(QStringLiteral("UPDATE transcription_jobs SET queue_hidden=1 WHERE queue_hidden=0 "
                                   "AND state IN ('Completed','Cancelled')"))) {
        return Result<int>::failure(
            queryError(QStringLiteral("Completed jobs could not be cleared."), query));
    }
    return Result<int>::success(query.numRowsAffected());
}

} // namespace BreezeDesk
