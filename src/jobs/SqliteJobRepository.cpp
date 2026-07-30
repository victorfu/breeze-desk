#include "breezedesk/jobs/SqliteJobRepository.h"

#include "breezedesk/core/TimeUtils.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/DatabaseSearchService.h"
#include "breezedesk/jobs/JobStateMachine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>
#include <utility>

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
    job.retryCount = query.value(QStringLiteral("retry_count")).toInt();
    job.createdAt = TimeUtils::fromStorageString(query.value(QStringLiteral("created_at")).toString());
    job.startedAt = TimeUtils::fromStorageString(query.value(QStringLiteral("started_at")).toString());
    job.completedAt = TimeUtils::fromStorageString(query.value(QStringLiteral("completed_at")).toString());
    job.interruptedAt =
        TimeUtils::fromStorageString(query.value(QStringLiteral("interrupted_at")).toString());
    job.lastCompletedChunk = query.value(QStringLiteral("last_completed_chunk")).toInt();
    job.queueHidden = query.value(QStringLiteral("queue_hidden")).toBool();
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
JobEvent readEvent(QSqlQuery& query) {
    JobEvent event;
    event.id = query.value(QStringLiteral("id")).toLongLong();
    event.jobId = query.value(QStringLiteral("job_id")).toString();
    event.eventType = query.value(QStringLiteral("event_type")).toString();
    event.severity = query.value(QStringLiteral("severity")).toString();
    if (!query.value(QStringLiteral("state")).isNull()) {
        event.state = jobStateFromName(query.value(QStringLiteral("state")).toString());
    }
    if (!query.value(QStringLiteral("stage")).isNull()) {
        event.stage = jobStageFromName(query.value(QStringLiteral("stage")).toString());
    }
    if (!query.value(QStringLiteral("progress")).isNull()) {
        event.progress = query.value(QStringLiteral("progress")).toDouble();
    }
    event.code = query.value(QStringLiteral("code")).toString();
    event.message = query.value(QStringLiteral("message")).toString();
    event.payload = objectFromText(query.value(QStringLiteral("payload_json")).toString());
    event.createdAt = TimeUtils::fromStorageString(query.value(QStringLiteral("created_at")).toString());
    return event;
}
AsrExecutionLease readLease(QSqlQuery& query) {
    AsrExecutionLease lease;
    lease.ownerToken = query.value(QStringLiteral("owner_token")).toString();
    lease.jobId = query.value(QStringLiteral("job_id")).toString();
    lease.acquiredAt = TimeUtils::fromStorageString(query.value(QStringLiteral("acquired_at")).toString());
    lease.heartbeatAt = TimeUtils::fromStorageString(query.value(QStringLiteral("heartbeat_at")).toString());
    lease.expiresAt = TimeUtils::fromStorageString(query.value(QStringLiteral("expires_at")).toString());
    return lease;
}
UserFacingError corruptLeaseError(const QString& technicalDetails) {
    return UserFacingError::database(ErrorCode::DatabaseCorrupt,
                                     QStringLiteral("The ASR execution lease is damaged."),
                                     technicalDetails);
}
Result<void> validateLeaseIdentity(QSqlDatabase& database, const AsrExecutionLease& lease) {
    if (lease.ownerToken.trimmed().isEmpty() || lease.jobId.trimmed().isEmpty()) {
        return Result<void>::failure(corruptLeaseError(
            QStringLiteral("The execution lease has an empty owner or job identifier.")));
    }

    QSqlQuery job(database);
    job.prepare(QStringLiteral("SELECT 1 FROM transcription_jobs WHERE id=?"));
    job.addBindValue(lease.jobId);
    if (!job.exec()) {
        return Result<void>::failure(
            queryError(QStringLiteral("The execution lease job could not be checked."), job));
    }
    if (!job.next()) {
        return Result<void>::failure(corruptLeaseError(
            QStringLiteral("The execution lease references a missing transcription job.")));
    }
    return Result<void>::success();
}
Result<void> validateLeaseTimestamps(const AsrExecutionLease& lease) {
    if (!lease.acquiredAt.isValid() || !lease.heartbeatAt.isValid() ||
        !lease.expiresAt.isValid()) {
        return Result<void>::failure(corruptLeaseError(
            QStringLiteral("The execution lease contains an invalid timestamp.")));
    }
    return Result<void>::success();
}
QString nonNull(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value;
}
Result<void> executionWrite(DatabaseManager& databaseManager, const QString& jobId,
                            const QString& ownerToken,
                            const std::function<Result<void>(QSqlDatabase&)>& operation) {
    return ownerToken.isEmpty() ? databaseManager.immediateTransaction(operation)
                                : databaseManager.executionLeaseTransaction(jobId, ownerToken, operation);
}
Result<void> insertEvent(QSqlDatabase& database, JobEvent* event) {
    if (event == nullptr || event->jobId.isEmpty() || event->eventType.trimmed().isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A job event requires a job and event type.")));
    }
    if (!event->createdAt.isValid()) {
        event->createdAt = QDateTime::currentDateTimeUtc();
    }
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO transcription_job_events(job_id,event_type,severity,state,stage,progress,code,message,"
        "payload_json,created_at) VALUES(?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(event->jobId);
    query.addBindValue(event->eventType.trimmed());
    query.addBindValue(event->severity.trimmed().isEmpty() ? QStringLiteral("info")
                                                           : event->severity.trimmed().toLower());
    query.addBindValue(event->state.has_value() ? QVariant(jobStateName(*event->state)) : QVariant());
    query.addBindValue(event->stage.has_value() ? QVariant(jobStageName(*event->stage)) : QVariant());
    query.addBindValue(event->progress.has_value() ? QVariant(qBound(0.0, *event->progress, 1.0))
                                                   : QVariant());
    query.addBindValue(nonNull(event->code));
    query.addBindValue(nonNull(event->message));
    query.addBindValue(textFromObject(event->payload));
    query.addBindValue(TimeUtils::toStorageString(event->createdAt));
    if (!query.exec()) {
        return Result<void>::failure(queryError(QStringLiteral("The job event could not be saved."), query));
    }
    event->id = query.lastInsertId().toLongLong();
    return Result<void>::success();
}
Result<std::optional<TranscriptionJob>> findJob(QSqlDatabase& database, const QString& id) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT * FROM transcription_jobs WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec()) {
        return Result<std::optional<TranscriptionJob>>::failure(
            queryError(QStringLiteral("The transcription job could not be loaded."), query));
    }
    if (!query.next()) {
        return Result<std::optional<TranscriptionJob>>::success(std::nullopt);
    }
    return Result<std::optional<TranscriptionJob>>::success(readJob(query));
}
Result<std::optional<AsrExecutionLease>> findRawLease(QSqlDatabase& database) {
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT * FROM asr_execution_lease WHERE resource='asr'"))) {
        return Result<std::optional<AsrExecutionLease>>::failure(
            queryError(QStringLiteral("The ASR execution lease could not be loaded."), query));
    }
    if (!query.next()) {
        return Result<std::optional<AsrExecutionLease>>::success(std::nullopt);
    }
    return Result<std::optional<AsrExecutionLease>>::success(readLease(query));
}
Result<std::optional<AsrExecutionLease>> findLease(QSqlDatabase& database) {
    auto lease = findRawLease(database);
    if (!lease || !lease.value().has_value()) {
        return lease;
    }
    const auto identity = validateLeaseIdentity(database, *lease.value());
    if (!identity) {
        return Result<std::optional<AsrExecutionLease>>::failure(identity.error());
    }
    const auto timestamps = validateLeaseTimestamps(*lease.value());
    if (!timestamps) {
        return Result<std::optional<AsrExecutionLease>>::failure(timestamps.error());
    }
    return lease;
}
Result<std::optional<TranscriptSegment>> latestSegment(QSqlDatabase& database, const QString& jobId,
                                                       const bool includeProvisional) {
    QSqlQuery query(database);
    query.prepare(
        includeProvisional
            ? QStringLiteral("SELECT * FROM transcript_segments WHERE job_id=? ORDER BY end_ms DESC,"
                             "ordinal DESC LIMIT 1")
            : QStringLiteral("SELECT * FROM transcript_segments WHERE job_id=? AND is_provisional=0 "
                             "ORDER BY end_ms DESC,ordinal DESC LIMIT 1"));
    query.addBindValue(jobId);
    if (!query.exec()) {
        return Result<std::optional<TranscriptSegment>>::failure(
            queryError(QStringLiteral("The latest transcript segment could not be loaded."), query));
    }
    if (!query.next()) {
        return Result<std::optional<TranscriptSegment>>::success(std::nullopt);
    }
    return Result<std::optional<TranscriptSegment>>::success(readSegment(query));
}
Result<JobClaimResult> claimInDatabase(QSqlDatabase& database, const std::optional<QString>& requestedJobId,
                                       const QString& ownerToken, const qint64 leaseDurationMs) {
    JobClaimResult claim;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    auto leaseResult = findLease(database);
    if (!leaseResult) {
        return Result<JobClaimResult>::failure(leaseResult.error());
    }
    if (leaseResult.value().has_value() && leaseResult.value()->expiresAt > now) {
        claim.activeJobId = leaseResult.value()->jobId;
        claim.lease = leaseResult.value();
        if (leaseResult.value()->ownerToken == ownerToken &&
            (!requestedJobId.has_value() || *requestedJobId == leaseResult.value()->jobId)) {
            const auto current = findJob(database, leaseResult.value()->jobId);
            if (!current) {
                return Result<JobClaimResult>::failure(current.error());
            }
            if (current.value().has_value()) {
                claim.claimed = true;
                claim.job = current.value();
            }
        }
        return Result<JobClaimResult>::success(claim);
    }
    if (leaseResult.value().has_value()) {
        const QString expiredJobId = leaseResult.value()->jobId;
        const QString nowText = TimeUtils::toStorageString(now);
        const auto expiredJob = findJob(database, expiredJobId);
        if (!expiredJob) {
            return Result<JobClaimResult>::failure(expiredJob.error());
        }
        const bool cancellationPending =
            expiredJob.value().has_value() && expiredJob.value()->state == JobState::Cancelling;
        const QString recoveryMessage =
            cancellationPending
                ? QStringLiteral("Cancellation completed after the ASR execution lease expired.")
                : QStringLiteral("The ASR execution lease expired.");
        QSqlQuery recover(database);
        if (cancellationPending) {
            recover.prepare(
                QStringLiteral("UPDATE transcription_jobs SET state='Cancelled',error_code='JobCancelled',"
                               "error_message=? WHERE id=? AND state='Cancelling'"));
            recover.addBindValue(recoveryMessage);
        } else {
            recover.prepare(QStringLiteral(
                "UPDATE transcription_jobs SET state='Interrupted',interrupted_at=?,"
                "error_code='WorkerCrashed',error_message=? WHERE id=? AND state IN ('Preparing',"
                "'Normalizing','WaitingForModel','LoadingModel','AnalyzingSpeech','Transcribing',"
                "'Finalizing')"));
            recover.addBindValue(nowText);
            recover.addBindValue(recoveryMessage);
        }
        recover.addBindValue(expiredJobId);
        if (!recover.exec()) {
            return Result<JobClaimResult>::failure(queryError(
                QStringLiteral("The expired transcription lease could not be recovered."), recover));
        }
        if (recover.numRowsAffected() > 0) {
            QSqlQuery chunks(database);
            if (cancellationPending) {
                chunks.prepare(QStringLiteral(
                    "UPDATE job_chunks SET state='Cancelled',error=? WHERE job_id=? AND state IN "
                    "('Running','Interrupted','Failed')"));
            } else {
            chunks.prepare(QStringLiteral(
                    "UPDATE job_chunks SET state='Interrupted',error=? WHERE job_id=? AND state='Running'"));
            }
            chunks.addBindValue(recoveryMessage);
            chunks.addBindValue(expiredJobId);
            if (!chunks.exec()) {
                return Result<JobClaimResult>::failure(queryError(
                    QStringLiteral("Expired transcription chunks could not be recovered."), chunks));
            }
            JobEvent event;
            event.jobId = expiredJobId;
            event.eventType =
                cancellationPending ? QStringLiteral("cancelled") : QStringLiteral("lease_expired");
            event.severity = QStringLiteral("warning");
            event.state = cancellationPending ? JobState::Cancelled : JobState::Interrupted;
            event.code =
                cancellationPending ? QStringLiteral("JobCancelled") : QStringLiteral("WorkerCrashed");
            event.message = recoveryMessage;
            const auto eventResult = insertEvent(database, &event);
            if (!eventResult) {
                return Result<JobClaimResult>::failure(eventResult.error());
            }
        }
        QSqlQuery clear(database);
        if (!clear.exec(QStringLiteral("DELETE FROM asr_execution_lease WHERE resource='asr'"))) {
            return Result<JobClaimResult>::failure(
                queryError(QStringLiteral("The expired ASR execution lease could not be cleared."), clear));
        }
    }

    QSqlQuery active(database);
    active.prepare(QStringLiteral(
        "SELECT id FROM transcription_jobs WHERE state IN ('Preparing','Normalizing','WaitingForModel',"
        "'LoadingModel','AnalyzingSpeech','Transcribing','Finalizing','Cancelling') ORDER BY started_at,id "
        "LIMIT "
        "1"));
    if (!active.exec()) {
        return Result<JobClaimResult>::failure(
            queryError(QStringLiteral("The active transcription job could not be checked."), active));
    }
    if (active.next()) {
        claim.activeJobId = active.value(0).toString();
        return Result<JobClaimResult>::success(claim);
    }

    QSqlQuery queued(database);
    if (requestedJobId.has_value()) {
        queued.prepare(QStringLiteral("SELECT * FROM transcription_jobs WHERE id=?"));
        queued.addBindValue(*requestedJobId);
    } else {
        queued.prepare(QStringLiteral("SELECT * FROM transcription_jobs WHERE state='Queued' AND "
                                      "queue_hidden=0 ORDER BY queue_position,"
                                      "created_at,id LIMIT 1"));
    }
    if (!queued.exec()) {
        return Result<JobClaimResult>::failure(
            queryError(QStringLiteral("The queued transcription job could not be loaded."), queued));
    }
    if (!queued.next()) {
        if (requestedJobId.has_value()) {
            return Result<JobClaimResult>::failure(UserFacingError::validation(
                ErrorCode::NotFound, QStringLiteral("The queued transcription job does not exist.")));
        }
        return Result<JobClaimResult>::success(claim);
    }
    const TranscriptionJob selected = readJob(queued);
    if (selected.state != JobState::Queued) {
        return Result<JobClaimResult>::failure(
            UserFacingError::validation(ErrorCode::InvalidStateTransition,
                                        QStringLiteral("Only queued transcription jobs can be claimed.")));
    }
    const QDateTime expiresAt = now.addMSecs(leaseDurationMs);
    QSqlQuery update(database);
    update.prepare(QStringLiteral(
        "UPDATE transcription_jobs SET state='Preparing',started_at=COALESCE(started_at,?) WHERE id=? AND "
        "state='Queued'"));
    update.addBindValue(TimeUtils::toStorageString(now));
    update.addBindValue(selected.id);
    if (!update.exec()) {
        return Result<JobClaimResult>::failure(
            queryError(QStringLiteral("The queued transcription job could not be claimed."), update));
    }
    if (update.numRowsAffected() == 0) {
        return Result<JobClaimResult>::success(claim);
    }
    QSqlQuery lease(database);
    lease.prepare(QStringLiteral(
        "INSERT INTO asr_execution_lease(resource,owner_token,job_id,acquired_at,heartbeat_at,expires_at) "
        "VALUES('asr',?,?,?,?,?)"));
    lease.addBindValue(ownerToken);
    lease.addBindValue(selected.id);
    lease.addBindValue(TimeUtils::toStorageString(now));
    lease.addBindValue(TimeUtils::toStorageString(now));
    lease.addBindValue(TimeUtils::toStorageString(expiresAt));
    if (!lease.exec()) {
        return Result<JobClaimResult>::failure(
            queryError(QStringLiteral("The ASR execution lease could not be acquired."), lease));
    }
    JobEvent event;
    event.jobId = selected.id;
    event.eventType = QStringLiteral("claimed");
    event.state = JobState::Preparing;
    event.stage = selected.stage;
    event.progress = selected.progress;
    const auto eventResult = insertEvent(database, &event);
    if (!eventResult) {
        return Result<JobClaimResult>::failure(eventResult.error());
    }
    auto claimedJob = findJob(database, selected.id);
    if (!claimedJob) {
        return Result<JobClaimResult>::failure(claimedJob.error());
    }
    if (!claimedJob.value().has_value()) {
        return Result<JobClaimResult>::failure(UserFacingError::database(
            ErrorCode::NotFound, QStringLiteral("The claimed transcription job no longer exists.")));
    }
    AsrExecutionLease acquired;
    acquired.ownerToken = ownerToken;
    acquired.jobId = selected.id;
    acquired.acquiredAt = now;
    acquired.heartbeatAt = now;
    acquired.expiresAt = expiresAt;
    claim.claimed = true;
    claim.job = claimedJob.value();
    claim.lease = acquired;
    claim.activeJobId = selected.id;
    return Result<JobClaimResult>::success(claim);
}
} // namespace

