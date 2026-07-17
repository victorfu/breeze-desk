#include <breezedesk/ipc/AsrWorkerClient.h>
#include <breezedesk/ipc/LocalEndpoint.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QRandomGenerator>
#include <QtCore/QSaveFile>
#include <QtCore/QScopeGuard>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUuid>
#include <QtCore/QtEndian>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <limits>
#include <optional>

#ifndef BREEZEDESK_TEST_WORKER_PATH
#error BREEZEDESK_TEST_WORKER_PATH must name the worker executable
#endif
#ifndef BREEZEDESK_TEST_MODEL_DEFAULT_PATH
#error BREEZEDESK_TEST_MODEL_DEFAULT_PATH must name the default test model
#endif
#ifndef BREEZEDESK_TEST_AUDIO_SOURCE_PATH
#error BREEZEDESK_TEST_AUDIO_SOURCE_PATH must name the pinned source audio
#endif

using namespace BreezeDesk::Ipc;

namespace {

constexpr auto kExpectedModelSha256 = "c77c5766f1cef09b6b7d47f21b546cbddd4157886b3b5d6d4f709e91e66c7c2b";

struct PcmWave {
    QByteArray samples;
    quint32 sampleRate = 0;
    quint16 channels = 0;
    quint16 bitsPerSample = 0;
};

std::optional<PcmWave> readPcmWave(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 12 || bytes.first(4) != QByteArrayLiteral("RIFF") ||
        bytes.sliced(8, 4) != QByteArrayLiteral("WAVE")) {
        return std::nullopt;
    }

    PcmWave result;
    bool formatFound = false;
    qsizetype offset = 12;
    while (offset + 8 <= bytes.size()) {
        const QByteArrayView chunkId(bytes.constData() + offset, 4);
        const quint32 chunkSize =
            qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + offset + 4));
        const qsizetype chunkOffset = offset + 8;
        if (chunkSize > static_cast<quint32>(bytes.size() - chunkOffset)) {
            return std::nullopt;
        }
        if (chunkId == QByteArrayView("fmt ", 4)) {
            if (chunkSize < 16U) {
                return std::nullopt;
            }
            const auto* format = reinterpret_cast<const uchar*>(bytes.constData() + chunkOffset);
            const quint16 audioFormat = qFromLittleEndian<quint16>(format);
            if (audioFormat != 1U) {
                return std::nullopt;
            }
            result.channels = qFromLittleEndian<quint16>(format + 2);
            result.sampleRate = qFromLittleEndian<quint32>(format + 4);
            result.bitsPerSample = qFromLittleEndian<quint16>(format + 14);
            formatFound = true;
        } else if (chunkId == QByteArrayView("data", 4)) {
            result.samples = bytes.sliced(chunkOffset, static_cast<qsizetype>(chunkSize));
        }
        offset = chunkOffset + static_cast<qsizetype>(chunkSize) + static_cast<qsizetype>(chunkSize & 1U);
    }
    if (!formatFound || result.samples.isEmpty() || result.channels != 1U || result.sampleRate != 16'000U ||
        result.bitsPerSample != 16U) {
        return std::nullopt;
    }
    return result;
}

