#include "breezedesk/models/ModelFileOperations.h"
#include "breezedesk/models/ModelManager.h"
#include "breezedesk/ui/ModelManagerViewModel.h"
#include "ModelManagerViewModelOperations.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

using namespace BreezeDesk;

namespace {

constexpr auto TestModelId = "breeze-asr-25-q5";

class EnvironmentVariableGuard final {
  public:
    explicit EnvironmentVariableGuard(QByteArray name)
        : m_name(std::move(name)),
          m_wasSet(qEnvironmentVariableIsSet(m_name.constData())),
          m_value(qgetenv(m_name.constData())) {}

    ~EnvironmentVariableGuard() {
        if (m_wasSet) {
            qputenv(m_name.constData(), m_value);
        } else {
            qunsetenv(m_name.constData());
        }
    }

  private:
    QByteArray m_name;
    bool m_wasSet{false};
    QByteArray m_value;
};

bool writeModelFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QByteArray(2'048, '\x01')) == 2'048;
}

QModelIndex modelIndex(ModelManagerViewModel& viewModel, const QString& id) {
    QAbstractItemModel* models = viewModel.models();
    for (int row = 0; row < models->rowCount(); ++row) {
        const QModelIndex candidate = models->index(row, 0);
        if (models->data(candidate, ModelListModel::IdRole).toString() == id) {
            return candidate;
        }
    }
    return {};
}

ModelVerificationResult successfulVerification(const ModelVerificationSnapshot& snapshot) {
    ModelVerificationResult result;
    result.id = snapshot.id;
    result.installed = true;
    result.valid = true;
    return result;
}

} // namespace

class ModelManagerViewModelTest final : public QObject {
    Q_OBJECT

  private slots:
    void destructorCancelsAndDrainsWorker();
    void serviceReplacementDropsQueuedVerification();
    void duplicateVerifyAndRemoveAreRejected();
    void duplicateImportCommitsOnlyOnOwnerThread();
    void stalePreparedImportIsCleanedOnServiceReplacement();
    void failedPreparedImportIsCleaned();
};

