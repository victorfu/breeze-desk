#include "breezedesk/cli/CliTranscriptionPersistence.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/jobs/SqliteJobRepository.h"
#include "breezedesk/transcript/SqliteTranscriptRepository.h"

#include <QFile>
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
    void retriesFailedChunkWithoutRepeatingCompletedChunks();
    void cancellingLeaseWaitDoesNotInterruptCurrentOwner();
};

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

    CliTranscriptionPersistence firstRun(recordings, jobs, transcripts);
    const auto started = firstRun.beginNew(descriptor);
    if (!started)
        QFAIL(qPrintable(started.error().diagnosticString()));
    QCOMPARE(started.value().recordingId, QStringLiteral("recording-1"));
    QCOMPARE(started.value().jobId, QStringLiteral("job-1"));
    QVERIFY(!started.value().resumed);
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
