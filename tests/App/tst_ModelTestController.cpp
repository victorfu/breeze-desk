#include "breezedesk/app/ModelTestController.h"

#include "breezedesk/ipc/AsrWorkerClient.h"
#include "breezedesk/ipc/IAsrWorkerClient.h"
#include "breezedesk/ipc/LocalEndpoint.h"
#include "breezedesk/models/ModelManager.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest>

using namespace BreezeDesk;

namespace {

constexpr auto TestModelId = "breeze-asr-25-q5";

class FakeWorkerClient final : public Ipc::IAsrWorkerClient {
    Q_OBJECT

  public:
    enum class Behavior { Success, WaitForCancellation, SilentModelLoad };

    explicit FakeWorkerClient(Behavior behavior, QObject* parent = nullptr)
        : Ipc::IAsrWorkerClient(parent), m_behavior(behavior) {}

    void connectToWorker(const QString&, const QByteArray&) override {}
    void disconnectFromWorker() override {}
    [[nodiscard]] bool isReady() const noexcept override { return true; }

    QString sendRequest(Ipc::MessageType type, const QString& jobId, const QCborMap& payload) override {
        const QString requestId = QStringLiteral("request-%1").arg(++m_nextRequest);
        if (type == Ipc::MessageType::LoadModel) {
            ++loadRequests;
            loadedPath = payload.value(QStringLiteral("modelPath")).toString();
            loadedSha256 = payload.value(QStringLiteral("modelSha256")).toString();
            if (m_behavior != Behavior::SilentModelLoad) {
                QTimer::singleShot(0, this, [this, requestId] {
                    Ipc::Envelope loaded;
                    loaded.type = Ipc::MessageType::ModelLoaded;
                    loaded.requestId = requestId;
                    loaded.payload.insert(QStringLiteral("selectedBackend"), QStringLiteral("Auto"));
                    loaded.payload.insert(QStringLiteral("actualBackend"), QStringLiteral("CPU"));
                    loaded.payload.insert(QStringLiteral("runtimeVersion"), QStringLiteral("test-whisper-1"));
                    loaded.payload.insert(QStringLiteral("loadTimeMs"), 17);
                    emit envelopeReceived(loaded);
                });
            }
        } else if (type == Ipc::MessageType::StartTranscription) {
            ++transcriptionRequests;
            transcriptionRequestId = requestId;
            transcriptionJobId = jobId;
            fixturePath = payload.value(QStringLiteral("pcmPath")).toString();
            fixtureSize = QFileInfo(fixturePath).size();
            QTimer::singleShot(0, this, [this, requestId, jobId] {
                Ipc::Envelope progress;
                progress.type = Ipc::MessageType::Progress;
                progress.requestId = requestId;
                progress.jobId = jobId;
                progress.payload.insert(QStringLiteral("progress"), 50);
                emit envelopeReceived(progress);
                emit inferenceStarted();
                if (m_behavior == Behavior::Success) {
                    Ipc::Envelope completed;
                    completed.type = Ipc::MessageType::TranscriptionCompleted;
                    completed.requestId = requestId;
                    completed.jobId = jobId;
                    completed.payload.insert(QStringLiteral("segmentCount"), 0);
                    emit envelopeReceived(completed);
                }
            });
        } else if (type == Ipc::MessageType::CancelJob) {
            ++cancelRequests;
            QTimer::singleShot(0, this, [this] {
                Ipc::Envelope cancelled;
                cancelled.type = Ipc::MessageType::JobCancelled;
                cancelled.requestId = transcriptionRequestId;
                cancelled.jobId = transcriptionJobId;
                cancelled.payload.insert(QStringLiteral("partialResultsPreserved"), true);
                emit envelopeReceived(cancelled);
            });
        } else if (type == Ipc::MessageType::UnloadModel) {
            ++unloadRequests;
            QTimer::singleShot(0, this, [this, requestId] {
                Ipc::Envelope unloaded;
                unloaded.type = Ipc::MessageType::UnloadModel;
                unloaded.requestId = requestId;
                unloaded.payload.insert(QStringLiteral("unloaded"), true);
                emit envelopeReceived(unloaded);
            });
        }
        return requestId;
    }

    int loadRequests{0};
    int transcriptionRequests{0};
    int cancelRequests{0};
    int unloadRequests{0};
    qint64 fixtureSize{-1};
    QString loadedPath;
    QString loadedSha256;
    QString fixturePath;

  signals:
    void inferenceStarted();