void ModelManagerViewModelTest::destructorCancelsAndDrainsWorker() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    EnvironmentVariableGuard dataRootGuard(QByteArrayLiteral("BREEZEDESK_DATA_ROOT"));
    QVERIFY(qputenv("BREEZEDESK_DATA_ROOT", temporary.path().toUtf8()));

    QSemaphore started;
    std::atomic_bool workerFinished{false};
    Internal::ModelManagerViewModelOperations operations;
    operations.verify = [&started, &workerFinished](
                            ModelVerificationSnapshot snapshot,
                            const Internal::ModelManagerViewModelOperations::Cancellation& cancellation) {
        started.release();
        while (!cancellation->load(std::memory_order_relaxed)) {
            QThread::yieldCurrentThread();
        }
        ModelVerificationResult result;
        result.id = snapshot.id;
        result.cancelled = true;
        workerFinished.store(true, std::memory_order_relaxed);
        return result;
    };

    ModelManager manager;
    auto viewModel = Internal::ModelManagerViewModelTestAccess::create(std::move(operations));
    viewModel->installServices(&manager);
    viewModel->verify(QString::fromLatin1(TestModelId));
    QVERIFY2(started.tryAcquire(1, 2'000), "The deterministic verify worker did not start.");

    viewModel.reset();
    QVERIFY2(workerFinished.load(std::memory_order_relaxed),
             "The view-model destructor returned before its worker finished.");
}

void ModelManagerViewModelTest::serviceReplacementDropsQueuedVerification() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    EnvironmentVariableGuard dataRootGuard(QByteArrayLiteral("BREEZEDESK_DATA_ROOT"));
    QVERIFY(qputenv("BREEZEDESK_DATA_ROOT", temporary.path().toUtf8()));

    QSemaphore returned;
    Internal::ModelManagerViewModelOperations operations;
    operations.verify = [&returned](
                            ModelVerificationSnapshot snapshot,
                            const Internal::ModelManagerViewModelOperations::Cancellation&) {
        const ModelVerificationResult result = successfulVerification(snapshot);
        returned.release();
        return result;
    };

    ModelManager original;
    ModelManager replacement;
    auto viewModel = Internal::ModelManagerViewModelTestAccess::create(std::move(operations));
    viewModel->installServices(&original);
    QSignalSpy succeeded(viewModel.get(), &ModelManagerViewModel::operationSucceeded);
    viewModel->verify(QString::fromLatin1(TestModelId));
    QVERIFY2(returned.tryAcquire(1, 2'000), "The deterministic verify worker did not return.");

    viewModel->installServices(&replacement);
    QCoreApplication::processEvents();

    QCOMPARE(succeeded.count(), 0);
    const QModelIndex index = modelIndex(*viewModel, QString::fromLatin1(TestModelId));
    QVERIFY(index.isValid());
    QCOMPARE(viewModel->models()->data(index, ModelListModel::StateRole).toString(),
             QStringLiteral("NotInstalled"));
}

void ModelManagerViewModelTest::duplicateVerifyAndRemoveAreRejected() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    EnvironmentVariableGuard dataRootGuard(QByteArrayLiteral("BREEZEDESK_DATA_ROOT"));
    QVERIFY(qputenv("BREEZEDESK_DATA_ROOT", temporary.path().toUtf8()));

    QSemaphore started;
    QSemaphore release;
    std::atomic_int calls{0};
    Internal::ModelManagerViewModelOperations operations;
    operations.verify = [&started, &release, &calls](
                            ModelVerificationSnapshot snapshot,
                            const Internal::ModelManagerViewModelOperations::Cancellation& cancellation) {
        calls.fetch_add(1, std::memory_order_relaxed);
        started.release();
        while (!release.tryAcquire(1, 10)) {
            if (cancellation->load(std::memory_order_relaxed)) {
                ModelVerificationResult cancelled;
                cancelled.id = snapshot.id;
                cancelled.cancelled = true;
                return cancelled;
            }
        }
        return successfulVerification(snapshot);
    };

    ModelManager manager;
    const QString modelPath = manager.modelPath(QString::fromLatin1(TestModelId));
    QVERIFY(writeModelFile(modelPath));
    auto viewModel = Internal::ModelManagerViewModelTestAccess::create(std::move(operations));
    viewModel->installServices(&manager);
    QSignalSpy rejected(viewModel.get(), &ModelManagerViewModel::commandRejected);

    viewModel->verify(QString::fromLatin1(TestModelId));
    QVERIFY2(started.tryAcquire(1, 2'000), "The deterministic verify worker did not start.");
    viewModel->verify(QString::fromLatin1(TestModelId));
    QCOMPARE(calls.load(std::memory_order_relaxed), 1);
    QCOMPARE(rejected.count(), 1);

    viewModel->remove(QString::fromLatin1(TestModelId));
    QCOMPARE(rejected.count(), 2);
    QVERIFY(QFileInfo::exists(modelPath));

    QVERIFY(QFile::remove(modelPath));
    release.release();
    QTRY_COMPARE_WITH_TIMEOUT(rejected.count(), 3, 2'000);
    const QModelIndex index = modelIndex(*viewModel, QString::fromLatin1(TestModelId));
    QVERIFY(index.isValid());
    QVERIFY(!viewModel->models()->data(index, ModelListModel::InstalledRole).toBool());
}

void ModelManagerViewModelTest::duplicateImportCommitsOnlyOnOwnerThread() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    EnvironmentVariableGuard dataRootGuard(QByteArrayLiteral("BREEZEDESK_DATA_ROOT"));
    QVERIFY(qputenv("BREEZEDESK_DATA_ROOT", temporary.path().toUtf8()));
    const QString sourcePath = temporary.filePath(QStringLiteral("custom.bin"));
    QVERIFY(writeModelFile(sourcePath));

    QSemaphore started;
    QSemaphore release;
    std::atomic_int calls{0};
    std::mutex threadMutex;
    std::thread::id workerThread;
    Internal::ModelManagerViewModelOperations operations;
    operations.prepareImport =
        [&started, &release, &calls, &threadMutex, &workerThread](
            CustomModelImportRequest request,
            const Internal::ModelManagerViewModelOperations::Cancellation& cancellation) {
            calls.fetch_add(1, std::memory_order_relaxed);
            {
                const std::lock_guard lock(threadMutex);
                workerThread = std::this_thread::get_id();
            }
            started.release();
            while (!release.tryAcquire(1, 10)) {
                if (cancellation->load(std::memory_order_relaxed)) {
                    PreparedCustomModelImport cancelled;
                    cancelled.request = request;
                    cancelled.cancelled = true;
                    return cancelled;
                }
            }
            return ModelFileOperations::prepareImport(request, cancellation.get());
        };

    ModelManager manager;
    const std::thread::id ownerThread = std::this_thread::get_id();
    std::thread::id commitThread;
    connect(&manager, &ModelManager::modelsChanged, &manager,
            [&commitThread] { commitThread = std::this_thread::get_id(); }, Qt::DirectConnection);
    auto viewModel = Internal::ModelManagerViewModelTestAccess::create(std::move(operations));
    viewModel->installServices(&manager);
    QSignalSpy rejected(viewModel.get(), &ModelManagerViewModel::commandRejected);
    QSignalSpy succeeded(viewModel.get(), &ModelManagerViewModel::operationSucceeded);

    const QUrl sourceUrl = QUrl::fromLocalFile(sourcePath);
    viewModel->importCustom(sourceUrl);
    QVERIFY2(started.tryAcquire(1, 2'000), "The deterministic import worker did not start.");
    viewModel->importCustom(sourceUrl);
    QCOMPARE(calls.load(std::memory_order_relaxed), 1);
    QCOMPARE(rejected.count(), 1);

    release.release();
    QTRY_COMPARE_WITH_TIMEOUT(succeeded.count(), 1, 2'000);
    QCOMPARE(manager.customModels().size(), 1);
    QVERIFY(commitThread == ownerThread);
    {
        const std::lock_guard lock(threadMutex);
        QVERIFY(workerThread != ownerThread);
    }
    const CustomModelInfo imported = manager.customModels().constFirst();
    QVERIFY(!QFileInfo::exists(imported.path + QStringLiteral(".importing")));
}

void ModelManagerViewModelTest::stalePreparedImportIsCleanedOnServiceReplacement() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    EnvironmentVariableGuard dataRootGuard(QByteArrayLiteral("BREEZEDESK_DATA_ROOT"));
    QVERIFY(qputenv("BREEZEDESK_DATA_ROOT", temporary.path().toUtf8()));
    const QString sourcePath = temporary.filePath(QStringLiteral("stale.bin"));
    QVERIFY(writeModelFile(sourcePath));

    QSemaphore preparedReady;
    std::mutex requestMutex;
    CustomModelImportRequest preparedRequest;
    std::thread::id cleanupThread;
    Internal::ModelManagerViewModelOperations operations;
    operations.prepareImport =
        [&preparedReady, &requestMutex, &preparedRequest](
            CustomModelImportRequest request,
            const Internal::ModelManagerViewModelOperations::Cancellation& cancellation) {
            PreparedCustomModelImport prepared =
                ModelFileOperations::prepareImport(request, cancellation.get());
            {
                const std::lock_guard lock(requestMutex);
                preparedRequest = request;
            }
            preparedReady.release();
            return prepared;
        };
    operations.cleanupPreparedImport = [&cleanupThread](const PreparedCustomModelImport& prepared) {
        cleanupThread = std::this_thread::get_id();
        ModelFileOperations::cleanupPreparedImport(prepared);
    };

    ModelManager original;
    ModelManager replacement;
    const std::thread::id ownerThread = std::this_thread::get_id();
    auto viewModel = Internal::ModelManagerViewModelTestAccess::create(std::move(operations));
    viewModel->installServices(&original);
    QSignalSpy succeeded(viewModel.get(), &ModelManagerViewModel::operationSucceeded);
    viewModel->importCustom(QUrl::fromLocalFile(sourcePath));
    QVERIFY2(preparedReady.tryAcquire(1, 2'000), "The deterministic import worker did not prepare a file.");

    CustomModelImportRequest request;
    {
        const std::lock_guard lock(requestMutex);
        request = preparedRequest;
    }
    QVERIFY(QFileInfo::exists(request.stagingPath));
    viewModel->installServices(&replacement);
    QCoreApplication::processEvents();

    QCOMPARE(succeeded.count(), 0);
    QVERIFY(cleanupThread == ownerThread);
    QVERIFY(!QFileInfo::exists(request.stagingPath));
    QVERIFY(!QFileInfo::exists(request.destinationPath));
    QVERIFY(!QFileInfo::exists(request.checksumPath));
    QVERIFY(original.customModels().isEmpty());
    QVERIFY(replacement.customModels().isEmpty());
}

void ModelManagerViewModelTest::failedPreparedImportIsCleaned() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    EnvironmentVariableGuard dataRootGuard(QByteArrayLiteral("BREEZEDESK_DATA_ROOT"));
    QVERIFY(qputenv("BREEZEDESK_DATA_ROOT", temporary.path().toUtf8()));
    const QString sourcePath = temporary.filePath(QStringLiteral("failure.bin"));
    QVERIFY(writeModelFile(sourcePath));

    std::mutex requestMutex;
    CustomModelImportRequest failedRequest;
    Internal::ModelManagerViewModelOperations operations;
    operations.prepareImport = [&requestMutex, &failedRequest](
                                   CustomModelImportRequest request,
                                   const Internal::ModelManagerViewModelOperations::Cancellation&) {
        QFile staging(request.stagingPath);
        if (staging.open(QIODevice::WriteOnly)) {
            staging.write("unpublished", 11);
            staging.close();
        }
        {
            const std::lock_guard lock(requestMutex);
            failedRequest = request;
        }
        PreparedCustomModelImport failed;
        failed.request = request;
        failed.error = QStringLiteral("Injected import failure.");
        return failed;
    };

    ModelManager manager;
    auto viewModel = Internal::ModelManagerViewModelTestAccess::create(std::move(operations));
    viewModel->installServices(&manager);
    QSignalSpy rejected(viewModel.get(), &ModelManagerViewModel::commandRejected);
    viewModel->importCustom(QUrl::fromLocalFile(sourcePath));
    QTRY_COMPARE_WITH_TIMEOUT(rejected.count(), 1, 2'000);

    CustomModelImportRequest request;
    {
        const std::lock_guard lock(requestMutex);
        request = failedRequest;
    }
    QCOMPARE(rejected.constFirst().constFirst().toString(), QStringLiteral("Injected import failure."));
    QVERIFY(!QFileInfo::exists(request.stagingPath));
    QVERIFY(!QFileInfo::exists(request.destinationPath));
    QVERIFY(!QFileInfo::exists(request.checksumPath));
    QVERIFY(manager.customModels().isEmpty());
}

QTEST_GUILESS_MAIN(ModelManagerViewModelTest)
#include "tst_ModelManagerViewModel.moc"