SqliteJobRepository::SqliteJobRepository(DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager) {}

Result<void> SqliteJobRepository::create(TranscriptionJob job) {
    const auto created = createQueued(std::move(job));
    return created ? Result<void>::success() : Result<void>::failure(created.error());
}

Result<TranscriptionJob> SqliteJobRepository::createQueued(TranscriptionJob job) {
    if (job.recordingId.isEmpty()) {
        return Result<TranscriptionJob>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A recording is required for a transcription job.")));
    }
    if (job.id.isEmpty()) {
        job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (!job.createdAt.isValid()) {
        job.createdAt = QDateTime::currentDateTimeUtc();
    }
    job.state = JobState::Queued;
    job.stage = JobStage::Preparing;
    job.progress = 0.0;
    job.startedAt = {};
    job.completedAt = {};
    job.interruptedAt = {};
    job.errorCode.clear();
    job.errorMessage.clear();
    const auto saved = m_databaseManager.immediateTransaction([&](QSqlDatabase& database) {
        QSqlQuery position(database);
        if (!position.exec(QStringLiteral(
                "SELECT COALESCE(MAX(queue_position),-1)+1 FROM transcription_jobs WHERE state='Queued'")) ||
            !position.next()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The queue position could not be allocated."), position));
        }
        job.queuePosition = position.value(0).toInt();

        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "INSERT INTO transcription_jobs(id,recording_id,state,stage,progress,model_id,model_checksum,"
            "engine_version,worker_version,backend,language,preset,glossary_profile_id,meeting_context,"
            "vad_enabled,error_code,error_message,diagnostics_json,parameters_json,queue_position,"
            "retry_count,created_at,started_at,completed_at,interrupted_at,last_completed_"
            "chunk,"
            "queue_hidden) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        query.addBindValue(job.id);
        query.addBindValue(job.recordingId);
        query.addBindValue(jobStateName(job.state));
        query.addBindValue(jobStageName(job.stage));
        query.addBindValue(job.progress);
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
        query.addBindValue(job.retryCount);
        query.addBindValue(TimeUtils::toStorageString(job.createdAt));
        query.addBindValue(QVariant());
        query.addBindValue(QVariant());
        query.addBindValue(QVariant());
        query.addBindValue(job.lastCompletedChunk);
        query.addBindValue(job.queueHidden);
        if (!query.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The transcription job could not be created."), query));
        }
        JobEvent event;
        event.jobId = job.id;
        event.eventType = QStringLiteral("enqueued");
        event.state = job.state;
        event.stage = job.stage;
        event.progress = job.progress;
        return insertEvent(database, &event);
    });
    if (!saved) {
        return Result<TranscriptionJob>::failure(saved.error());
    }
    return Result<TranscriptionJob>::success(job);
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

Result<std::optional<TranscriptSegment>>
SqliteJobRepository::latestSegmentForJob(const QString& jobId, const bool includeProvisional) const {
    if (jobId.isEmpty()) {
        return Result<std::optional<TranscriptSegment>>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A transcription job is required.")));
    }
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult) {
        return Result<std::optional<TranscriptSegment>>::failure(connectionResult.error());
    }
    return latestSegment(connectionResult.value(), jobId, includeProvisional);
}

Result<JobEvent> SqliteJobRepository::appendEvent(JobEvent event, const QString& ownerToken) {
    const auto result = m_databaseManager.executionLeaseTransaction(
        event.jobId, ownerToken,
        [&](QSqlDatabase& database) { return insertEvent(database, &event); });
    return result ? Result<JobEvent>::success(event) : Result<JobEvent>::failure(result.error());
}

Result<QList<JobEvent>> SqliteJobRepository::eventsForJob(const QString& jobId, const qint64 afterId,
                                                          const int limit) const {
    if (jobId.isEmpty()) {
        return Result<QList<JobEvent>>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A transcription job is required.")));
    }
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult) {
        return Result<QList<JobEvent>>::failure(connectionResult.error());
    }
    QSqlQuery query(connectionResult.value());
    query.prepare(
        QStringLiteral("SELECT * FROM transcription_job_events WHERE job_id=? AND id>? ORDER BY id LIMIT ?"));
    query.addBindValue(jobId);
    query.addBindValue(qMax<qint64>(0, afterId));
    query.addBindValue(qBound(1, limit, 1'000));
    if (!query.exec()) {
        return Result<QList<JobEvent>>::failure(
            queryError(QStringLiteral("Job events could not be loaded."), query));
    }
    QList<JobEvent> events;
    while (query.next()) {
        events.append(readEvent(query));
    }
    return Result<QList<JobEvent>>::success(events);
}

Result<JobClaimResult> SqliteJobRepository::claimNextQueued(const QString& ownerToken,
                                                            const qint64 leaseDurationMs) {
    if (ownerToken.trimmed().isEmpty() || leaseDurationMs < 1'000) {
        return Result<JobClaimResult>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument,
            QStringLiteral("A lease owner and a duration of at least one second are required.")));
    }
    std::optional<JobClaimResult> claimed;
    const auto result = m_databaseManager.immediateTransaction([&](QSqlDatabase& database) {
        const auto value = claimInDatabase(database, std::nullopt, ownerToken, leaseDurationMs);
        if (!value) {
            return Result<void>::failure(value.error());
        }
        claimed = value.value();
        return Result<void>::success();
    });
    if (!result) {
        return Result<JobClaimResult>::failure(result.error());
    }
    return Result<JobClaimResult>::success(*claimed);
}

