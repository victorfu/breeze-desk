#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/jobs/JobQueue.h"
#include "breezedesk/jobs/JobRecoveryService.h"
#include "breezedesk/jobs/JobStateMachine.h"
#include "breezedesk/jobs/SqliteJobRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace BreezeDesk;

class JobsTest final : public QObject {
    Q_OBJECT

  private slots:
    void stateMachineRejectsInvalidTransitions();
    void progressNeverMovesBackwards();
    void queuePersistsChunksAndRecoversInterruption();
    void clearingCompletedQueuePermanentlyDeletesJobs();
    void removingTerminalJobPermanentlyDeletesIt();
    void runtimeDiagnosticsArePersisted();
    void jobParametersCanBeUpdated();
    void completedTranscriptionReplacesPreviousTranscript();
    void executionLeaseSerializesWorkersAndCompletesAtomically();
    void staleOwnerCannotMutateReclaimedJob();
    void recordingUpdatesArePartitionedByOwnership();
    void ownerlessTransitionsAreLimitedToQueueControl();
    void cancellationSurvivesLeaseRecovery();
    void concurrentRepositoriesClaimOnlyOneJob();
    void malformedExecutionLeaseFailsClosed_data();
    void malformedExecutionLeaseFailsClosed();
    void exactOwnerRepairsMalformedLeaseTimestamps_data();
    void exactOwnerRepairsMalformedLeaseTimestamps();
    void retryAndResumeResetExecutionStateOnTheSameJob();
    void chunkStateChangesAppendStructuredEvents();
};

void JobsTest::cancellationSurvivesLeaseRecovery() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    Recording recording;
    recording.id = QStringLiteral("recording-cancellation-recovery");
    recording.title = QStringLiteral("Cancellation recovery");
    QVERIFY(recordings.create(recording));
    SqliteJobRepository repository(database);

    TranscriptionJob startupJob;
    startupJob.id = QStringLiteral("cancelled-during-startup-recovery");
    startupJob.recordingId = recording.id;
    QVERIFY(repository.createQueued(startupJob));
    const QString startupOwner = QStringLiteral("dead-startup-owner");
    QVERIFY(repository.claimQueued(startupJob.id, startupOwner).value().claimed);
    QVERIFY(repository.transition(startupJob.id, JobState::LoadingModel, {}, {}, startupOwner));
    QVERIFY(repository.transition(startupJob.id, JobState::Transcribing, {}, {}, startupOwner));
    JobChunk startupChunk;
    startupChunk.jobId = startupJob.id;
    startupChunk.ordinal = 0;
    startupChunk.startMs = 0;
    startupChunk.endMs = 1'000;
    startupChunk.state = ChunkState::Running;
    QVERIFY(repository.replaceChunks(startupJob.id, {startupChunk}, startupOwner));
    QVERIFY(JobQueue(repository).cancel(startupJob.id));
    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery removeLease(connection.value());
    QVERIFY(removeLease.exec(QStringLiteral("DELETE FROM asr_execution_lease WHERE resource='asr'")));

    const auto startupRecovery = repository.markRunningJobsInterrupted(QStringLiteral("startup recovery"));
    QVERIFY(startupRecovery);
    QCOMPARE(startupRecovery.value(), 1);
    const auto startupRecovered = repository.findById(startupJob.id);
    QVERIFY(startupRecovered && startupRecovered.value().has_value());
    QCOMPARE(startupRecovered.value()->state, JobState::Cancelled);
    QCOMPARE(startupRecovered.value()->errorCode, QStringLiteral("JobCancelled"));
    QCOMPARE(repository.chunks(startupJob.id).value().constFirst().state, ChunkState::Cancelled);
    QVERIFY(!repository.activeLease().value().has_value());

    TranscriptionJob expiredJob;
    expiredJob.id = QStringLiteral("cancelled-after-expired-lease");
    expiredJob.recordingId = recording.id;
    QVERIFY(repository.createQueued(expiredJob));
    TranscriptionJob nextJob;
    nextJob.id = QStringLiteral("queued-after-cancelled-lease");
    nextJob.recordingId = recording.id;
    QVERIFY(repository.createQueued(nextJob));
    const QString expiredOwner = QStringLiteral("dead-expired-owner");
    QVERIFY(repository.claimQueued(expiredJob.id, expiredOwner, 10'000).value().claimed);
    JobChunk expiredChunk;
    expiredChunk.jobId = expiredJob.id;
    expiredChunk.ordinal = 0;
    expiredChunk.startMs = 0;
    expiredChunk.endMs = 1'000;
    expiredChunk.state = ChunkState::Running;
    QVERIFY(repository.replaceChunks(expiredJob.id, {expiredChunk}, expiredOwner));
    QVERIFY(JobQueue(repository).cancel(expiredJob.id));
    QSqlQuery expireLease(connection.value());
    QVERIFY(expireLease.exec(QStringLiteral(
        "UPDATE asr_execution_lease SET expires_at='2000-01-01T00:00:00.000Z' WHERE resource='asr'")));

    const auto nextClaim = repository.claimQueued(nextJob.id, QStringLiteral("next-owner"), 10'000);
    QVERIFY(nextClaim && nextClaim.value().claimed);
    QCOMPARE(nextClaim.value().job->id, nextJob.id);
    const auto expiredRecovered = repository.findById(expiredJob.id);
    QVERIFY(expiredRecovered && expiredRecovered.value().has_value());
    QCOMPARE(expiredRecovered.value()->state, JobState::Cancelled);
    QCOMPARE(expiredRecovered.value()->errorCode, QStringLiteral("JobCancelled"));
    QCOMPARE(repository.chunks(expiredJob.id).value().constFirst().state, ChunkState::Cancelled);
    const auto lease = repository.activeLease();
    QVERIFY(lease && lease.value().has_value());
    QCOMPARE(lease.value()->jobId, nextJob.id);
    QCOMPARE(lease.value()->ownerToken, QStringLiteral("next-owner"));
}

void JobsTest::stateMachineRejectsInvalidTransitions() {
    QVERIFY(JobStateMachine::canTransition(JobState::Queued, JobState::Preparing));
    QVERIFY(!JobStateMachine::canTransition(JobState::Queued, JobState::Completed));
    QVERIFY(!JobStateMachine::validateTransition(JobState::Completed, JobState::Queued));
    QVERIFY(JobStateMachine::canTransition(JobState::Cancelling, JobState::Cancelled));
    QVERIFY(!JobStateMachine::canTransition(JobState::Cancelling, JobState::Failed));
    QVERIFY(!JobStateMachine::canTransition(JobState::Cancelling, JobState::Interrupted));
    QVERIFY(JobStateMachine::isRunning(JobState::Transcribing));
    QVERIFY(JobStateMachine::isTerminal(JobState::Failed));
}

void JobsTest::progressNeverMovesBackwards() {
    MonotonicJobProgress progress;
    QCOMPARE(progress.advance(JobStage::NormalizingAudio, 0.8), 0.17);
    QCOMPARE(progress.advance(JobStage::InspectingMedia, 0.5), 0.17);
    QCOMPARE(progress.advance(JobStage::Transcribing, 0.5), 0.65);
    QCOMPARE(progress.advance(JobStage::Completed, 1.0), 1.0);
}

void JobsTest::queuePersistsChunksAndRecoversInterruption() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    Recording recording;
    recording.id = QStringLiteral("rec");
    recording.title = QStringLiteral("Meeting");
    const auto createRecording = recordings.create(recording);
    if (!createRecording)
        QFAIL(qPrintable(createRecording.error().diagnosticString()));
    SqliteJobRepository repository(database);
    JobQueue queue(repository);
    TranscriptionJob job;
    job.recordingId = recording.id;
    auto id = queue.enqueue(job);
    QVERIFY(id);
    const QString owner = QStringLiteral("crashed-owner");
    QVERIFY(repository.claimQueued(id.value(), owner).value().claimed);
    QVERIFY(repository.transition(id.value(), JobState::Normalizing, {}, {}, owner));
    QList<JobChunk> chunks;
    JobChunk first;
    first.jobId = id.value();
    first.ordinal = 0;
    first.startMs = 0;
    first.endMs = 600'000;
    first.state = ChunkState::Completed;
    chunks.append(first);
    JobChunk second;
    second.jobId = id.value();
    second.ordinal = 1;
    second.startMs = 599'100;
    second.endMs = 1'200'000;
    second.overlapBeforeMs = 900;
    second.state = ChunkState::Running;
    chunks.append(second);
    QVERIFY(repository.replaceChunks(id.value(), chunks, owner));
    QVERIFY(repository.transition(id.value(), JobState::AnalyzingSpeech, {}, {}, owner));
    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery removeLease(connection.value());
    QVERIFY(removeLease.exec(QStringLiteral("DELETE FROM asr_execution_lease WHERE resource='asr'")));
    JobRecoveryService recovery(repository);
    auto recovered = recovery.recoverAfterAbnormalShutdown();
    QVERIFY(recovered);
    QCOMPARE(recovered.value(), 1);
    auto recoveredJob = repository.findById(id.value());
    QVERIFY(recoveredJob && recoveredJob.value());
    QCOMPARE(recoveredJob.value()->state, JobState::Interrupted);
    auto savedChunks = repository.chunks(id.value());
    QVERIFY(savedChunks);
    QCOMPARE(savedChunks.value().at(0).state, ChunkState::Completed);
    QCOMPARE(savedChunks.value().at(1).state, ChunkState::Interrupted);
    QVERIFY(queue.resume(id.value()));
    QCOMPARE(repository.findById(id.value()).value()->state, JobState::Queued);
}

void JobsTest::clearingCompletedQueuePermanentlyDeletesJobs() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    Recording recording;
    recording.id = QStringLiteral("rec");
    recording.title = QStringLiteral("Meeting");
    QVERIFY(recordings.create(recording));
    SqliteJobRepository repository(database);
    TranscriptionJob job;
    job.id = QStringLiteral("job");
    job.recordingId = recording.id;
    QVERIFY(repository.create(job));
    const QString visibleOwner = QStringLiteral("visible-owner");
    QVERIFY(repository.claimQueued(job.id, visibleOwner).value().claimed);
    QVERIFY(repository.transition(job.id, JobState::LoadingModel, {}, {}, visibleOwner));
    QVERIFY(repository.transition(job.id, JobState::Transcribing, {}, {}, visibleOwner));
    QVERIFY(repository.transition(job.id, JobState::Finalizing, {}, {}, visibleOwner));
    QVERIFY(repository.transition(job.id, JobState::Completed, {}, {}, visibleOwner));

    TranscriptionJob hiddenJob;
    hiddenJob.id = QStringLiteral("legacy-hidden-job");
    hiddenJob.recordingId = recording.id;
    hiddenJob.queueHidden = true;
    QVERIFY(repository.create(hiddenJob));
    const QString hiddenOwner = QStringLiteral("hidden-owner");
    QVERIFY(repository.claimQueued(hiddenJob.id, hiddenOwner).value().claimed);
    QVERIFY(repository.transition(hiddenJob.id, JobState::LoadingModel, {}, {}, hiddenOwner));
    QVERIFY(repository.transition(hiddenJob.id, JobState::Transcribing, {}, {}, hiddenOwner));
    QVERIFY(repository.transition(hiddenJob.id, JobState::Finalizing, {}, {}, hiddenOwner));
    QVERIFY(repository.transition(hiddenJob.id, JobState::Completed, {}, {}, hiddenOwner));

    auto cleared = repository.clearCompleted();
    QVERIFY(cleared);
    QCOMPARE(cleared.value(), 1);
    QCOMPARE(repository.list(true).value().size(), 0);
    QVERIFY(!repository.findById(job.id).value().has_value());
    QVERIFY(repository.findById(hiddenJob.id).value().has_value());
}

