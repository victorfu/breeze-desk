#include <breezedesk/asr/AsrTypes.h>
#include <breezedesk/ipc/AsrWorkerClient.h>
#include <breezedesk/ipc/LocalEndpoint.h>

#include <QtCore/QCborArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QRandomGenerator>
#include <QtCore/QScopeGuard>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUuid>
#include <QtCore/QtEndian>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#ifndef BREEZEDESK_TEST_WORKER_PATH
#error BREEZEDESK_TEST_WORKER_PATH must name the worker executable
#endif

using namespace BreezeDesk::Ipc;

namespace {

#ifdef BREEZEDESK_TEST_VAD_MODEL_PATH
bool writeSilenceWav(const QString& path, int durationMs) {
    constexpr quint32 sampleRate = 16'000;
    constexpr quint16 channelCount = 1;
    constexpr quint16 bitsPerSample = 16;
    constexpr quint16 bytesPerSample = bitsPerSample / 8;
    const quint32 sampleCount = sampleRate * static_cast<quint32>(durationMs) / 1'000U;
    const quint32 dataSize = sampleCount * bytesPerSample;
    QByteArray bytes(44 + static_cast<qsizetype>(dataSize), '\0');
    bytes.replace(0, 4, QByteArrayLiteral("RIFF"));
    qToLittleEndian<quint32>(36U + dataSize, reinterpret_cast<uchar*>(bytes.data() + 4));
    bytes.replace(8, 4, QByteArrayLiteral("WAVE"));
    bytes.replace(12, 4, QByteArrayLiteral("fmt "));
    qToLittleEndian<quint32>(16U, reinterpret_cast<uchar*>(bytes.data() + 16));
    qToLittleEndian<quint16>(1U, reinterpret_cast<uchar*>(bytes.data() + 20));
    qToLittleEndian<quint16>(channelCount, reinterpret_cast<uchar*>(bytes.data() + 22));
    qToLittleEndian<quint32>(sampleRate, reinterpret_cast<uchar*>(bytes.data() + 24));
    qToLittleEndian<quint32>(sampleRate * channelCount * bytesPerSample,
                             reinterpret_cast<uchar*>(bytes.data() + 28));
    qToLittleEndian<quint16>(channelCount * bytesPerSample, reinterpret_cast<uchar*>(bytes.data() + 32));
    qToLittleEndian<quint16>(bitsPerSample, reinterpret_cast<uchar*>(bytes.data() + 34));
    bytes.replace(36, 4, QByteArrayLiteral("data"));
    qToLittleEndian<quint32>(dataSize, reinterpret_cast<uchar*>(bytes.data() + 40));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        return false;
    }
    file.close();
    return file.error() == QFileDevice::NoError;
}

QByteArray sha256File(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return hash.result().toHex();
}
#endif

} // namespace

class WorkerProcessTest final : public QObject {
    Q_OBJECT

  private slots:
    void reportsCapabilitiesAndShutsDown();
};