Result<JobClaimResult> SqliteJobRepository::claimQueued(const QString& jobId, const QString& ownerToken,
                                                        const qint64 leaseDurationMs) {
    if (jobId.isEmpty() || ownerToken.trimmed().isEmpty() || leaseDurationMs < 1'000) {
        return Result<JobClaimResult>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument,
            QStringLiteral("A queued job, lease owner, and valid lease duration are required.")));
    }
    std::optional<JobClaimResult> claimed;
    const auto result = m_databaseManager.immediateTransaction([&](QSqlDatabase& database) {
        const auto value = claimInDatabase(database, jobId, ownerToken, leaseDurationMs);
        if (!value) {
            return Result<void>::failure(value.error());
        }
        claimed = value.value();
        return Result<void>::success();
    });
    if (!result) {
        return Result<JobClaimResult>::failure(result.error());
    }
    return Result<JobClaimResult>::success(*claimed);
}

Result<AsrExecutionLease> SqliteJobRepository::renewLease(const QString& jobId, const QString& ownerToken,
                                                          const qint64 leaseDurationMs) {
    if (jobId.isEmpty() || ownerToken.trimmed().isEmpty() || leaseDurationMs < 1'000) {
        return Result<AsrExecutionLease>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument,
            QStringLiteral("A job, lease owner, and valid lease duration are required.")));
    }
    AsrExecutionLease renewed;
    const auto result = m_databaseManager.immediateTransaction([&](QSqlDatabase& database) {
        const auto current = findRawLease(database);
        if (!current) {
            return Result<void>::failure(current.error());
        }
        const QDateTime now = QDateTime::currentDateTimeUtc();
        if (!current.value().has_value() || current.value()->jobId != jobId ||
            current.value()->ownerToken != ownerToken) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::ExecutionLeaseLost,
                QStringLiteral("The ASR execution lease is no longer owned by this process.")));
        }
        const auto identity = validateLeaseIdentity(database, *current.value());
        if (!identity) {
            return Result<void>::failure(identity.error());
        }
        if (current.value()->expiresAt.isValid() && current.value()->expiresAt <= now) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::ExecutionLeaseLost,
                QStringLiteral("The ASR execution lease is no longer owned by this process.")));
        }
        renewed = *current.value();
        if (!renewed.acquiredAt.isValid()) {
            renewed.acquiredAt = now;
        }
        renewed.heartbeatAt = now;
        renewed.expiresAt = now.addMSecs(leaseDurationMs);
        QSqlQuery update(database);
        update.prepare(QStringLiteral(
            "UPDATE asr_execution_lease SET acquired_at=?,heartbeat_at=?,expires_at=? WHERE "
            "resource='asr' AND job_id=? AND owner_token=?"));
        update.addBindValue(TimeUtils::toStorageString(renewed.acquiredAt));
        update.addBindValue(TimeUtils::toStorageString(renewed.heartbeatAt));
        update.addBindValue(TimeUtils::toStorageString(renewed.expiresAt));
        update.addBindValue(jobId);
        update.addBindValue(ownerToken);
        if (!update.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The ASR execution lease could not be renewed."), update));
        }
        if (update.numRowsAffected() == 0) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::ExecutionLeaseLost, QStringLiteral("The ASR execution lease was lost.")));
        }
        return Result<void>::success();
    });
    if (!result) {
        return Result<AsrExecutionLease>::failure(result.error());
    }
    return Result<AsrExecutionLease>::success(renewed);
}