bool writeRepeatedPcmWave(const QString& path, const PcmWave& source, int repetitions) {
    if (repetitions <= 0 ||
        source.samples.size() > std::numeric_limits<quint32>::max() / static_cast<quint32>(repetitions)) {
        return false;
    }
    const quint32 dataSize = static_cast<quint32>(source.samples.size()) * static_cast<quint32>(repetitions);
    QByteArray header(44, '\0');
    header.replace(0, 4, QByteArrayLiteral("RIFF"));
    qToLittleEndian<quint32>(36U + dataSize, reinterpret_cast<uchar*>(header.data() + 4));
    header.replace(8, 4, QByteArrayLiteral("WAVE"));
    header.replace(12, 4, QByteArrayLiteral("fmt "));
    qToLittleEndian<quint32>(16U, reinterpret_cast<uchar*>(header.data() + 16));
    qToLittleEndian<quint16>(1U, reinterpret_cast<uchar*>(header.data() + 20));
    qToLittleEndian<quint16>(source.channels, reinterpret_cast<uchar*>(header.data() + 22));
    qToLittleEndian<quint32>(source.sampleRate, reinterpret_cast<uchar*>(header.data() + 24));
    constexpr quint16 bytesPerSample = 2;
    qToLittleEndian<quint32>(source.sampleRate * source.channels * bytesPerSample,
                             reinterpret_cast<uchar*>(header.data() + 28));
    qToLittleEndian<quint16>(source.channels * bytesPerSample, reinterpret_cast<uchar*>(header.data() + 32));
    qToLittleEndian<quint16>(source.bitsPerSample, reinterpret_cast<uchar*>(header.data() + 34));
    header.replace(36, 4, QByteArrayLiteral("data"));
    qToLittleEndian<quint32>(dataSize, reinterpret_cast<uchar*>(header.data() + 40));

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(header) != header.size()) {
        return false;
    }
    for (int repetition = 0; repetition < repetitions; ++repetition) {
        if (output.write(source.samples) != source.samples.size()) {
            output.cancelWriting();
            return false;
        }
    }
    return output.commit();
}

QString modelPath() {
    const QString environmentPath = qEnvironmentVariable("BREEZEDESK_TEST_MODEL_PATH");
    return environmentPath.isEmpty() ? QString::fromUtf8(BREEZEDESK_TEST_MODEL_DEFAULT_PATH)
                                     : environmentPath;
}

QByteArray randomToken() {
    QByteArray token(32, Qt::Uninitialized);
    for (char& byte : token) {
        byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xFFU);
    }
    return token;
}

std::optional<Envelope> responseFor(const QSignalSpy& messages, const QString& requestId, MessageType type) {
    for (const auto& arguments : messages) {
        const auto envelope = qvariant_cast<Envelope>(arguments.constFirst());
        if (envelope.requestId == requestId && envelope.type == type) {
            return envelope;
        }
    }
    return std::nullopt;
}

std::optional<Envelope> errorFor(const QSignalSpy& messages, const QString& requestId) {
    return responseFor(messages, requestId, MessageType::Error);
}

QString errorDiagnostic(const std::optional<Envelope>& error, QProcess& worker) {
    if (error) {
        return QStringLiteral("worker error: %1 (%2)")
            .arg(error->payload.value(QStringLiteral("message")).toString(),
                 error->payload.value(QStringLiteral("technicalDetails")).toString());
    }
    return QStringLiteral("worker state=%1 stderr=%2")
        .arg(static_cast<int>(worker.state()))
        .arg(QString::fromUtf8(worker.readAllStandardError()));
}

} // namespace

class ModelIntegrationTest final : public QObject {
    Q_OBJECT

  private slots:
    void workerLoadsTranscribesAndCancels();
};

