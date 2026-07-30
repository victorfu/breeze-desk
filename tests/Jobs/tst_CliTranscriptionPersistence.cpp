#include "breezedesk/cli/CliTranscriptionPersistence.h"
#include "breezedesk/cli/CliChunkEnvelopeGate.h"
#include "breezedesk/cli/CliExitCode.h"
#include "breezedesk/core/TimeUtils.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/jobs/JobQueue.h"
#include "breezedesk/jobs/SqliteJobRepository.h"
#include "breezedesk/transcript/SqliteTranscriptRepository.h"

#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

using namespace BreezeDesk;

namespace {

JobChunk chunk(const int ordinal, const qint64 startMs, const qint64 endMs,
               const qint64 overlapBeforeMs = 0, const qint64 overlapAfterMs = 0) {
    JobChunk value;
    value.ordinal = ordinal;
    value.startMs = startMs;
    value.endMs = endMs;
    value.overlapBeforeMs = overlapBeforeMs;
    value.overlapAfterMs = overlapAfterMs;
    return value;
}

TranscriptSegment segment(const qint64 startMs, const qint64 endMs, const QString& text) {
    TranscriptSegment value;
    value.startMs = startMs;
    value.endMs = endMs;
    value.originalText = text;
    return value;
}

} // namespace

class CliTranscriptionPersistenceTest final : public QObject {
    Q_OBJECT

  private slots:
    void mapsStartupLeaseLossToDatabaseFailure();
    void beginNewRejectsInvalidChunkRangesBeforeMutation();
    void resumeRejectsDiscontinuousChunkPlanBeforeMutation();
    void resumeAllowsPendingPlanDurationCorrection();
    void resumeAllowsStartedPlanDurationOvershoot();
    void checkpointsPartialResultsAndResumesOnlyUnfinishedChunks();
    void bindsSourceAfterPreparingInterruption();
    void rejectsSourceBindingAfterPreparation();
    void synchronizesDecodedAudioDurationBeforeTranscription();
    void retriesFailedChunkWithoutRepeatingCompletedChunks();
    void beginsAfterRecoveringUnleasedRunningJob();
    void resumesAnUnleasedRunningJobAfterRecovery();
    void newSessionCheckpointFailureRetainsItsLease();
    void resumedSessionCheckpointFailureRetainsItsLease();
    void staleOwnerCannotUseNoOpFastPaths();
    void cancellingLeaseWaitDoesNotInterruptCurrentOwner();
    void externalCancellationPreservesCompletedWorkAndPartialSegments();
    void cancellationCheckpointFailureRetainsItsLease();
    void externallyCancelledLeaseWaitDoesNotInterruptCurrentOwner();
    void quiescenceRetainsLeaseUntilCancellationCheckpoint();
    void quiescenceStillStopsAfterLeaseLoss();
    void rejectsOutOfRangeSegmentsWithoutReplacingValidPartial();
    void protocolFailureQuiescesBeforeOwnerFencedCheckpoint();
    void protocolFailureGateIgnoresLateBatchUntilTerminal();
};

void CliTranscriptionPersistenceTest::protocolFailureGateIgnoresLateBatchUntilTerminal() {
    CliChunkEnvelopeGate gate;
    gate.protocolFailurePending = true;
    QString chosenError =
        QStringLiteral("The ASR worker returned a segment outside the active chunk.");
    int persistedSegmentCount = 1;
    bool completed = false;

    const auto deliver = [&](const Ipc::MessageType type) {
        if (!gate.shouldProcess(type)) {
            return;
        }
        if (type == Ipc::MessageType::PartialSegment) {
            ++persistedSegmentCount;
        } else if (type == Ipc::MessageType::JobCancelled) {
            chosenError = QStringLiteral("Transcription was cancelled.");
        } else if (type == Ipc::MessageType::TranscriptionCompleted) {
            completed = true;
        }
    };

    deliver(Ipc::MessageType::PartialSegment);
    QCOMPARE(persistedSegmentCount, 1);
    QCOMPARE(chosenError,
             QStringLiteral("The ASR worker returned a segment outside the active chunk."));
    QVERIFY(gate.requestInFlight);

    deliver(Ipc::MessageType::ModelLoaded);
    QVERIFY(gate.requestInFlight);
    QCOMPARE(persistedSegmentCount, 1);
    QCOMPARE(chosenError,
             QStringLiteral("The ASR worker returned a segment outside the active chunk."));

    deliver(Ipc::MessageType::JobCancelled);
    QVERIFY(!gate.requestInFlight);
    QVERIFY(gate.protocolFailurePending);
    QCOMPARE(persistedSegmentCount, 1);
    QCOMPARE(chosenError,
             QStringLiteral("The ASR worker returned a segment outside the active chunk."));
    QVERIFY(!completed);
}

void CliTranscriptionPersistenceTest::protocolFailureQuiescesBeforeOwnerFencedCheckpoint() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("protocol-quiescence.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-protocol-quiescence");
    descriptor.recording.title = QStringLiteral("Protocol quiescence");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.recording.durationMs = 1'000;
    descriptor.job.id = QStringLiteral("job-protocol-quiescence");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 1'000)};
    const QString ownerToken = QStringLiteral("protocol-quiescence-owner");

    CliTranscriptionPersistence persistence(recordings, jobs, transcripts, ownerToken);
    QVERIFY(persistence.beginNew(descriptor));
    QVERIFY(persistence.beginModelLoad());
    QVERIFY(persistence.beginTranscription());
    QVERIFY(persistence.beginChunk(0));
    QVERIFY(persistence.saveChunkSegments(
        0, {segment(100, 500, QStringLiteral("preserved before protocol failure"))}, true));

    bool workerStopped = false;
    const auto quiesced = persistence.quiesceWorkerBeforeTerminalCheckpoint([&] {
        workerStopped = true;
        const auto activeLease = jobs.activeLease();
        QVERIFY(activeLease && activeLease.value().has_value());
        QCOMPARE(activeLease.value()->jobId, descriptor.job.id);
        QCOMPARE(activeLease.value()->ownerToken, ownerToken);
        QCOMPARE(jobs.findById(descriptor.job.id).value()->state, JobState::Transcribing);
        QCOMPARE(jobs.chunks(descriptor.job.id).value().constFirst().state,
                 ChunkState::Running);
    });
    QVERIFY(quiesced);
    QVERIFY(workerStopped);

    const QString reason =
        QStringLiteral("The ASR worker returned a segment outside the active chunk.");
    QVERIFY(persistence.fail(QStringLiteral("WorkerProtocolMismatch"), reason));
    const auto failedJob = jobs.findById(descriptor.job.id);
    QVERIFY(failedJob && failedJob.value().has_value());
    QCOMPARE(failedJob.value()->state, JobState::Failed);
    QCOMPARE(failedJob.value()->errorCode, QStringLiteral("WorkerProtocolMismatch"));
    QCOMPARE(failedJob.value()->errorMessage, reason);
    QCOMPARE(jobs.chunks(descriptor.job.id).value().constFirst().state, ChunkState::Failed);
    QVERIFY(!jobs.activeLease().value().has_value());

    const auto persisted = transcripts.segmentsForJob(descriptor.job.id, true);
    QVERIFY(persisted);
    QCOMPARE(persisted.value().size(), 1);
    QCOMPARE(persisted.value().constFirst().originalText,
             QStringLiteral("preserved before protocol failure"));
}