void WorkerProcessTest::reportsCapabilitiesAndShutsDown() {
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    QByteArray token(32, Qt::Uninitialized);
    for (char& byte : token) {
        byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xFFU);
    }
    const QString endpoint = LocalEndpoint::userScopedName(
        QStringLiteral("test.worker.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QProcess worker;
    const auto cleanupWorker = qScopeGuard([&worker] {
        if (worker.state() != QProcess::NotRunning) {
            worker.kill();
            worker.waitForFinished(2'000);
        }
    });
    worker.setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("BREEZEDESK_DATA_ROOT"), dataRoot.path());
    environment.insert(QStringLiteral("QT_LOGGING_RULES"),
                       QStringLiteral("breezedesk.asr.worker.debug=true"));
    worker.setProcessEnvironment(environment);
    worker.start(
        QString::fromUtf8(BREEZEDESK_TEST_WORKER_PATH),
        {QStringLiteral("--server"), endpoint, QStringLiteral("--session-token"),
         QString::fromLatin1(token.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)),
         QStringLiteral("--worker-version"), QStringLiteral("test")});
    QVERIFY2(worker.waitForStarted(3'000), qPrintable(worker.errorString()));

    AsrWorkerClient client(QStringLiteral("test-client"));
    QSignalSpy ready(&client, &AsrWorkerClient::ready);
    for (int attempt = 0; attempt < 100 && ready.isEmpty(); ++attempt) {
        client.connectToWorker(endpoint, token);
        QTest::qWait(50);
    }
    const QString readinessDiagnostic =
        QStringLiteral("workerState=%1 exitCode=%2 processError=%3 stderr=%4")
            .arg(static_cast<int>(worker.state()))
            .arg(worker.exitCode())
            .arg(worker.errorString(), QString::fromUtf8(worker.readAllStandardError()));
    QVERIFY2(ready.size() == 1, qPrintable(readinessDiagnostic));
    QVERIFY(client.isReady());

    QSignalSpy messages(&client, &AsrWorkerClient::envelopeReceived);
    QSignalSpy errors(&client, &AsrWorkerClient::protocolError);
    const QString requestId = client.sendRequest(MessageType::GetCapabilities, {}, {});
    QVERIFY(!requestId.isEmpty());
    for (int attempt = 0; attempt < 40 && messages.isEmpty() && errors.isEmpty(); ++attempt) {
        QTest::qWait(50);
    }
    const QString diagnostic = QStringLiteral("clientErrors=%1 workerState=%2 stderr=%3")
                                   .arg(errors.size())
                                   .arg(static_cast<int>(worker.state()))
                                   .arg(QString::fromUtf8(worker.readAllStandardError()));
    QVERIFY2(!messages.isEmpty(), qPrintable(diagnostic));
    const auto capabilities = qvariant_cast<Envelope>(messages.constFirst().constFirst());
    QCOMPARE(capabilities.requestId, requestId);
    QCOMPARE(capabilities.type, MessageType::Capabilities);
    QCOMPARE(capabilities.payload.value(QStringLiteral("protocolVersion")).toInteger(), kProtocolVersion);
    QVERIFY(capabilities.payload.value(QStringLiteral("runtimeAvailable")).isBool());
    QCOMPARE(capabilities.payload.value(QStringLiteral("runtimeAvailable")).toBool(),
             BREEZEDESK_TEST_EXPECT_WHISPER != 0);
    QVERIFY(!capabilities.payload.value(QStringLiteral("whisperVersion")).toString().isEmpty());
    QCOMPARE(capabilities.payload.value(QStringLiteral("streamingVad")).toBool(), true);
#if defined(Q_OS_MACOS)
    if (capabilities.payload.value(QStringLiteral("runtimeAvailable")).toBool()) {
        QCOMPARE(capabilities.payload.value(QStringLiteral("compiledBackend")).toString(),
                 QStringLiteral("Metal"));
    }
#endif

#ifdef BREEZEDESK_TEST_VAD_MODEL_PATH
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    constexpr int fixtureDurationMs = 1'200;
    const QString audioPath = temporaryDirectory.filePath(QStringLiteral("silence.wav"));
    QVERIFY(writeSilenceWav(audioPath, fixtureDurationMs));
    const QString vadModelPath = QString::fromUtf8(BREEZEDESK_TEST_VAD_MODEL_PATH);
    const QByteArray vadModelSha256 = sha256File(vadModelPath);
    QCOMPARE(vadModelSha256.size(), 64);

    messages.clear();
    const QString rejectedLoadRequest =
        client.sendRequest(MessageType::LoadModel, {},
                           {{QStringLiteral("modelPath"), vadModelPath},
                            {QStringLiteral("modelSha256"), QString(64, QLatin1Char('0'))},
                            {QStringLiteral("backend"), QStringLiteral("cpu")},
                            {QStringLiteral("flashAttention"), false}});
    Envelope rejectedLoad;
    for (int attempt = 0; attempt < 100 && rejectedLoad.requestId.isEmpty(); ++attempt) {
        for (const auto& arguments : messages) {
            const auto envelope = qvariant_cast<Envelope>(arguments.constFirst());
            if (envelope.requestId == rejectedLoadRequest && envelope.type == MessageType::Error) {
                rejectedLoad = envelope;
                break;
            }
        }
        if (rejectedLoad.requestId.isEmpty()) {
            QTest::qWait(50);
        }
    }
    QCOMPARE(rejectedLoad.requestId, rejectedLoadRequest);
    QCOMPARE(rejectedLoad.payload.value(QStringLiteral("code")).toInteger(),
             static_cast<qint64>(BreezeDesk::Asr::AsrErrorCode::ModelChecksumMismatch));

    messages.clear();
    const QString rejectedAnalysisJobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString rejectedAnalysisRequest =
        client.sendRequest(MessageType::AnalyzeSpeech, rejectedAnalysisJobId,
                           {{QStringLiteral("pcmPath"), audioPath},
                            {QStringLiteral("vadModelPath"), vadModelPath},
                            {QStringLiteral("vadModelSha256"), QString(64, QLatin1Char('0'))}});
    Envelope rejectedAnalysis;
    for (int attempt = 0; attempt < 100 && rejectedAnalysis.requestId.isEmpty(); ++attempt) {
        for (const auto& arguments : messages) {
            const auto envelope = qvariant_cast<Envelope>(arguments.constFirst());
            if (envelope.requestId == rejectedAnalysisRequest && envelope.type == MessageType::Error) {
                rejectedAnalysis = envelope;
                break;
            }
        }
        if (rejectedAnalysis.requestId.isEmpty()) {
            QTest::qWait(50);
        }
    }
    QCOMPARE(rejectedAnalysis.requestId, rejectedAnalysisRequest);
    QCOMPARE(rejectedAnalysis.payload.value(QStringLiteral("code")).toInteger(),
             static_cast<qint64>(BreezeDesk::Asr::AsrErrorCode::ModelChecksumMismatch));

    messages.clear();
    const QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QCborMap analysisPayload;
    analysisPayload.insert(QStringLiteral("pcmPath"), audioPath);
    analysisPayload.insert(QStringLiteral("vadModelPath"), vadModelPath);
    analysisPayload.insert(QStringLiteral("vadModelSha256"), QString::fromLatin1(vadModelSha256));
    const QString analysisRequestId = client.sendRequest(MessageType::AnalyzeSpeech, jobId, analysisPayload);
    QVERIFY(!analysisRequestId.isEmpty());

    Envelope analysisResponse;
    Envelope analysisError;
    bool analysisCompleted = false;
    for (int attempt = 0; attempt < 600 && !analysisCompleted && analysisError.requestId.isEmpty();
         ++attempt) {
        for (const auto& arguments : messages) {
            const auto envelope = qvariant_cast<Envelope>(arguments.constFirst());
            if (envelope.requestId != analysisRequestId) {
                continue;
            }
            if (envelope.type == MessageType::SpeechAnalysisCompleted) {
                analysisResponse = envelope;
                analysisCompleted = true;
                break;
            }
            if (envelope.type == MessageType::Error) {
                analysisError = envelope;
                break;
            }
        }
        if (!analysisCompleted && analysisError.requestId.isEmpty()) {
            QTest::qWait(50);
        }
    }
    const QString analysisDiagnostic =
        analysisError.requestId.isEmpty()
            ? QStringLiteral("workerState=%1 stderr=%2")
                  .arg(static_cast<int>(worker.state()))
                  .arg(QString::fromUtf8(worker.readAllStandardError()))
            : QStringLiteral("workerError=%1 details=%2")
                  .arg(analysisError.payload.value(QStringLiteral("message")).toString(),
                       analysisError.payload.value(QStringLiteral("technicalDetails")).toString());
    QVERIFY2(analysisCompleted, qPrintable(analysisDiagnostic));
    QCOMPARE(analysisResponse.jobId, jobId);
    QCOMPARE(analysisResponse.payload.value(QStringLiteral("durationMs")).toInteger(), fixtureDurationMs);
    const QCborArray chunks = analysisResponse.payload.value(QStringLiteral("chunks")).toArray();
    QCOMPARE(chunks.size(), 1);
    const QCborMap onlyChunk = chunks.at(0).toMap();
    QCOMPARE(onlyChunk.value(QStringLiteral("startMs")).toInteger(), 0);
    QCOMPARE(onlyChunk.value(QStringLiteral("endMs")).toInteger(), fixtureDurationMs);
#endif

    client.sendRequest(MessageType::Shutdown, {}, {});
    QTRY_COMPARE_WITH_TIMEOUT(worker.state(), QProcess::NotRunning, 7'000);
    QCOMPARE(worker.exitStatus(), QProcess::NormalExit);
    QCOMPARE(worker.exitCode(), 0);
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    WorkerProcessTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_WorkerProcess.moc"
