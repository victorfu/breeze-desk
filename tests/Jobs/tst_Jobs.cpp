#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/jobs/JobQueue.h"
#include "breezedesk/jobs/JobRecoveryService.h"
#include "breezedesk/jobs/JobStateMachine.h"
#include "breezedesk/jobs/SqliteJobRepository.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace BreezeDesk;

class JobsTest final : public QObject {
    Q_OBJECT

  private slots:
    void stateMachineRejectsInvalidTransitions();
    void progressNeverMovesBackwards();
    void queuePersistsChunksAndRecoversInterruption();
    void clearingCompletedQueuePreservesJobHistory();
    void runtimeDiagnosticsArePersisted();
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

QTEST_GUILESS_MAIN(JobsTest)
#include "tst_Jobs.moc"