void CliTranscriptionPersistenceTest::rejectsOutOfRangeSegmentsWithoutReplacingValidPartial() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("segment-range.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-segment-range");
    descriptor.recording.title = QStringLiteral("Segment range");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.recording.durationMs = 1'000;
    descriptor.job.id = QStringLiteral("job-segment-range");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 1'000)};

    CliTranscriptionPersistence persistence(recordings, jobs, transcripts,
                                            QStringLiteral("segment-range-owner"));
    QVERIFY(persistence.beginNew(descriptor));
    QVERIFY(persistence.beginModelLoad());
    QVERIFY(persistence.beginTranscription());
    QVERIFY(persistence.beginChunk(0));
    QVERIFY(persistence.saveChunkSegments(
        0, {segment(100, 500, QStringLiteral("preserved valid partial"))}, true));

    const auto rejected = persistence.saveChunkSegments(
        0, {segment(100, 500, QStringLiteral("preserved valid partial")),
            segment(900, 1'001, QStringLiteral("cross-chunk partial"))},
        true);
    QVERIFY(!rejected);
    QCOMPARE(rejected.error().code, ErrorCode::WorkerProtocolMismatch);

    const auto persisted = transcripts.segmentsForJob(descriptor.job.id, true);
    QVERIFY(persisted);
    QCOMPARE(persisted.value().size(), 1);
    QCOMPARE(persisted.value().constFirst().originalText,
             QStringLiteral("preserved valid partial"));
    QCOMPARE(persisted.value().constFirst().startMs, 100);
    QCOMPARE(persisted.value().constFirst().endMs, 500);
}

void CliTranscriptionPersistenceTest::mapsStartupLeaseLossToDatabaseFailure() {
    const UserFacingError error = UserFacingError::validation(
        ErrorCode::ExecutionLeaseLost, QStringLiteral("The transcription lease was reclaimed."));
    QCOMPARE(transcriptionSessionFailureExitCode(error), CliExitCode::DatabaseFailure);
}

void CliTranscriptionPersistenceTest::beginNewRejectsInvalidChunkRangesBeforeMutation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("invalid-chunk.wav"));
    QFile source(sourcePath);
    QVERIFY2(source.open(QIODevice::WriteOnly), qPrintable(sourcePath));
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    Recording recoverySentinel;
    recoverySentinel.id = QStringLiteral("invalid-chunk-recovery-sentinel-recording");
    recoverySentinel.title = QStringLiteral("Recovery sentinel");
    QVERIFY(recordings.create(recoverySentinel));
    TranscriptionJob sentinelJob;
    sentinelJob.id = QStringLiteral("invalid-chunk-recovery-sentinel-job");
    sentinelJob.recordingId = recoverySentinel.id;
    QVERIFY(jobs.createQueued(sentinelJob));
    QVERIFY(jobs.replaceChunks(sentinelJob.id, {chunk(0, 0, 1'000)}));
    const QString sentinelOwner = QStringLiteral("invalid-chunk-recovery-sentinel-owner");
    const auto sentinelClaim = jobs.claimQueued(sentinelJob.id, sentinelOwner);
    QVERIFY(sentinelClaim && sentinelClaim.value().claimed);
    QVERIFY(jobs.transition(sentinelJob.id, JobState::LoadingModel, {}, {}, sentinelOwner));
    QVERIFY(jobs.transition(sentinelJob.id, JobState::Transcribing, {}, {}, sentinelOwner));
    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery removeSentinelLease(connection.value());
    removeSentinelLease.prepare(QStringLiteral(
        "DELETE FROM asr_execution_lease WHERE resource='asr' AND job_id=?"));
    removeSentinelLease.addBindValue(sentinelJob.id);
    QVERIFY(removeSentinelLease.exec());
    QCOMPARE(removeSentinelLease.numRowsAffected(), 1);

    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("invalid-chunk-recording");
    descriptor.recording.title = QStringLiteral("Invalid chunk");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.job.id = QStringLiteral("invalid-chunk-job");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, -1, 1'000)};

    CliTranscriptionPersistence persistence(recordings, jobs, transcripts);
    const auto started = persistence.beginNew(descriptor);
    QVERIFY(!started);
    QCOMPARE(started.error().code, ErrorCode::InvalidArgument);
    const auto savedRecording = recordings.findById(descriptor.recording.id);
    QVERIFY(savedRecording);
    QVERIFY(!savedRecording.value().has_value());
    const auto savedJob = jobs.findById(descriptor.job.id);
    QVERIFY(savedJob);
    QVERIFY(!savedJob.value().has_value());
    const auto savedChunks = jobs.chunks(descriptor.job.id);
    QVERIFY(savedChunks);
    QVERIFY(savedChunks.value().isEmpty());
    const auto sentinelAfter = jobs.findById(sentinelJob.id);
    QVERIFY(sentinelAfter && sentinelAfter.value().has_value());
    QCOMPARE(sentinelAfter.value()->state, JobState::Transcribing);
    const auto leaseAfter = jobs.activeLease();
    QVERIFY(leaseAfter);
    QVERIFY(!leaseAfter.value().has_value());
}

void CliTranscriptionPersistenceTest::resumeRejectsDiscontinuousChunkPlanBeforeMutation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("discontinuous-resume.wav"));
    QFile source(sourcePath);
    QVERIFY2(source.open(QIODevice::WriteOnly), qPrintable(sourcePath));
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    Recording recording;
    recording.id = QStringLiteral("discontinuous-resume-recording");
    recording.title = QStringLiteral("Discontinuous resume");
    recording.sourcePath = sourcePath;
    recording.durationMs = 2'000;
    QVERIFY(recordings.create(recording));
    TranscriptionJob job;
    job.id = QStringLiteral("discontinuous-resume-job");
    job.recordingId = recording.id;
    QVERIFY(jobs.createQueued(job));
    QVERIFY(jobs.replaceChunks(job.id, {chunk(0, 0, 900), chunk(1, 1'000, 2'000)}));
    const QString owner = QStringLiteral("discontinuous-resume-owner");
    const auto claim = jobs.claimQueued(job.id, owner);
    QVERIFY(claim && claim.value().claimed);
    QVERIFY(jobs.transition(job.id, JobState::LoadingModel, {}, {}, owner));
    QVERIFY(jobs.transition(job.id, JobState::Transcribing, {}, {}, owner));
    QVERIFY(jobs.updateProgress(job.id, JobStage::Transcribing, 0.5, -1, owner));
    QVERIFY(jobs.transition(job.id, JobState::Interrupted, QStringLiteral("WorkerCrashed"),
                            QStringLiteral("seed interrupted plan"), owner));

    Recording recoverySentinel;
    recoverySentinel.id = QStringLiteral("discontinuous-recovery-sentinel-recording");
    recoverySentinel.title = QStringLiteral("Discontinuous recovery sentinel");
    QVERIFY(recordings.create(recoverySentinel));
    TranscriptionJob sentinelJob;
    sentinelJob.id = QStringLiteral("discontinuous-recovery-sentinel-job");
    sentinelJob.recordingId = recoverySentinel.id;
    QVERIFY(jobs.createQueued(sentinelJob));
    QVERIFY(jobs.replaceChunks(sentinelJob.id, {chunk(0, 0, 1'000)}));
    const QString sentinelOwner = QStringLiteral("discontinuous-recovery-sentinel-owner");
    const auto sentinelClaim = jobs.claimQueued(sentinelJob.id, sentinelOwner);
    QVERIFY(sentinelClaim && sentinelClaim.value().claimed);
    QVERIFY(jobs.transition(sentinelJob.id, JobState::LoadingModel, {}, {}, sentinelOwner));
    QVERIFY(jobs.transition(sentinelJob.id, JobState::Transcribing, {}, {}, sentinelOwner));

    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery removeSentinelLease(connection.value());
    removeSentinelLease.prepare(QStringLiteral(
        "DELETE FROM asr_execution_lease WHERE resource='asr' AND job_id=?"));
    removeSentinelLease.addBindValue(sentinelJob.id);
    QVERIFY(removeSentinelLease.exec());
    QCOMPARE(removeSentinelLease.numRowsAffected(), 1);
    const auto eventCount = [&connection](const QString& jobId) {
        QSqlQuery count(connection.value());
        count.prepare(
            QStringLiteral("SELECT COUNT(*) FROM transcription_job_events WHERE job_id=?"));
        count.addBindValue(jobId);
        if (!count.exec() || !count.next()) {
            return -1;
        }
        return count.value(0).toInt();
    };
    const int targetEventsBefore = eventCount(job.id);
    const int sentinelEventsBefore = eventCount(sentinelJob.id);
    QVERIFY(targetEventsBefore >= 0);
    QVERIFY(sentinelEventsBefore >= 0);

    CliTranscriptionPersistence persistence(recordings, jobs, transcripts);
    const auto resumed = persistence.resume(job.id, sourcePath);
    QVERIFY(!resumed);
    QCOMPARE(resumed.error().code, ErrorCode::InvalidArgument);
    const auto targetAfter = jobs.findById(job.id);
    QVERIFY(targetAfter && targetAfter.value().has_value());
    QCOMPARE(targetAfter.value()->state, JobState::Interrupted);
    const auto chunksAfter = jobs.chunks(job.id);
    QVERIFY(chunksAfter);
    QCOMPARE(chunksAfter.value().size(), 2);
    QCOMPARE(chunksAfter.value().at(0).startMs, qint64{0});
    QCOMPARE(chunksAfter.value().at(0).endMs, qint64{900});
    QCOMPARE(chunksAfter.value().at(1).startMs, qint64{1'000});
    QCOMPARE(chunksAfter.value().at(1).endMs, qint64{2'000});
    QCOMPARE(eventCount(job.id), targetEventsBefore);
    const auto sentinelAfter = jobs.findById(sentinelJob.id);
    QVERIFY(sentinelAfter && sentinelAfter.value().has_value());
    QCOMPARE(sentinelAfter.value()->state, JobState::Transcribing);
    QCOMPARE(eventCount(sentinelJob.id), sentinelEventsBefore);
    const auto leaseAfter = jobs.activeLease();
    QVERIFY(leaseAfter);
    QVERIFY(!leaseAfter.value().has_value());
}

void CliTranscriptionPersistenceTest::resumeAllowsPendingPlanDurationCorrection() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("pending-duration-correction.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("pending-duration-correction-recording");
    descriptor.recording.title = QStringLiteral("Pending duration correction");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.recording.durationMs = 2'000;
    descriptor.job.id = QStringLiteral("pending-duration-correction-job");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 2'000)};

    CliTranscriptionPersistence firstRun(recordings, jobs, transcripts);
    QVERIFY(firstRun.beginNew(descriptor));
    QVERIFY(firstRun.updateNormalizedAudio(directory.filePath(QStringLiteral("normalized.wav")),
                                           1'900, QString(64, QLatin1Char('a')), true));
    QVERIFY(firstRun.interrupt(QStringLiteral("interrupted before provisional plan replacement")));

    CliTranscriptionPersistence resumedRun(recordings, jobs, transcripts);
    const auto resumed = resumedRun.resume(descriptor.job.id, sourcePath);
    QVERIFY(resumed);
    QCOMPARE(resumed.value().chunks.constLast().endMs, qint64{2'000});
    QCOMPARE(recordings.findById(descriptor.recording.id).value()->durationMs, qint64{1'900});
    QVERIFY(resumedRun.interrupt(QStringLiteral("test complete")));
}

void CliTranscriptionPersistenceTest::resumeAllowsStartedPlanDurationOvershoot() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("started-duration-overshoot.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("started-duration-overshoot-recording");
    descriptor.recording.title = QStringLiteral("Started duration overshoot");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.recording.durationMs = 2'000;
    descriptor.job.id = QStringLiteral("started-duration-overshoot-job");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 2'000)};

    CliTranscriptionPersistence firstRun(recordings, jobs, transcripts);
    QVERIFY(firstRun.beginNew(descriptor));
    QVERIFY(firstRun.beginModelLoad());
    QVERIFY(firstRun.beginTranscription());
    QVERIFY(firstRun.interrupt(QStringLiteral("interrupted after transcription started")));
    const auto storedRecording = recordings.findById(descriptor.recording.id);
    QVERIFY(storedRecording && storedRecording.value().has_value());
    Recording corrected = *storedRecording.value();
    corrected.durationMs = 1'900;
    QVERIFY(recordings.update(corrected));

    CliTranscriptionPersistence resumedRun(recordings, jobs, transcripts);
    const auto resumed = resumedRun.resume(descriptor.job.id, sourcePath);
    QVERIFY(resumed);
    QCOMPARE(resumed.value().chunks.constLast().endMs, qint64{2'000});
    QCOMPARE(recordings.findById(descriptor.recording.id).value()->durationMs, qint64{1'900});
    QVERIFY(resumedRun.interrupt(QStringLiteral("test complete")));
}

void CliTranscriptionPersistenceTest::staleOwnerCannotUseNoOpFastPaths() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("stale-fast-path.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager staleDatabase({databasePath});
    DatabaseManager currentDatabase({databasePath});
    QVERIFY(staleDatabase.initialize());
    QVERIFY(currentDatabase.initialize());
    SqliteRecordingRepository recordings(staleDatabase);
    SqliteJobRepository staleJobs(staleDatabase);
    SqliteJobRepository currentJobs(currentDatabase);
    SqliteTranscriptRepository transcripts(staleDatabase);

    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-stale-fast-path");
    descriptor.recording.title = QStringLiteral("Stale fast path");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.job.id = QStringLiteral("job-stale-fast-path");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 1'000)};
    const QString sourceHash(64, QLatin1Char('a'));
    CliTranscriptionPersistence stale(recordings, staleJobs, transcripts,
                                      QStringLiteral("stale-owner"));
    QVERIFY(stale.beginNew(descriptor));
    QVERIFY(stale.bindSourceMedia(sourceHash));
    QVERIFY(stale.beginNormalization());

    const auto connection = currentDatabase.connection();
    QVERIFY(connection);
    QSqlQuery expire(connection.value());
    QVERIFY(expire.exec(QStringLiteral(
        "UPDATE asr_execution_lease SET expires_at='2000-01-01T00:00:00.000Z' WHERE resource='asr'")));
    QVERIFY(currentJobs.markRunningJobsInterrupted(QStringLiteral("forced test handoff")));
    QVERIFY(JobQueue(currentJobs).resume(descriptor.job.id));
    const QString currentOwner = QStringLiteral("current-owner");
    QVERIFY(currentJobs.claimQueued(descriptor.job.id, currentOwner, 10'000).value().claimed);
    QVERIFY(currentJobs.transition(descriptor.job.id, JobState::Normalizing, {}, {}, currentOwner));
    QVERIFY(currentJobs.transition(descriptor.job.id, JobState::LoadingModel, {}, {}, currentOwner));
    QVERIFY(currentJobs.updateProgress(descriptor.job.id, JobStage::LoadingModel, 0.42, -1,
                                       currentOwner));

    const auto sameSource = stale.bindSourceMedia(sourceHash);
    QVERIFY(!sameSource);
    QCOMPARE(sameSource.error().code, ErrorCode::ExecutionLeaseLost);
    const auto sameState = stale.beginModelLoad();
    QVERIFY(!sameState);
    QCOMPARE(sameState.error().code, ErrorCode::ExecutionLeaseLost);
    const auto olderProgress = stale.updateNormalizationProgress(0.75);
    QVERIFY(!olderProgress);
    QCOMPARE(olderProgress.error().code, ErrorCode::ExecutionLeaseLost);
    const auto retainedLease = currentJobs.activeLease();
    QVERIFY(retainedLease && retainedLease.value().has_value());
    QCOMPARE(retainedLease.value()->jobId, descriptor.job.id);
    QCOMPARE(retainedLease.value()->ownerToken, currentOwner);
}

void CliTranscriptionPersistenceTest::externalCancellationPreservesCompletedWorkAndPartialSegments() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("external-cancel.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager database({databasePath});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-external-cancel");
    descriptor.recording.title = QStringLiteral("External cancellation");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.job.id = QStringLiteral("job-external-cancel");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 1'000), chunk(1, 1'000, 2'000)};

    CliTranscriptionPersistence persistence(recordings, jobs, transcripts, QStringLiteral("headless-owner"));
    QVERIFY(persistence.beginNew(descriptor));
    QVERIFY(persistence.beginModelLoad());
    QVERIFY(persistence.beginTranscription());
    QVERIFY(persistence.beginChunk(0));
    QVERIFY(persistence.completeChunk(0, {segment(0, 900, QStringLiteral("completed"))}));
    QVERIFY(persistence.beginChunk(1));
    QVERIFY(
        persistence.saveChunkSegments(1, {segment(1'000, 1'300, QStringLiteral("preserved partial"))}, true));

    DatabaseManager externalDatabase({databasePath});
    QVERIFY(externalDatabase.initialize());
    SqliteJobRepository externalJobs(externalDatabase);
    QVERIFY(JobQueue(externalJobs).cancel(descriptor.job.id));
    const auto requested = persistence.cancellationRequested();
    QVERIFY(requested);
    QVERIFY(requested.value());
    QVERIFY(persistence.cancel(QStringLiteral("cancelled from another process")));

    const auto cancelledJob = jobs.findById(descriptor.job.id);
    QVERIFY(cancelledJob && cancelledJob.value().has_value());
    QCOMPARE(cancelledJob.value()->state, JobState::Cancelled);
    const auto savedChunks = jobs.chunks(descriptor.job.id);
    QVERIFY(savedChunks);
    QCOMPARE(savedChunks.value().size(), 2);
    QCOMPARE(savedChunks.value().at(0).state, ChunkState::Completed);
    QCOMPARE(savedChunks.value().at(1).state, ChunkState::Cancelled);
    const auto savedSegments = transcripts.segmentsForJob(descriptor.job.id, true);
    QVERIFY(savedSegments);
    QCOMPARE(savedSegments.value().size(), 2);
    QCOMPARE(savedSegments.value().at(0).originalText, QStringLiteral("completed"));
    QCOMPARE(savedSegments.value().at(1).originalText, QStringLiteral("preserved partial"));
    QVERIFY(savedSegments.value().at(1).provisional);
    QVERIFY(!jobs.activeLease().value().has_value());
    QVERIFY(!persistence.isActive());
}

void CliTranscriptionPersistenceTest::cancellationCheckpointFailureRetainsItsLease() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("cancel-checkpoint.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-cancel-checkpoint");
    descriptor.recording.title = QStringLiteral("Cancellation checkpoint");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.job.id = QStringLiteral("job-cancel-checkpoint");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 1'000)};

    CliTranscriptionPersistence persistence(recordings, jobs, transcripts,
                                            QStringLiteral("checkpoint-owner"));
    QVERIFY(persistence.beginNew(descriptor));
    QVERIFY(persistence.beginModelLoad());
    QVERIFY(persistence.beginTranscription());
    QVERIFY(persistence.beginChunk(0));
    QVERIFY(JobQueue(jobs).cancel(descriptor.job.id));

    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery rejectTerminal(connection.value());
    QVERIFY(rejectTerminal.exec(
        QStringLiteral("CREATE TRIGGER reject_cancel_terminal BEFORE UPDATE OF state ON transcription_jobs "
                       "WHEN OLD.id='job-cancel-checkpoint' AND NEW.state='Cancelled' BEGIN "
                       "SELECT RAISE(ABORT,'forced cancellation checkpoint failure'); END")));
    const auto rejected = persistence.cancel(QStringLiteral("cancel requested"));
    QVERIFY(!rejected);
    QVERIFY(rejected.error().technicalDetails.contains(QStringLiteral("forced cancellation")));
    QVERIFY(persistence.isActive());
    QCOMPARE(jobs.findById(descriptor.job.id).value()->state, JobState::Cancelling);
    const auto retainedLease = jobs.activeLease();
    QVERIFY(retainedLease && retainedLease.value().has_value());
    QCOMPARE(retainedLease.value()->jobId, descriptor.job.id);
    QCOMPARE(retainedLease.value()->ownerToken, QStringLiteral("checkpoint-owner"));

    QSqlQuery allowTerminal(connection.value());
    QVERIFY(allowTerminal.exec(QStringLiteral("DROP TRIGGER reject_cancel_terminal")));
    QVERIFY(persistence.cancel(QStringLiteral("cancel requested")));
    QCOMPARE(jobs.findById(descriptor.job.id).value()->state, JobState::Cancelled);
    QVERIFY(!jobs.activeLease().value().has_value());
    QVERIFY(!persistence.isActive());
}

void CliTranscriptionPersistenceTest::newSessionCheckpointFailureRetainsItsLease() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("checkpoint-new.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery rejectCheckpoint(connection.value());
    QVERIFY(rejectCheckpoint.exec(
        QStringLiteral("CREATE TRIGGER reject_new_session_checkpoint BEFORE UPDATE ON transcription_jobs "
        "WHEN OLD.id='job-checkpoint-new' AND OLD.state='Preparing' BEGIN "
        "SELECT RAISE(ABORT,'forced new-session checkpoint failure'); END")));

    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-checkpoint-new");
    descriptor.recording.title = QStringLiteral("New checkpoint failure");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.job.id = QStringLiteral("job-checkpoint-new");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 1'000)};
    CliTranscriptionPersistence persistence(recordings, jobs, transcripts,
                                             QStringLiteral("checkpoint-new-owner"));
    const auto started = persistence.beginNew(descriptor);
    QVERIFY(!started);
    QVERIFY(started.error().technicalDetails.contains(QStringLiteral("forced new-session")));
    QVERIFY(persistence.isActive());
    QCOMPARE(jobs.findById(descriptor.job.id).value()->state, JobState::Preparing);
    const auto lease = jobs.activeLease();
    QVERIFY(lease && lease.value().has_value());
    QCOMPARE(lease.value()->jobId, descriptor.job.id);
    QCOMPARE(lease.value()->ownerToken, QStringLiteral("checkpoint-new-owner"));

    QSqlQuery allowCheckpoint(connection.value());
    QVERIFY(allowCheckpoint.exec(QStringLiteral("DROP TRIGGER reject_new_session_checkpoint")));
    QVERIFY(persistence.interrupt(QStringLiteral("test cleanup")));
    QVERIFY(!persistence.isActive());
    QVERIFY(!jobs.activeLease().value().has_value());
}

void CliTranscriptionPersistenceTest::resumedSessionCheckpointFailureRetainsItsLease() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("checkpoint-resume.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-checkpoint-resume");
    descriptor.recording.title = QStringLiteral("Resume checkpoint failure");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.job.id = QStringLiteral("job-checkpoint-resume");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 1'000)};
    CliTranscriptionPersistence firstRun(recordings, jobs, transcripts,
                                         QStringLiteral("checkpoint-first-owner"));
    QVERIFY(firstRun.beginNew(descriptor));
    QVERIFY(firstRun.beginChunk(0));
    QVERIFY(firstRun.interrupt(QStringLiteral("prepare resume fixture")));

    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery rejectChunkReset(connection.value());
    QVERIFY(rejectChunkReset.exec(QStringLiteral(
        "CREATE TRIGGER reject_resume_chunk_reset BEFORE UPDATE ON job_chunks "
        "WHEN OLD.job_id='job-checkpoint-resume' AND OLD.state='Interrupted' AND NEW.state='Pending' "
        "BEGIN SELECT RAISE(ABORT,'forced resume chunk reset failure'); END")));
    QSqlQuery rejectCheckpoint(connection.value());
    QVERIFY(rejectCheckpoint.exec(QStringLiteral(
        "CREATE TRIGGER reject_resume_checkpoint BEFORE UPDATE OF state ON transcription_jobs "
        "WHEN OLD.id='job-checkpoint-resume' AND OLD.state='Preparing' AND NEW.state='Interrupted' "
        "BEGIN SELECT RAISE(ABORT,'forced resume checkpoint failure'); END")));

    CliTranscriptionPersistence resumed(recordings, jobs, transcripts,
                                        QStringLiteral("checkpoint-resume-owner"));
    const auto result = resumed.resume(descriptor.job.id, sourcePath);
    QVERIFY(!result);
    QVERIFY(result.error().technicalDetails.contains(QStringLiteral("forced resume checkpoint")));
    QVERIFY(resumed.isActive());
    QCOMPARE(jobs.findById(descriptor.job.id).value()->state, JobState::Preparing);
    const auto lease = jobs.activeLease();
    QVERIFY(lease && lease.value().has_value());
    QCOMPARE(lease.value()->jobId, descriptor.job.id);
    QCOMPARE(lease.value()->ownerToken, QStringLiteral("checkpoint-resume-owner"));

    QSqlQuery allowChunkReset(connection.value());
    QVERIFY(allowChunkReset.exec(QStringLiteral("DROP TRIGGER reject_resume_chunk_reset")));
    QSqlQuery allowCheckpoint(connection.value());
    QVERIFY(allowCheckpoint.exec(QStringLiteral("DROP TRIGGER reject_resume_checkpoint")));
    QVERIFY(resumed.interrupt(QStringLiteral("test cleanup")));
    QVERIFY(!resumed.isActive());
    QVERIFY(!jobs.activeLease().value().has_value());
}

void CliTranscriptionPersistenceTest::beginsAfterRecoveringUnleasedRunningJob() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    Recording abandonedRecording;
    abandonedRecording.id = QStringLiteral("recording-abandoned-cli");
    abandonedRecording.title = QStringLiteral("Abandoned CLI job");
    abandonedRecording.sourcePath = directory.filePath(QStringLiteral("abandoned.wav"));
    QVERIFY(recordings.create(abandonedRecording));
    TranscriptionJob abandonedJob;
    abandonedJob.id = QStringLiteral("job-abandoned-cli");
    abandonedJob.recordingId = abandonedRecording.id;
    QVERIFY(jobs.createQueued(abandonedJob));
    const QString abandonedOwner = QStringLiteral("dead-cli-owner");
    const auto abandonedClaim = jobs.claimNextQueued(abandonedOwner);
    QVERIFY(abandonedClaim && abandonedClaim.value().claimed);
    QVERIFY(jobs.transition(abandonedJob.id, JobState::LoadingModel, {}, {}, abandonedOwner));
    QVERIFY(jobs.transition(abandonedJob.id, JobState::Transcribing, {}, {}, abandonedOwner));

    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery removeLease(connection.value());
    QVERIFY(removeLease.exec(QStringLiteral("DELETE FROM asr_execution_lease WHERE resource='asr'")));

    const QString sourcePath = directory.filePath(QStringLiteral("new.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();
    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-new-cli");
    descriptor.recording.title = QStringLiteral("New CLI job");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.job.id = QStringLiteral("job-new-cli");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 1'000)};

    CliTranscriptionPersistence persistence(recordings, jobs, transcripts, QStringLiteral("new-cli-owner"));
    const auto started = persistence.beginNew(descriptor);
    if (!started)
        QFAIL(qPrintable(started.error().diagnosticString()));
    QCOMPARE(jobs.findById(abandonedJob.id).value()->state, JobState::Interrupted);
    QCOMPARE(started.value().jobId, descriptor.job.id);
    const auto lease = jobs.activeLease();
    QVERIFY(lease && lease.value().has_value());
    QCOMPARE(lease.value()->jobId, descriptor.job.id);
    QCOMPARE(lease.value()->ownerToken, QStringLiteral("new-cli-owner"));
    QVERIFY(persistence.interrupt(QStringLiteral("test complete")));
}

void CliTranscriptionPersistenceTest::resumesAnUnleasedRunningJobAfterRecovery() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("resume.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    Recording recording;
    recording.id = QStringLiteral("recording-resume-orphan");
    recording.title = QStringLiteral("Resume orphan");
    recording.sourcePath = sourcePath;
    QVERIFY(recordings.create(recording));
    TranscriptionJob job;
    job.id = QStringLiteral("job-resume-orphan");
    job.recordingId = recording.id;
    QVERIFY(jobs.createQueued(job));
    JobChunk planned = chunk(0, 0, 1'000);
    planned.jobId = job.id;
    QVERIFY(jobs.replaceChunks(job.id, {planned}));
    const QString abandonedOwner = QStringLiteral("dead-resume-owner");
    const auto claimed = jobs.claimNextQueued(abandonedOwner);
    QVERIFY(claimed && claimed.value().claimed);
    QVERIFY(jobs.transition(job.id, JobState::LoadingModel, {}, {}, abandonedOwner));

    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery removeLease(connection.value());
    QVERIFY(removeLease.exec(QStringLiteral("DELETE FROM asr_execution_lease WHERE resource='asr'")));

    CliTranscriptionPersistence persistence(recordings, jobs, transcripts,
                                             QStringLiteral("resuming-cli-owner"));
    const auto resumed = persistence.resume(job.id, sourcePath);
    if (!resumed)
        QFAIL(qPrintable(resumed.error().diagnosticString()));
    QCOMPARE(resumed.value().jobId, job.id);
    QCOMPARE(jobs.findById(job.id).value()->state, JobState::Preparing);
    const auto lease = jobs.activeLease();
    QVERIFY(lease && lease.value().has_value());
    QCOMPARE(lease.value()->ownerToken, QStringLiteral("resuming-cli-owner"));
    const auto unchangedRecording = recordings.findById(recording.id);
    QVERIFY(unchangedRecording && unchangedRecording.value().has_value());
    QVERIFY(unchangedRecording.value()->normalizedPcmPath.isEmpty());
    QVERIFY(persistence.interrupt(QStringLiteral("test complete")));
}

void CliTranscriptionPersistenceTest::quiescenceRetainsLeaseUntilCancellationCheckpoint() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("quiescence-cancel.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-quiescence-cancel");
    descriptor.recording.title = QStringLiteral("Quiescence cancellation");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.job.id = QStringLiteral("job-quiescence-cancel");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 1'000)};

    const QString ownerToken = QStringLiteral("quiescence-owner");
    CliTranscriptionPersistence persistence(recordings, jobs, transcripts, ownerToken);
    QVERIFY(persistence.beginNew(descriptor));
    QVERIFY(persistence.beginModelLoad());
    QVERIFY(persistence.beginTranscription());
    QVERIFY(persistence.beginChunk(0));
    QVERIFY(JobQueue(jobs).cancel(descriptor.job.id));

    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery shortenLease(connection.value());
    shortenLease.prepare(
        QStringLiteral("UPDATE asr_execution_lease SET expires_at=? WHERE resource='asr'"));
    shortenLease.addBindValue(TimeUtils::toStorageString(QDateTime::currentDateTimeUtc().addSecs(2)));
    QVERIFY(shortenLease.exec());

    bool quiescenceCalled = false;
    const auto quiesced = persistence.quiesceWorkerBeforeTerminalCheckpoint([&] {
        quiescenceCalled = true;
        const auto lease = jobs.activeLease();
        QVERIFY(lease && lease.value().has_value());
        QCOMPARE(lease.value()->jobId, descriptor.job.id);
        QCOMPARE(lease.value()->ownerToken, ownerToken);
        QVERIFY(lease.value()->expiresAt > QDateTime::currentDateTimeUtc().addSecs(10));
        const auto savedJob = jobs.findById(descriptor.job.id);
        QVERIFY(savedJob && savedJob.value().has_value());
        QCOMPARE(savedJob.value()->state, JobState::Cancelling);
        const auto savedChunks = jobs.chunks(descriptor.job.id);
        QVERIFY(savedChunks);
        QCOMPARE(savedChunks.value().constFirst().state, ChunkState::Running);
    });
    QVERIFY(quiesced);
    QVERIFY(quiescenceCalled);

    QVERIFY(persistence.cancel(QStringLiteral("cancel after worker quiescence")));
    QCOMPARE(jobs.findById(descriptor.job.id).value()->state, JobState::Cancelled);
    QCOMPARE(jobs.chunks(descriptor.job.id).value().constFirst().state, ChunkState::Cancelled);
    QVERIFY(!jobs.activeLease().value().has_value());
}

void CliTranscriptionPersistenceTest::quiescenceStillStopsAfterLeaseLoss() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("quiescence-lease-loss.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-quiescence-lease-loss");
    descriptor.recording.title = QStringLiteral("Quiescence lease loss");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.job.id = QStringLiteral("job-quiescence-lease-loss");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 1'000)};

    CliTranscriptionPersistence persistence(recordings, jobs, transcripts,
                                             QStringLiteral("stale-quiescence-owner"));
    QVERIFY(persistence.beginNew(descriptor));
    QVERIFY(persistence.beginModelLoad());
    QVERIFY(persistence.beginTranscription());
    QVERIFY(persistence.beginChunk(0));

    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery removeLease(connection.value());
    QVERIFY(removeLease.exec(QStringLiteral("DELETE FROM asr_execution_lease WHERE resource='asr'")));

    bool quiescenceCalled = false;
    const auto quiesced = persistence.quiesceWorkerBeforeTerminalCheckpoint(
        [&quiescenceCalled] { quiescenceCalled = true; });
    QVERIFY(!quiesced);
    QCOMPARE(quiesced.error().code, ErrorCode::ExecutionLeaseLost);
    QVERIFY(quiescenceCalled);

    const auto staleCheckpoint = persistence.interrupt(QStringLiteral("must not be persisted"));
    QVERIFY(!staleCheckpoint);
    QCOMPARE(staleCheckpoint.error().code, ErrorCode::ExecutionLeaseLost);
    QCOMPARE(jobs.findById(descriptor.job.id).value()->state, JobState::Transcribing);
    QCOMPARE(jobs.chunks(descriptor.job.id).value().constFirst().state, ChunkState::Running);
}

void CliTranscriptionPersistenceTest::synchronizesDecodedAudioDurationBeforeTranscription() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("screen-recording.mp4"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-mp4-duration");
    descriptor.recording.title = QStringLiteral("Screen recording");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.recording.durationMs = 40'789;
    descriptor.recording.sourceHash = QString(64, QLatin1Char('a'));
    descriptor.recording.waveformPath = directory.filePath(QStringLiteral("stale.waveform"));
    descriptor.job.id = QStringLiteral("job-mp4-duration");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 40'789)};

    CliTranscriptionPersistence persistence(recordings, jobs, transcripts);
    QVERIFY(persistence.beginNew(descriptor));
    QVERIFY(persistence.beginNormalization());
    const QString normalizedPath = directory.filePath(QStringLiteral("decoded.wav"));
    const QString decodedSourceHash(64, QLatin1Char('b'));
    QVERIFY(persistence.updateNormalizedAudio(normalizedPath, 40'745, decodedSourceHash, true));
    QVERIFY(persistence.replaceChunkPlan({chunk(0, 0, 40'745)}));

    const auto recording = recordings.findById(descriptor.recording.id);
    QVERIFY(recording && recording.value().has_value());
    QCOMPARE(recording.value()->normalizedPcmPath, QFileInfo(normalizedPath).absoluteFilePath());
    QCOMPARE(recording.value()->durationMs, 40'745);
    QCOMPARE(recording.value()->sourceHash, decodedSourceHash);
    QVERIFY(recording.value()->waveformPath.isEmpty());
    QCOMPARE(persistence.identity().chunks.size(), 1);
    QCOMPARE(persistence.identity().chunks.constFirst().endMs, 40'745);
    const auto savedChunks = jobs.chunks(descriptor.job.id);
    QVERIFY(savedChunks);
    QCOMPARE(savedChunks.value().constFirst().endMs, 40'745);
    QVERIFY(persistence.interrupt(QStringLiteral("test complete")));
}

void CliTranscriptionPersistenceTest::checkpointsPartialResultsAndResumesOnlyUnfinishedChunks() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("會議 audio.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-1");
    descriptor.recording.title = QStringLiteral("Architecture meeting");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.recording.normalizedPcmPath = directory.filePath(QStringLiteral("normalized.wav"));
    descriptor.recording.durationMs = 2'000;
    descriptor.recording.sampleRate = 16'000;
    descriptor.recording.channelCount = 1;
    descriptor.job.id = QStringLiteral("job-1");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.job.modelId = QStringLiteral("breeze-asr-25-q5");
    descriptor.job.modelChecksum = QString(64, QLatin1Char('a'));
    descriptor.job.language = QStringLiteral("zh");
    descriptor.job.preset = QStringLiteral("balanced");
    descriptor.job.vadEnabled = true;
    descriptor.chunks = {chunk(0, 0, 1'000, 0, 100), chunk(1, 900, 2'000, 100)};
    const QString sourceHash(64, QLatin1Char('a'));

    CliTranscriptionPersistence firstRun(recordings, jobs, transcripts);
    const auto started = firstRun.beginNew(descriptor);
    if (!started)
        QFAIL(qPrintable(started.error().diagnosticString()));
    QCOMPARE(started.value().recordingId, QStringLiteral("recording-1"));
    QCOMPARE(started.value().jobId, QStringLiteral("job-1"));
    QVERIFY(!started.value().resumed);
    QVERIFY(firstRun.bindSourceMedia(sourceHash));
    QVERIFY(firstRun.beginNormalization());
    QVERIFY(firstRun.updateNormalizationProgress(0.75));
    QVERIFY(firstRun.beginModelLoad());
    QVERIFY(firstRun.beginSpeechAnalysis());
    QVERIFY(firstRun.replaceChunkPlan(
        {chunk(0, 0, 1'000, 0, 100), chunk(1, 900, 2'000, 100)}));
    QVERIFY(firstRun.beginTranscription());

    const auto firstChunk = firstRun.beginChunk(0);
    QVERIFY(firstChunk);
    QCOMPARE(firstChunk.value().attempts, 1);
    QVERIFY(firstRun.saveChunkSegments(0, {segment(0, 700, QStringLiteral("BreezeDesk 專案"))}, true));
    QVERIFY(firstRun.completeChunk(0, {segment(0, 700, QStringLiteral("BreezeDesk 專案"))}));

    const auto secondChunk = firstRun.beginChunk(1);
    QVERIFY(secondChunk);
    QCOMPARE(secondChunk.value().attempts, 1);
    QVERIFY(firstRun.saveChunkSegments(1, {segment(900, 1'250, QStringLiteral("partial result"))}, true));
    QVERIFY(firstRun.interrupt(QStringLiteral("worker exited")));

    const auto interruptedJob = jobs.findById(QStringLiteral("job-1"));
    QVERIFY(interruptedJob && interruptedJob.value());
    QCOMPARE(interruptedJob.value()->state, JobState::Interrupted);
    const auto interruptedChunks = jobs.chunks(QStringLiteral("job-1"));
    QVERIFY(interruptedChunks);
    QCOMPARE(interruptedChunks.value().at(0).state, ChunkState::Completed);
    QCOMPARE(interruptedChunks.value().at(1).state, ChunkState::Interrupted);
    const auto checkpointed = transcripts.segmentsForJob(QStringLiteral("job-1"), true);
    QVERIFY(checkpointed);
    QCOMPARE(checkpointed.value().size(), 2);
    QVERIFY(!checkpointed.value().at(0).provisional);
    QVERIFY(checkpointed.value().at(1).provisional);

    CliTranscriptionPersistence wrongSource(recordings, jobs, transcripts);
    const auto rejected = wrongSource.resume(
        QStringLiteral("job-1"), directory.filePath(QStringLiteral("different.wav")));
    QVERIFY(!rejected);

    CliTranscriptionPersistence resumedRun(recordings, jobs, transcripts);
    const auto resumed = resumedRun.resume(QStringLiteral("job-1"), sourcePath);
    if (!resumed)
        QFAIL(qPrintable(resumed.error().diagnosticString()));
    QVERIFY(resumed.value().resumed);
    QCOMPARE(resumed.value().chunks.at(0).state, ChunkState::Completed);
    QCOMPARE(resumed.value().chunks.at(1).state, ChunkState::Pending);
    QCOMPARE(resumed.value().chunks.at(1).attempts, 1);
    const auto changedSource = resumedRun.bindSourceMedia(QString(64, QLatin1Char('b')));
    QVERIFY(!changedSource);
    QCOMPARE(changedSource.error().code, ErrorCode::InvalidStateTransition);
    QVERIFY(resumedRun.bindSourceMedia(sourceHash));
    QVERIFY(resumedRun.beginModelLoad());
    QVERIFY(resumedRun.beginTranscription());

    const auto retriedChunk = resumedRun.beginChunk(1);
    QVERIFY(retriedChunk);
    QCOMPARE(retriedChunk.value().attempts, 2);
    QVERIFY(resumedRun.saveChunkSegments(1, {segment(900, 1'900, QStringLiteral("final result"))}, true));
    QVERIFY(resumedRun.completeChunk(1, {segment(900, 1'900, QStringLiteral("final result"))}));
    QVERIFY(resumedRun.complete());

    const auto completedJob = jobs.findById(QStringLiteral("job-1"));
    QVERIFY(completedJob && completedJob.value());
    QCOMPARE(completedJob.value()->state, JobState::Completed);
    QCOMPARE(completedJob.value()->progress, 1.0);
    QCOMPARE(completedJob.value()->lastCompletedChunk, 1);
    const auto completedRecording = recordings.findById(QStringLiteral("recording-1"));
    QVERIFY(completedRecording && completedRecording.value());
    QCOMPARE(completedRecording.value()->activeJobId, QStringLiteral("job-1"));
    const auto finalSegments = transcripts.segmentsForJob(QStringLiteral("job-1"), true);
    QVERIFY(finalSegments);
    QCOMPARE(finalSegments.value().size(), 2);
    QVERIFY(std::all_of(finalSegments.value().cbegin(), finalSegments.value().cend(),
                        [](const TranscriptSegment& value) { return !value.provisional; }));
    QCOMPARE(finalSegments.value().at(1).originalText, QStringLiteral("final result"));
    QCOMPARE(finalSegments.value().at(1).attempt, 2);
}

void CliTranscriptionPersistenceTest::bindsSourceAfterPreparingInterruption() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("interrupted-hash.mp4"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-interrupted-hash");
    descriptor.recording.title = QStringLiteral("Interrupted while hashing");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.recording.durationMs = 1'000;
    descriptor.job.id = QStringLiteral("job-interrupted-hash");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 1'000)};

    CliTranscriptionPersistence firstRun(recordings, jobs, transcripts);
    QVERIFY(firstRun.beginNew(descriptor));
    QVERIFY(firstRun.interrupt(QStringLiteral("interrupted while hashing source")));

    CliTranscriptionPersistence resumedRun(recordings, jobs, transcripts);
    QVERIFY(resumedRun.resume(descriptor.job.id, sourcePath));
    const QString sourceHash(64, QLatin1Char('c'));
    QVERIFY(resumedRun.bindSourceMedia(sourceHash));

    const auto savedJob = jobs.findById(descriptor.job.id);
    QVERIFY(savedJob && savedJob.value().has_value());
    QCOMPARE(savedJob.value()->parameters.value(QStringLiteral("sourceHash")).toString(), sourceHash);
    QVERIFY(resumedRun.interrupt(QStringLiteral("test complete")));
}

void CliTranscriptionPersistenceTest::rejectsSourceBindingAfterPreparation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("legacy-unbound.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-legacy-unbound");
    descriptor.recording.title = QStringLiteral("Legacy unbound source");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.recording.durationMs = 1'000;
    descriptor.job.id = QStringLiteral("job-legacy-unbound");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.chunks = {chunk(0, 0, 1'000)};

    CliTranscriptionPersistence firstRun(recordings, jobs, transcripts);
    QVERIFY(firstRun.beginNew(descriptor));
    QVERIFY(firstRun.beginModelLoad());
    QVERIFY(firstRun.beginTranscription());
    QVERIFY(firstRun.interrupt(QStringLiteral("interrupted before first chunk")));

    CliTranscriptionPersistence resumedRun(recordings, jobs, transcripts);
    QVERIFY(resumedRun.resume(descriptor.job.id, sourcePath));
    const auto bound = resumedRun.bindSourceMedia(QString(64, QLatin1Char('d')));
    QVERIFY(!bound);
    QCOMPARE(bound.error().code, ErrorCode::InvalidStateTransition);
    QVERIFY(resumedRun.interrupt(QStringLiteral("test complete")));
}

void CliTranscriptionPersistenceTest::retriesFailedChunkWithoutRepeatingCompletedChunks() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("long.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    DurableTranscriptionDescriptor descriptor;
    descriptor.recording.id = QStringLiteral("recording-failed-resume");
    descriptor.recording.title = QStringLiteral("Long recording");
    descriptor.recording.sourcePath = sourcePath;
    descriptor.recording.normalizedPcmPath = directory.filePath(QStringLiteral("normalized.wav"));
    descriptor.recording.durationMs = 2'000;
    descriptor.job.id = QStringLiteral("job-failed-resume");
    descriptor.job.recordingId = descriptor.recording.id;
    descriptor.job.modelId = QStringLiteral("breeze-asr-25-q5");
    descriptor.chunks = {chunk(0, 0, 1'000), chunk(1, 1'000, 2'000)};

    CliTranscriptionPersistence firstRun(recordings, jobs, transcripts);
    QVERIFY(firstRun.beginNew(descriptor));
    QVERIFY(firstRun.beginModelLoad());
    QVERIFY(firstRun.beginTranscription());
    QVERIFY(firstRun.beginChunk(0));
    QVERIFY(firstRun.completeChunk(0, {segment(0, 900, QStringLiteral("kept"))}));
    QVERIFY(firstRun.beginChunk(1));
    QVERIFY(firstRun.fail(QStringLiteral("InvalidAudio"), QStringLiteral("rounded endpoint")));

    CliTranscriptionPersistence retry(recordings, jobs, transcripts);
    const auto resumed = retry.resume(descriptor.job.id, sourcePath);
    if (!resumed) {
        QFAIL(qPrintable(resumed.error().diagnosticString()));
    }
    QCOMPARE(resumed.value().chunks.at(0).state, ChunkState::Completed);
    QCOMPARE(resumed.value().chunks.at(1).state, ChunkState::Pending);
    QCOMPARE(resumed.value().chunks.at(0).attempts, 1);
    QCOMPARE(resumed.value().chunks.at(1).attempts, 1);
}

void CliTranscriptionPersistenceTest::cancellingLeaseWaitDoesNotInterruptCurrentOwner() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("queued.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    Recording recording;
    recording.id = QStringLiteral("recording-wait");
    recording.title = QStringLiteral("Lease wait");
    recording.sourcePath = sourcePath;
    recording.normalizedPcmPath = directory.filePath(QStringLiteral("published.wav"));
    recording.waveformPath = directory.filePath(QStringLiteral("published.bwpk"));
    recording.sourceHash = QString(64, QLatin1Char('a'));
    QVERIFY(recordings.create(recording));

    TranscriptionJob current;
    current.id = QStringLiteral("current-owner-job");
    current.recordingId = recording.id;
    QVERIFY(jobs.createQueued(current));
    const auto claimed = jobs.claimQueued(current.id, QStringLiteral("gui-owner"));
    QVERIFY(claimed && claimed.value().claimed);

    DurableTranscriptionDescriptor descriptor;
    descriptor.recording = recording;
    descriptor.recording.normalizedPcmPath = directory.filePath(QStringLiteral("future-owner.wav"));
    descriptor.recording.waveformPath.clear();
    descriptor.recording.sourceHash.clear();
    descriptor.job.id = QStringLiteral("waiting-cli-job");
    descriptor.job.recordingId = recording.id;
    descriptor.job.modelId = QStringLiteral("breeze-asr-25-q5");
    descriptor.chunks = {chunk(0, 0, 1'000)};

    int cancellationPolls = 0;
    QStringList waitMessages;
    CliTranscriptionPersistence waiting(
        recordings, jobs, transcripts, QStringLiteral("cli-owner"),
        [&cancellationPolls] { return cancellationPolls++ > 0; },
        [&waitMessages](const QString& message) { waitMessages.append(message); });
    const auto started = waiting.beginNew(descriptor);
    QVERIFY(!started);
    QCOMPARE(started.error().code, ErrorCode::OperationCancelled);
    QCOMPARE(waitMessages.size(), 1);

    const auto currentAfterCancellation = jobs.findById(current.id);
    QVERIFY(currentAfterCancellation && currentAfterCancellation.value().has_value());
    QCOMPARE(currentAfterCancellation.value()->state, JobState::Preparing);
    const auto lease = jobs.activeLease();
    QVERIFY(lease && lease.value().has_value());
    QCOMPARE(lease.value()->ownerToken, QStringLiteral("gui-owner"));
    QCOMPARE(lease.value()->jobId, current.id);

    const auto cancelledWaiter = jobs.findById(descriptor.job.id);
    QVERIFY(cancelledWaiter && cancelledWaiter.value().has_value());
    QCOMPARE(cancelledWaiter.value()->state, JobState::Cancelled);
    const auto preservedRecording = recordings.findById(recording.id);
    QVERIFY(preservedRecording && preservedRecording.value().has_value());
    QCOMPARE(preservedRecording.value()->normalizedPcmPath, recording.normalizedPcmPath);
    QCOMPARE(preservedRecording.value()->waveformPath, recording.waveformPath);
    QCOMPARE(preservedRecording.value()->sourceHash, recording.sourceHash);
}

void CliTranscriptionPersistenceTest::externallyCancelledLeaseWaitDoesNotInterruptCurrentOwner() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("externally-queued.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("fixture"), qint64{7});
    source.close();

    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager database({databasePath});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    Recording recording;
    recording.id = QStringLiteral("recording-external-wait");
    recording.title = QStringLiteral("External lease wait cancellation");
    recording.sourcePath = sourcePath;
    QVERIFY(recordings.create(recording));

    TranscriptionJob current;
    current.id = QStringLiteral("external-wait-current-owner");
    current.recordingId = recording.id;
    QVERIFY(jobs.createQueued(current));
    const auto claimed = jobs.claimQueued(current.id, QStringLiteral("gui-owner"));
    QVERIFY(claimed && claimed.value().claimed);

    DatabaseManager externalDatabase({databasePath});
    QVERIFY(externalDatabase.initialize());
    SqliteJobRepository externalJobs(externalDatabase);
    DurableTranscriptionDescriptor descriptor;
    descriptor.recording = recording;
    descriptor.job.id = QStringLiteral("externally-cancelled-waiter");
    descriptor.job.recordingId = recording.id;
    descriptor.chunks = {chunk(0, 0, 1'000)};

    bool cancellationSent = false;
    CliTranscriptionPersistence waiting(recordings, jobs, transcripts, QStringLiteral("waiting-owner"),
                                        [&externalJobs, &cancellationSent, jobId = descriptor.job.id] {
                                            if (!cancellationSent) {
                                                cancellationSent = true;
                                                const auto cancelled = JobQueue(externalJobs).cancel(jobId);
                                                return !cancelled;
                                            }
                                            return false;
                                        });
    const auto started = waiting.beginNew(descriptor);
    QVERIFY(!started);
    QCOMPARE(started.error().code, ErrorCode::JobCancelled);
    QVERIFY(cancellationSent);
    QVERIFY(!waiting.isActive());

    const auto currentAfterCancellation = jobs.findById(current.id);
    QVERIFY(currentAfterCancellation && currentAfterCancellation.value().has_value());
    QCOMPARE(currentAfterCancellation.value()->state, JobState::Preparing);
    const auto lease = jobs.activeLease();
    QVERIFY(lease && lease.value().has_value());
    QCOMPARE(lease.value()->jobId, current.id);
    QCOMPARE(lease.value()->ownerToken, QStringLiteral("gui-owner"));
    QCOMPARE(jobs.findById(descriptor.job.id).value()->state, JobState::Cancelled);
}

QTEST_GUILESS_MAIN(CliTranscriptionPersistenceTest)
#include "tst_CliTranscriptionPersistence.moc"