Result<void> SqliteJobRepository::releaseLease(const QString& jobId, const QString& ownerToken) {
    if (jobId.isEmpty() || ownerToken.trimmed().isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A job and lease owner are required.")));
    }
    return m_databaseManager.immediateTransaction([&](QSqlDatabase& database) {
        QSqlQuery remove(database);
        remove.prepare(QStringLiteral(
            "DELETE FROM asr_execution_lease WHERE resource='asr' AND job_id=? AND owner_token=?"));
        remove.addBindValue(jobId);
        remove.addBindValue(ownerToken);
        if (!remove.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The ASR execution lease could not be released."), remove));
        }
        if (remove.numRowsAffected() == 0) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidStateTransition,
                QStringLiteral("The ASR execution lease is not owned by this process.")));
        }
        return Result<void>::success();
    });
}

Result<std::optional<AsrExecutionLease>> SqliteJobRepository::activeLease() const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult) {
        return Result<std::optional<AsrExecutionLease>>::failure(connectionResult.error());
    }
    return findLease(connectionResult.value());
}

Result<void> SqliteJobRepository::completeAndActivate(const QString& recordingId, const QString& jobId,
                                                      const QString& ownerToken) {
    if (recordingId.isEmpty() || jobId.isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A recording and transcription job are required.")));
    }
    return m_databaseManager.executionLeaseTransaction(jobId, ownerToken, [&](QSqlDatabase& database) {
        const auto current = findJob(database, jobId);
        if (!current) {
            return Result<void>::failure(current.error());
        }
        if (!current.value().has_value() || current.value()->recordingId != recordingId) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::NotFound, QStringLiteral("The transcription job does not exist.")));
        }
        if (current.value()->state != JobState::Finalizing) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidStateTransition,
                QStringLiteral("Only a finalizing transcription job can be completed.")));
        }
        const QString now = TimeUtils::nowStorageString();
        QSqlQuery complete(database);
        complete.prepare(QStringLiteral(
            "UPDATE transcription_jobs SET state='Completed',stage='Completed',progress=1,completed_at=?,"
            "error_code='',error_message='' WHERE id=? AND recording_id=? AND state='Finalizing'"));
        complete.addBindValue(now);
        complete.addBindValue(jobId);
        complete.addBindValue(recordingId);
        if (!complete.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The transcription job could not be completed."), complete));
        }
        if (complete.numRowsAffected() == 0) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidStateTransition, QStringLiteral("The transcription completion was lost.")));
        }
        QSqlQuery activate(database);
        activate.prepare(QStringLiteral("UPDATE recordings SET active_job_id=?,updated_at=? WHERE id=?"));
        activate.addBindValue(jobId);
        activate.addBindValue(now);
        activate.addBindValue(recordingId);
        if (!activate.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The completed transcript could not be activated."), activate));
        }
        if (activate.numRowsAffected() == 0) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::NotFound, QStringLiteral("The recording no longer exists.")));
        }
        QSqlQuery discardOldTranscript(database);
        discardOldTranscript.prepare(QStringLiteral(
            "DELETE FROM transcript_segments WHERE recording_id=? AND job_id<>? AND job_id IN (SELECT id "
            "FROM transcription_jobs WHERE recording_id=? AND state='Completed')"));
        discardOldTranscript.addBindValue(recordingId);
        discardOldTranscript.addBindValue(jobId);
        discardOldTranscript.addBindValue(recordingId);
        if (!discardOldTranscript.exec()) {
            return Result<void>::failure(queryError(
                QStringLiteral("The previous transcript could not be replaced."), discardOldTranscript));
        }
        QSqlQuery hideOldCompletions(database);
        hideOldCompletions.prepare(
            QStringLiteral("UPDATE transcription_jobs SET queue_hidden=1 WHERE recording_id=? AND "
                           "id<>? AND state='Completed'"));
        hideOldCompletions.addBindValue(recordingId);
        hideOldCompletions.addBindValue(jobId);
        if (!hideOldCompletions.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The previous transcription record could not be archived."),
                           hideOldCompletions));
        }
        JobEvent event;
        event.jobId = jobId;
        event.eventType = QStringLiteral("completed");
        event.state = JobState::Completed;
        event.stage = JobStage::Completed;
        event.progress = 1.0;
        const auto eventResult = insertEvent(database, &event);
        if (!eventResult) {
            return eventResult;
        }
        QSqlQuery release(database);
        if (ownerToken.isEmpty()) {
            release.prepare(
                QStringLiteral("DELETE FROM asr_execution_lease WHERE resource='asr' AND job_id=?"));
            release.addBindValue(jobId);
        } else {
            release.prepare(QStringLiteral(
                "DELETE FROM asr_execution_lease WHERE resource='asr' AND job_id=? AND owner_token=?"));
            release.addBindValue(jobId);
            release.addBindValue(ownerToken);
        }
        if (!release.exec()) {
            return Result<void>::failure(queryError(
                QStringLiteral("The completed ASR execution lease could not be released."), release));
        }
        return DatabaseSearchService(m_databaseManager).rebuildRecording(database, recordingId);
    });
}

