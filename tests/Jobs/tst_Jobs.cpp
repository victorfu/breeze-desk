#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/jobs/JobQueue.h"
#include "breezedesk/jobs/JobRecoveryService.h"
#include "breezedesk/jobs/JobStateMachine.h"
#include "breezedesk/jobs/SqliteJobRepository.h"

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
    void clearingCompletedQueuePreservesJobHistory();
    void removingTerminalJobHidesItFromQueueButPreservesHistory();
    void runtimeDiagnosticsArePersisted();
    void revisionHistoryPreservesAttemptsAndFallsBack();
    void executionLeaseSerializesWorkersAndCompletesAtomically();
    void concurrentRepositoriesClaimOnlyOneJob();
    void retryAndResumeResetExecutionStateOnTheSameRevision();
    void chunkStateChangesAppendStructuredEvents();
};

void JobsTest::stateMachineRejectsInvalidTransitions() {
    QVERIFY(JobStateMachine::canTransition(JobState::Queued, JobState::Preparing));
    QVERIFY(!JobStateMachine::canTransition(JobState::Queued, JobState::Completed));
    QVERIFY(!JobStateMachine::validateTransition(JobState::Completed, JobState::Queued));
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
    job.revisionNumber = 0;
    auto id = queue.enqueue(job);
    QVERIFY(id);
    QVERIFY(repository.transition(id.value(), JobState::Preparing));
    QVERIFY(repository.transition(id.value(), JobState::Normalizing));
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
    QVERIFY(repository.replaceChunks(id.value(), chunks));
    QVERIFY(repository.transition(id.value(), JobState::AnalyzingSpeech));
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

void JobsTest::clearingCompletedQueuePreservesJobHistory() {
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
    QVERIFY(repository.transition(job.id, JobState::Preparing));
    QVERIFY(repository.transition(job.id, JobState::LoadingModel));
    QVERIFY(repository.transition(job.id, JobState::Transcribing));
    QVERIFY(repository.transition(job.id, JobState::Finalizing));
    QVERIFY(repository.transition(job.id, JobState::Completed));
    auto cleared = repository.clearCompleted();
    QVERIFY(cleared);
    QCOMPARE(cleared.value(), 1);
    QCOMPARE(repository.list(true).value().size(), 0);
    QVERIFY(repository.findById(job.id).value().has_value());
}

void JobsTest::removingTerminalJobHidesItFromQueueButPreservesHistory() {
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
    QVERIFY(repository.transition(failedJob.id, JobState::Preparing));
    QVERIFY(repository.transition(failedJob.id, JobState::Failed, QStringLiteral("ModelLoadFailed"),
                                  QStringLiteral("The model could not be loaded.")));

    QVERIFY(queue.remove(failedJob.id));
    QCOMPARE(repository.list(true).value().size(), 0);
    QVERIFY(repository.findById(failedJob.id).value().has_value());

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
    QVERIFY(repository.updateRuntimeInfo(
        job.id, QStringLiteral("metal"), QStringLiteral("1.9.1"), QStringLiteral("1.0.0"),
        {{QStringLiteral("loadTimeMs"), 420}, {QStringLiteral("usedFallback"), false}}));
    const auto saved = repository.findById(job.id);
    QVERIFY(saved && saved.value().has_value());
    QCOMPARE(saved.value()->backend, QStringLiteral("metal"));
    QCOMPARE(saved.value()->engineVersion, QStringLiteral("1.9.1"));
    QCOMPARE(saved.value()->workerVersion, QStringLiteral("1.0.0"));
    QCOMPARE(saved.value()->diagnostics.value(QStringLiteral("loadTimeMs")).toInt(), 420);
    QVERIFY(saved.value()->diagnostics.value(QStringLiteral("existing")).toBool());
}

void JobsTest::revisionHistoryPreservesAttemptsAndFallsBack() {
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
    QCOMPARE(createdFirst.value().revisionNumber, 1);
    QVERIFY(repository.transition(first.id, JobState::Preparing));
    QVERIFY(repository.transition(first.id, JobState::LoadingModel));
    QVERIFY(repository.transition(first.id, JobState::Transcribing));
    QVERIFY(repository.transition(first.id, JobState::Finalizing));
    QVERIFY(repository.completeAndActivate(recording.id, first.id));

    TranscriptionJob failed;
    failed.id = QStringLiteral("job-2");
    failed.recordingId = recording.id;
    failed.queueHidden = true;
    const auto createdFailed = repository.createQueued(failed);
    QVERIFY(createdFailed);
    QCOMPARE(createdFailed.value().revisionNumber, 2);
    QVERIFY(createdFailed.value().queueHidden);
    QVERIFY(repository.transition(failed.id, JobState::Preparing));
    QVERIFY(repository.transition(failed.id, JobState::Failed, QStringLiteral("ModelLoadFailed"),
                                  QStringLiteral("Model failed")));

    TranscriptionJob latest;
    latest.id = QStringLiteral("job-3");
    latest.recordingId = recording.id;
    const auto createdLatest = repository.createQueued(latest);
    QVERIFY(createdLatest);
    QCOMPARE(createdLatest.value().revisionNumber, 3);
    QVERIFY(repository.transition(latest.id, JobState::Preparing));
    QVERIFY(repository.transition(latest.id, JobState::LoadingModel));
    QVERIFY(repository.transition(latest.id, JobState::Transcribing));
    QVERIFY(repository.transition(latest.id, JobState::Finalizing));
    QVERIFY(repository.completeAndActivate(recording.id, latest.id));

    auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery segment(connection.value());
    segment.prepare(QStringLiteral(
        "INSERT INTO transcript_segments(id,recording_id,job_id,ordinal,start_ms,end_ms,original_text,"
        "edited_text,is_provisional,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
    const auto insertSegment = [&](const QString& id, const int ordinal, const qint64 startMs,
                                   const qint64 endMs, const QString& original, const QString& edited,
                                   const bool provisional) {
        segment.bindValue(0, id);
        segment.bindValue(1, recording.id);
        segment.bindValue(2, latest.id);
        segment.bindValue(3, ordinal);
        segment.bindValue(4, startMs);
        segment.bindValue(5, endMs);
        segment.bindValue(6, original);
        segment.bindValue(7, edited);
        segment.bindValue(8, provisional);
        segment.bindValue(9, QStringLiteral("2026-01-01T00:00:00.000Z"));
        segment.bindValue(10, QStringLiteral("2026-01-01T00:00:00.000Z"));
        return segment.exec();
    };
    QVERIFY(insertSegment(QStringLiteral("segment-1"), 0, 0, 1'000, QStringLiteral("original"),
                          QStringLiteral("edited"), false));
    QVERIFY(insertSegment(QStringLiteral("segment-2"), 1, 1'000, 2'000, QStringLiteral("latest"),
                          QStringLiteral(""), true));

    const auto revisions = repository.listForRecording(recording.id);
    QVERIFY(revisions);
    QCOMPARE(revisions.value().size(), 3);
    QCOMPARE(revisions.value().at(0).job.id, latest.id);
    QCOMPARE(revisions.value().at(1).job.id, failed.id);
    QVERIFY(revisions.value().at(1).queueHidden);
    QCOMPARE(revisions.value().at(0).segmentCount, 2);
    QVERIFY(revisions.value().at(0).hasManualEdits);
    QVERIFY(revisions.value().at(0).hasProvisionalSegments);
    QVERIFY(revisions.value().at(0).latestSegment.has_value());
    QCOMPARE(revisions.value().at(0).latestSegment->id, QStringLiteral("segment-2"));
    const auto latestCompleted = repository.latestForRecording(recording.id);
    QVERIFY(latestCompleted && latestCompleted.value().has_value());
    QCOMPARE(latestCompleted.value()->job.id, latest.id);

    QVERIFY(repository.setActiveRevision(recording.id, first.id));
    const auto deletedFirst = repository.deleteRevision(recording.id, first.id);
    QVERIFY(deletedFirst);
    QCOMPARE(deletedFirst.value().activeJobId, latest.id);
    QVERIFY(!repository.findById(first.id).value().has_value());

    TranscriptionJob queued;
    queued.id = QStringLiteral("job-4");
    queued.recordingId = recording.id;
    QVERIFY(repository.createQueued(queued));
    const auto rejected = repository.deleteRevision(recording.id, queued.id);
    QVERIFY(!rejected);
    QCOMPARE(rejected.error().code, ErrorCode::InvalidStateTransition);

    const auto deletedLatest = repository.deleteRevision(recording.id, latest.id);
    QVERIFY(deletedLatest);
    QVERIFY(deletedLatest.value().activeJobId.isEmpty());
    QSqlQuery remainingSegments(connection.value());
    remainingSegments.prepare(QStringLiteral("SELECT COUNT(*) FROM transcript_segments WHERE job_id=?"));
    remainingSegments.addBindValue(latest.id);
    QVERIFY(remainingSegments.exec());
    QVERIFY(remainingSegments.next());
    QCOMPARE(remainingSegments.value(0).toInt(), 0);
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

    QVERIFY(repository.transition(visible.id, JobState::LoadingModel));
    QVERIFY(repository.transition(visible.id, JobState::Transcribing));
    QVERIFY(repository.transition(visible.id, JobState::Finalizing));
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

void JobsTest::retryAndResumeResetExecutionStateOnTheSameRevision() {
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
    QVERIFY(repository.transition(job.id, JobState::Preparing));
    QVERIFY(repository.updateProgress(job.id, JobStage::Transcribing, 0.75, 2));
    QVERIFY(repository.transition(job.id, JobState::Failed, QStringLiteral("WorkerFailed"),
                                  QStringLiteral("failure")));
    QVERIFY(repository.removeFromQueue(job.id));
    QVERIFY(repository.transition(job.id, JobState::Queued));
    auto retried = repository.findById(job.id);
    QVERIFY(retried && retried.value().has_value());
    QCOMPARE(retried.value()->revisionNumber, 1);
    QCOMPARE(retried.value()->retryCount, 1);
    QCOMPARE(retried.value()->stage, JobStage::Preparing);
    QCOMPARE(retried.value()->progress, 0.0);
    QCOMPARE(retried.value()->lastCompletedChunk, 2);
    QVERIFY(retried.value()->queueHidden);
    QVERIFY(!retried.value()->startedAt.isValid());
    QVERIFY(repository.transition(job.id, JobState::Preparing));
    QVERIFY(repository.updateProgress(job.id, JobStage::Preparing, 0.1, 2));
    QVERIFY(repository.transition(job.id, JobState::Interrupted, QStringLiteral("WorkerCrashed"),
                                  QStringLiteral("interrupted")));

    TranscriptionJob another;
    another.id = QStringLiteral("another");
    another.recordingId = recording.id;
    const auto createdAnother = repository.createQueued(another);
    QVERIFY(createdAnother);
    QVERIFY(repository.transition(job.id, JobState::Queued));
    const auto resumed = repository.findById(job.id);
    QVERIFY(resumed && resumed.value().has_value());
    QCOMPARE(resumed.value()->revisionNumber, 1);
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

    first.state = ChunkState::Running;
    first.attempts = 1;
    QVERIFY(repository.updateChunk(first));
    first.attempts = 2;
    QVERIFY(repository.updateChunk(first));
    first.state = ChunkState::Completed;
    QVERIFY(repository.updateChunk(first));
    second.state = ChunkState::Failed;
    second.error = QStringLiteral("decoder failed");
    QVERIFY(repository.updateChunk(second));

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