void JobsTest::removingTerminalJobPermanentlyDeletesIt() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    Recording recording;
    recording.id = QStringLiteral("rec");
    recording.title = QStringLiteral("Meeting");
    QVERIFY(recordings.create(recording));

    SqliteJobRepository repository(database);
    JobQueue queue(repository);
    TranscriptionJob failedJob;
    failedJob.id = QStringLiteral("failed-job");
    failedJob.recordingId = recording.id;
    QVERIFY(repository.create(failedJob));
    const QString failedOwner = QStringLiteral("failed-owner");
    QVERIFY(repository.claimQueued(failedJob.id, failedOwner).value().claimed);
    QVERIFY(repository.transition(failedJob.id, JobState::Failed, QStringLiteral("ModelLoadFailed"),
                                  QStringLiteral("The model could not be loaded."), failedOwner));

    QVERIFY(queue.remove(failedJob.id));
    QCOMPARE(repository.list(true).value().size(), 0);
    QVERIFY(!repository.findById(failedJob.id).value().has_value());
    QVERIFY(recordings.findById(recording.id).value().has_value());

    TranscriptionJob queuedJob;
    queuedJob.id = QStringLiteral("queued-job");
    queuedJob.recordingId = recording.id;
    QVERIFY(repository.create(queuedJob));
    const auto rejected = queue.remove(queuedJob.id);
    QVERIFY(!rejected);
    QCOMPARE(rejected.error().code, ErrorCode::InvalidStateTransition);
    QCOMPARE(repository.list(true).value().size(), 1);
}

void JobsTest::runtimeDiagnosticsArePersisted() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    Recording recording;
    recording.id = QStringLiteral("rec");
    recording.title = QStringLiteral("Meeting");
    QVERIFY(recordings.create(recording));
    SqliteJobRepository repository(database);
    TranscriptionJob job;
    job.id = QStringLiteral("job");
    job.recordingId = recording.id;
    job.backend = QStringLiteral("auto");
    job.diagnostics = {{QStringLiteral("existing"), true}};
    QVERIFY(repository.create(job));
    const QString owner = QStringLiteral("runtime-owner");
    QVERIFY(repository.claimQueued(job.id, owner).value().claimed);
    QVERIFY(repository.updateRuntimeInfo(
        job.id, QStringLiteral("metal"), QStringLiteral("1.9.1"), QStringLiteral("1.0.0"),
        {{QStringLiteral("loadTimeMs"), 420}, {QStringLiteral("usedFallback"), false}}, owner));
    const auto saved = repository.findById(job.id);
    QVERIFY(saved && saved.value().has_value());
    QCOMPARE(saved.value()->backend, QStringLiteral("metal"));
    QCOMPARE(saved.value()->engineVersion, QStringLiteral("1.9.1"));
    QCOMPARE(saved.value()->workerVersion, QStringLiteral("1.0.0"));
    QCOMPARE(saved.value()->diagnostics.value(QStringLiteral("loadTimeMs")).toInt(), 420);
    QVERIFY(saved.value()->diagnostics.value(QStringLiteral("existing")).toBool());
}