Result<void> SqliteJobRepository::transition(const QString& id, const JobState state,
                                             const QString& errorCode, const QString& errorMessage,
                                             const QString& ownerToken) {
    if (id.isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A transcription job is required.")));
    }
    return executionWrite(m_databaseManager, id, ownerToken, [&](QSqlDatabase& database) {
        const auto currentResult = findJob(database, id);
        if (!currentResult) {
            return Result<void>::failure(currentResult.error());
        }
        if (!currentResult.value().has_value()) {
            return Result<void>::failure(UserFacingError::database(
                ErrorCode::NotFound, QStringLiteral("The transcription job no longer exists."), id));
        }
        const TranscriptionJob current = *currentResult.value();
        if (ownerToken.isEmpty()) {
            const bool cancellationRequest =
                state == JobState::Cancelling && JobStateMachine::isRunning(current.state);
            const bool cancelWithoutExecutor =
                state == JobState::Cancelled &&
                (current.state == JobState::Queued || current.state == JobState::Interrupted);
            const bool queueRequest =
                state == JobState::Queued &&
                (current.state == JobState::Interrupted || current.state == JobState::Failed ||
                 current.state == JobState::Cancelled);
            if (!cancellationRequest && !cancelWithoutExecutor && !queueRequest) {
                return Result<void>::failure(UserFacingError::validation(
                    ErrorCode::InvalidStateTransition,
                    QStringLiteral("An active execution lease is required for this job transition.")));
            }
        }
        const auto validation = JobStateMachine::validateTransition(current.state, state);
        if (!validation) {
            return validation;
        }
        if (current.state == state) {
            return Result<void>::success();
        }
        QStringList updates = {QStringLiteral("state=?"), QStringLiteral("error_code=?"),
                               QStringLiteral("error_message=?")};
        QVariantList values = {jobStateName(state), nonNull(errorCode), nonNull(errorMessage)};
        const QString now = TimeUtils::nowStorageString();
        if (state == JobState::Queued) {
            QSqlQuery position(database);
            if (!position.exec(QStringLiteral("SELECT COALESCE(MAX(queue_position),-1)+1 FROM "
                                              "transcription_jobs WHERE state='Queued'")) ||
                !position.next()) {
                return Result<void>::failure(queryError(
                    QStringLiteral("The resumed job queue position could not be allocated."), position));
            }
            updates.append({QStringLiteral("stage='Preparing'"), QStringLiteral("progress=0"),
                            QStringLiteral("started_at=NULL"), QStringLiteral("completed_at=NULL"),
                            QStringLiteral("interrupted_at=NULL"),
                            QStringLiteral("retry_count=retry_count+1"), QStringLiteral("queue_position=?")});
            values.append(position.value(0).toInt());
        }
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
        QSqlQuery query(database);
        query.prepare(QStringLiteral("UPDATE transcription_jobs SET ") + updates.join(QLatin1Char(',')) +
                      QStringLiteral(" WHERE id=? AND state=?"));
        for (const QVariant& value : values) {
            query.addBindValue(value);
        }
        query.addBindValue(id);
        query.addBindValue(jobStateName(current.state));
        if (!query.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The job state could not be changed."), query));
        }
        if (query.numRowsAffected() == 0) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidStateTransition,
                QStringLiteral("The job state changed before this transition could be saved.")));
        }
        JobEvent event;
        event.jobId = id;
        if (JobStateMachine::isTerminal(state) || state == JobState::Interrupted) {
            switch (state) {
            case JobState::Completed:
                event.eventType = QStringLiteral("completed");
                break;
            case JobState::Cancelled:
                event.eventType = QStringLiteral("cancelled");
                break;
            case JobState::Failed:
                event.eventType = QStringLiteral("failed");
                break;
            case JobState::Interrupted:
                event.eventType = QStringLiteral("interrupted");
                break;
            default:
                break;
            }
        } else if (state == JobState::Queued && current.state == JobState::Interrupted) {
            event.eventType = QStringLiteral("resume");
        } else if (state == JobState::Queued) {
            event.eventType = QStringLiteral("retry");
        } else {
            event.eventType = QStringLiteral("state_changed");
        }
        event.severity = state == JobState::Failed ? QStringLiteral("error")
                         : state == JobState::Cancelled || state == JobState::Interrupted
                             ? QStringLiteral("warning")
                             : QStringLiteral("info");
        event.state = state;
        event.stage = state == JobState::Completed ? std::optional<JobStage>(JobStage::Completed)
                      : state == JobState::Queued  ? std::optional<JobStage>(JobStage::Preparing)
                                                   : std::optional<JobStage>(current.stage);
        event.progress = state == JobState::Completed ? std::optional<double>(1.0)
                         : state == JobState::Queued  ? std::optional<double>(0.0)
                                                      : std::optional<double>(current.progress);
        event.code = errorCode;
        event.message = errorMessage;
        const auto eventResult = insertEvent(database, &event);
        if (!eventResult) {
            return eventResult;
        }
        if (JobStateMachine::isTerminal(state) || state == JobState::Interrupted) {
            QSqlQuery release(database);
            release.prepare(ownerToken.isEmpty()
                                ? QStringLiteral("DELETE FROM asr_execution_lease WHERE job_id=?")
                                : QStringLiteral("DELETE FROM asr_execution_lease WHERE job_id=? "
                                                 "AND owner_token=?"));
            release.addBindValue(id);
            if (!ownerToken.isEmpty()) {
                release.addBindValue(ownerToken);
            }
            if (!release.exec()) {
                return Result<void>::failure(queryError(
                    QStringLiteral("The completed ASR execution lease could not be released."), release));
            }
        }
        return Result<void>::success();
    });
}

