#include "breezedesk/cli/CliTranscriptionPersistence.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
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
               const qint64 overlapBeforeMs = 0) {
    JobChunk value;
    value.ordinal = ordinal;
    value.startMs = startMs;
    value.endMs = endMs;
    value.overlapBeforeMs = overlapBeforeMs;
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
    void checkpointsPartialResultsAndResumesOnlyUnfinishedChunks();
    void bindsSourceAfterPreparingInterruption();
    void rejectsSourceBindingAfterPreparation();
    void synchronizesDecodedAudioDurationBeforeTranscription();
    void retriesFailedChunkWithoutRepeatingCompletedChunks();
    void beginsAfterRecoveringUnleasedRunningJob();
    void resumesAnUnleasedRunningJobAfterRecovery();
    void newSessionCheckpointFailureRetainsItsLease();
    void resumedSessionCheckpointFailureRetainsItsLease();
    void cancellingLeaseWaitDoesNotInterruptCurrentOwner();
};

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
    QVERIFY(rejectCheckpoint.exec(QStringLiteral(
        "CREATE TRIGGER reject_new_session_checkpoint BEFORE UPDATE ON transcription_jobs "
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
    const auto result = resumed.resume(descriptor.job.id, sourcePath, {});
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
    const auto abandonedClaim = jobs.claimNextQueued(QStringLiteral("dead-cli-owner"));
    QVERIFY(abandonedClaim && abandonedClaim.value().claimed);
    QVERIFY(jobs.transition(abandonedJob.id, JobState::LoadingModel));
    QVERIFY(jobs.transition(abandonedJob.id, JobState::Transcribing));

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
    const auto claimed = jobs.claimNextQueued(QStringLiteral("dead-resume-owner"));
    QVERIFY(claimed && claimed.value().claimed);
    QVERIFY(jobs.transition(job.id, JobState::LoadingModel));

    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery removeLease(connection.value());
    QVERIFY(removeLease.exec(QStringLiteral("DELETE FROM asr_execution_lease WHERE resource='asr'")));

    CliTranscriptionPersistence persistence(recordings, jobs, transcripts,
                                             QStringLiteral("resuming-cli-owner"));
    const auto resumed = persistence.resume(job.id, sourcePath, {});
    if (!resumed)
        QFAIL(qPrintable(resumed.error().diagnosticString()));
    QCOMPARE(resumed.value().jobId, job.id);
    QCOMPARE(jobs.findById(job.id).value()->state, JobState::Preparing);
    const auto lease = jobs.activeLease();
    QVERIFY(lease && lease.value().has_value());
    QCOMPARE(lease.value()->ownerToken, QStringLiteral("resuming-cli-owner"));
    QVERIFY(persistence.interrupt(QStringLiteral("test complete")));
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
    descriptor.chunks = {chunk(0, 0, 1'000), chunk(1, 900, 2'000, 100)};
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
    QVERIFY(firstRun.replaceChunkPlan({chunk(0, 0, 1'000), chunk(1, 900, 2'000, 100)}));
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
    const auto rejected =
        wrongSource.resume(QStringLiteral("job-1"), directory.filePath(QStringLiteral("different.wav")),
                           descriptor.recording.normalizedPcmPath);
    QVERIFY(!rejected);

    CliTranscriptionPersistence resumedRun(recordings, jobs, transcripts);
    const auto resumed =
        resumedRun.resume(QStringLiteral("job-1"), sourcePath, descriptor.recording.normalizedPcmPath);
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
    QVERIFY(resumedRun.resume(descriptor.job.id, sourcePath, {}));
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
    QVERIFY(resumedRun.resume(descriptor.job.id, sourcePath, {}));
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
    const auto resumed = retry.resume(descriptor.job.id, sourcePath, descriptor.recording.normalizedPcmPath);
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
    QVERIFY(recordings.create(recording));

    TranscriptionJob current;
    current.id = QStringLiteral("current-owner-job");
    current.recordingId = recording.id;
    QVERIFY(jobs.createQueued(current));
    const auto claimed = jobs.claimQueued(current.id, QStringLiteral("gui-owner"));
    QVERIFY(claimed && claimed.value().claimed);

    DurableTranscriptionDescriptor descriptor;
    descriptor.recording = recording;
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
}

QTEST_GUILESS_MAIN(CliTranscriptionPersistenceTest)
#include "tst_CliTranscriptionPersistence.moc"
