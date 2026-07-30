#include "breezedesk/app/TranscriptionCoordinator.h"
#include "breezedesk/app/WorkerProcessManager.h"
#include "breezedesk/audio/WaveformGenerator.h"
#include "breezedesk/core/FileHash.h"
#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/glossary/SqliteGlossaryRepository.h"
#include "breezedesk/jobs/JobQueue.h"
#include "breezedesk/jobs/SqliteJobRepository.h"
#include "breezedesk/models/ModelManager.h"
#include "breezedesk/settings/SettingsManagers.h"
#include "breezedesk/settings/SettingsStore.h"
#include "breezedesk/transcript/SqliteTranscriptRepository.h"

#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QScopeGuard>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <limits>

#ifndef BREEZEDESK_COORDINATOR_WORKER_PATH
#error BREEZEDESK_COORDINATOR_WORKER_PATH must name the coordinator test worker
#endif

using namespace BreezeDesk;

namespace {

bool writeFixture(const QString& path, const qsizetype size = 64) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QByteArray(size, '\0')) == size;
}

bool writePcmWaveFixture(const QString& path, const qint64 durationMs) {
    if (durationMs <= 0 || durationMs > std::numeric_limits<quint32>::max() / 32) {
        return false;
    }
    const quint32 dataBytes = static_cast<quint32>(durationMs * 32);
    constexpr quint32 junkBytes = 5;
    const quint32 riffSize = 4U + (8U + 16U) + (8U + junkBytes + 1U) + (8U + dataBytes);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << riffSize;
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32{16} << quint16{1} << quint16{1} << quint32{16'000} << quint32{32'000} << quint16{2}
           << quint16{16};
    stream.writeRawData("JUNK", 4);
    stream << junkBytes;
    stream.writeRawData("abcde", static_cast<int>(junkBytes));
    stream << quint8{0};
    stream.writeRawData("data", 4);
    stream << dataBytes;
    if (stream.status() != QDataStream::Ok || !file.flush()) {
        return false;
    }
    const qint64 dataOffset = file.pos();
    return file.resize(dataOffset + dataBytes);
}

} // namespace

class TranscriptionCoordinatorTest final : public QObject {
    Q_OBJECT

  private slots:
    void snapshotsSharedGlossary();
    void initializationRecoversUnleasedRunningJob();
    void terminalCheckpointFailureRetainsLeaseUntilRecovery();
    void observesExternalLeaseWhileIdlePausedAndReserved();
    void leaseHandoffAbandonsLocalSessionWithoutCheckpoint();
    void leaseHandoffDuringModelLoadUsesRequestScopedBarrier();
    void progressCheckpointFailureKeepsNextJobQueuedBehindWorkerBarrier();
    void completedChunkCheckpointFailureRetainsRunningChunk();
    void modelLoadCancellationKeepsLeaseUntilTerminalEnvelope();
    void forcedCancellationRetriesAfterRestartPolicyIsExhausted();
    void staleConnectionRetryDoesNotKillReplacementWorker();
    void workerCrashContinuesWithNextQueuedJob();
    void staleCancellationGraceDoesNotKillReplacementWorker();
    void externalOwnedRunningJobReceivesCancellationRequest();
    void analyzesLongAudioAndPersistsGlobalSegments();
    void rejectsStaleCacheWhenSourceContentsChange();
    void rejectsResumedChunksBoundToDifferentSource();
    void rejectsStartedPlanThatWouldOmitCanonicalTail();
    void runtimeUnavailableFailsBeforeMediaPreparation();
};

void TranscriptionCoordinatorTest::externalOwnedRunningJobReceivesCancellationRequest() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    ModelManager models;
    WorkerProcessManager worker;

    Recording recording;
    recording.id = QStringLiteral("recording-external-owner");
    recording.title = QStringLiteral("Externally owned job");
    QVERIFY(recordings.create(recording));
    TranscriptionJob job;
    job.id = QStringLiteral("job-external-owner");
    job.recordingId = recording.id;
    QVERIFY(jobs.createQueued(job));
    const auto claimed = jobs.claimQueued(job.id, QStringLiteral("headless-owner"));
    QVERIFY(claimed && claimed.value().claimed);

    TranscriptionCoordinator coordinator(recordings, jobs, transcripts, models, worker);
    QSignalSpy errors(&coordinator, &TranscriptionCoordinator::errorOccurred);
    coordinator.cancel(job.id);
    const auto requested = jobs.findById(job.id);
    QVERIFY(requested && requested.value().has_value());
    QCOMPARE(requested.value()->state, JobState::Cancelling);
    const auto lease = jobs.activeLease();
    QVERIFY(lease && lease.value().has_value());
    QCOMPARE(lease.value()->jobId, job.id);
    QCOMPARE(lease.value()->ownerToken, QStringLiteral("headless-owner"));
    QVERIFY(errors.isEmpty());
}

void TranscriptionCoordinatorTest::snapshotsSharedGlossary() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    SqliteGlossaryRepository glossary(database);
    ModelManager models;
    WorkerProcessManager worker;

    GlossaryTerm enabled;
    enabled.profileId = DefaultGlossaryProfileId;
    enabled.canonicalText = QStringLiteral("BreezeDesk");
    enabled.enabled = true;
    const auto enabledId = glossary.createTerm(enabled);
    QVERIFY(enabledId);
    GlossaryTerm disabled;
    disabled.profileId = DefaultGlossaryProfileId;
    disabled.canonicalText = QStringLiteral("MediaTek");
    disabled.enabled = false;
    const auto disabledId = glossary.createTerm(disabled);
    QVERIFY(disabledId);

    Recording recording;
    recording.id = QStringLiteral("recording-glossary");
    recording.title = QStringLiteral("Glossary snapshot");
    QVERIFY(recordings.create(recording));

    TranscriptionCoordinator coordinator(recordings, jobs, transcripts, models, worker);
    coordinator.setGlossaryRepository(&glossary);
    coordinator.setExternalWorkerReserved(true);
    coordinator.enqueue(QStringLiteral("job-glossary"), recording.id);

    const auto stored = jobs.findById(QStringLiteral("job-glossary"));
    QVERIFY(stored && stored.value().has_value());
    QCOMPARE(stored.value()->glossaryProfileId, DefaultGlossaryProfileId);
    const QJsonArray snapshot = stored.value()->parameters.value(QStringLiteral("glossaryTerms")).toArray();
    QCOMPARE(snapshot.size(), 2);
    QMap<QString, bool> enabledById;
    for (const QJsonValue& value : snapshot) {
        const QJsonObject term = value.toObject();
        enabledById.insert(term.value(QStringLiteral("id")).toString(),
                           term.value(QStringLiteral("enabled")).toBool());
    }
    QVERIFY(enabledById.value(enabledId.value()));
    QVERIFY(!enabledById.value(disabledId.value()));
}

void TranscriptionCoordinatorTest::initializationRecoversUnleasedRunningJob() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    ModelManager models;
    WorkerProcessManager worker;

    Recording recording;
    recording.id = QStringLiteral("recording-orphan-recovery");
    recording.title = QStringLiteral("Orphan recovery");
    QVERIFY(recordings.create(recording));

    TranscriptionJob orphan;
    orphan.id = QStringLiteral("job-orphan-running");
    orphan.recordingId = recording.id;
    QVERIFY(jobs.createQueued(orphan));
    const QString abandonedOwner = QStringLiteral("abandoned-owner");
    const auto claimed = jobs.claimNextQueued(abandonedOwner);
    QVERIFY(claimed && claimed.value().claimed);
    QCOMPARE(claimed.value().job->id, orphan.id);
    QVERIFY(jobs.transition(orphan.id, JobState::LoadingModel, {}, {}, abandonedOwner));
    QVERIFY(jobs.transition(orphan.id, JobState::Transcribing, {}, {}, abandonedOwner));

    JobChunk completed;
    completed.jobId = orphan.id;
    completed.ordinal = 0;
    completed.startMs = 0;
    completed.endMs = 1'000;
    completed.state = ChunkState::Completed;
    JobChunk running;
    running.jobId = orphan.id;
    running.ordinal = 1;
    running.startMs = 1'000;
    running.endMs = 2'000;
    running.state = ChunkState::Running;
    QVERIFY(jobs.replaceChunks(orphan.id, {completed, running}, abandonedOwner));

    TranscriptionJob queued;
    queued.id = QStringLiteral("job-waiting-after-orphan");
    queued.recordingId = recording.id;
    QVERIFY(jobs.createQueued(queued));

    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery removeLease(connection.value());
    QVERIFY(removeLease.exec(QStringLiteral("DELETE FROM asr_execution_lease WHERE resource='asr'")));

    TranscriptionCoordinator coordinator(recordings, jobs, transcripts, models, worker);
    coordinator.setExternalWorkerReserved(true);
    QSignalSpy errors(&coordinator, &TranscriptionCoordinator::errorOccurred);
    coordinator.initialize();

    const auto recovered = jobs.findById(orphan.id);
    QVERIFY(recovered && recovered.value().has_value());
    QCOMPARE(recovered.value()->state, JobState::Interrupted);
    const auto recoveredChunks = jobs.chunks(orphan.id);
    QVERIFY(recoveredChunks);
    QCOMPARE(recoveredChunks.value().size(), 2);
    QCOMPARE(recoveredChunks.value().at(0).state, ChunkState::Completed);
    QCOMPARE(recoveredChunks.value().at(1).state, ChunkState::Interrupted);
    QCOMPARE(jobs.findById(queued.id).value()->state, JobState::Queued);
    QVERIFY(!jobs.activeLease().value().has_value());
    QVERIFY(errors.isEmpty());
}