void ModelIntegrationTest::workerLoadsTranscribesAndCancels() {
    const QString selectedModelPath = modelPath();
    QFile model(selectedModelPath);
    QVERIFY2(model.open(QIODevice::ReadOnly),
             qPrintable(QStringLiteral("Tiny model is missing. Run scripts/download-test-model.sh or set "
                                       "BREEZEDESK_TEST_MODEL_PATH. Expected: %1")
                            .arg(selectedModelPath)));
    QCryptographicHash modelHash(QCryptographicHash::Sha256);
    QVERIFY(modelHash.addData(&model));
    QCOMPARE(QString::fromLatin1(modelHash.result().toHex()), QString::fromLatin1(kExpectedModelSha256));

    const auto sourceAudio = readPcmWave(QString::fromUtf8(BREEZEDESK_TEST_AUDIO_SOURCE_PATH));
    QVERIFY2(sourceAudio.has_value(), "Unable to read the pinned whisper.cpp audio sample");
    constexpr qint64 sourceDurationMs = 11'000;
    constexpr int cancellationRepetitions = 12;
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString shortAudioPath = temporaryDirectory.filePath(QStringLiteral("short.wav"));
    const QString longAudioPath = temporaryDirectory.filePath(QStringLiteral("long.wav"));
    QVERIFY(writeRepeatedPcmWave(shortAudioPath, *sourceAudio, 1));
    QVERIFY(writeRepeatedPcmWave(longAudioPath, *sourceAudio, cancellationRepetitions));

    const QByteArray token = randomToken();
    const QString endpoint = LocalEndpoint::userScopedName(
        QStringLiteral("test.model.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QProcess worker;
    const auto cleanupWorker = qScopeGuard([&worker] {
        if (worker.state() != QProcess::NotRunning) {
            worker.kill();
            worker.waitForFinished(2'000);
        }
    });
    worker.setProcessChannelMode(QProcess::SeparateChannels);
    worker.start(
        QString::fromUtf8(BREEZEDESK_TEST_WORKER_PATH),
        {QStringLiteral("--server"), endpoint, QStringLiteral("--session-token"),
         QString::fromLatin1(token.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)),
         QStringLiteral("--worker-version"), QStringLiteral("model-integration-test")});
    QVERIFY2(worker.waitForStarted(5'000), qPrintable(worker.errorString()));

    AsrWorkerClient client(QStringLiteral("model-integration-test"));
    QSignalSpy ready(&client, &AsrWorkerClient::ready);
    for (int attempt = 0; attempt < 100 && ready.isEmpty(); ++attempt) {
        client.connectToWorker(endpoint, token);
        QTest::qWait(50);
    }
    QCOMPARE(ready.size(), 1);
    QSignalSpy messages(&client, &AsrWorkerClient::envelopeReceived);

    QCborMap loadPayload;
    loadPayload.insert(QStringLiteral("modelPath"), selectedModelPath);
    loadPayload.insert(QStringLiteral("modelSha256"), QString::fromLatin1(kExpectedModelSha256));
    loadPayload.insert(QStringLiteral("backend"), QStringLiteral("auto"));
    loadPayload.insert(QStringLiteral("flashAttention"), false);
    const QString loadRequestId = client.sendRequest(MessageType::LoadModel, {}, loadPayload);
    std::optional<Envelope> loaded;
    std::optional<Envelope> loadError;
    for (int attempt = 0; attempt < 600 && !loaded && !loadError; ++attempt) {
        loaded = responseFor(messages, loadRequestId, MessageType::ModelLoaded);
        loadError = errorFor(messages, loadRequestId);
        if (!loaded && !loadError) {
            QTest::qWait(50);
        }
    }
    const QString loadDiagnostic = errorDiagnostic(loadError, worker);
    QVERIFY2(loaded.has_value(), qPrintable(loadDiagnostic));
#if defined(Q_OS_MACOS)
    QCOMPARE(loaded->payload.value(QStringLiteral("actualBackend")).toString(), QStringLiteral("Metal"));
#else
    QVERIFY(!loaded->payload.value(QStringLiteral("actualBackend")).toString().isEmpty());
#endif

    messages.clear();
    const QString transcriptionJobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QCborMap transcriptionPayload;
    transcriptionPayload.insert(QStringLiteral("pcmPath"), shortAudioPath);
    transcriptionPayload.insert(QStringLiteral("startMs"), 0);
    transcriptionPayload.insert(QStringLiteral("endMs"), sourceDurationMs);
    transcriptionPayload.insert(QStringLiteral("finalChunk"), true);
    transcriptionPayload.insert(QStringLiteral("language"), QStringLiteral("en"));
    transcriptionPayload.insert(QStringLiteral("preset"), QStringLiteral("fast"));
    transcriptionPayload.insert(QStringLiteral("tokenTimestamps"), false);
    transcriptionPayload.insert(QStringLiteral("vadEnabled"), false);
    const QString transcriptionRequestId =
        client.sendRequest(MessageType::StartTranscription, transcriptionJobId, transcriptionPayload);
    std::optional<Envelope> transcriptionCompleted;
    std::optional<Envelope> transcriptionError;
    for (int attempt = 0; attempt < 1'200 && !transcriptionCompleted && !transcriptionError; ++attempt) {
        transcriptionCompleted =
            responseFor(messages, transcriptionRequestId, MessageType::TranscriptionCompleted);
        transcriptionError = errorFor(messages, transcriptionRequestId);
        if (!transcriptionCompleted && !transcriptionError) {
            QTest::qWait(50);
        }
    }
    const QString transcriptionDiagnostic = errorDiagnostic(transcriptionError, worker);
    QVERIFY2(transcriptionCompleted.has_value(), qPrintable(transcriptionDiagnostic));
    QVERIFY(responseFor(messages, transcriptionRequestId, MessageType::PartialSegment).has_value());

    QList<qint64> progressValues;
    for (const auto& arguments : messages) {
        const auto envelope = qvariant_cast<Envelope>(arguments.constFirst());
        if (envelope.requestId == transcriptionRequestId && envelope.type == MessageType::Progress) {
            progressValues.append(envelope.payload.value(QStringLiteral("progress")).toInteger());
        }
    }
    QVERIFY(!progressValues.isEmpty());
    for (qsizetype index = 1; index < progressValues.size(); ++index) {
        QVERIFY(progressValues.at(index) >= progressValues.at(index - 1));
    }
    QCOMPARE(progressValues.constLast(), 100);

    messages.clear();
    const QString cancellationJobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QCborMap cancellationPayload = transcriptionPayload;
    cancellationPayload.insert(QStringLiteral("pcmPath"), longAudioPath);
    cancellationPayload.insert(QStringLiteral("endMs"), sourceDurationMs * cancellationRepetitions);
    cancellationPayload.insert(QStringLiteral("finalChunk"), false);
    const QString cancellationRequestId =
        client.sendRequest(MessageType::StartTranscription, cancellationJobId, cancellationPayload);

    bool cancellationSent = false;
    std::optional<Envelope> cancelled;
    std::optional<Envelope> cancellationError;
    std::optional<Envelope> unexpectedlyCompleted;
    for (int attempt = 0; attempt < 1'200 && !cancelled && !cancellationError && !unexpectedlyCompleted;
         ++attempt) {
        if (!cancellationSent &&
            responseFor(messages, cancellationRequestId, MessageType::Progress).has_value()) {
            const QString cancelRequestId = client.sendRequest(MessageType::CancelJob, cancellationJobId, {});
            QVERIFY(!cancelRequestId.isEmpty());
            cancellationSent = true;
        }
        cancelled = responseFor(messages, cancellationRequestId, MessageType::JobCancelled);
        cancellationError = errorFor(messages, cancellationRequestId);
        unexpectedlyCompleted = responseFor(messages, cancellationRequestId, MessageType::ChunkCompleted);
        if (!cancelled && !cancellationError && !unexpectedlyCompleted) {
            QTest::qWait(50);
        }
    }
    const QString cancellationDiagnostic = errorDiagnostic(cancellationError, worker);
    QVERIFY2(cancellationSent, "No inference progress arrived before cancellation timeout");
    QVERIFY2(!unexpectedlyCompleted.has_value(), "Inference completed before abort was observed");
    QVERIFY2(cancelled.has_value(), qPrintable(cancellationDiagnostic));
    QCOMPARE(cancelled->payload.value(QStringLiteral("partialResultsPreserved")).toBool(), true);

    client.sendRequest(MessageType::Shutdown, {}, {});
    QTRY_COMPARE_WITH_TIMEOUT(worker.state(), QProcess::NotRunning, 10'000);
    QCOMPARE(worker.exitStatus(), QProcess::NormalExit);
    QCOMPARE(worker.exitCode(), 0);
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    ModelIntegrationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_ModelIntegration.moc"