void JobsTest::jobParametersCanBeUpdated() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    Recording recording;
    recording.id = QStringLiteral("rec-parameters");
    recording.title = QStringLiteral("Parameter persistence");
    QVERIFY(recordings.create(recording));
    SqliteJobRepository repository(database);
    TranscriptionJob job;
    job.id = QStringLiteral("job-parameters");
    job.recordingId = recording.id;
    job.parameters = {{QStringLiteral("existing"), true}};
    QVERIFY(repository.create(job));
    const QString owner = QStringLiteral("parameters-owner");
    QVERIFY(repository.claimQueued(job.id, owner).value().claimed);

    const QJsonObject parameters = {{QStringLiteral("sourceHash"), QString(64, QLatin1Char('a'))},
                                    {QStringLiteral("durationMs"), 1'000}};
    QVERIFY(repository.updateParameters(job.id, parameters, owner));
    const auto saved = repository.findById(job.id);
    QVERIFY(saved && saved.value().has_value());
    QCOMPARE(saved.value()->parameters, parameters);
    const auto missing =
        repository.updateParameters(QStringLiteral("missing-job"), parameters, owner);
    QVERIFY(!missing);
    QCOMPARE(missing.error().code, ErrorCode::ExecutionLeaseLost);
}

void JobsTest::completedTranscriptionReplacesPreviousTranscript() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    Recording recording;
    recording.id = QStringLiteral("rec");
    recording.title = QStringLiteral("Repeated transcription");
    QVERIFY(recordings.create(recording));
    SqliteJobRepository repository(database);

    TranscriptionJob first;
    first.id = QStringLiteral("job-1");
    first.recordingId = recording.id;
    const auto createdFirst = repository.createQueued(first);
    QVERIFY(createdFirst);
    const QString firstOwner = QStringLiteral("first-owner");
    QVERIFY(repository.claimQueued(first.id, firstOwner).value().claimed);
    QVERIFY(repository.transition(first.id, JobState::LoadingModel, {}, {}, firstOwner));
    QVERIFY(repository.transition(first.id, JobState::Transcribing, {}, {}, firstOwner));
    auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery segment(connection.value());
    segment.prepare(QStringLiteral(
        "INSERT INTO transcript_segments(id,recording_id,job_id,ordinal,start_ms,end_ms,original_text,"
        "edited_text,is_provisional,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
    const auto insertSegment = [&](const QString& id, const QString& jobId, const QString& text,
                                   const bool provisional = false) {
        segment.bindValue(0, id);
        segment.bindValue(1, recording.id);
        segment.bindValue(2, jobId);
        segment.bindValue(3, 0);
        segment.bindValue(4, 0);
        segment.bindValue(5, 1'000);
        segment.bindValue(6, text);
        segment.bindValue(7, QStringLiteral(""));
        segment.bindValue(8, provisional);
        segment.bindValue(9, QStringLiteral("2026-01-01T00:00:00.000Z"));
        segment.bindValue(10, QStringLiteral("2026-01-01T00:00:00.000Z"));
        return segment.exec();
    };
    QVERIFY(insertSegment(QStringLiteral("segment-1"), first.id, QStringLiteral("First transcript")));
    QVERIFY(repository.transition(first.id, JobState::Finalizing, {}, {}, firstOwner));
    QVERIFY(repository.completeAndActivate(recording.id, first.id, firstOwner));

    TranscriptionJob failed;
    failed.id = QStringLiteral("job-2");
    failed.recordingId = recording.id;
    failed.queueHidden = true;
    const auto createdFailed = repository.createQueued(failed);
    QVERIFY(createdFailed);
    QVERIFY(createdFailed.value().queueHidden);
    const QString failedOwner = QStringLiteral("failed-owner");
    QVERIFY(repository.claimQueued(failed.id, failedOwner).value().claimed);
    QVERIFY(repository.transition(failed.id, JobState::Failed, QStringLiteral("ModelLoadFailed"),
                                  QStringLiteral("Model failed"), failedOwner));

    TranscriptionJob latest;
    latest.id = QStringLiteral("job-3");
    latest.recordingId = recording.id;
    const auto createdLatest = repository.createQueued(latest);
    QVERIFY(createdLatest);
    const QString latestOwner = QStringLiteral("latest-owner");
    QVERIFY(repository.claimQueued(latest.id, latestOwner).value().claimed);
    QVERIFY(repository.transition(latest.id, JobState::LoadingModel, {}, {}, latestOwner));
    QVERIFY(repository.transition(latest.id, JobState::Transcribing, {}, {}, latestOwner));
    QVERIFY(insertSegment(QStringLiteral("segment-3"), latest.id, QStringLiteral("Replacement transcript")));

    QCOMPARE(recordings.findById(recording.id).value()->activeJobId, first.id);
    QSqlQuery beforeCompletion(connection.value());
    QVERIFY(beforeCompletion.exec(QStringLiteral("SELECT COUNT(*) FROM transcript_segments")));
    QVERIFY(beforeCompletion.next());
    QCOMPARE(beforeCompletion.value(0).toInt(), 2);

    QVERIFY(repository.transition(latest.id, JobState::Finalizing, {}, {}, latestOwner));
    QVERIFY(repository.completeAndActivate(recording.id, latest.id, latestOwner));

    QCOMPARE(recordings.findById(recording.id).value()->activeJobId, latest.id);
    QSqlQuery remainingSegments(connection.value());
    QVERIFY(remainingSegments.exec(QStringLiteral("SELECT job_id,original_text,is_provisional FROM "
                                                  "transcript_segments")));
    QVERIFY(remainingSegments.next());
    QCOMPARE(remainingSegments.value(0).toString(), latest.id);
    QCOMPARE(remainingSegments.value(1).toString(), QStringLiteral("Replacement transcript"));
    QVERIFY(!remainingSegments.value(2).toBool());
    QVERIFY(!remainingSegments.next());

    const auto archivedFirst = repository.findById(first.id);
    QVERIFY(archivedFirst && archivedFirst.value().has_value());
    QVERIFY(archivedFirst.value()->queueHidden);
    QVERIFY(repository.deleteTerminalJob(latest.id));
    const auto retainedLatest = repository.findById(latest.id);
    QVERIFY(retainedLatest && retainedLatest.value().has_value());
    QVERIFY(retainedLatest.value()->queueHidden);
    QCOMPARE(recordings.findById(recording.id).value()->activeJobId, latest.id);
    QCOMPARE(repository.latestSegmentForJob(latest.id).value()->originalText,
             QStringLiteral("Replacement transcript"));
}

void JobsTest::executionLeaseSerializesWorkersAndCompletesAtomically() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    Recording recording;
    recording.id = QStringLiteral("rec");
    recording.title = QStringLiteral("Lease test");
    QVERIFY(recordings.create(recording));
    SqliteJobRepository repository(database);

    TranscriptionJob hidden;
    hidden.id = QStringLiteral("hidden");
    hidden.recordingId = recording.id;
    hidden.queueHidden = true;
    QVERIFY(repository.createQueued(hidden));
    TranscriptionJob visible;
    visible.id = QStringLiteral("visible");
    visible.recordingId = recording.id;
    QVERIFY(repository.createQueued(visible));
    TranscriptionJob next;
    next.id = QStringLiteral("next");
    next.recordingId = recording.id;
    QVERIFY(repository.createQueued(next));

    const auto claimed = repository.claimNextQueued(QStringLiteral("worker-a"), 10'000);
    QVERIFY(claimed && claimed.value().claimed);
    QCOMPARE(claimed.value().job->id, visible.id);
    const auto busy = repository.claimQueued(hidden.id, QStringLiteral("worker-b"), 10'000);
    QVERIFY(busy);
    QVERIFY(!busy.value().claimed);
    QCOMPARE(busy.value().activeJobId, visible.id);
    QVERIFY(!repository.renewLease(visible.id, QStringLiteral("worker-b"), 10'000));
    QVERIFY(repository.renewLease(visible.id, QStringLiteral("worker-a"), 10'000));

    QVERIFY(repository.transition(visible.id, JobState::LoadingModel, {}, {}, QStringLiteral("worker-a")));
    QVERIFY(repository.transition(visible.id, JobState::Transcribing, {}, {}, QStringLiteral("worker-a")));
    QVERIFY(repository.transition(visible.id, JobState::Finalizing, {}, {}, QStringLiteral("worker-a")));
    QVERIFY(!repository.completeAndActivate(recording.id, visible.id, QStringLiteral("worker-b")));
    QCOMPARE(repository.findById(visible.id).value()->state, JobState::Finalizing);
    QVERIFY(repository.completeAndActivate(recording.id, visible.id, QStringLiteral("worker-a")));
    QVERIFY(!repository.activeLease().value().has_value());
    QCOMPARE(recordings.findById(recording.id).value()->activeJobId, visible.id);

    const auto hiddenClaim = repository.claimQueued(hidden.id, QStringLiteral("worker-b"), 10'000);
    QVERIFY(hiddenClaim && hiddenClaim.value().claimed);
    QCOMPARE(hiddenClaim.value().job->id, hidden.id);
    const auto liveRecovery = repository.markRunningJobsInterrupted(QStringLiteral("startup recovery"));
    QVERIFY(liveRecovery);
    QCOMPARE(liveRecovery.value(), 0);
    QCOMPARE(repository.findById(hidden.id).value()->state, JobState::Preparing);

    auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery expire(connection.value());
    QVERIFY(expire.exec(QStringLiteral(
        "UPDATE asr_execution_lease SET expires_at='2000-01-01T00:00:00.000Z' WHERE resource='asr'")));
    const auto reclaimed = repository.claimNextQueued(QStringLiteral("worker-c"), 10'000);
    QVERIFY(reclaimed && reclaimed.value().claimed);
    QCOMPARE(reclaimed.value().job->id, next.id);
    QCOMPARE(repository.findById(hidden.id).value()->state, JobState::Interrupted);
    const auto hiddenEvents = repository.eventsForJob(hidden.id);
    QVERIFY(hiddenEvents);
    QVERIFY(
        std::any_of(hiddenEvents.value().cbegin(), hiddenEvents.value().cend(), [](const JobEvent& event) {
            return event.eventType == QStringLiteral("lease_expired");
        }));
}

void JobsTest::staleOwnerCannotMutateReclaimedJob() {
    QTemporaryDir directory;
    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager databaseA({databasePath});
    DatabaseManager databaseB({databasePath});
    QVERIFY(databaseA.initialize());
    QVERIFY(databaseB.initialize());
    SqliteRecordingRepository recordingsA(databaseA);
    SqliteRecordingRepository recordingsB(databaseB);
    SqliteJobRepository jobsA(databaseA);
    SqliteJobRepository jobsB(databaseB);

    Recording recording;
    recording.id = QStringLiteral("lease-fenced-recording");
    recording.title = QStringLiteral("Lease fencing");
    recording.sourcePath = QStringLiteral("source-a.mp4");
    QVERIFY(recordingsA.create(recording));

    TranscriptionJob job;
    job.id = QStringLiteral("reclaimed-job");
    job.recordingId = recording.id;
    QVERIFY(jobsA.createQueued(job));
    JobChunk chunk;
    chunk.id = QStringLiteral("shared-chunk");
    chunk.jobId = job.id;
    chunk.ordinal = 0;
    chunk.startMs = 0;
    chunk.endMs = 1'000;
    QVERIFY(jobsA.replaceChunks(job.id, {chunk}));

    const QString ownerA = QStringLiteral("expired-owner-a");
    QVERIFY(jobsA.claimQueued(job.id, ownerA, 10'000).value().claimed);
    QVERIFY(jobsA.transition(job.id, JobState::LoadingModel, {}, {}, ownerA));
    QVERIFY(jobsA.transition(job.id, JobState::Transcribing, {}, {}, ownerA));
    chunk.state = ChunkState::Running;
    chunk.attempts = 1;
    QVERIFY(jobsA.updateChunk(chunk, ownerA));

    const auto connection = databaseA.connection();
    QVERIFY(connection);
    QSqlQuery expire(connection.value());
    QVERIFY(expire.exec(QStringLiteral(
        "UPDATE asr_execution_lease SET expires_at='2000-01-01T00:00:00.000Z' WHERE resource='asr'")));
    const auto expiredWrite =
        jobsA.updateProgress(job.id, JobStage::Finalizing, 0.99, 9, ownerA);
    QVERIFY(!expiredWrite);
    QCOMPARE(expiredWrite.error().code, ErrorCode::ExecutionLeaseLost);

    QVERIFY(jobsA.markRunningJobsInterrupted(QStringLiteral("expired owner recovery")));
    QVERIFY(JobQueue(jobsA).resume(job.id));

    const QString ownerB = QStringLiteral("current-owner-b");
    const auto reclaimed = jobsB.claimQueued(job.id, ownerB, 10'000);
    QVERIFY(reclaimed && reclaimed.value().claimed);
    QVERIFY(jobsB.transition(job.id, JobState::LoadingModel, {}, {}, ownerB));
    QVERIFY(jobsB.transition(job.id, JobState::Transcribing, {}, {}, ownerB));
    QVERIFY(jobsB.updateProgress(job.id, JobStage::Transcribing, 0.72, 0, ownerB));
    const QJsonObject parametersB = {{QStringLiteral("sourceHash"), QString(64, QLatin1Char('b'))}};
    QVERIFY(jobsB.updateParameters(job.id, parametersB, ownerB));
    QVERIFY(jobsB.updateRuntimeInfo(job.id, QStringLiteral("cuda"), QStringLiteral("engine-b"),
                                    QStringLiteral("worker-b"),
                                    {{QStringLiteral("owner"), QStringLiteral("b")}}, ownerB));
    JobChunk chunkB = jobsB.chunks(job.id).value().constFirst();
    chunkB.state = ChunkState::Running;
    chunkB.attempts = 2;
    chunkB.resultHash = QStringLiteral("result-b");
    QVERIFY(jobsB.updateChunk(chunkB, ownerB));
    JobEvent eventB;
    eventB.jobId = job.id;
    eventB.eventType = QStringLiteral("owner_b_event");
    QVERIFY(jobsB.appendEvent(eventB, ownerB));

    const Recording staleRecording = recordingsA.findById(recording.id).value().value();
    Recording recordingB = staleRecording;
    recordingB.normalizedPcmPath = QStringLiteral("normalized-b.wav");
    recordingB.sourceHash = QString(64, QLatin1Char('b'));
    recordingB.durationMs = 2'000;
    QVERIFY(recordingsB.update(recordingB, job.id, ownerB));

    const auto staleTransition = jobsA.transition(job.id, JobState::Failed,
                                                  QStringLiteral("StaleFailure"),
                                                  QStringLiteral("stale owner"), ownerA);
    QVERIFY(!staleTransition);
    QCOMPARE(staleTransition.error().code, ErrorCode::ExecutionLeaseLost);
    QVERIFY(!jobsA.updateProgress(job.id, JobStage::Finalizing, 1.0, 4, ownerA));
    QVERIFY(!jobsA.updateParameters(job.id,
                                    {{QStringLiteral("sourceHash"), QString(64, QLatin1Char('a'))}},
                                    ownerA));
    QVERIFY(!jobsA.updateRuntimeInfo(job.id, QStringLiteral("cpu"), QStringLiteral("engine-a"),
                                     QStringLiteral("worker-a"),
                                     {{QStringLiteral("owner"), QStringLiteral("a")}}, ownerA));
    JobChunk replacement = chunkB;
    replacement.id = QStringLiteral("stale-replacement");
    replacement.state = ChunkState::Pending;
    replacement.attempts = 0;
    replacement.resultHash.clear();
    QVERIFY(!jobsA.replaceChunks(job.id, {replacement}, ownerA));
    JobChunk staleChunk = chunkB;
    staleChunk.state = ChunkState::Failed;
    staleChunk.error = QStringLiteral("stale chunk failure");
    staleChunk.resultHash = QStringLiteral("result-a");
    QVERIFY(!jobsA.updateChunk(staleChunk, ownerA));
    JobEvent staleEvent;
    staleEvent.jobId = job.id;
    staleEvent.eventType = QStringLiteral("stale_owner_event");
    QVERIFY(!jobsA.appendEvent(staleEvent, ownerA));
    Recording staleRecordingWrite = staleRecording;
    staleRecordingWrite.normalizedPcmPath = QStringLiteral("normalized-a.wav");
    staleRecordingWrite.sourceHash = QString(64, QLatin1Char('a'));
    QVERIFY(!recordingsA.update(staleRecordingWrite, job.id, ownerA));
    QVERIFY(!jobsA.completeAndActivate(recording.id, job.id, ownerA));

    const auto durableJob = jobsB.findById(job.id);
    QVERIFY(durableJob && durableJob.value().has_value());
    QCOMPARE(durableJob.value()->state, JobState::Transcribing);
    QCOMPARE(durableJob.value()->stage, JobStage::Transcribing);
    QCOMPARE(durableJob.value()->parameters, parametersB);
    QCOMPARE(durableJob.value()->backend, QStringLiteral("cuda"));
    QCOMPARE(durableJob.value()->engineVersion, QStringLiteral("engine-b"));
    QCOMPARE(durableJob.value()->workerVersion, QStringLiteral("worker-b"));
    const JobChunk durableChunk = jobsB.chunks(job.id).value().constFirst();
    QCOMPARE(durableChunk.id, chunkB.id);
    QCOMPARE(durableChunk.state, ChunkState::Running);
    QCOMPARE(durableChunk.attempts, 2);
    QCOMPARE(durableChunk.resultHash, QStringLiteral("result-b"));
    const Recording durableRecording = recordingsB.findById(recording.id).value().value();
    QCOMPARE(durableRecording.normalizedPcmPath, QStringLiteral("normalized-b.wav"));
    QCOMPARE(durableRecording.sourceHash, QString(64, QLatin1Char('b')));
    QCOMPARE(durableRecording.durationMs, 2'000);
    const auto events = jobsB.eventsForJob(job.id);
    QVERIFY(events);
    QVERIFY(std::any_of(events.value().cbegin(), events.value().cend(), [](const JobEvent& event) {
        return event.eventType == QStringLiteral("owner_b_event");
    }));
    QVERIFY(std::none_of(events.value().cbegin(), events.value().cend(), [](const JobEvent& event) {
        return event.eventType == QStringLiteral("stale_owner_event");
    }));
    QVERIFY(jobsB.renewLease(job.id, ownerB, 10'000));

    QVERIFY(JobQueue(jobsA).cancel(job.id));
    QCOMPARE(jobsB.findById(job.id).value()->state, JobState::Cancelling);
    QVERIFY(!jobsA.transition(job.id, JobState::Cancelled, QStringLiteral("JobCancelled"),
                              QStringLiteral("stale cancellation"), ownerA));
    QVERIFY(jobsB.renewLease(job.id, ownerB, 10'000));
    chunkB.state = ChunkState::Cancelled;
    chunkB.error = QStringLiteral("cancelled by user");
    QVERIFY(jobsB.updateChunk(chunkB, ownerB));
    QVERIFY(jobsB.transition(job.id, JobState::Cancelled, QStringLiteral("JobCancelled"),
                             QStringLiteral("cancelled by user"), ownerB));
    QVERIFY(!jobsB.activeLease().value().has_value());
}

void JobsTest::recordingUpdatesArePartitionedByOwnership() {
    QTemporaryDir directory;
    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager databaseA({databasePath});
    DatabaseManager databaseB({databasePath});
    QVERIFY(databaseA.initialize());
    QVERIFY(databaseB.initialize());
    SqliteRecordingRepository recordingsA(databaseA);
    SqliteRecordingRepository recordingsB(databaseB);
    SqliteJobRepository jobsA(databaseA);

    Recording recording;
    recording.id = QStringLiteral("partitioned-recording");
    recording.title = QStringLiteral("Original title");
    recording.notes = QStringLiteral("Original notes");
    recording.sourcePath = QStringLiteral("original-source.mp4");
    recording.managedMediaPath = QStringLiteral("managed-source.mp4");
    recording.tags = {QStringLiteral("Original tag")};
    QVERIFY(recordingsA.create(recording));

    TranscriptionJob job;
    job.id = QStringLiteral("partitioned-job");
    job.recordingId = recording.id;
    QVERIFY(jobsA.createQueued(job));
    const QString owner = QStringLiteral("partitioned-owner");
    QVERIFY(jobsA.claimQueued(job.id, owner, 120'000).value().claimed);

    // Simulate a worker retaining an old snapshot while two UI connections edit independent fields.
    Recording workerSnapshot = recordingsA.findById(recording.id).value().value();
    QVERIFY(recordingsB.updateTitle(recording.id, QStringLiteral("Concurrent title")));
    QVERIFY(recordingsA.updateNotes(recording.id, QStringLiteral("Concurrent notes")));
    QVERIFY(recordingsB.updateReviewState(recording.id, QStringLiteral("reviewed")));
    QVERIFY(recordingsA.setTags(recording.id, {QStringLiteral("Concurrent tag")}));
    workerSnapshot.title = QStringLiteral("Stale title");
    workerSnapshot.notes = QStringLiteral("Stale notes");
    workerSnapshot.sourcePath = QStringLiteral("worker-must-not-change-source.mp4");
    workerSnapshot.managedMediaPath = QStringLiteral("worker-must-not-change-managed.mp4");
    workerSnapshot.reviewState = QStringLiteral("unreviewed");
    workerSnapshot.tags = {QStringLiteral("Stale tag")};
    workerSnapshot.normalizedPcmPath = QStringLiteral("owner-cache.wav");
    workerSnapshot.sourceHash = QString(64, QLatin1Char('a'));
    workerSnapshot.mediaType = QStringLiteral("video/mp4");
    workerSnapshot.durationMs = 4'200;
    workerSnapshot.sampleRate = 16'000;
    workerSnapshot.channelCount = 1;
    workerSnapshot.waveformPath = QStringLiteral("owner-waveform.bwpk");
    QVERIFY(recordingsA.update(workerSnapshot, job.id, owner));

    Recording durable = recordingsB.findById(recording.id).value().value();
    QCOMPARE(durable.title, QStringLiteral("Concurrent title"));
    QCOMPARE(durable.notes, QStringLiteral("Concurrent notes"));
    QCOMPARE(durable.sourcePath, QStringLiteral("original-source.mp4"));
    QCOMPARE(durable.managedMediaPath, QStringLiteral("managed-source.mp4"));
    QCOMPARE(durable.reviewState, QStringLiteral("reviewed"));
    QCOMPARE(durable.tags, QStringList{QStringLiteral("Concurrent tag")});
    QCOMPARE(durable.normalizedPcmPath, QStringLiteral("owner-cache.wav"));
    QCOMPARE(durable.waveformPath, QStringLiteral("owner-waveform.bwpk"));

    // Narrow metadata writes must not replay a stale row over worker-owned cache/provenance.
    QVERIFY(recordingsB.updateReviewState(recording.id, QStringLiteral("unreviewed")));
    QVERIFY(recordingsA.updateTitle(recording.id, QStringLiteral("Latest title")));
    QVERIFY(recordingsB.updateNotes(recording.id, QStringLiteral("Latest notes")));
    durable = recordingsA.findById(recording.id).value().value();
    QCOMPARE(durable.title, QStringLiteral("Latest title"));
    QCOMPARE(durable.notes, QStringLiteral("Latest notes"));
    QCOMPARE(durable.reviewState, QStringLiteral("unreviewed"));
    QCOMPARE(durable.normalizedPcmPath, QStringLiteral("owner-cache.wav"));
    QCOMPARE(durable.sourceHash, QString(64, QLatin1Char('a')));
    QCOMPARE(durable.durationMs, qint64{4'200});

    Recording unfencedReplacement = durable;
    unfencedReplacement.title = QStringLiteral("Forbidden replacement");
    const auto fullUpdate = recordingsB.update(unfencedReplacement);
    QVERIFY(!fullUpdate);
    QCOMPARE(fullUpdate.error().code, ErrorCode::InvalidStateTransition);
    QCOMPARE(fullUpdate.error().message,
             QStringLiteral("The recording cannot be replaced while it is being transcribed."));
    const auto relink =
        recordingsB.relinkSource(recording.id, QStringLiteral("replacement.mp4"), true);
    QVERIFY(!relink);
    QCOMPARE(relink.error().code, ErrorCode::InvalidStateTransition);
    QCOMPARE(relink.error().message,
             QStringLiteral(
                 "The recording source cannot be relinked while it is being transcribed."));

    Recording other;
    other.id = QStringLiteral("different-recording");
    other.title = QStringLiteral("Different recording");
    QVERIFY(recordingsB.create(other));
    other.normalizedPcmPath = QStringLiteral("wrong-recording.wav");
    const auto wrongRecordingUpdate = recordingsA.update(other, job.id, owner);
    QVERIFY(!wrongRecordingUpdate);
    QCOMPARE(wrongRecordingUpdate.error().code, ErrorCode::InvalidArgument);

    durable = recordingsB.findById(recording.id).value().value();
    QCOMPARE(durable.title, QStringLiteral("Latest title"));
    QCOMPARE(durable.notes, QStringLiteral("Latest notes"));
    QCOMPARE(durable.sourcePath, QStringLiteral("original-source.mp4"));
    QCOMPARE(durable.managedMediaPath, QStringLiteral("managed-source.mp4"));
    QCOMPARE(durable.tags, QStringList{QStringLiteral("Concurrent tag")});
    QCOMPARE(durable.normalizedPcmPath, QStringLiteral("owner-cache.wav"));

    // A destructive ownerless operation must not cascade the job and lease out from under its
    // active worker. The UI removes managed/cache files only after this repository call succeeds.
    QVERIFY(recordingsB.moveToTrash(recording.id));
    const auto permanentDelete = recordingsB.permanentlyDelete(recording.id);
    QVERIFY(!permanentDelete);
    QCOMPARE(permanentDelete.error().code, ErrorCode::InvalidStateTransition);
    QCOMPARE(permanentDelete.error().message,
             QStringLiteral(
                 "The recording cannot be permanently deleted while it is being transcribed."));
    const auto preservedRecording = recordingsA.findById(recording.id);
    QVERIFY(preservedRecording && preservedRecording.value().has_value());
    QVERIFY(preservedRecording.value()->deletedAt.isValid());
    QCOMPARE(preservedRecording.value()->sourcePath, QStringLiteral("original-source.mp4"));
    QCOMPARE(preservedRecording.value()->managedMediaPath, QStringLiteral("managed-source.mp4"));
    QCOMPARE(preservedRecording.value()->normalizedPcmPath, QStringLiteral("owner-cache.wav"));
    QCOMPARE(preservedRecording.value()->waveformPath, QStringLiteral("owner-waveform.bwpk"));
    const auto preservedJob = jobsA.findById(job.id);
    QVERIFY(preservedJob && preservedJob.value().has_value());
    const auto preservedLease = jobsA.activeLease();
    QVERIFY(preservedLease && preservedLease.value().has_value());
    QCOMPARE(preservedLease.value()->jobId, job.id);
    QCOMPARE(preservedLease.value()->ownerToken, owner);

    const auto connection = databaseA.connection();
    QVERIFY(connection);
    QSqlQuery corruptLease(connection.value());
    corruptLease.prepare(QStringLiteral(
        "UPDATE asr_execution_lease SET expires_at='not-a-valid-timestamp' WHERE job_id=?"));
    corruptLease.addBindValue(job.id);
    QVERIFY(corruptLease.exec());
    QCOMPARE(corruptLease.numRowsAffected(), 1);
    const auto corruptLeaseFullUpdate = recordingsB.update(unfencedReplacement);
    QVERIFY(!corruptLeaseFullUpdate);
    QCOMPARE(corruptLeaseFullUpdate.error().code, ErrorCode::InvalidStateTransition);
    const auto corruptLeaseRelink =
        recordingsB.relinkSource(recording.id, QStringLiteral("corrupt-lease-relink.mp4"), true);
    QVERIFY(!corruptLeaseRelink);
    QCOMPARE(corruptLeaseRelink.error().code, ErrorCode::InvalidStateTransition);
    const auto corruptLeaseDelete = recordingsB.permanentlyDelete(recording.id);
    QVERIFY(!corruptLeaseDelete);
    QCOMPARE(corruptLeaseDelete.error().code, ErrorCode::InvalidStateTransition);
    QVERIFY(recordingsA.findById(recording.id).value().has_value());
    QVERIFY(jobsA.findById(job.id).value().has_value());
}

void JobsTest::ownerlessTransitionsAreLimitedToQueueControl() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    Recording recording;
    recording.id = QStringLiteral("transition-policy-recording");
    recording.title = QStringLiteral("Transition policy");
    QVERIFY(recordings.create(recording));
    SqliteJobRepository jobs(database);
    TranscriptionJob job;
    job.id = QStringLiteral("transition-policy-job");
    job.recordingId = recording.id;
    QVERIFY(jobs.createQueued(job));

    QVERIFY(!jobs.transition(job.id, JobState::Preparing));
    const QString owner = QStringLiteral("transition-owner");
    QVERIFY(jobs.claimQueued(job.id, owner).value().claimed);
    QVERIFY(jobs.transition(job.id, JobState::LoadingModel, {}, {}, owner));
    QVERIFY(!jobs.transition(job.id, JobState::Failed, QStringLiteral("Unowned"),
                             QStringLiteral("must be rejected")));
    QVERIFY(jobs.renewLease(job.id, owner, 10'000));
    QVERIFY(JobQueue(jobs).cancel(job.id));
    QCOMPARE(jobs.findById(job.id).value()->state, JobState::Cancelling);
    QVERIFY(!jobs.transition(job.id, JobState::Cancelled, QStringLiteral("JobCancelled"),
                             QStringLiteral("ownerless terminal")));
    QVERIFY(jobs.transition(job.id, JobState::Cancelled, QStringLiteral("JobCancelled"),
                            QStringLiteral("owner checkpoint"), owner));

    TranscriptionJob queued;
    queued.id = QStringLiteral("queue-control-job");
    queued.recordingId = recording.id;
    QVERIFY(jobs.createQueued(queued));
    QVERIFY(JobQueue(jobs).cancel(queued.id));
    QCOMPARE(jobs.findById(queued.id).value()->state, JobState::Cancelled);
    QVERIFY(JobQueue(jobs).retry(queued.id));
    QCOMPARE(jobs.findById(queued.id).value()->state, JobState::Queued);
    JobChunk initialChunk;
    initialChunk.jobId = queued.id;
    initialChunk.ordinal = 0;
    initialChunk.startMs = 0;
    initialChunk.endMs = 1'000;
    QVERIFY(jobs.replaceChunks(queued.id, {initialChunk}));
    QVERIFY(!jobs.replaceChunks(queued.id, {initialChunk}));
}

void JobsTest::concurrentRepositoriesClaimOnlyOneJob() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager guiDatabase({path});
    DatabaseManager cliDatabase({path});
    QVERIFY(guiDatabase.initialize());
    SqliteRecordingRepository recordings(guiDatabase);
    Recording recording;
    recording.id = QStringLiteral("rec");
    recording.title = QStringLiteral("Concurrent claim");
    QVERIFY(recordings.create(recording));
    SqliteJobRepository guiRepository(guiDatabase);
    TranscriptionJob job;
    job.id = QStringLiteral("job");
    job.recordingId = recording.id;
    QVERIFY(guiRepository.createQueued(job));
    QVERIFY(cliDatabase.initialize());
    SqliteJobRepository cliRepository(cliDatabase);

    std::mutex mutex;
    std::condition_variable condition;
    int ready = 0;
    bool start = false;
    std::optional<Result<JobClaimResult>> guiClaim;
    std::optional<Result<JobClaimResult>> cliClaim;
    const auto claim = [&](SqliteJobRepository& repository, const QString& owner,
                           std::optional<Result<JobClaimResult>>& result) {
        {
            std::unique_lock lock(mutex);
            ++ready;
            condition.notify_all();
            condition.wait(lock, [&]() { return start; });
        }
        result = repository.claimNextQueued(owner, 10'000);
    };
    std::thread guiWorker(claim, std::ref(guiRepository), QStringLiteral("gui-owner"), std::ref(guiClaim));
    std::thread cliWorker(claim, std::ref(cliRepository), QStringLiteral("cli-owner"), std::ref(cliClaim));
    {
        std::unique_lock lock(mutex);
        condition.wait(lock, [&]() { return ready == 2; });
        start = true;
    }
    condition.notify_all();
    guiWorker.join();
    cliWorker.join();

    QVERIFY(guiClaim.has_value());
    QVERIFY(cliClaim.has_value());
    QVERIFY(*guiClaim);
    QVERIFY(*cliClaim);
    const int claimedCount =
        static_cast<int>(guiClaim->value().claimed) + static_cast<int>(cliClaim->value().claimed);
    QCOMPARE(claimedCount, 1);
    QCOMPARE(guiClaim->value().activeJobId, job.id);
    QCOMPARE(cliClaim->value().activeJobId, job.id);
    const auto lease = guiRepository.activeLease();
    QVERIFY(lease && lease.value().has_value());
    const QString expectedOwner =
        guiClaim->value().claimed ? QStringLiteral("gui-owner") : QStringLiteral("cli-owner");
    QCOMPARE(lease.value()->ownerToken, expectedOwner);
    const auto events = guiRepository.eventsForJob(job.id);
    QVERIFY(events);
    QCOMPARE(
        std::count_if(events.value().cbegin(), events.value().cend(),
                      [](const JobEvent& event) { return event.eventType == QStringLiteral("claimed"); }),
        1);
}

void JobsTest::malformedExecutionLeaseFailsClosed_data() {
    QTest::addColumn<QString>("column");
    QTest::addColumn<QString>("corruptValue");
    QTest::addColumn<bool>("disableForeignKeys");

    QTest::newRow("invalid-acquired-at") << QStringLiteral("acquired_at")
                                          << QStringLiteral("not-a-timestamp") << false;
    QTest::newRow("invalid-heartbeat-at") << QStringLiteral("heartbeat_at")
                                           << QStringLiteral("not-a-timestamp") << false;
    QTest::newRow("invalid-expires-at") << QStringLiteral("expires_at")
                                         << QStringLiteral("not-a-timestamp") << false;
    QTest::newRow("empty-owner") << QStringLiteral("owner_token") << QStringLiteral("   ")
                                  << false;
    QTest::newRow("missing-job") << QStringLiteral("job_id")
                                  << QStringLiteral("missing-lease-job") << true;
}

void JobsTest::malformedExecutionLeaseFailsClosed() {
    QFETCH(QString, column);
    QFETCH(QString, corruptValue);
    QFETCH(bool, disableForeignKeys);

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager databaseA({path});
    DatabaseManager databaseB({path});
    QVERIFY(databaseA.initialize());
    QVERIFY(databaseB.initialize());
    SqliteRecordingRepository recordings(databaseA);
    SqliteJobRepository jobsA(databaseA);
    SqliteJobRepository jobsB(databaseB);

    Recording recording;
    recording.id = QStringLiteral("malformed-lease-recording");
    recording.title = QStringLiteral("Malformed lease");
    QVERIFY(recordings.create(recording));

    TranscriptionJob activeJob;
    activeJob.id = QStringLiteral("malformed-lease-active-job");
    activeJob.recordingId = recording.id;
    QVERIFY(jobsA.createQueued(activeJob));
    JobChunk activeChunk;
    activeChunk.id = QStringLiteral("malformed-lease-chunk");
    activeChunk.jobId = activeJob.id;
    activeChunk.ordinal = 0;
    activeChunk.startMs = 0;
    activeChunk.endMs = 1'000;
    QVERIFY(jobsA.replaceChunks(activeJob.id, {activeChunk}));

    TranscriptionJob queuedJob;
    queuedJob.id = QStringLiteral("malformed-lease-queued-job");
    queuedJob.recordingId = recording.id;
    QVERIFY(jobsA.createQueued(queuedJob));

    const QString owner = QStringLiteral("malformed-lease-owner");
    QVERIFY(jobsA.claimQueued(activeJob.id, owner, 120'000).value().claimed);
    QVERIFY(jobsA.transition(activeJob.id, JobState::LoadingModel, {}, {}, owner));
    QVERIFY(jobsA.transition(activeJob.id, JobState::Transcribing, {}, {}, owner));
    activeChunk.state = ChunkState::Running;
    activeChunk.attempts = 1;
    QVERIFY(jobsA.updateChunk(activeChunk, owner));

    const auto connection = databaseB.connection();
    QVERIFY(connection);
    const auto queryValues = [&](const QString& sql, const int valueCount) {
        QSqlQuery query(connection.value());
        if (!query.exec(sql) || !query.next()) {
            return QStringList{QStringLiteral("query-failed"), query.lastError().text()};
        }
        QStringList values;
        for (int index = 0; index < valueCount; ++index) {
            values.append(query.value(index).toString());
        }
        return values;
    };
    const QStringList jobSnapshot = queryValues(
        QStringLiteral("SELECT state,stage,progress,error_code,error_message,interrupted_at FROM "
                       "transcription_jobs WHERE id='malformed-lease-active-job'"),
        6);
    const QStringList queuedSnapshot = queryValues(
        QStringLiteral("SELECT state,stage,progress,error_code,error_message,interrupted_at FROM "
                       "transcription_jobs WHERE id='malformed-lease-queued-job'"),
        6);
    const QStringList chunkSnapshot = queryValues(
        QStringLiteral("SELECT id,state,attempts,error FROM job_chunks WHERE "
                       "id='malformed-lease-chunk'"),
        4);
    const QStringList eventSnapshot = queryValues(
        QStringLiteral("SELECT COUNT(*),COALESCE(MAX(id),0) FROM transcription_job_events"), 2);

    if (disableForeignKeys) {
        QSqlQuery foreignKeys(connection.value());
        QVERIFY(foreignKeys.exec(QStringLiteral("PRAGMA foreign_keys=OFF")));
    }
    QSqlQuery corrupt(connection.value());
    corrupt.prepare(QStringLiteral("UPDATE asr_execution_lease SET %1=? WHERE resource='asr'")
                        .arg(column));
    corrupt.addBindValue(corruptValue);
    QVERIFY(corrupt.exec());
    QCOMPARE(corrupt.numRowsAffected(), 1);
    const QStringList corruptLeaseSnapshot = queryValues(
        QStringLiteral("SELECT owner_token,job_id,acquired_at,heartbeat_at,expires_at FROM "
                       "asr_execution_lease WHERE resource='asr'"),
        5);

    const auto activeLease = jobsB.activeLease();
    QVERIFY(!activeLease);
    QCOMPARE(activeLease.error().code, ErrorCode::DatabaseCorrupt);
    const auto nextClaim = jobsB.claimNextQueued(QStringLiteral("other-owner"), 10'000);
    QVERIFY(!nextClaim);
    QCOMPARE(nextClaim.error().code, ErrorCode::DatabaseCorrupt);
    const auto directClaim =
        jobsB.claimQueued(queuedJob.id, QStringLiteral("other-owner"), 10'000);
    QVERIFY(!directClaim);
    QCOMPARE(directClaim.error().code, ErrorCode::DatabaseCorrupt);
    const auto recovery =
        jobsB.markRunningJobsInterrupted(QStringLiteral("must not recover a corrupt lease"));
    QVERIFY(!recovery);
    QCOMPARE(recovery.error().code, ErrorCode::DatabaseCorrupt);

    QCOMPARE(queryValues(
                 QStringLiteral("SELECT state,stage,progress,error_code,error_message,interrupted_at "
                                "FROM transcription_jobs WHERE id='malformed-lease-active-job'"),
                 6),
             jobSnapshot);
    QCOMPARE(queryValues(
                 QStringLiteral("SELECT state,stage,progress,error_code,error_message,interrupted_at "
                                "FROM transcription_jobs WHERE id='malformed-lease-queued-job'"),
                 6),
             queuedSnapshot);
    QCOMPARE(queryValues(QStringLiteral("SELECT id,state,attempts,error FROM job_chunks WHERE "
                                        "id='malformed-lease-chunk'"),
                         4),
             chunkSnapshot);
    QCOMPARE(queryValues(
                 QStringLiteral("SELECT COUNT(*),COALESCE(MAX(id),0) FROM transcription_job_events"),
                 2),
             eventSnapshot);
    QCOMPARE(queryValues(
                 QStringLiteral("SELECT owner_token,job_id,acquired_at,heartbeat_at,expires_at FROM "
                                "asr_execution_lease WHERE resource='asr'"),
                 5),
             corruptLeaseSnapshot);
}

void JobsTest::exactOwnerRepairsMalformedLeaseTimestamps_data() {
    QTest::addColumn<QString>("column");

    QTest::newRow("acquired-at") << QStringLiteral("acquired_at");
    QTest::newRow("heartbeat-at") << QStringLiteral("heartbeat_at");
    QTest::newRow("expires-at") << QStringLiteral("expires_at");
}

void JobsTest::exactOwnerRepairsMalformedLeaseTimestamps() {
    QFETCH(QString, column);

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager databaseA({path});
    DatabaseManager databaseB({path});
    QVERIFY(databaseA.initialize());
    QVERIFY(databaseB.initialize());
    SqliteRecordingRepository recordings(databaseA);
    SqliteJobRepository jobsA(databaseA);
    SqliteJobRepository jobsB(databaseB);

    Recording recording;
    recording.id = QStringLiteral("repair-lease-recording");
    recording.title = QStringLiteral("Repair lease");
    QVERIFY(recordings.create(recording));
    TranscriptionJob job;
    job.id = QStringLiteral("repair-lease-job");
    job.recordingId = recording.id;
    QVERIFY(jobsA.createQueued(job));
    const QString owner = QStringLiteral("repair-lease-owner");
    const auto claim = jobsA.claimQueued(job.id, owner, 120'000);
    QVERIFY(claim && claim.value().claimed && claim.value().lease.has_value());
    const QDateTime originalAcquiredAt = claim.value().lease->acquiredAt;

    const auto connection = databaseB.connection();
    QVERIFY(connection);
    QSqlQuery corrupt(connection.value());
    corrupt.prepare(QStringLiteral("UPDATE asr_execution_lease SET %1='not-a-timestamp' WHERE "
                                   "resource='asr'")
                        .arg(column));
    QVERIFY(corrupt.exec());
    QCOMPARE(corrupt.numRowsAffected(), 1);

    const auto unreadable = jobsB.activeLease();
    QVERIFY(!unreadable);
    QCOMPARE(unreadable.error().code, ErrorCode::DatabaseCorrupt);
    const auto wrongOwner =
        jobsB.renewLease(job.id, QStringLiteral("not-the-owner"), 120'000);
    QVERIFY(!wrongOwner);
    QCOMPARE(wrongOwner.error().code, ErrorCode::ExecutionLeaseLost);
    QSqlQuery stillCorrupt(connection.value());
    QVERIFY(stillCorrupt.exec(
        QStringLiteral("SELECT %1 FROM asr_execution_lease WHERE resource='asr'").arg(column)));
    QVERIFY(stillCorrupt.next());
    QCOMPARE(stillCorrupt.value(0).toString(), QStringLiteral("not-a-timestamp"));
    stillCorrupt.finish();

    const auto repaired = jobsA.renewLease(job.id, owner, 120'000);
    QVERIFY(repaired);
    QCOMPARE(repaired.value().jobId, job.id);
    QCOMPARE(repaired.value().ownerToken, owner);
    QVERIFY(repaired.value().acquiredAt.isValid());
    QVERIFY(repaired.value().heartbeatAt.isValid());
    QVERIFY(repaired.value().expiresAt.isValid());
    QVERIFY(repaired.value().expiresAt > repaired.value().heartbeatAt);
    if (column != QStringLiteral("acquired_at")) {
        QCOMPARE(repaired.value().acquiredAt, originalAcquiredAt);
    }

    const auto readable = jobsB.activeLease();
    QVERIFY(readable && readable.value().has_value());
    QCOMPARE(readable.value()->jobId, job.id);
    QCOMPARE(readable.value()->ownerToken, owner);
    QVERIFY(readable.value()->acquiredAt.isValid());
    QVERIFY(readable.value()->heartbeatAt.isValid());
    QVERIFY(readable.value()->expiresAt.isValid());
}

void JobsTest::retryAndResumeResetExecutionStateOnTheSameJob() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    Recording recording;
    recording.id = QStringLiteral("rec");
    recording.title = QStringLiteral("Retry test");
    QVERIFY(recordings.create(recording));
    SqliteJobRepository repository(database);

    TranscriptionJob job;
    job.id = QStringLiteral("job");
    job.recordingId = recording.id;
    QVERIFY(repository.createQueued(job));
    const QString firstOwner = QStringLiteral("retry-first-owner");
    QVERIFY(repository.claimQueued(job.id, firstOwner).value().claimed);
    QVERIFY(repository.updateProgress(job.id, JobStage::Transcribing, 0.75, 2, firstOwner));
    QVERIFY(repository.transition(job.id, JobState::Failed, QStringLiteral("WorkerFailed"),
                                  QStringLiteral("failure"), firstOwner));
    QVERIFY(repository.transition(job.id, JobState::Queued));
    auto retried = repository.findById(job.id);
    QVERIFY(retried && retried.value().has_value());
    QCOMPARE(retried.value()->retryCount, 1);
    QCOMPARE(retried.value()->stage, JobStage::Preparing);
    QCOMPARE(retried.value()->progress, 0.0);
    QCOMPARE(retried.value()->lastCompletedChunk, 2);
    QVERIFY(!retried.value()->queueHidden);
    QVERIFY(!retried.value()->startedAt.isValid());
    const QString secondOwner = QStringLiteral("retry-second-owner");
    QVERIFY(repository.claimQueued(job.id, secondOwner).value().claimed);
    QVERIFY(repository.updateProgress(job.id, JobStage::Preparing, 0.1, 2, secondOwner));
    QVERIFY(repository.transition(job.id, JobState::Interrupted, QStringLiteral("WorkerCrashed"),
                                  QStringLiteral("interrupted"), secondOwner));

    TranscriptionJob another;
    another.id = QStringLiteral("another");
    another.recordingId = recording.id;
    const auto createdAnother = repository.createQueued(another);
    QVERIFY(createdAnother);
    QVERIFY(repository.transition(job.id, JobState::Queued));
    const auto resumed = repository.findById(job.id);
    QVERIFY(resumed && resumed.value().has_value());
    QCOMPARE(resumed.value()->retryCount, 2);
    QVERIFY(resumed.value()->queuePosition > createdAnother.value().queuePosition);
    const auto events = repository.eventsForJob(job.id);
    QVERIFY(events);
    QStringList types;
    for (const JobEvent& event : events.value()) {
        types.append(event.eventType);
    }
    QVERIFY(types.contains(QStringLiteral("failed")));
    QVERIFY(types.contains(QStringLiteral("retry")));
    QVERIFY(types.contains(QStringLiteral("interrupted")));
    QVERIFY(types.contains(QStringLiteral("resume")));
}

void JobsTest::chunkStateChangesAppendStructuredEvents() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    Recording recording;
    recording.id = QStringLiteral("rec");
    recording.title = QStringLiteral("Chunk events");
    QVERIFY(recordings.create(recording));
    SqliteJobRepository repository(database);
    TranscriptionJob job;
    job.id = QStringLiteral("job");
    job.recordingId = recording.id;
    QVERIFY(repository.createQueued(job));

    JobChunk first;
    first.id = QStringLiteral("chunk-1");
    first.jobId = job.id;
    first.ordinal = 0;
    first.startMs = 0;
    first.endMs = 1'000;
    JobChunk second;
    second.id = QStringLiteral("chunk-2");
    second.jobId = job.id;
    second.ordinal = 1;
    second.startMs = 1'000;
    second.endMs = 2'000;
    QVERIFY(repository.replaceChunks(job.id, {first, second}));
    const QString owner = QStringLiteral("chunk-owner");
    QVERIFY(repository.claimQueued(job.id, owner).value().claimed);

    first.state = ChunkState::Running;
    first.attempts = 1;
    QVERIFY(repository.updateChunk(first, owner));
    first.attempts = 2;
    QVERIFY(repository.updateChunk(first, owner));
    first.state = ChunkState::Completed;
    QVERIFY(repository.updateChunk(first, owner));
    second.state = ChunkState::Failed;
    second.error = QStringLiteral("decoder failed");
    QVERIFY(repository.updateChunk(second, owner));

    const auto events = repository.eventsForJob(job.id);
    QVERIFY(events);
    QList<JobEvent> chunkEvents;
    for (const JobEvent& event : events.value()) {
        if (event.eventType.startsWith(QStringLiteral("chunk_"))) {
            chunkEvents.append(event);
        }
    }
    QCOMPARE(chunkEvents.size(), 3);
    QCOMPARE(chunkEvents.at(0).eventType, QStringLiteral("chunk_started"));
    QCOMPARE(chunkEvents.at(0).payload.value(QStringLiteral("ordinal")).toInt(), 0);
    QCOMPARE(chunkEvents.at(0).payload.value(QStringLiteral("total")).toInt(), 2);
    QCOMPARE(chunkEvents.at(1).eventType, QStringLiteral("chunk_completed"));
    QCOMPARE(chunkEvents.at(2).eventType, QStringLiteral("chunk_failed"));
    QCOMPARE(chunkEvents.at(2).severity, QStringLiteral("error"));
}

QTEST_GUILESS_MAIN(JobsTest)
#include "tst_Jobs.moc"