Result<void> SqliteJobRepository::updateProgress(const QString& id, const JobStage stage,
                                                 const double progress, const int lastCompletedChunk,
                                                 const QString& ownerToken) {
    return m_databaseManager.executionLeaseTransaction(id, ownerToken, [&](QSqlDatabase& database) {
        const auto current = findJob(database, id);
        if (!current) {
            return Result<void>::failure(current.error());
        }
        if (!current.value().has_value()) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::NotFound, QStringLiteral("The transcription job no longer exists.")));
        }
        if (static_cast<int>(stage) < static_cast<int>(current.value()->stage)) {
            return Result<void>::failure(
                UserFacingError::validation(ErrorCode::InvalidStateTransition,
                                            QStringLiteral("Job progress stages cannot move backwards.")));
        }
        const double boundedProgress = qBound(0.0, progress, 1.0);
        QSqlQuery query(database);
        query.prepare(QStringLiteral("UPDATE transcription_jobs SET stage=?,progress=MAX(progress,?),"
                                     "last_completed_chunk=MAX(last_completed_chunk,?) WHERE id=?"));
        query.addBindValue(jobStageName(stage));
        query.addBindValue(boundedProgress);
        query.addBindValue(lastCompletedChunk);
        query.addBindValue(id);
        if (!query.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("Job progress could not be saved."), query));
        }
        if (stage != current.value()->stage) {
            JobEvent event;
            event.jobId = id;
            event.eventType = QStringLiteral("stage_changed");
            event.state = current.value()->state;
            event.stage = stage;
            event.progress = std::max(current.value()->progress, boundedProgress);
            return insertEvent(database, &event);
        }
        return Result<void>::success();
    });
}

Result<void> SqliteJobRepository::updateRuntimeInfo(const QString& id, const QString& actualBackend,
                                                    const QString& engineVersion,
                                                    const QString& workerVersion,
                                                    const QJsonObject& diagnostics,
                                                    const QString& ownerToken) {
    return m_databaseManager.executionLeaseTransaction(id, ownerToken, [&](QSqlDatabase& database) {
        const auto current = findJob(database, id);
        if (!current)
            return Result<void>::failure(current.error());
        if (!current.value())
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::NotFound, QStringLiteral("The transcription job no longer exists.")));
        QJsonObject mergedDiagnostics = current.value()->diagnostics;
        for (auto iterator = diagnostics.constBegin(); iterator != diagnostics.constEnd(); ++iterator)
            mergedDiagnostics.insert(iterator.key(), iterator.value());
        QSqlQuery query(database);
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
        if (query.numRowsAffected() == 0)
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::NotFound, QStringLiteral("The transcription job no longer exists.")));
        return Result<void>::success();
    });
}

Result<void> SqliteJobRepository::updateParameters(const QString& id, const QJsonObject& parameters,
                                                   const QString& ownerToken) {
    return m_databaseManager.executionLeaseTransaction(id, ownerToken, [&](QSqlDatabase& database) {
        QSqlQuery query(database);
        query.prepare(QStringLiteral("UPDATE transcription_jobs SET parameters_json=? WHERE id=?"));
        query.addBindValue(textFromObject(parameters));
        query.addBindValue(id);
        if (!query.exec())
            return Result<void>::failure(
                queryError(QStringLiteral("The transcription job parameters could not be saved."), query));
        if (query.numRowsAffected() == 0)
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::NotFound, QStringLiteral("The transcription job no longer exists.")));
        return Result<void>::success();
    });
}