void TranscriptionCoordinatorTest::terminalCheckpointFailureRetainsLeaseUntilRecovery() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const auto restoreEnvironment = qScopeGuard([previousDataRoot, previousWorkerPath] {
        const auto restore = [](const char* name, const QByteArray& value) {
            if (value.isNull()) {
                qunsetenv(name);
            } else {
                qputenv(name, value);
            }
        };
        restore("BREEZEDESK_DATA_ROOT", previousDataRoot);
        restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
    });
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    QVERIFY(StoragePaths::ensureLayout());

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    ModelManager models;
    WorkerProcessManager worker;

    Recording recording;
    recording.id = QStringLiteral("recording-terminal-checkpoint");
    recording.title = QStringLiteral("Terminal checkpoint failure");
    recording.sourcePath = directory.filePath(QStringLiteral("cancel-before-preparation.wav"));
    QVERIFY(recordings.create(recording));

    TranscriptionCoordinator coordinator(recordings, jobs, transcripts, models, worker);
    QSignalSpy errors(&coordinator, &TranscriptionCoordinator::errorOccurred);
    QSignalSpy finished(&coordinator, &TranscriptionCoordinator::transcriptionFinished);
    coordinator.initialize();
    coordinator.enqueue(QStringLiteral("job-terminal-checkpoint"), recording.id);
    const auto preparing = jobs.findById(QStringLiteral("job-terminal-checkpoint"));
    QVERIFY(preparing && preparing.value().has_value());
    QCOMPARE(preparing.value()->state, JobState::Preparing);

    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery rejectCheckpoint(connection.value());
    QVERIFY(rejectCheckpoint.exec(
        QStringLiteral("CREATE TRIGGER reject_cancel_checkpoint BEFORE UPDATE OF state ON transcription_jobs "
        "WHEN OLD.id='job-terminal-checkpoint' AND NEW.state='Cancelled' BEGIN "
        "SELECT RAISE(ABORT,'forced terminal checkpoint failure'); END")));

    coordinator.cancel(QStringLiteral("job-terminal-checkpoint"));
    const auto cancelling = jobs.findById(QStringLiteral("job-terminal-checkpoint"));
    QVERIFY(cancelling && cancelling.value().has_value());
    QCOMPARE(cancelling.value()->state, JobState::Cancelling);
    const auto retainedLease = jobs.activeLease();
    QVERIFY(retainedLease && retainedLease.value().has_value());
    QCOMPARE(retainedLease.value()->jobId, QStringLiteral("job-terminal-checkpoint"));
    QVERIFY(coordinator.isTranscriptionActive());
    QCOMPARE(finished.size(), 0);
    QVERIFY(!errors.isEmpty());

    QSqlQuery allowCheckpoint(connection.value());
    QVERIFY(allowCheckpoint.exec(QStringLiteral("DROP TRIGGER reject_cancel_checkpoint")));
    const auto cancelled = [&jobs] {
        const auto current = jobs.findById(QStringLiteral("job-terminal-checkpoint"));
        return current && current.value().has_value() && current.value()->state == JobState::Cancelled;
    };
    QTRY_VERIFY_WITH_TIMEOUT(cancelled(), 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(!jobs.activeLease().value().has_value(), 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(!coordinator.isTranscriptionActive(), 5'000);
    QCOMPARE(finished.size(), 1);
}

void TranscriptionCoordinatorTest::observesExternalLeaseWhileIdlePausedAndReserved() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager guiDatabase({databasePath});
    DatabaseManager externalDatabase({databasePath});
    QVERIFY(guiDatabase.initialize());
    QVERIFY(externalDatabase.initialize());
    SqliteRecordingRepository recordings(guiDatabase);
    SqliteJobRepository guiJobs(guiDatabase);
    SqliteJobRepository externalJobs(externalDatabase);
    SqliteTranscriptRepository transcripts(guiDatabase);
    ModelManager models;
    WorkerProcessManager worker;

    Recording recording;
    recording.id = QStringLiteral("recording-idle-external-lease");
    recording.title = QStringLiteral("Idle external lease");
    QVERIFY(recordings.create(recording));

    TranscriptionCoordinator coordinator(recordings, guiJobs, transcripts, models, worker);
    coordinator.initialize();
    coordinator.setPauseAfterCurrent(true);
    coordinator.setExternalWorkerReserved(true);
    const QString localJobId = QStringLiteral("job-local-paused");
    coordinator.enqueue(localJobId, recording.id);
    QCOMPARE(guiJobs.findById(localJobId).value()->state, JobState::Queued);
    QVERIFY(!coordinator.isTranscriptionActive());

    TranscriptionJob externalJob;
    externalJob.id = QStringLiteral("job-external-after-idle");
    externalJob.recordingId = recording.id;
    QVERIFY(externalJobs.createQueued(externalJob));
    const QString externalOwner = QStringLiteral("external-idle-owner");
    const auto claimed = externalJobs.claimQueued(externalJob.id, externalOwner, 10'000);
    QVERIFY(claimed && claimed.value().claimed);

    QTRY_VERIFY_WITH_TIMEOUT(coordinator.isTranscriptionActive(), 3'000);
    QCOMPARE(guiJobs.findById(localJobId).value()->state, JobState::Queued);
    QVERIFY(externalJobs.transition(externalJob.id, JobState::Failed,
                                    QStringLiteral("ExternalTestFinished"),
                                    QStringLiteral("External owner finished."), externalOwner));

    QTRY_VERIFY_WITH_TIMEOUT(!coordinator.isTranscriptionActive(), 3'000);
    QCOMPARE(guiJobs.findById(localJobId).value()->state, JobState::Queued);
}

void TranscriptionCoordinatorTest::leaseHandoffAbandonsLocalSessionWithoutCheckpoint() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const QByteArray previousOverrideOnly = qgetenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY");
    const QByteArray previousRestartState =
        qgetenv("BREEZEDESK_TEST_COORDINATOR_RESTART_STATE");
    const auto restoreEnvironment =
        qScopeGuard([previousDataRoot, previousWorkerPath, previousOverrideOnly,
                     previousRestartState] {
        const auto restore = [](const char* name, const QByteArray& value) {
            if (value.isNull()) {
                qunsetenv(name);
            } else {
                qputenv(name, value);
            }
        };
        restore("BREEZEDESK_DATA_ROOT", previousDataRoot);
        restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
        restore("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", previousOverrideOnly);
        restore("BREEZEDESK_TEST_COORDINATOR_RESTART_STATE", previousRestartState);
    });
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    qputenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", "1");
    qputenv("BREEZEDESK_TEST_COORDINATOR_RESTART_STATE",
            directory.filePath(QStringLiteral("worker-launch-count.txt")).toUtf8());
    QVERIFY(StoragePaths::ensureLayout());

    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager guiDatabase({databasePath});
    DatabaseManager externalDatabase({databasePath});
    QVERIFY(guiDatabase.initialize());
    QVERIFY(externalDatabase.initialize());
    SqliteRecordingRepository recordings(guiDatabase);
    SqliteJobRepository guiJobs(guiDatabase);
    SqliteJobRepository externalJobs(externalDatabase);
    SqliteTranscriptRepository transcripts(guiDatabase);
    ModelManager models;
    WorkerProcessManager worker;
    QVERIFY(writeFixture(models.modelPath(models.defaultModelId())));
    SettingsStore settingsStore(directory.filePath(QStringLiteral("settings.ini")));
    TranscriptionSettingsManager settings(settingsStore);
    settings.setDefaultModelId(models.defaultModelId());
    settings.setVadEnabled(false);

    Recording recording;
    recording.id = QStringLiteral("recording-lease-handoff");
    recording.title = QStringLiteral("Lease handoff");
    recording.sourcePath = directory.filePath(QStringLiteral("lease-handoff-source.mp4"));
    recording.normalizedPcmPath = directory.filePath(QStringLiteral("lease-handoff-normalized.wav"));
    QVERIFY(writeFixture(recording.sourcePath));
    QVERIFY(writePcmWaveFixture(recording.normalizedPcmPath, 2'000));
    recording.sourceHash = FileHash::sha256(recording.sourcePath);
    recording.durationMs = 2'000;
    recording.sampleRate = 16'000;
    recording.channelCount = 1;
    QVERIFY(recordings.create(recording));

    TranscriptionCoordinator coordinator(recordings, guiJobs, transcripts, models, worker, &settings);
    QSignalSpy errors(&coordinator, &TranscriptionCoordinator::errorOccurred);
    QSignalSpy finished(&coordinator, &TranscriptionCoordinator::transcriptionFinished);
    QSignalSpy transcriptChanges(&coordinator, &TranscriptionCoordinator::transcriptChanged);
    QSignalSpy libraryChanges(&coordinator, &TranscriptionCoordinator::libraryChanged);
    QSignalSpy workerInterruptions(&worker, &WorkerProcessManager::workerInterrupted);
    coordinator.initialize();
    const QString jobId = QStringLiteral("job-lease-handoff");
    coordinator.enqueue(jobId, recording.id);
    const auto sawTranscriptChange = [&transcriptChanges, &recording, &jobId](const bool editingLocked) {
        for (const QList<QVariant>& arguments : transcriptChanges) {
            if (arguments.size() == 3 && arguments.at(0).toString() == recording.id &&
                arguments.at(1).toString() == jobId && arguments.at(2).toBool() == editingLocked) {
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(sawTranscriptChange(true), 30'000);
    const auto partials = transcripts.segmentsForJob(jobId, true);
    QVERIFY(partials);
    QCOMPARE(partials.value().size(), 1);
    QVERIFY(partials.value().constFirst().provisional);
    transcriptChanges.clear();
    const auto guiLease = guiJobs.activeLease();
    QVERIFY(guiLease && guiLease.value().has_value());
    const QString staleOwner = guiLease.value()->ownerToken;
    QVERIFY(!staleOwner.isEmpty());
    coordinator.setPauseAfterCurrent(true);

    const auto connection = externalDatabase.connection();
    QVERIFY(connection);
    QSqlQuery expire(connection.value());
    QVERIFY(expire.exec(QStringLiteral(
        "UPDATE asr_execution_lease SET expires_at='2000-01-01T00:00:00.000Z' WHERE resource='asr'")));
    QVERIFY(externalJobs.markRunningJobsInterrupted(QStringLiteral("forced test handoff")));
    QVERIFY(JobQueue(externalJobs).resume(jobId));
    const QString currentOwner = QStringLiteral("external-current-owner");
    QVERIFY(externalJobs.claimQueued(jobId, currentOwner, 10'000).value().claimed);
    QVERIFY(externalJobs.transition(jobId, JobState::LoadingModel, {}, {}, currentOwner));
    QVERIFY(externalJobs.transition(jobId, JobState::Transcribing, {}, {}, currentOwner));
    QVERIFY(externalJobs.updateProgress(jobId, JobStage::Transcribing, 0.61, 0, currentOwner));

    const auto reportedLeaseLoss = [&errors] {
        for (const QList<QVariant>& arguments : errors) {
            if (!arguments.isEmpty() &&
                arguments.constFirst().toString().contains(QStringLiteral("lost the global execution lease"),
                                                           Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(reportedLeaseLoss(), 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(sawTranscriptChange(true), 1'000);
    QVERIFY(!sawTranscriptChange(false));
    QVERIFY(worker.forcedCancellationPending());
    QTest::qWait(1'200);

    const auto durable = externalJobs.findById(jobId);
    QVERIFY(durable && durable.value().has_value());
    QCOMPARE(durable.value()->state, JobState::Transcribing);
    QCOMPARE(durable.value()->stage, JobStage::Transcribing);
    QCOMPARE(durable.value()->progress, 0.61);
    const auto currentLease = externalJobs.activeLease();
    QVERIFY(currentLease && currentLease.value().has_value());
    QCOMPARE(currentLease.value()->jobId, jobId);
    QCOMPARE(currentLease.value()->ownerToken, currentOwner);
    QVERIFY(externalJobs.renewLease(jobId, currentOwner, 10'000));
    QCOMPARE(finished.size(), 0);
    QVERIFY(sawTranscriptChange(true));
    QVERIFY(!guiJobs.transition(jobId, JobState::Interrupted, QStringLiteral("StaleOwner"),
                                QStringLiteral("stale checkpoint"), staleOwner));

    transcriptChanges.clear();
    QVERIFY(externalJobs.transition(jobId, JobState::Finalizing, {}, {}, currentOwner));
    QVERIFY(externalJobs.completeAndActivate(recording.id, jobId, currentOwner));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5'000);
    QCOMPARE(finished.constFirst().at(0).toString(), recording.id);
    QCOMPARE(finished.constFirst().at(1).toString(), jobId);
    QVERIFY(finished.constFirst().at(2).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!transcriptChanges.isEmpty(), 1'000);
    const QList<QVariant>& unlocked = transcriptChanges.constLast();
    QCOMPARE(unlocked.at(0).toString(), recording.id);
    QCOMPARE(unlocked.at(1).toString(), jobId);
    QVERIFY(!unlocked.at(2).toBool());
    QCOMPARE(libraryChanges.size(), 1);
    QVERIFY(!coordinator.isTranscriptionActive());

    const QString nextJobId = QStringLiteral("job-after-lease-handoff");
    coordinator.enqueue(nextJobId, recording.id);
    coordinator.setPauseAfterCurrent(false);
    QTRY_VERIFY_WITH_TIMEOUT(!workerInterruptions.isEmpty(), 7'000);
    if (worker.forcedCancellationPending()) {
        const auto fencedJob = guiJobs.findById(nextJobId);
        QVERIFY(fencedJob && fencedJob.value().has_value());
        QCOMPARE(fencedJob.value()->state, JobState::Queued);
    }
    QTRY_VERIFY_WITH_TIMEOUT(worker.isReady(), 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(!worker.forcedCancellationPending(), 1'000);
    const auto nextJobCompleted = [&guiJobs, &nextJobId] {
        const auto current = guiJobs.findById(nextJobId);
        return current && current.value().has_value() && current.value()->state == JobState::Completed;
    };
    QTRY_VERIFY_WITH_TIMEOUT(nextJobCompleted(), 10'000);
    const auto nextJob = guiJobs.findById(nextJobId);
    QVERIFY(nextJob && nextJob.value().has_value());
    QCOMPARE(nextJob.value()->state, JobState::Completed);
}

void TranscriptionCoordinatorTest::leaseHandoffDuringModelLoadUsesRequestScopedBarrier() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const QByteArray previousOverrideOnly =
        qgetenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY");
    const QByteArray previousDeferredModelState =
        qgetenv("BREEZEDESK_TEST_COORDINATOR_DEFER_MODEL_LOAD_STATE");
    const auto restoreEnvironment =
        qScopeGuard([previousDataRoot, previousWorkerPath, previousOverrideOnly,
                     previousDeferredModelState] {
            const auto restore = [](const char* name, const QByteArray& value) {
                if (value.isNull()) {
                    qunsetenv(name);
                } else {
                    qputenv(name, value);
                }
            };
            restore("BREEZEDESK_DATA_ROOT", previousDataRoot);
            restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
            restore("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", previousOverrideOnly);
            restore("BREEZEDESK_TEST_COORDINATOR_DEFER_MODEL_LOAD_STATE",
                    previousDeferredModelState);
        });
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    qputenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", "1");
    const QString deferredModelState =
        directory.filePath(QStringLiteral("deferred-model-request.txt"));
    qputenv("BREEZEDESK_TEST_COORDINATOR_DEFER_MODEL_LOAD_STATE",
            deferredModelState.toUtf8());
    QVERIFY(StoragePaths::ensureLayout());

    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager guiDatabase({databasePath});
    DatabaseManager externalDatabase({databasePath});
    QVERIFY(guiDatabase.initialize());
    QVERIFY(externalDatabase.initialize());
    SqliteRecordingRepository recordings(guiDatabase);
    SqliteJobRepository guiJobs(guiDatabase);
    SqliteJobRepository externalJobs(externalDatabase);
    SqliteTranscriptRepository transcripts(guiDatabase);
    ModelManager models;
    WorkerProcessManager worker;
    QVERIFY(writeFixture(models.modelPath(models.defaultModelId())));
    SettingsStore settingsStore(directory.filePath(QStringLiteral("settings.ini")));
    TranscriptionSettingsManager settings(settingsStore);
    settings.setDefaultModelId(models.defaultModelId());
    settings.setVadEnabled(false);

    Recording recording;
    recording.id = QStringLiteral("recording-model-load-handoff");
    recording.title = QStringLiteral("Model load handoff");
    recording.sourcePath = directory.filePath(QStringLiteral("model-load-source.mp4"));
    recording.normalizedPcmPath = directory.filePath(QStringLiteral("model-load-normalized.wav"));
    QVERIFY(writeFixture(recording.sourcePath));
    QVERIFY(writePcmWaveFixture(recording.normalizedPcmPath, 2'000));
    recording.sourceHash = FileHash::sha256(recording.sourcePath);
    recording.durationMs = 2'000;
    recording.sampleRate = 16'000;
    recording.channelCount = 1;
    QVERIFY(recordings.create(recording));

    TranscriptionCoordinator coordinator(recordings, guiJobs, transcripts, models, worker, &settings);
    QSignalSpy errors(&coordinator, &TranscriptionCoordinator::errorOccurred);
    QSignalSpy workerInterruptions(&worker, &WorkerProcessManager::workerInterrupted);
    QSignalSpy barrierSettled(&worker, &WorkerProcessManager::forcedCancellationSettled);
    coordinator.initialize();
    const QString jobId = QStringLiteral("job-model-load-handoff");
    coordinator.enqueue(jobId, recording.id);
    const auto modelLoadWasDeferred = [&deferredModelState] {
        QFile state(deferredModelState);
        return state.open(QIODevice::ReadOnly) && !state.readAll().trimmed().isEmpty();
    };
    QTRY_VERIFY_WITH_TIMEOUT(modelLoadWasDeferred(), 30'000);
    QCOMPARE(guiJobs.findById(jobId).value()->state, JobState::LoadingModel);
    coordinator.setPauseAfterCurrent(true);

    const auto connection = externalDatabase.connection();
    QVERIFY(connection);
    QSqlQuery expire(connection.value());
    QVERIFY(expire.exec(QStringLiteral(
        "UPDATE asr_execution_lease SET expires_at='2000-01-01T00:00:00.000Z' WHERE resource='asr'")));
    QVERIFY(externalJobs.markRunningJobsInterrupted(QStringLiteral("forced model-load handoff")));
    QVERIFY(JobQueue(externalJobs).resume(jobId));
    const QString currentOwner = QStringLiteral("external-model-load-owner");
    const auto claimed = externalJobs.claimQueued(jobId, currentOwner, 10'000);
    QVERIFY(claimed && claimed.value().claimed);
    QVERIFY(externalJobs.transition(jobId, JobState::LoadingModel, {}, {}, currentOwner));

    const auto reportedLeaseLoss = [&errors] {
        for (const QList<QVariant>& arguments : errors) {
            if (!arguments.isEmpty() &&
                arguments.constFirst().toString().contains(
                    QStringLiteral("lost the global execution lease"), Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(reportedLeaseLoss(), 5'000);
    QVERIFY(worker.forcedCancellationPending());
    QTRY_COMPARE_WITH_TIMEOUT(workerInterruptions.size(), 1, 2'000);

    QTRY_VERIFY_WITH_TIMEOUT(!worker.forcedCancellationPending(), 5'000);
    QCOMPARE(barrierSettled.size(), 1);
    QVERIFY(worker.isReady());
    QCOMPARE(workerInterruptions.size(), 1);
    const auto durableLease = externalJobs.activeLease();
    QVERIFY(durableLease && durableLease.value().has_value());
    QCOMPARE(durableLease.value()->jobId, jobId);
    QCOMPARE(durableLease.value()->ownerToken, currentOwner);
}

void TranscriptionCoordinatorTest::progressCheckpointFailureKeepsNextJobQueuedBehindWorkerBarrier() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const QByteArray previousOverrideOnly =
        qgetenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY");
    const auto restoreEnvironment =
        qScopeGuard([previousDataRoot, previousWorkerPath, previousOverrideOnly] {
            const auto restore = [](const char* name, const QByteArray& value) {
                if (value.isNull()) {
                    qunsetenv(name);
                } else {
                    qputenv(name, value);
                }
            };
            restore("BREEZEDESK_DATA_ROOT", previousDataRoot);
            restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
            restore("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", previousOverrideOnly);
        });
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    qputenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", "1");
    QVERIFY(StoragePaths::ensureLayout());

    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager guiDatabase({databasePath});
    DatabaseManager externalDatabase({databasePath});
    QVERIFY(guiDatabase.initialize());
    QVERIFY(externalDatabase.initialize());
    SqliteRecordingRepository recordings(guiDatabase);
    SqliteJobRepository jobs(guiDatabase);
    SqliteJobRepository externalJobs(externalDatabase);
    SqliteTranscriptRepository transcripts(guiDatabase);
    ModelManager models;
    WorkerProcessManager worker;
    QVERIFY(writeFixture(models.modelPath(models.defaultModelId())));
    SettingsStore settingsStore(directory.filePath(QStringLiteral("settings.ini")));
    TranscriptionSettingsManager settings(settingsStore);
    settings.setDefaultModelId(models.defaultModelId());
    settings.setVadEnabled(false);

    Recording recording;
    recording.id = QStringLiteral("recording-progress-checkpoint-failure");
    recording.title = QStringLiteral("Progress checkpoint failure");
    recording.sourcePath = directory.filePath(QStringLiteral("progress-checkpoint-source.mp4"));
    recording.normalizedPcmPath =
        directory.filePath(QStringLiteral("progress-checkpoint-normalized.wav"));
    QVERIFY(writeFixture(recording.sourcePath));
    QVERIFY(writePcmWaveFixture(recording.normalizedPcmPath, 2'000));
    recording.sourceHash = FileHash::sha256(recording.sourcePath);
    recording.durationMs = 2'000;
    recording.sampleRate = 16'000;
    recording.channelCount = 1;
    QVERIFY(recordings.create(recording));

    const auto connection = guiDatabase.connection();
    QVERIFY(connection);
    QSqlQuery rejectProgress(connection.value());
    QVERIFY(rejectProgress.exec(QStringLiteral(
        "CREATE TRIGGER reject_transcription_progress BEFORE UPDATE OF progress ON transcription_jobs "
        "WHEN OLD.id='job-progress-checkpoint-failure' AND NEW.stage='Transcribing' "
        "AND NEW.progress>0.35 BEGIN SELECT RAISE(ABORT,'forced progress checkpoint failure'); END")));
    QSqlQuery rejectFailedChunk(connection.value());
    QVERIFY(rejectFailedChunk.exec(QStringLiteral(
        "CREATE TRIGGER reject_failed_chunk_checkpoint BEFORE UPDATE OF state ON job_chunks "
        "WHEN OLD.job_id='job-progress-checkpoint-failure' AND NEW.state='Failed' "
        "BEGIN SELECT RAISE(ABORT,'forced chunk checkpoint failure'); END")));

    TranscriptionCoordinator coordinator(recordings, jobs, transcripts, models, worker, &settings);
    QSignalSpy workerInterruptions(&worker, &WorkerProcessManager::workerInterrupted);
    coordinator.initialize();
    const QString failedJobId = QStringLiteral("job-progress-checkpoint-failure");
    coordinator.enqueue(failedJobId, recording.id);
    const auto retainedRunningCheckpoint = [&jobs, &worker, &failedJobId] {
        const auto current = jobs.findById(failedJobId);
        const auto chunks = jobs.chunks(failedJobId);
        const auto lease = jobs.activeLease();
        return current && current.value().has_value() &&
               current.value()->state == JobState::Transcribing && chunks &&
               chunks.value().size() == 1 && chunks.value().constFirst().state == ChunkState::Running &&
               lease && lease.value().has_value() && lease.value()->jobId == failedJobId &&
               worker.forcedCancellationPending();
    };
    QTRY_VERIFY_WITH_TIMEOUT(retainedRunningCheckpoint(), 30'000);
    const auto retainedLease = externalJobs.activeLease();
    QVERIFY(retainedLease && retainedLease.value().has_value());
    const QString retainedOwner = retainedLease.value()->ownerToken;
    QVERIFY(!retainedOwner.isEmpty());
    coordinator.setPauseAfterCurrent(true);

    const QString nextJobId = QStringLiteral("job-after-progress-checkpoint-failure");
    coordinator.enqueue(nextJobId, recording.id);
    QCOMPARE(jobs.findById(nextJobId).value()->state, JobState::Queued);
    const QString externalOwner = QStringLiteral("external-progress-barrier-owner");
    const auto blockedClaim = externalJobs.claimQueued(nextJobId, externalOwner, 10'000);
    QVERIFY(blockedClaim);
    QVERIFY(!blockedClaim.value().claimed);
    QCOMPARE(blockedClaim.value().activeJobId, failedJobId);
    const auto stillRetainedLease = externalJobs.activeLease();
    QVERIFY(stillRetainedLease && stillRetainedLease.value().has_value());
    QCOMPARE(stillRetainedLease.value()->jobId, failedJobId);
    QCOMPARE(stillRetainedLease.value()->ownerToken, retainedOwner);
    QCOMPARE(jobs.findById(failedJobId).value()->state, JobState::Transcribing);
    QCOMPARE(jobs.chunks(failedJobId).value().constFirst().state, ChunkState::Running);

    QSqlQuery allowFailedChunk(connection.value());
    QVERIFY(allowFailedChunk.exec(QStringLiteral("DROP TRIGGER reject_failed_chunk_checkpoint")));
    const auto terminalCheckpointCompleted = [&jobs, &failedJobId] {
        const auto current = jobs.findById(failedJobId);
        const auto chunks = jobs.chunks(failedJobId);
        const auto lease = jobs.activeLease();
        return current && current.value().has_value() && current.value()->state == JobState::Failed &&
               chunks && chunks.value().size() == 1 &&
               chunks.value().constFirst().state == ChunkState::Failed && lease &&
               !lease.value().has_value();
    };
    QTRY_VERIFY_WITH_TIMEOUT(!workerInterruptions.isEmpty(), 7'000);
    QTRY_VERIFY_WITH_TIMEOUT(terminalCheckpointCompleted(), 3'000);
    const auto queued = jobs.findById(nextJobId);
    QVERIFY(queued && queued.value().has_value());
    QCOMPARE(queued.value()->state, JobState::Queued);
    const auto claimedAfterQuiescence =
        externalJobs.claimQueued(nextJobId, externalOwner, 10'000);
    QVERIFY(claimedAfterQuiescence && claimedAfterQuiescence.value().claimed);
}

void TranscriptionCoordinatorTest::completedChunkCheckpointFailureRetainsRunningChunk() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const QByteArray previousOverrideOnly =
        qgetenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY");
    const auto restoreEnvironment =
        qScopeGuard([previousDataRoot, previousWorkerPath, previousOverrideOnly] {
            const auto restore = [](const char* name, const QByteArray& value) {
                if (value.isNull()) {
                    qunsetenv(name);
                } else {
                    qputenv(name, value);
                }
            };
            restore("BREEZEDESK_DATA_ROOT", previousDataRoot);
            restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
            restore("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", previousOverrideOnly);
        });
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    qputenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", "1");
    QVERIFY(StoragePaths::ensureLayout());

    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager guiDatabase({databasePath});
    DatabaseManager externalDatabase({databasePath});
    QVERIFY(guiDatabase.initialize());
    QVERIFY(externalDatabase.initialize());
    SqliteRecordingRepository recordings(guiDatabase);
    SqliteJobRepository guiJobs(guiDatabase);
    SqliteJobRepository externalJobs(externalDatabase);
    SqliteTranscriptRepository transcripts(guiDatabase);
    ModelManager models;
    WorkerProcessManager worker;
    QVERIFY(writeFixture(models.modelPath(models.defaultModelId())));
    SettingsStore settingsStore(directory.filePath(QStringLiteral("settings.ini")));
    TranscriptionSettingsManager settings(settingsStore);
    settings.setDefaultModelId(models.defaultModelId());
    settings.setVadEnabled(false);

    Recording recording;
    recording.id = QStringLiteral("recording-completed-chunk-checkpoint-failure");
    recording.title = QStringLiteral("Completed chunk checkpoint failure");
    recording.sourcePath = directory.filePath(QStringLiteral("completed-chunk-source.mp4"));
    recording.normalizedPcmPath =
        directory.filePath(QStringLiteral("completed-chunk-normalized.wav"));
    QVERIFY(writeFixture(recording.sourcePath));
    QVERIFY(writePcmWaveFixture(recording.normalizedPcmPath, 2'000));
    recording.sourceHash = FileHash::sha256(recording.sourcePath);
    recording.durationMs = 2'000;
    recording.sampleRate = 16'000;
    recording.channelCount = 1;
    QVERIFY(recordings.create(recording));

    const auto connection = guiDatabase.connection();
    QVERIFY(connection);
    QSqlQuery rejectCompletedChunk(connection.value());
    QVERIFY(rejectCompletedChunk.exec(QStringLiteral(
        "CREATE TRIGGER reject_completed_chunk_checkpoint BEFORE UPDATE OF state ON job_chunks "
        "WHEN OLD.job_id='job-completed-chunk-checkpoint-failure' AND NEW.state='Completed' "
        "BEGIN SELECT RAISE(ABORT,'forced completed chunk checkpoint failure'); END")));
    QSqlQuery rejectFailedChunk(connection.value());
    QVERIFY(rejectFailedChunk.exec(QStringLiteral(
        "CREATE TRIGGER reject_failed_after_completed_checkpoint BEFORE UPDATE OF state ON job_chunks "
        "WHEN OLD.job_id='job-completed-chunk-checkpoint-failure' AND NEW.state='Failed' "
        "BEGIN SELECT RAISE(ABORT,'forced failed chunk checkpoint failure'); END")));

    TranscriptionCoordinator coordinator(recordings, guiJobs, transcripts, models, worker,
                                         &settings);
    QSignalSpy finished(&coordinator, &TranscriptionCoordinator::transcriptionFinished);
    coordinator.initialize();
    const QString failedJobId = QStringLiteral("job-completed-chunk-checkpoint-failure");
    coordinator.enqueue(failedJobId, recording.id);
    const auto rejectedCompletionRetainsLease =
        [&guiJobs, &transcripts, &failedJobId] {
            const auto current = guiJobs.findById(failedJobId);
            const auto chunks = guiJobs.chunks(failedJobId);
            const auto segments = transcripts.segmentsForJob(failedJobId, true);
            const auto lease = guiJobs.activeLease();
            return current && current.value().has_value() &&
                   current.value()->state == JobState::Transcribing && chunks &&
                   chunks.value().size() == 1 &&
                   chunks.value().constFirst().state == ChunkState::Running && segments &&
                   segments.value().size() == 1 && !segments.value().constFirst().provisional &&
                   lease && lease.value().has_value() && lease.value()->jobId == failedJobId;
        };
    QTRY_VERIFY_WITH_TIMEOUT(rejectedCompletionRetainsLease(), 30'000);
    QVERIFY(!worker.forcedCancellationPending());
    const auto retainedLease = externalJobs.activeLease();
    QVERIFY(retainedLease && retainedLease.value().has_value());
    const QString retainedOwner = retainedLease.value()->ownerToken;
    QVERIFY(!retainedOwner.isEmpty());

    coordinator.setPauseAfterCurrent(true);
    const QString nextJobId = QStringLiteral("job-after-completed-chunk-checkpoint-failure");
    coordinator.enqueue(nextJobId, recording.id);
    const QString externalOwner = QStringLiteral("external-completed-chunk-owner");
    const auto blockedClaim = externalJobs.claimQueued(nextJobId, externalOwner, 10'000);
    QVERIFY(blockedClaim && !blockedClaim.value().claimed);
    QCOMPARE(blockedClaim.value().activeJobId, failedJobId);
    QTest::qWait(1'200);
    QVERIFY(rejectedCompletionRetainsLease());
    const auto leaseAfterRetry = externalJobs.activeLease();
    QVERIFY(leaseAfterRetry && leaseAfterRetry.value().has_value());
    QCOMPARE(leaseAfterRetry.value()->ownerToken, retainedOwner);
    QCOMPARE(finished.size(), 0);

    QSqlQuery allowFailedChunk(connection.value());
    QVERIFY(allowFailedChunk.exec(
        QStringLiteral("DROP TRIGGER reject_failed_after_completed_checkpoint")));
    const auto terminalCheckpointCompleted = [&guiJobs, &failedJobId] {
        const auto current = guiJobs.findById(failedJobId);
        const auto chunks = guiJobs.chunks(failedJobId);
        const auto lease = guiJobs.activeLease();
        return current && current.value().has_value() && current.value()->state == JobState::Failed &&
               chunks && chunks.value().size() == 1 &&
               chunks.value().constFirst().state == ChunkState::Failed && lease &&
               !lease.value().has_value();
    };
    QTRY_VERIFY_WITH_TIMEOUT(terminalCheckpointCompleted(), 3'000);
    QCOMPARE(finished.size(), 1);
    QVERIFY(!finished.constFirst().at(2).toBool());
    const auto claimedAfterCheckpoint =
        externalJobs.claimQueued(nextJobId, externalOwner, 10'000);
    QVERIFY(claimedAfterCheckpoint && claimedAfterCheckpoint.value().claimed);
}

void TranscriptionCoordinatorTest::modelLoadCancellationKeepsLeaseUntilTerminalEnvelope() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const QByteArray previousOverrideOnly =
        qgetenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY");
    const QByteArray previousDeferredModelState =
        qgetenv("BREEZEDESK_TEST_COORDINATOR_DEFER_MODEL_LOAD_STATE");
    const auto restoreEnvironment = qScopeGuard(
        [previousDataRoot, previousWorkerPath, previousOverrideOnly,
         previousDeferredModelState] {
            const auto restore = [](const char* name, const QByteArray& value) {
                if (value.isNull()) {
                    qunsetenv(name);
                } else {
                    qputenv(name, value);
                }
            };
            restore("BREEZEDESK_DATA_ROOT", previousDataRoot);
            restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
            restore("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", previousOverrideOnly);
            restore("BREEZEDESK_TEST_COORDINATOR_DEFER_MODEL_LOAD_STATE",
                    previousDeferredModelState);
        });
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    qputenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", "1");
    const QString deferredModelState =
        directory.filePath(QStringLiteral("cancelled-model-request.txt"));
    qputenv("BREEZEDESK_TEST_COORDINATOR_DEFER_MODEL_LOAD_STATE",
            deferredModelState.toUtf8());
    QVERIFY(StoragePaths::ensureLayout());

    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager guiDatabase({databasePath});
    DatabaseManager externalDatabase({databasePath});
    QVERIFY(guiDatabase.initialize());
    QVERIFY(externalDatabase.initialize());
    SqliteRecordingRepository recordings(guiDatabase);
    SqliteJobRepository guiJobs(guiDatabase);
    SqliteJobRepository externalJobs(externalDatabase);
    SqliteTranscriptRepository transcripts(guiDatabase);
    ModelManager models;
    WorkerProcessManager worker;
    QVERIFY(writeFixture(models.modelPath(models.defaultModelId())));
    SettingsStore settingsStore(directory.filePath(QStringLiteral("settings.ini")));
    TranscriptionSettingsManager settings(settingsStore);
    settings.setDefaultModelId(models.defaultModelId());
    settings.setVadEnabled(false);

    Recording recording;
    recording.id = QStringLiteral("recording-cancel-model-load");
    recording.title = QStringLiteral("Cancel model load");
    recording.sourcePath = directory.filePath(QStringLiteral("cancel-model-source.mp4"));
    recording.normalizedPcmPath =
        directory.filePath(QStringLiteral("cancel-model-normalized.wav"));
    QVERIFY(writeFixture(recording.sourcePath));
    QVERIFY(writePcmWaveFixture(recording.normalizedPcmPath, 2'000));
    recording.sourceHash = FileHash::sha256(recording.sourcePath);
    recording.durationMs = 2'000;
    recording.sampleRate = 16'000;
    recording.channelCount = 1;
    QVERIFY(recordings.create(recording));

    TranscriptionCoordinator coordinator(recordings, guiJobs, transcripts, models, worker,
                                         &settings);
    QSignalSpy finished(&coordinator, &TranscriptionCoordinator::transcriptionFinished);
    QSignalSpy barrierSettled(&worker, &WorkerProcessManager::forcedCancellationSettled);
    coordinator.initialize();
    const QString cancelledJobId = QStringLiteral("job-cancel-model-load");
    coordinator.enqueue(cancelledJobId, recording.id);
    const auto modelLoadWasDeferred = [&deferredModelState] {
        QFile state(deferredModelState);
        return state.open(QIODevice::ReadOnly) && !state.readAll().trimmed().isEmpty();
    };
    QTRY_VERIFY_WITH_TIMEOUT(modelLoadWasDeferred(), 30'000);
    QCOMPARE(guiJobs.findById(cancelledJobId).value()->state, JobState::LoadingModel);
    const auto ownedLease = externalJobs.activeLease();
    QVERIFY(ownedLease && ownedLease.value().has_value());
    const QString ownerToken = ownedLease.value()->ownerToken;
    QVERIFY(!ownerToken.isEmpty());

    coordinator.setPauseAfterCurrent(true);
    coordinator.cancel(cancelledJobId);
    QCOMPARE(guiJobs.findById(cancelledJobId).value()->state, JobState::Cancelling);
    QVERIFY(worker.forcedCancellationPending());
    QVERIFY(coordinator.isTranscriptionActive());

    const QString nextJobId = QStringLiteral("job-after-cancelled-model-load");
    coordinator.enqueue(nextJobId, recording.id);
    const QString externalOwner = QStringLiteral("external-model-cancel-owner");
    const auto blockedClaim = externalJobs.claimQueued(nextJobId, externalOwner, 10'000);
    QVERIFY(blockedClaim);
    QVERIFY(!blockedClaim.value().claimed);
    QCOMPARE(blockedClaim.value().activeJobId, cancelledJobId);
    const auto leaseDuringBarrier = externalJobs.activeLease();
    QVERIFY(leaseDuringBarrier && leaseDuringBarrier.value().has_value());
    QCOMPARE(leaseDuringBarrier.value()->jobId, cancelledJobId);
    QCOMPARE(leaseDuringBarrier.value()->ownerToken, ownerToken);

    QTRY_VERIFY_WITH_TIMEOUT(!worker.forcedCancellationPending(), 3'000);
    QCOMPARE(barrierSettled.size(), 1);
    const auto cancelled = [&guiJobs, &cancelledJobId] {
        const auto current = guiJobs.findById(cancelledJobId);
        return current && current.value().has_value() &&
               current.value()->state == JobState::Cancelled;
    };
    QTRY_VERIFY_WITH_TIMEOUT(cancelled(), 3'000);
    QVERIFY(!externalJobs.activeLease().value().has_value());
    QVERIFY(!coordinator.isTranscriptionActive());
    QCOMPARE(finished.size(), 1);
    QVERIFY(!finished.constFirst().at(2).toBool());

    const auto claimedAfterTerminal =
        externalJobs.claimQueued(nextJobId, externalOwner, 10'000);
    QVERIFY(claimedAfterTerminal && claimedAfterTerminal.value().claimed);
}

void TranscriptionCoordinatorTest::forcedCancellationRetriesAfterRestartPolicyIsExhausted() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const QByteArray previousOverrideOnly = qgetenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY");
    const QByteArray previousRecoveryState =
        qgetenv("BREEZEDESK_TEST_COORDINATOR_FORCED_RECOVERY_STATE");
    const QByteArray previousRetryDelay =
        qgetenv("BREEZEDESK_TEST_FORCED_CANCELLATION_RETRY_MS");
    const auto restoreEnvironment = qScopeGuard(
        [previousWorkerPath, previousOverrideOnly, previousRecoveryState, previousRetryDelay] {
            const auto restore = [](const char* name, const QByteArray& value) {
                if (value.isNull()) {
                    qunsetenv(name);
                } else {
                    qputenv(name, value);
                }
            };
            restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
            restore("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", previousOverrideOnly);
            restore("BREEZEDESK_TEST_COORDINATOR_FORCED_RECOVERY_STATE", previousRecoveryState);
            restore("BREEZEDESK_TEST_FORCED_CANCELLATION_RETRY_MS", previousRetryDelay);
        });
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    qputenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", "1");
    const QString recoveryState =
        directory.filePath(QStringLiteral("forced-recovery-launch-count.txt"));
    qputenv("BREEZEDESK_TEST_COORDINATOR_FORCED_RECOVERY_STATE", recoveryState.toUtf8());
    qputenv("BREEZEDESK_TEST_FORCED_CANCELLATION_RETRY_MS", "500");

    WorkerProcessManager worker;
    QSignalSpy restartStopped(&worker, &WorkerProcessManager::automaticRestartStopped);
    QSignalSpy barrierSettled(&worker, &WorkerProcessManager::forcedCancellationSettled);
    QVERIFY2(worker.start(), qPrintable(worker.lastError()));
    QTRY_VERIFY_WITH_TIMEOUT(worker.isReady(), 5'000);
    worker.forceCancelAfterGrace(QStringLiteral("job-restart-exhaustion"),
                                 QStringLiteral("request-restart-exhaustion"));
    QVERIFY(worker.forcedCancellationPending());

    QTRY_VERIFY_WITH_TIMEOUT(!restartStopped.isEmpty(), 5'000);
    QVERIFY(worker.forcedCancellationPending());
    QVERIFY(!worker.isReady());
    QTRY_VERIFY_WITH_TIMEOUT(worker.isReady(), 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(!worker.forcedCancellationPending(), 1'000);
    QCOMPARE(barrierSettled.size(), 1);

    QFile launches(recoveryState);
    QVERIFY(launches.open(QIODevice::ReadOnly));
    QCOMPARE(launches.readAll().trimmed(), QByteArray("5"));
}

void TranscriptionCoordinatorTest::staleConnectionRetryDoesNotKillReplacementWorker() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const QByteArray previousOverrideOnly = qgetenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY");
    const QByteArray previousRetryState =
        qgetenv("BREEZEDESK_TEST_COORDINATOR_CONNECT_RETRY_STATE");
    const auto restoreEnvironment =
        qScopeGuard([previousWorkerPath, previousOverrideOnly, previousRetryState] {
            const auto restore = [](const char* name, const QByteArray& value) {
                if (value.isNull()) {
                    qunsetenv(name);
                } else {
                    qputenv(name, value);
                }
            };
            restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
            restore("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", previousOverrideOnly);
            restore("BREEZEDESK_TEST_COORDINATOR_CONNECT_RETRY_STATE", previousRetryState);
        });
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    qputenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", "1");
    const QString retryState = directory.filePath(QStringLiteral("connect-retry-launch-count.txt"));
    qputenv("BREEZEDESK_TEST_COORDINATOR_CONNECT_RETRY_STATE", retryState.toUtf8());

    WorkerProcessManager worker;
    QSignalSpy workerInterruptions(&worker, &WorkerProcessManager::workerInterrupted);
    QSignalSpy barrierSettled(&worker, &WorkerProcessManager::forcedCancellationSettled);
    bool replacementStartsSucceeded = true;
    QObject::connect(&worker, &WorkerProcessManager::workerInterrupted, &worker,
                     [&worker, &replacementStartsSucceeded] {
                         // Restart synchronously so the first generation's next
                         // 50 ms retry is guaranteed to overlap generation two.
                         replacementStartsSucceeded = worker.start() && replacementStartsSucceeded;
                     });
    QVERIFY2(worker.start(), qPrintable(worker.lastError()));
    QTest::qWait(4'000);
    QVERIFY(!worker.isReady());
    worker.forceCancelAfterGrace(QStringLiteral("job-stale-connect-retry"),
                                 QStringLiteral("request-stale-connect-retry"));
    QVERIFY(worker.forcedCancellationPending());

    QTRY_VERIFY_WITH_TIMEOUT(!workerInterruptions.isEmpty(), 2'000);
    QTRY_VERIFY_WITH_TIMEOUT(worker.isReady(), 6'000);
    QTRY_VERIFY_WITH_TIMEOUT(!worker.forcedCancellationPending(), 1'000);
    QVERIFY(replacementStartsSucceeded);
    QCOMPARE(barrierSettled.size(), 1);
    QCOMPARE(workerInterruptions.size(), 1);

    QFile launches(retryState);
    QVERIFY(launches.open(QIODevice::ReadOnly));
    QCOMPARE(launches.readAll().trimmed(), QByteArray("2"));
}

void TranscriptionCoordinatorTest::workerCrashContinuesWithNextQueuedJob() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const QByteArray previousOverrideOnly = qgetenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY");
    const auto restoreEnvironment =
        qScopeGuard([previousDataRoot, previousWorkerPath, previousOverrideOnly] {
            const auto restore = [](const char* name, const QByteArray& value) {
                if (value.isNull()) {
                    qunsetenv(name);
                } else {
                    qputenv(name, value);
                }
            };
            restore("BREEZEDESK_DATA_ROOT", previousDataRoot);
            restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
            restore("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", previousOverrideOnly);
        });
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    qputenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", "1");
    QVERIFY(StoragePaths::ensureLayout());

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    ModelManager models;
    WorkerProcessManager worker;
    QVERIFY(writeFixture(models.modelPath(models.defaultModelId())));
    SettingsStore settingsStore(directory.filePath(QStringLiteral("settings.ini")));
    TranscriptionSettingsManager settings(settingsStore);
    settings.setDefaultModelId(models.defaultModelId());
    settings.setVadEnabled(false);

    Recording recording;
    recording.id = QStringLiteral("recording-worker-crash");
    recording.title = QStringLiteral("Worker crash recovery");
    recording.sourcePath = directory.filePath(QStringLiteral("worker-crash-source.mp4"));
    recording.normalizedPcmPath = directory.filePath(QStringLiteral("worker-crash-normalized.wav"));
    QVERIFY(writeFixture(recording.sourcePath));
    QVERIFY(writePcmWaveFixture(recording.normalizedPcmPath, 2'000));
    recording.sourceHash = FileHash::sha256(recording.sourcePath);
    recording.durationMs = 2'000;
    recording.sampleRate = 16'000;
    recording.channelCount = 1;
    QVERIFY(recordings.create(recording));

    TranscriptionCoordinator coordinator(recordings, jobs, transcripts, models, worker, &settings);
    QSignalSpy workerInterruptions(&worker, &WorkerProcessManager::workerInterrupted);
    coordinator.initialize();
    const QString crashedJobId = QStringLiteral("job-worker-crash");
    const QString nextJobId = QStringLiteral("job-after-worker-crash");
    coordinator.enqueue(crashedJobId, recording.id);
    coordinator.enqueue(nextJobId, recording.id);

    QTRY_VERIFY_WITH_TIMEOUT(!workerInterruptions.isEmpty(), 30'000);
    const auto firstInterrupted = [&jobs, &crashedJobId] {
        const auto current = jobs.findById(crashedJobId);
        return current && current.value().has_value() &&
               current.value()->state == JobState::Interrupted;
    };
    QTRY_VERIFY_WITH_TIMEOUT(firstInterrupted(), 5'000);
    const auto nextCompleted = [&jobs, &nextJobId] {
        const auto current = jobs.findById(nextJobId);
        return current && current.value().has_value() && current.value()->state == JobState::Completed;
    };
    QTRY_VERIFY_WITH_TIMEOUT(nextCompleted(), 15'000);
}

void TranscriptionCoordinatorTest::staleCancellationGraceDoesNotKillReplacementWorker() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const QByteArray previousOverrideOnly = qgetenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY");
    const QByteArray previousRestartState =
        qgetenv("BREEZEDESK_TEST_COORDINATOR_RESTART_STATE");
    const QByteArray previousStaleGraceState =
        qgetenv("BREEZEDESK_TEST_COORDINATOR_STALE_GRACE_STATE");
    const auto restoreEnvironment = qScopeGuard(
        [previousDataRoot, previousWorkerPath, previousOverrideOnly, previousRestartState,
         previousStaleGraceState] {
            const auto restore = [](const char* name, const QByteArray& value) {
                if (value.isNull()) {
                    qunsetenv(name);
                } else {
                    qputenv(name, value);
                }
            };
            restore("BREEZEDESK_DATA_ROOT", previousDataRoot);
            restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
            restore("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", previousOverrideOnly);
            restore("BREEZEDESK_TEST_COORDINATOR_RESTART_STATE", previousRestartState);
            restore("BREEZEDESK_TEST_COORDINATOR_STALE_GRACE_STATE", previousStaleGraceState);
        });
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    qputenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", "1");
    qunsetenv("BREEZEDESK_TEST_COORDINATOR_RESTART_STATE");
    const QString launchStatePath =
        directory.filePath(QStringLiteral("stale-grace-worker-launch-count.txt"));
    qputenv("BREEZEDESK_TEST_COORDINATOR_STALE_GRACE_STATE", launchStatePath.toUtf8());
    QVERIFY(StoragePaths::ensureLayout());

    const QString databasePath = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager guiDatabase({databasePath});
    DatabaseManager externalDatabase({databasePath});
    QVERIFY(guiDatabase.initialize());
    QVERIFY(externalDatabase.initialize());
    SqliteRecordingRepository recordings(guiDatabase);
    SqliteJobRepository guiJobs(guiDatabase);
    SqliteJobRepository externalJobs(externalDatabase);
    SqliteTranscriptRepository transcripts(guiDatabase);
    ModelManager models;
    WorkerProcessManager worker;
    QVERIFY(writeFixture(models.modelPath(models.defaultModelId())));
    SettingsStore settingsStore(directory.filePath(QStringLiteral("settings.ini")));
    TranscriptionSettingsManager settings(settingsStore);
    settings.setDefaultModelId(models.defaultModelId());
    settings.setVadEnabled(false);

    Recording recording;
    recording.id = QStringLiteral("recording-stale-grace");
    recording.title = QStringLiteral("Stale cancellation grace");
    recording.sourcePath = directory.filePath(QStringLiteral("stale-grace-source.mp4"));
    recording.normalizedPcmPath = directory.filePath(QStringLiteral("stale-grace-normalized.wav"));
    QVERIFY(writeFixture(recording.sourcePath));
    QVERIFY(writePcmWaveFixture(recording.normalizedPcmPath, 2'000));
    recording.sourceHash = FileHash::sha256(recording.sourcePath);
    recording.durationMs = 2'000;
    recording.sampleRate = 16'000;
    recording.channelCount = 1;
    QVERIFY(recordings.create(recording));

    TranscriptionCoordinator coordinator(recordings, guiJobs, transcripts, models, worker, &settings);
    QSignalSpy errors(&coordinator, &TranscriptionCoordinator::errorOccurred);
    QSignalSpy finished(&coordinator, &TranscriptionCoordinator::transcriptionFinished);
    QSignalSpy jobChanges(&coordinator, &TranscriptionCoordinator::jobChanged);
    QSignalSpy workerInterruptions(&worker, &WorkerProcessManager::workerInterrupted);
    coordinator.initialize();

    const QString oldJobId = QStringLiteral("job-stale-grace-handoff");
    coordinator.enqueue(oldJobId, recording.id);
    const auto oldJobHasPartial = [&transcripts, &oldJobId] {
        const auto partials = transcripts.segmentsForJob(oldJobId, true);
        return partials && partials.value().size() == 1 && partials.value().constFirst().provisional;
    };
    QTRY_VERIFY_WITH_TIMEOUT(oldJobHasPartial(), 30'000);
    coordinator.setPauseAfterCurrent(true);

    const auto connection = externalDatabase.connection();
    QVERIFY(connection);
    QSqlQuery expire(connection.value());
    QVERIFY(expire.exec(QStringLiteral(
        "UPDATE asr_execution_lease SET expires_at='2000-01-01T00:00:00.000Z' WHERE resource='asr'")));
    QVERIFY(externalJobs.markRunningJobsInterrupted(QStringLiteral("stale grace handoff")));
    QVERIFY(JobQueue(externalJobs).resume(oldJobId));
    const QString currentOwner = QStringLiteral("external-stale-grace-owner");
    const auto claimed = externalJobs.claimQueued(oldJobId, currentOwner, 10'000);
    QVERIFY(claimed && claimed.value().claimed);
    QVERIFY(externalJobs.transition(oldJobId, JobState::LoadingModel, {}, {}, currentOwner));
    QVERIFY(externalJobs.transition(oldJobId, JobState::Transcribing, {}, {}, currentOwner));
    QVERIFY(externalJobs.updateProgress(oldJobId, JobStage::Transcribing, 0.5, 0, currentOwner));

    const auto reportedLeaseLoss = [&errors] {
        for (const QList<QVariant>& arguments : errors) {
            if (!arguments.isEmpty() &&
                arguments.constFirst().toString().contains(
                    QStringLiteral("lost the global execution lease"), Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(reportedLeaseLoss(), 5'000);
    QVERIFY(worker.forcedCancellationPending());
    QElapsedTimer oldGraceDeadline;
    oldGraceDeadline.start();

    QVERIFY(externalJobs.transition(oldJobId, JobState::Finalizing, {}, {}, currentOwner));
    QVERIFY(externalJobs.completeAndActivate(recording.id, oldJobId, currentOwner));
    const auto oldJobFinished = [&finished, &oldJobId] {
        for (const QList<QVariant>& arguments : finished) {
            if (arguments.size() == 3 && arguments.at(1).toString() == oldJobId &&
                arguments.at(2).toBool()) {
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(oldJobFinished(), 5'000);

    const QString nextJobId = QStringLiteral("job-stale-grace-next");
    coordinator.enqueue(nextJobId, recording.id);
    coordinator.setPauseAfterCurrent(false);
    QTRY_COMPARE_WITH_TIMEOUT(workerInterruptions.size(), 1, 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(worker.isReady(), 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(!worker.forcedCancellationPending(), 1'000);
    const auto nextJobIsTranscribing = [&guiJobs, &nextJobId] {
        const auto current = guiJobs.findById(nextJobId);
        return current && current.value().has_value() &&
               current.value()->state == JobState::Transcribing;
    };
    QTRY_VERIFY_WITH_TIMEOUT(nextJobIsTranscribing(), 10'000);
    QVERIFY2(oldGraceDeadline.elapsed() < 4'500,
             "The replacement job did not begin before the old cancellation grace deadline");

    const auto runningChunks = guiJobs.chunks(nextJobId);
    QVERIFY(runningChunks);
    QCOMPARE(runningChunks.value().size(), 1);
    QCOMPARE(runningChunks.value().constFirst().state, ChunkState::Running);
    const qint64 waitForOldDeadlineMs = 5'300 - oldGraceDeadline.elapsed();
    if (waitForOldDeadlineMs > 0) {
        QTest::qWait(static_cast<int>(waitForOldDeadlineMs));
    }

    const auto afterOldDeadline = guiJobs.findById(nextJobId);
    QVERIFY(afterOldDeadline && afterOldDeadline.value().has_value());
    QCOMPARE(afterOldDeadline.value()->state, JobState::Transcribing);
    const auto chunksAfterOldDeadline = guiJobs.chunks(nextJobId);
    QVERIFY(chunksAfterOldDeadline);
    QCOMPARE(chunksAfterOldDeadline.value().size(), 1);
    QCOMPARE(chunksAfterOldDeadline.value().constFirst().state, ChunkState::Running);
    QCOMPARE(workerInterruptions.size(), 1);

    const auto nextJobCompleted = [&guiJobs, &nextJobId] {
        const auto current = guiJobs.findById(nextJobId);
        return current && current.value().has_value() && current.value()->state == JobState::Completed;
    };
    QTRY_VERIFY_WITH_TIMEOUT(nextJobCompleted(), 8'000);
    QCOMPARE(workerInterruptions.size(), 1);
    for (const QList<QVariant>& arguments : jobChanges) {
        QVERIFY(arguments.size() >= 4);
        QVERIFY(arguments.at(0).toString() != nextJobId ||
                arguments.at(3).toString() != jobStateName(JobState::Interrupted));
    }

    QFile launchState(launchStatePath);
    QVERIFY(launchState.open(QIODevice::ReadOnly));
    QCOMPARE(launchState.readAll().trimmed(), QByteArray("2"));
}

void TranscriptionCoordinatorTest::analyzesLongAudioAndPersistsGlobalSegments() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const auto restoreEnvironment = qScopeGuard([previousDataRoot, previousWorkerPath] {
        if (previousDataRoot.isNull()) {
            qunsetenv("BREEZEDESK_DATA_ROOT");
        } else {
            qputenv("BREEZEDESK_DATA_ROOT", previousDataRoot);
        }
        if (previousWorkerPath.isNull()) {
            qunsetenv("BREEZEDESK_ASR_WORKER_PATH");
        } else {
            qputenv("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
        }
    });
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    QVERIFY(StoragePaths::ensureLayout());

    ModelManager models;
    QVERIFY(models.manifest().find(QStringLiteral("breeze-asr-25-q5")) != nullptr);
    QVERIFY(writeFixture(models.modelPath(QStringLiteral("breeze-asr-25-q5"))));
#ifndef BREEZEDESK_COORDINATOR_VAD_MODEL_PATH
    QSKIP("The verified Silero VAD test model is not available in this build");
#else
    const QString vadFixturePath = QString::fromUtf8(BREEZEDESK_COORDINATOR_VAD_MODEL_PATH);
    QVERIFY2(QFile::copy(vadFixturePath, models.modelPath(QStringLiteral("silero-vad-v6.2.0"))),
             qPrintable(vadFixturePath));
#endif

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    const QString sourcePath = directory.filePath(QStringLiteral("長會議 source.m4a"));
    const QString normalizedPath = directory.filePath(QStringLiteral("長會議 normalized.wav"));
    QVERIFY(writeFixture(sourcePath));
    QVERIFY(writePcmWaveFixture(normalizedPath, 1'300'000));
    Recording recording;
    recording.id = QStringLiteral("recording-coordinator");
    recording.title = QStringLiteral("Long architecture meeting");
    recording.sourcePath = sourcePath;
    recording.normalizedPcmPath = normalizedPath;
    recording.sourceHash = FileHash::sha256(sourcePath);
    recording.durationMs = 1'300'123;
    recording.sampleRate = 16'000;
    recording.channelCount = 1;
    QVERIFY(recordings.create(recording));

    {
        WorkerProcessManager worker;
        TranscriptionCoordinator coordinator(recordings, jobs, transcripts, models, worker);
        QSignalSpy errors(&coordinator, &TranscriptionCoordinator::errorOccurred);
        coordinator.initialize();
        coordinator.enqueue(QStringLiteral("job-coordinator"), recording.id);

        QElapsedTimer timeout;
        timeout.start();
        JobState state = JobState::Queued;
        while (timeout.elapsed() < 60'000) {
            const auto current = jobs.findById(QStringLiteral("job-coordinator"));
            if (current && current.value().has_value()) {
                state = current.value()->state;
                if (state == JobState::Completed || state == JobState::Failed ||
                    state == JobState::Interrupted) {
                    break;
                }
            }
            QTest::qWait(25);
        }
        QStringList errorMessages;
        for (const QList<QVariant>& arguments : errors) {
            errorMessages.append(arguments.constFirst().toString());
        }
        const QString diagnostic =
            QStringLiteral("state=%1 workerError=%2 coordinatorErrors=%3")
                .arg(jobStateName(state), worker.lastError(), errorMessages.join(QStringLiteral(" | ")));
        QVERIFY2(state == JobState::Completed, qPrintable(diagnostic));

        const auto completedJob = jobs.findById(QStringLiteral("job-coordinator"));
        QVERIFY(completedJob && completedJob.value().has_value());
        QCOMPARE(completedJob.value()->backend, QStringLiteral("CPU"));
        QCOMPARE(completedJob.value()->engineVersion, QStringLiteral("fake-whisper-1.2.3"));
        QCOMPARE(completedJob.value()->diagnostics.value(QStringLiteral("selectedBackend")).toString(),
                 QStringLiteral("Auto"));
        QCOMPARE(completedJob.value()->diagnostics.value(QStringLiteral("modelLoadTimeMs")).toInt(), 42);

        const auto chunks = jobs.chunks(QStringLiteral("job-coordinator"));
        QVERIFY(chunks);
        QCOMPARE(chunks.value().size(), 2);
        QCOMPARE(chunks.value().at(0).startMs, 0);
        QCOMPARE(chunks.value().at(0).endMs, 650'000);
        QCOMPARE(chunks.value().at(0).overlapAfterMs, 900);
        QCOMPARE(chunks.value().at(1).startMs, 649'100);
        QCOMPARE(chunks.value().at(1).overlapBeforeMs, 900);
        QCOMPARE(chunks.value().at(1).endMs, 1'300'000);
        QCOMPARE(chunks.value().at(0).state, ChunkState::Completed);
        QCOMPARE(chunks.value().at(1).state, ChunkState::Completed);
        QCOMPARE(chunks.value()
                     .at(0)
                     .diagnostics.value(QStringLiteral("timingsMs"))
                     .toObject()
                     .value(QStringLiteral("encode"))
                     .toDouble(),
                 12.5);

        const auto segments = transcripts.segmentsForJob(QStringLiteral("job-coordinator"), false);
        QVERIFY(segments);
        QCOMPARE(segments.value().size(), 2);
        QCOMPARE(segments.value().at(0).startMs, 100);
        QCOMPARE(segments.value().at(0).endMs, 1'000);
        QCOMPARE(segments.value().at(1).startMs, 649'200);
        QCOMPARE(segments.value().at(1).endMs, 650'100);
        QCOMPARE(segments.value().at(1).originalText, QStringLiteral("second chunk"));

        const auto updatedRecording = recordings.findById(recording.id);
        QVERIFY(updatedRecording && updatedRecording.value().has_value());
        QCOMPARE(updatedRecording.value()->durationMs, 1'300'000);
        QCOMPARE(updatedRecording.value()->activeJobId, QStringLiteral("job-coordinator"));
        QVERIFY(QFileInfo(updatedRecording.value()->waveformPath).isFile());
        QString waveformError;
        const auto waveform = WaveformGenerator::read(updatedRecording.value()->waveformPath, &waveformError);
        QVERIFY2(!waveform.isEmpty(), qPrintable(waveformError));
        QCOMPARE(waveform.first().minimums.size(), 81'250);

        coordinator.enqueue(QStringLiteral("job-cancel"), recording.id);
        JobState cancellationState = JobState::Queued;
        timeout.restart();
        while (timeout.elapsed() < 10'000) {
            const auto current = jobs.findById(QStringLiteral("job-cancel"));
            if (current && current.value().has_value()) {
                cancellationState = current.value()->state;
                if (cancellationState == JobState::AnalyzingSpeech) {
                    break;
                }
            }
            QTest::qWait(25);
        }
        QCOMPARE(cancellationState, JobState::AnalyzingSpeech);
        DatabaseManager externalDatabase({directory.filePath(QStringLiteral("library.sqlite"))});
        QVERIFY(externalDatabase.initialize());
        SqliteJobRepository externalJobs(externalDatabase);
        QVERIFY(JobQueue(externalJobs).cancel(QStringLiteral("job-cancel")));
        QCOMPARE(externalJobs.findById(QStringLiteral("job-cancel")).value()->state, JobState::Cancelling);
        timeout.restart();
        while (timeout.elapsed() < 10'000) {
            const auto current = jobs.findById(QStringLiteral("job-cancel"));
            if (current && current.value().has_value()) {
                cancellationState = current.value()->state;
                if (cancellationState == JobState::Cancelled || cancellationState == JobState::Failed ||
                    cancellationState == JobState::Interrupted) {
                    break;
                }
            }
            QTest::qWait(25);
        }
        QCOMPARE(cancellationState, JobState::Cancelled);
        const auto cancelledChunks = jobs.chunks(QStringLiteral("job-cancel"));
        QVERIFY(cancelledChunks);
        QVERIFY(cancelledChunks.value().isEmpty());

        QString removalError;
        QVERIFY(!models.removeModel(QStringLiteral("breeze-asr-25-q5"), &removalError));
        QVERIFY(removalError.contains(QStringLiteral("currently loaded")));
    }

    QVERIFY(models.removeModel(QStringLiteral("breeze-asr-25-q5")));
    QVERIFY(models.removeModel(QStringLiteral("silero-vad-v6.2.0")));
}

void TranscriptionCoordinatorTest::rejectsStaleCacheWhenSourceContentsChange() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const auto restoreEnvironment = qScopeGuard([previousDataRoot, previousWorkerPath] {
        const auto restore = [](const char* name, const QByteArray& value) {
            if (value.isNull()) {
                qunsetenv(name);
            } else {
                qputenv(name, value);
            }
        };
        restore("BREEZEDESK_DATA_ROOT", previousDataRoot);
        restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
    });
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    QVERIFY(StoragePaths::ensureLayout());

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    ModelManager models;

    const QString sourcePath = directory.filePath(QStringLiteral("replaced-source.mp4"));
    const QString normalizedPath = directory.filePath(QStringLiteral("stale-normalized.wav"));
    const QString waveformPath = directory.filePath(QStringLiteral("stale.waveform"));
    QVERIFY(writeFixture(sourcePath));
    QVERIFY(writePcmWaveFixture(normalizedPath, 1'000));
    QVERIFY(writeFixture(waveformPath));
    Recording recording;
    recording.id = QStringLiteral("recording-stale-cache");
    recording.title = QStringLiteral("Replaced source");
    recording.sourcePath = sourcePath;
    recording.normalizedPcmPath = normalizedPath;
    recording.waveformPath = waveformPath;
    recording.sourceHash = QString(64, QLatin1Char('a'));
    recording.durationMs = 1'000;
    QVERIFY(recordings.create(recording));

    WorkerProcessManager worker;
    TranscriptionCoordinator coordinator(recordings, jobs, transcripts, models, worker);
    coordinator.initialize();
    coordinator.enqueue(QStringLiteral("job-stale-cache"), recording.id);

    const auto jobFailed = [&jobs] {
        const auto current = jobs.findById(QStringLiteral("job-stale-cache"));
        return current && current.value().has_value() && current.value()->state == JobState::Failed;
    };
    QTRY_VERIFY_WITH_TIMEOUT(jobFailed(), 20'000);
    const auto failed = jobs.findById(QStringLiteral("job-stale-cache"));
    QVERIFY(failed && failed.value().has_value());
    QCOMPARE(failed.value()->errorCode, QStringLiteral("UnsupportedMedia"));
    QCOMPARE(failed.value()->parameters.value(QStringLiteral("sourceHash")).toString(),
             FileHash::sha256(sourcePath));

    const auto invalidated = recordings.findById(recording.id);
    QVERIFY(invalidated && invalidated.value().has_value());
    QVERIFY(invalidated.value()->sourceHash.isEmpty());
    QVERIFY(invalidated.value()->normalizedPcmPath.isEmpty());
    QVERIFY(invalidated.value()->waveformPath.isEmpty());
}

void TranscriptionCoordinatorTest::rejectsResumedChunksBoundToDifferentSource() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const auto restoreEnvironment = qScopeGuard([previousDataRoot, previousWorkerPath] {
        const auto restore = [](const char* name, const QByteArray& value) {
            if (value.isNull()) {
                qunsetenv(name);
            } else {
                qputenv(name, value);
            }
        };
        restore("BREEZEDESK_DATA_ROOT", previousDataRoot);
        restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
    });
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    QVERIFY(StoragePaths::ensureLayout());

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    ModelManager models;

    const QString sourcePath = directory.filePath(QStringLiteral("current-source.mp4"));
    const QString normalizedPath = directory.filePath(QStringLiteral("current-normalized.wav"));
    QVERIFY(writeFixture(sourcePath));
    QVERIFY(writePcmWaveFixture(normalizedPath, 1'000));
    Recording recording;
    recording.id = QStringLiteral("recording-resume-source");
    recording.title = QStringLiteral("Changed resumed source");
    recording.sourcePath = sourcePath;
    recording.normalizedPcmPath = normalizedPath;
    recording.sourceHash = FileHash::sha256(sourcePath);
    recording.durationMs = 1'000;
    QVERIFY(recordings.create(recording));

    TranscriptionJob job;
    job.id = QStringLiteral("job-resume-source");
    job.recordingId = recording.id;
    job.parameters = {{QStringLiteral("sourceHash"), QString(64, QLatin1Char('a'))}};
    QVERIFY(jobs.createQueued(job));
    JobChunk completedChunk;
    completedChunk.jobId = job.id;
    completedChunk.ordinal = 0;
    completedChunk.startMs = 0;
    completedChunk.endMs = 1'000;
    completedChunk.state = ChunkState::Completed;
    completedChunk.attempts = 1;
    QVERIFY(jobs.replaceChunks(job.id, {completedChunk}));

    WorkerProcessManager worker;
    TranscriptionCoordinator coordinator(recordings, jobs, transcripts, models, worker);
    coordinator.initialize();
    const auto jobFailed = [&jobs, &job] {
        const auto current = jobs.findById(job.id);
        return current && current.value().has_value() && current.value()->state == JobState::Failed;
    };
    QTRY_VERIFY_WITH_TIMEOUT(jobFailed(), 10'000);
    const auto failed = jobs.findById(job.id);
    QVERIFY(failed && failed.value().has_value());
    QCOMPARE(failed.value()->errorCode, QStringLiteral("SourceMediaChanged"));
    QCOMPARE(failed.value()->parameters.value(QStringLiteral("sourceHash")).toString(),
             QString(64, QLatin1Char('a')));
}

void TranscriptionCoordinatorTest::rejectsStartedPlanThatWouldOmitCanonicalTail() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const QByteArray previousOverrideOnly = qgetenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY");
    const QByteArray previousSentinel =
        qgetenv("BREEZEDESK_TEST_COORDINATOR_TRANSCRIPTION_SENTINEL");
    const auto restoreEnvironment =
        qScopeGuard([previousDataRoot, previousWorkerPath, previousOverrideOnly, previousSentinel] {
            const auto restore = [](const char* name, const QByteArray& value) {
                if (value.isNull()) {
                    qunsetenv(name);
                } else {
                    qputenv(name, value);
                }
            };
            restore("BREEZEDESK_DATA_ROOT", previousDataRoot);
            restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
            restore("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", previousOverrideOnly);
            restore("BREEZEDESK_TEST_COORDINATOR_TRANSCRIPTION_SENTINEL", previousSentinel);
        });
    const QString transcriptionSentinel =
        directory.filePath(QStringLiteral("transcription-requested.sentinel"));
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    qputenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", "1");
    qputenv("BREEZEDESK_TEST_COORDINATOR_TRANSCRIPTION_SENTINEL",
            transcriptionSentinel.toUtf8());
    QVERIFY(StoragePaths::ensureLayout());

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);
    ModelManager models;

    const QString sourcePath = directory.filePath(QStringLiteral("tail-source.mp4"));
    const QString normalizedPath = directory.filePath(QStringLiteral("tail-normalized.wav"));
    const QString waveformPath = directory.filePath(QStringLiteral("tail.waveform"));
    QVERIFY(writeFixture(sourcePath));
    QVERIFY(writePcmWaveFixture(normalizedPath, 1'000));
    QVERIFY(writeFixture(waveformPath));
    const QString sourceHash = FileHash::sha256(sourcePath);
    QVERIFY(!sourceHash.isEmpty());

    Recording recording;
    recording.id = QStringLiteral("recording-short-durable-tail");
    recording.title = QStringLiteral("Canonical audio exceeds durable plan");
    recording.sourcePath = sourcePath;
    recording.normalizedPcmPath = normalizedPath;
    recording.waveformPath = waveformPath;
    recording.sourceHash = sourceHash;
    recording.durationMs = 900;
    QVERIFY(recordings.create(recording));

    TranscriptionJob job;
    job.id = QStringLiteral("job-short-durable-tail");
    job.recordingId = recording.id;
    job.vadEnabled = false;
    job.parameters = {{QStringLiteral("sourceHash"), sourceHash}};
    QVERIFY(jobs.createQueued(job));

    JobChunk durableChunk;
    durableChunk.id = QStringLiteral("chunk-short-durable-tail");
    durableChunk.jobId = job.id;
    durableChunk.ordinal = 0;
    durableChunk.startMs = 0;
    durableChunk.endMs = 900;
    durableChunk.state = ChunkState::Interrupted;
    durableChunk.attempts = 1;
    durableChunk.error = QStringLiteral("preserve interrupted checkpoint");
    durableChunk.resultHash = QStringLiteral("preserve-result-hash");
    durableChunk.diagnostics = {{QStringLiteral("preserve"), true}};
    QVERIFY(jobs.replaceChunks(job.id, {durableChunk}));

    const QString seedOwner = QStringLiteral("tail-checkpoint-seed-owner");
    const auto claimed = jobs.claimQueued(job.id, seedOwner);
    QVERIFY(claimed && claimed.value().claimed);
    TranscriptSegment partial;
    partial.id = QStringLiteral("segment-short-durable-tail");
    partial.ordinal = 0;
    partial.startMs = 100;
    partial.endMs = 800;
    partial.originalText = QStringLiteral("preserve partial transcript");
    QVERIFY(transcripts.replaceChunk(recording.id, job.id, durableChunk.id, {partial}, true, 1,
                                     seedOwner));
    QVERIFY(jobs.transition(job.id, JobState::Interrupted, QStringLiteral("WorkerCrashed"),
                            QStringLiteral("seed interrupted checkpoint"), seedOwner));
    const auto releasedLease = jobs.activeLease();
    QVERIFY(releasedLease && !releasedLease.value().has_value());
    QVERIFY(jobs.transition(job.id, JobState::Queued));

    WorkerProcessManager worker;
    TranscriptionCoordinator coordinator(recordings, jobs, transcripts, models, worker);
    coordinator.initialize();
    const auto jobFailed = [&jobs, &job] {
        const auto current = jobs.findById(job.id);
        return current && current.value().has_value() && current.value()->state == JobState::Failed;
    };
    QTRY_VERIFY_WITH_TIMEOUT(jobFailed(), 10'000);

    const auto failed = jobs.findById(job.id);
    QVERIFY(failed && failed.value().has_value());
    QCOMPARE(failed.value()->errorCode, QStringLiteral("ChunkPlanMismatch"));

    const auto savedChunks = jobs.chunks(job.id);
    QVERIFY(savedChunks);
    QCOMPARE(savedChunks.value().size(), 1);
    const JobChunk& savedChunk = savedChunks.value().constFirst();
    QCOMPARE(savedChunk.id, durableChunk.id);
    QCOMPARE(savedChunk.startMs, durableChunk.startMs);
    QCOMPARE(savedChunk.endMs, durableChunk.endMs);
    QCOMPARE(savedChunk.state, durableChunk.state);
    QCOMPARE(savedChunk.attempts, durableChunk.attempts);
    QCOMPARE(savedChunk.error, durableChunk.error);
    QCOMPARE(savedChunk.resultHash, durableChunk.resultHash);
    QCOMPARE(savedChunk.diagnostics, durableChunk.diagnostics);

    const auto savedSegments = transcripts.segmentsForJob(job.id, true);
    QVERIFY(savedSegments);
    QCOMPARE(savedSegments.value().size(), 1);
    QCOMPARE(savedSegments.value().constFirst().id, partial.id);
    QCOMPARE(savedSegments.value().constFirst().chunkId, durableChunk.id);
    QCOMPARE(savedSegments.value().constFirst().originalText, partial.originalText);
    QCOMPARE(savedSegments.value().constFirst().provisional, true);
    QCOMPARE(savedSegments.value().constFirst().attempt, 1);

    const auto canonicalRecording = recordings.findById(recording.id);
    QVERIFY(canonicalRecording && canonicalRecording.value().has_value());
    QCOMPARE(canonicalRecording.value()->durationMs, 1'000);
    QVERIFY(!QFileInfo::exists(transcriptionSentinel));
}

void TranscriptionCoordinatorTest::runtimeUnavailableFailsBeforeMediaPreparation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previousDataRoot = qgetenv("BREEZEDESK_DATA_ROOT");
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const QByteArray previousRuntimeAvailability = qgetenv("BREEZEDESK_TEST_COORDINATOR_RUNTIME_AVAILABLE");
    const auto restoreEnvironment =
        qScopeGuard([previousDataRoot, previousWorkerPath, previousRuntimeAvailability] {
            const auto restore = [](const char* name, const QByteArray& value) {
                if (value.isNull()) {
                    qunsetenv(name);
                } else {
                    qputenv(name, value);
                }
            };
            restore("BREEZEDESK_DATA_ROOT", previousDataRoot);
            restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
            restore("BREEZEDESK_TEST_COORDINATOR_RUNTIME_AVAILABLE", previousRuntimeAvailability);
        });
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    qputenv("BREEZEDESK_ASR_WORKER_PATH", BREEZEDESK_COORDINATOR_WORKER_PATH);
    qputenv("BREEZEDESK_TEST_COORDINATOR_RUNTIME_AVAILABLE", QByteArrayLiteral("0"));
    QVERIFY(StoragePaths::ensureLayout());

    ModelManager models;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    const QString sourcePath = directory.filePath(QStringLiteral("runtime-preflight.m4a"));
    QVERIFY(writeFixture(sourcePath));
    Recording recording;
    recording.id = QStringLiteral("recording-runtime-unavailable");
    recording.title = QStringLiteral("Runtime preflight");
    recording.sourcePath = sourcePath;
    QVERIFY(recordings.create(recording));

    WorkerProcessManager worker;
    TranscriptionCoordinator coordinator(recordings, jobs, transcripts, models, worker);
    QSignalSpy errors(&coordinator, &TranscriptionCoordinator::errorOccurred);
    coordinator.initialize();
    coordinator.enqueue(QStringLiteral("job-runtime-unavailable"), recording.id);

    const auto jobFailed = [&jobs] {
        const auto current = jobs.findById(QStringLiteral("job-runtime-unavailable"));
        return current && current.value().has_value() && current.value()->state == JobState::Failed;
    };
    QTRY_VERIFY_WITH_TIMEOUT(jobFailed(), 10'000);
    const auto failed = jobs.findById(QStringLiteral("job-runtime-unavailable"));
    QVERIFY(failed && failed.value().has_value());
    QCOMPARE(failed.value()->errorCode, QStringLiteral("BackendUnavailable"));
    QVERIFY(failed.value()->errorMessage.contains(QStringLiteral("whisper.cpp")));
    QCOMPARE(failed.value()->stage, JobStage::Preparing);
    QCOMPARE(failed.value()->progress, 0.0);
    const auto chunks = jobs.chunks(QStringLiteral("job-runtime-unavailable"));
    QVERIFY(chunks);
    QVERIFY(chunks.value().isEmpty());
    QVERIFY(!errors.isEmpty());

    const auto savedRecording = recordings.findById(recording.id);
    QVERIFY(savedRecording && savedRecording.value().has_value());
    QVERIFY(savedRecording.value()->normalizedPcmPath.isEmpty());
    QVERIFY(savedRecording.value()->waveformPath.isEmpty());
}

QTEST_GUILESS_MAIN(TranscriptionCoordinatorTest)
#include "tst_TranscriptionCoordinator.moc"
