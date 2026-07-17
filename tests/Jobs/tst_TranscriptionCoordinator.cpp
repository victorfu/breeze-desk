#include "breezedesk/app/TranscriptionCoordinator.h"
#include "breezedesk/app/WorkerProcessManager.h"
#include "breezedesk/audio/WaveformGenerator.h"
#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/jobs/SqliteJobRepository.h"
#include "breezedesk/models/ModelManager.h"
#include "breezedesk/transcript/SqliteTranscriptRepository.h"

#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

#ifndef BREEZEDESK_COORDINATOR_WORKER_PATH
#error BREEZEDESK_COORDINATOR_WORKER_PATH must name the coordinator test worker
#endif

using namespace BreezeDesk;

namespace {

bool writeFixture(const QString& path, const qsizetype size = 64) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QByteArray(size, '\0')) == size;
}

bool writePcmWaveFixture(const QString& path) {
    constexpr quint32 sampleCount = 512;
    constexpr quint32 dataBytes = sampleCount * 2U;
    constexpr quint32 junkBytes = 5;
    constexpr quint32 riffSize = 4U + (8U + 16U) + (8U + junkBytes + 1U) + (8U + dataBytes);
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
    for (quint32 index = 0; index < sampleCount; ++index)
        stream << static_cast<qint16>(static_cast<int>(index % 200U) - 100);
    return stream.status() == QDataStream::Ok;
}

} // namespace

class TranscriptionCoordinatorTest final : public QObject {
    Q_OBJECT

  private slots:
    void analyzesLongAudioAndPersistsGlobalSegments();
    void runtimeUnavailableFailsBeforeMediaPreparation();
};

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
    QVERIFY(writeFixture(models.modelPath(QStringLiteral("silero-vad-v6.2.0"))));

    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    const QString sourcePath = directory.filePath(QStringLiteral("長會議 source.m4a"));
    const QString normalizedPath = directory.filePath(QStringLiteral("長會議 normalized.wav"));
    QVERIFY(writeFixture(sourcePath));
    QVERIFY(writePcmWaveFixture(normalizedPath));
    Recording recording;
    recording.id = QStringLiteral("recording-coordinator");
    recording.title = QStringLiteral("Long architecture meeting");
    recording.sourcePath = sourcePath;
    recording.normalizedPcmPath = normalizedPath;
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
        while (timeout.elapsed() < 10'000) {
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
        QCOMPARE(waveform.first().minimums.size(), 2);

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
        coordinator.cancel(QStringLiteral("job-cancel"));
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