Result<void> SqliteJobRepository::replaceChunks(const QString& jobId, const QList<JobChunk>& chunks,
                                                const QString& ownerToken) {
    for (int i = 0; i < chunks.size(); ++i) {
        if (chunks.at(i).ordinal != i || chunks.at(i).startMs < 0 ||
            chunks.at(i).endMs <= chunks.at(i).startMs) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidArgument,
                QStringLiteral("Job chunks must be ordered and have valid time ranges.")));
        }
    }
    return executionWrite(m_databaseManager, jobId, ownerToken, [&](QSqlDatabase& database) {
        if (ownerToken.isEmpty()) {
            const auto current = findJob(database, jobId);
            if (!current) {
                return Result<void>::failure(current.error());
            }
            if (!current.value().has_value()) {
                return Result<void>::failure(UserFacingError::validation(
                    ErrorCode::NotFound, QStringLiteral("The transcription job no longer exists.")));
            }
            QSqlQuery existing(database);
            existing.prepare(QStringLiteral("SELECT 1 FROM job_chunks WHERE job_id=? LIMIT 1"));
            existing.addBindValue(jobId);
            if (!existing.exec()) {
                return Result<void>::failure(
                    queryError(QStringLiteral("Existing job chunks could not be checked."), existing));
            }
            if (current.value()->state != JobState::Queued || existing.next()) {
                return Result<void>::failure(UserFacingError::validation(
                    ErrorCode::InvalidStateTransition,
                    QStringLiteral("An initial chunk plan can only be created once while queued.")));
            }
        }
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

Result<void> SqliteJobRepository::updateChunk(const JobChunk& chunk, const QString& ownerToken) {
    if (chunk.id.isEmpty() || chunk.jobId.isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A transcription job chunk is required.")));
    }
    return m_databaseManager.executionLeaseTransaction(
        chunk.jobId, ownerToken, [&](QSqlDatabase& database) {
        QSqlQuery current(database);
        current.prepare(QStringLiteral(
            "SELECT c.state,c.ordinal,j.state,j.stage,j.progress,(SELECT COUNT(*) FROM job_chunks total "
            "WHERE total.job_id=c.job_id) AS total FROM job_chunks c JOIN transcription_jobs j ON "
            "j.id=c.job_id WHERE c.id=? AND c.job_id=?"));
        current.addBindValue(chunk.id);
        current.addBindValue(chunk.jobId);
        if (!current.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The current job chunk could not be loaded."), current));
        }
        if (!current.next()) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::NotFound, QStringLiteral("The transcription job chunk no longer exists.")));
        }
        const ChunkState previousState = chunkStateFromName(current.value(0).toString());
        const int ordinal = current.value(1).toInt();
        const JobState jobState = jobStateFromName(current.value(2).toString());
        const JobStage jobStage = jobStageFromName(current.value(3).toString());
        const double jobProgress = current.value(4).toDouble();
        const int total = current.value(5).toInt();

        QSqlQuery update(database);
        update.prepare(QStringLiteral(
            "UPDATE job_chunks SET state=?,attempts=?,started_at=?,completed_at=?,error=?,result_hash=?,"
            "diagnostics_json=? WHERE id=? AND job_id=?"));
        update.addBindValue(chunkStateName(chunk.state));
        update.addBindValue(chunk.attempts);
        update.addBindValue(chunk.startedAt.isValid() ? QVariant(TimeUtils::toStorageString(chunk.startedAt))
                                                      : QVariant());
        update.addBindValue(chunk.completedAt.isValid()
                                ? QVariant(TimeUtils::toStorageString(chunk.completedAt))
                                : QVariant());
        update.addBindValue(nonNull(chunk.error));
        update.addBindValue(nonNull(chunk.resultHash));
        update.addBindValue(textFromObject(chunk.diagnostics));
        update.addBindValue(chunk.id);
        update.addBindValue(chunk.jobId);
        if (!update.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The job chunk could not be updated."), update));
        }
        if (previousState == chunk.state) {
            return Result<void>::success();
        }

        JobEvent event;
        event.jobId = chunk.jobId;
        switch (chunk.state) {
        case ChunkState::Running:
            event.eventType = QStringLiteral("chunk_started");
            break;
        case ChunkState::Completed:
            event.eventType = QStringLiteral("chunk_completed");
            break;
        case ChunkState::Cancelled:
            event.eventType = QStringLiteral("chunk_cancelled");
            event.severity = QStringLiteral("warning");
            break;
        case ChunkState::Failed:
            event.eventType = QStringLiteral("chunk_failed");
            event.severity = QStringLiteral("error");
            break;
        case ChunkState::Interrupted:
            event.eventType = QStringLiteral("chunk_interrupted");
            event.severity = QStringLiteral("warning");
            break;
        case ChunkState::Pending:
            event.eventType = QStringLiteral("chunk_reset");
            break;
        }
        event.state = jobState;
        event.stage = jobStage;
        event.progress = jobProgress;
        event.message = chunk.error;
        event.payload = {{QStringLiteral("ordinal"), ordinal}, {QStringLiteral("total"), total}};
        return insertEvent(database, &event);
    });
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
    int affected = 0;
    const auto recovered = m_databaseManager.immediateTransaction([&](QSqlDatabase& database) {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const auto leaseResult = findLease(database);
        if (!leaseResult) {
            return Result<void>::failure(leaseResult.error());
        }

        QString protectedJobId;
        if (leaseResult.value().has_value() && leaseResult.value()->expiresAt > now) {
            protectedJobId = leaseResult.value()->jobId;
        }

        QSqlQuery running(database);
        running.prepare(QStringLiteral(
            "SELECT id,state FROM transcription_jobs WHERE state IN "
            "('Preparing','Normalizing','WaitingForModel',"
            "'LoadingModel','AnalyzingSpeech','Transcribing','Finalizing','Cancelling') AND (?='' OR id<>?) "
            "ORDER BY id"));
        running.addBindValue(nonNull(protectedJobId));
        running.addBindValue(nonNull(protectedJobId));
        if (!running.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("Running jobs could not be inspected for recovery."), running));
        }
        QList<QPair<QString, JobState>> recoverableJobs;
        while (running.next()) {
            recoverableJobs.append(
                {running.value(0).toString(), jobStateFromName(running.value(1).toString())});
        }

        const QString recoveryReason =
            reason.trimmed().isEmpty()
                ? QStringLiteral("The previous transcription worker stopped unexpectedly.")
                : reason;
        const QString nowText = TimeUtils::toStorageString(now);
        for (const auto& [jobId, previousState] : recoverableJobs) {
            const bool cancellationPending = previousState == JobState::Cancelling;
            const QString message =
                cancellationPending
                    ? QStringLiteral("Cancellation completed after the previous worker stopped.")
                    : recoveryReason;
            QSqlQuery job(database);
            if (cancellationPending) {
            job.prepare(QStringLiteral(
                    "UPDATE transcription_jobs SET state='Cancelled',error_code='JobCancelled',"
                    "error_message=? WHERE id=? AND state='Cancelling'"));
            } else {
                job.prepare(
                    QStringLiteral("UPDATE transcription_jobs SET state='Interrupted',interrupted_at=?,"
                                   "error_code='WorkerCrashed',error_message=? WHERE id=? AND state=?"));
            job.addBindValue(nowText);
                job.addBindValue(message);
            }
            if (cancellationPending) {
                job.addBindValue(message);
            }
            job.addBindValue(jobId);
            if (!cancellationPending) {
                job.addBindValue(jobStateName(previousState));
            }
            if (!job.exec()) {
                return Result<void>::failure(
                    queryError(QStringLiteral("A running job could not be recovered."), job));
            }
            if (job.numRowsAffected() == 0) {
                continue;
            }
            ++affected;

            QSqlQuery chunks(database);
            if (cancellationPending) {
                chunks.prepare(QStringLiteral(
                    "UPDATE job_chunks SET state='Cancelled',error=? WHERE job_id=? AND state IN "
                    "('Running','Interrupted','Failed')"));
            } else {
            chunks.prepare(QStringLiteral(
                "UPDATE job_chunks SET state='Interrupted',error=? WHERE job_id=? AND state='Running'"));
            }
            chunks.addBindValue(message);
            chunks.addBindValue(jobId);
            if (!chunks.exec()) {
                return Result<void>::failure(
                    queryError(QStringLiteral("Running chunks could not be recovered."), chunks));
            }

            JobEvent event;
            event.jobId = jobId;
            event.eventType = cancellationPending ? QStringLiteral("cancelled")
                                                  : QStringLiteral("recovered_as_interrupted");
            event.severity = QStringLiteral("warning");
            event.state = cancellationPending ? JobState::Cancelled : JobState::Interrupted;
            event.code =
                cancellationPending ? QStringLiteral("JobCancelled") : QStringLiteral("WorkerCrashed");
            event.message = message;
            const auto eventResult = insertEvent(database, &event);
            if (!eventResult) {
                return eventResult;
            }
        }

        if (leaseResult.value().has_value() && protectedJobId.isEmpty()) {
            QSqlQuery clearLease(database);
            if (!clearLease.exec(QStringLiteral("DELETE FROM asr_execution_lease WHERE resource='asr'"))) {
                return Result<void>::failure(queryError(
                    QStringLiteral("The expired ASR execution lease could not be cleared."), clearLease));
            }
        }
        return Result<void>::success();
    });
    if (!recovered) {
        return Result<int>::failure(recovered.error());
    }
    return Result<int>::success(affected);
}