  private:
    Behavior m_behavior;
    int m_nextRequest{0};
    QString transcriptionRequestId;
    QString transcriptionJobId;
};

bool writeModelPlaceholder(ModelManager& models) {
    QFile model(models.modelPath(QString::fromLatin1(TestModelId)));
    return model.open(QIODevice::WriteOnly) && model.write(QByteArray(2'048, 'm')) == 2'048;
}

ModelTestDependencies dependencies(ModelManager* models, FakeWorkerClient* worker,
                                   const QTemporaryDir& directory) {
    ModelTestDependencies result;
    result.models = models;
    result.workerClient = worker;
    result.ensureWorkerStarted = [] { return true; };
    result.temporaryDirectory = directory.filePath(QStringLiteral("temp"));
    result.timeouts = {100, 100, 100, 100};
    return result;
}

} // namespace

class ModelTestControllerTest final : public QObject {
    Q_OBJECT

  private slots:
    void runsNativeWorkerProtocolAndCleansFixture();
    void rejectsBusyAndMissingModels();
    void cancelsInferenceAndUnloadsModel();
    void abortsWorkerOnTimeout();
    void nativeWorkerSmokeWhenConfigured();
};

void ModelTestControllerTest::runsNativeWorkerProtocolAndCleansFixture() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    ModelManager models;
    QVERIFY(writeModelPlaceholder(models));
    FakeWorkerClient worker(FakeWorkerClient::Behavior::Success);
    bool reserved = false;
    bool invalidated = false;
    auto deps = dependencies(&models, &worker, directory);
    deps.workerReserved = [&reserved] { return reserved; };
    deps.setExternalWorkerReserved = [&reserved](bool value) { reserved = value; };
    deps.invalidateWorkerModelCache = [&invalidated] { invalidated = true; };
    ModelTestController controller(std::move(deps));
    QSignalSpy succeeded(&controller, &ModelTestController::testSucceeded);
    QSignalSpy failed(&controller, &ModelTestController::testFailed);

    controller.testModel(QString::fromLatin1(TestModelId));
    QTRY_COMPARE_WITH_TIMEOUT(succeeded.size(), 1, 2'000);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(worker.loadRequests, 1);
    QCOMPARE(worker.loadedSha256,
             QString::fromLatin1(models.expectedSha256(QString::fromLatin1(TestModelId))));
    QCOMPARE(worker.transcriptionRequests, 1);
    QCOMPARE(worker.unloadRequests, 1);
    QCOMPARE(worker.fixtureSize, 3'000LL * 16'000LL * 2LL / 1'000LL);
    QVERIFY(!QFileInfo::exists(worker.fixturePath));
    QVERIFY(invalidated);
    QVERIFY(!reserved);
    QCOMPARE(succeeded.constFirst().at(2).toString(), QStringLiteral("CPU"));
    QCOMPARE(succeeded.constFirst().at(3).toString(), QStringLiteral("test-whisper-1"));

    QString removalError;
    QVERIFY2(models.removeModel(QString::fromLatin1(TestModelId), &removalError), qPrintable(removalError));
}

void ModelTestControllerTest::rejectsBusyAndMissingModels() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    ModelManager models;
    FakeWorkerClient worker(FakeWorkerClient::Behavior::Success);
    bool busy = true;
    auto deps = dependencies(&models, &worker, directory);
    deps.workerReserved = [&busy] { return busy; };
    ModelTestController controller(std::move(deps));
    QSignalSpy failed(&controller, &ModelTestController::testFailed);

    QVERIFY(writeModelPlaceholder(models));
    controller.testModel(QString::fromLatin1(TestModelId));
    QCOMPARE(failed.size(), 1);
    QCOMPARE(worker.loadRequests, 0);

    busy = false;
    QVERIFY(QFile::remove(models.modelPath(QString::fromLatin1(TestModelId))));
    controller.testModel(QString::fromLatin1(TestModelId));
    QCOMPARE(failed.size(), 2);
    QCOMPARE(worker.loadRequests, 0);
}

void ModelTestControllerTest::cancelsInferenceAndUnloadsModel() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    ModelManager models;
    QVERIFY(writeModelPlaceholder(models));
    FakeWorkerClient worker(FakeWorkerClient::Behavior::WaitForCancellation);
    bool reserved = false;
    auto deps = dependencies(&models, &worker, directory);
    deps.setExternalWorkerReserved = [&reserved](bool value) { reserved = value; };
    ModelTestController controller(std::move(deps));
    QSignalSpy inferenceStarted(&worker, &FakeWorkerClient::inferenceStarted);
    QSignalSpy cancelled(&controller, &ModelTestController::testCancelled);
    QSignalSpy failed(&controller, &ModelTestController::testFailed);

    controller.testModel(QString::fromLatin1(TestModelId));
    QVERIFY(inferenceStarted.wait(1'000));
    controller.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.size(), 1, 2'000);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(worker.cancelRequests, 1);
    QCOMPARE(worker.unloadRequests, 1);
    QVERIFY(!QFileInfo::exists(worker.fixturePath));
    QVERIFY(!reserved);
}

void ModelTestControllerTest::abortsWorkerOnTimeout() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    ModelManager models;
    QVERIFY(writeModelPlaceholder(models));
    FakeWorkerClient worker(FakeWorkerClient::Behavior::SilentModelLoad);
    bool reserved = false;
    bool aborted = false;
    auto deps = dependencies(&models, &worker, directory);
    deps.setExternalWorkerReserved = [&reserved](bool value) { reserved = value; };
    deps.abortWorker = [&aborted] { aborted = true; };
    deps.timeouts.modelLoadMs = 20;
    ModelTestController controller(std::move(deps));
    QSignalSpy failed(&controller, &ModelTestController::testFailed);

    controller.testModel(QString::fromLatin1(TestModelId));
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 1'000);
    QVERIFY(aborted);
    QVERIFY(!reserved);
    QVERIFY(!controller.isRunning());
    QVERIFY(failed.constFirst().at(1).toString().contains(QStringLiteral("timed out")));
}

void ModelTestControllerTest::nativeWorkerSmokeWhenConfigured() {
    const QString sourceModel = qEnvironmentVariable("BREEZEDESK_TEST_BREEZE_MODEL_PATH");
    const QString workerPath = qEnvironmentVariable("BREEZEDESK_TEST_ASR_WORKER_PATH");
    if (!QFileInfo(sourceModel).isFile() || !QFileInfo(workerPath).isExecutable()) {
        QSKIP("Set BREEZEDESK_TEST_BREEZE_MODEL_PATH and BREEZEDESK_TEST_ASR_WORKER_PATH to run this smoke "
              "test.",
              0);
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());
    ModelManager models;
    const QString installedPath = models.modelPath(QString::fromLatin1(TestModelId));
    QVERIFY2(QFile::link(sourceModel, installedPath), qPrintable(installedPath));

    const QByteArray sessionToken(32, 't');
    const QString endpoint = Ipc::LocalEndpoint::userScopedName(
        QStringLiteral("mt.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QProcess worker;
    const auto cleanupWorker = qScopeGuard([&worker] {
        if (worker.state() != QProcess::NotRunning) {
            worker.kill();
            worker.waitForFinished(2'000);
        }
    });
    worker.setProcessChannelMode(QProcess::SeparateChannels);
    worker.start(workerPath, {QStringLiteral("--server"), endpoint, QStringLiteral("--session-token"),
                              QString::fromLatin1(sessionToken.toBase64(QByteArray::Base64UrlEncoding |
                                                                        QByteArray::OmitTrailingEquals)),
                              QStringLiteral("--worker-version"), QStringLiteral("model-controller-smoke")});
    QVERIFY2(worker.waitForStarted(5'000), qPrintable(worker.errorString()));

    Ipc::AsrWorkerClient client(QStringLiteral("model-controller-smoke"));
    QSignalSpy ready(&client, &Ipc::AsrWorkerClient::ready);
    for (int attempt = 0; attempt < 100 && ready.isEmpty(); ++attempt) {
        client.connectToWorker(endpoint, sessionToken);
        QTest::qWait(50);
    }
    const QString connectionDiagnostic =
        QStringLiteral("worker state=%1 error=%2 stderr=%3")
            .arg(static_cast<int>(worker.state()))
            .arg(worker.errorString(), QString::fromUtf8(worker.readAllStandardError()));
    QVERIFY2(!ready.isEmpty(), qPrintable(connectionDiagnostic));

    ModelTestDependencies deps;
    deps.models = &models;
    deps.workerClient = &client;
    deps.ensureWorkerStarted = [] { return true; };
    deps.abortWorker = [&worker] { worker.kill(); };
    deps.temporaryDirectory = directory.filePath(QStringLiteral("temp"));
    ModelTestController controller(std::move(deps));
    controller.setBackendPreference(QStringLiteral("Auto"), false);
    QSignalSpy succeeded(&controller, &ModelTestController::testSucceeded);
    QSignalSpy failed(&controller, &ModelTestController::testFailed);

    controller.testModel(QString::fromLatin1(TestModelId));
    QTRY_VERIFY_WITH_TIMEOUT(!succeeded.isEmpty() || !failed.isEmpty(), 240'000);
    QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.constFirst().at(1).toString()));
    QCOMPARE(succeeded.size(), 1);
    QVERIFY(!succeeded.constFirst().at(2).toString().isEmpty());
    QVERIFY(!succeeded.constFirst().at(3).toString().isEmpty());

    client.sendRequest(Ipc::MessageType::Shutdown, {}, {});
    QTRY_COMPARE_WITH_TIMEOUT(worker.state(), QProcess::NotRunning, 10'000);
}

QTEST_GUILESS_MAIN(ModelTestControllerTest)

#include "tst_ModelTestController.moc"