Result<void> SqliteJobRepository::deleteTerminalJob(const QString& id) {
    return m_databaseManager.immediateTransaction([&](QSqlDatabase& database) {
        const auto current = findJob(database, id);
        if (!current) {
            return Result<void>::failure(current.error());
        }
        if (!current.value()) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::NotFound, QStringLiteral("The transcription job no longer exists.")));
        }
        if (!JobStateMachine::isTerminal(current.value()->state) &&
            current.value()->state != JobState::Interrupted) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidStateTransition,
                QStringLiteral("Only completed, cancelled, failed, or interrupted jobs can be removed.")));
        }

        QSqlQuery active(database);
        active.prepare(QStringLiteral("SELECT active_job_id=? FROM recordings WHERE id=?"));
        active.addBindValue(id);
        active.addBindValue(current.value()->recordingId);
        if (!active.exec() || !active.next()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The recording transcript could not be checked."), active));
        }

        QSqlQuery remove(database);
        if (active.value(0).toBool()) {
            remove.prepare(QStringLiteral("UPDATE transcription_jobs SET queue_hidden=1 WHERE id=?"));
        } else {
            remove.prepare(QStringLiteral("DELETE FROM transcription_jobs WHERE id=?"));
        }
        remove.addBindValue(id);
        if (!remove.exec() || remove.numRowsAffected() == 0) {
            return Result<void>::failure(
                queryError(QStringLiteral("The transcription job could not be removed."), remove));
        }
        return DatabaseSearchService(m_databaseManager)
            .rebuildRecording(database, current.value()->recordingId);
    });
}

Result<int> SqliteJobRepository::clearCompleted() {
    int removedCount = 0;
    const auto removed = m_databaseManager.immediateTransaction([&](QSqlDatabase& database) {
        QSqlQuery recordings(database);
        if (!recordings.exec(QStringLiteral("SELECT DISTINCT recording_id FROM transcription_jobs "
                                            "WHERE queue_hidden=0 AND state IN ('Completed','Cancelled')"))) {
            return Result<void>::failure(queryError(
                QStringLiteral("Recordings affected by completed jobs could not be loaded."), recordings));
        }
        QStringList recordingIds;
        while (recordings.next()) {
            recordingIds.append(recordings.value(0).toString());
        }

        QSqlQuery hideActive(database);
        if (!hideActive.exec(QStringLiteral(
                "UPDATE transcription_jobs SET queue_hidden=1 WHERE queue_hidden=0 AND state='Completed' "
                "AND EXISTS(SELECT 1 FROM recordings r WHERE r.active_job_id=transcription_jobs.id)"))) {
            return Result<void>::failure(
                queryError(QStringLiteral("Completed jobs could not be cleared."), hideActive));
        }
        removedCount = hideActive.numRowsAffected();

        QSqlQuery remove(database);
        if (!remove.exec(QStringLiteral(
                "DELETE FROM transcription_jobs WHERE queue_hidden=0 AND state IN ('Completed','Cancelled') "
                "AND NOT EXISTS(SELECT 1 FROM recordings r WHERE r.active_job_id=transcription_jobs.id)"))) {
            return Result<void>::failure(
                queryError(QStringLiteral("Completed jobs could not be removed."), remove));
        }
        removedCount += remove.numRowsAffected();

        DatabaseSearchService search(m_databaseManager);
        for (const QString& recordingId : std::as_const(recordingIds)) {
            const auto rebuilt = search.rebuildRecording(database, recordingId);
            if (!rebuilt) {
                return rebuilt;
            }
        }
        return Result<void>::success();
    });
    return removed ? Result<int>::success(removedCount) : Result<int>::failure(removed.error());
}

} // namespace BreezeDesk
