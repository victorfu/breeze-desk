#include <breezedesk/app/WorkerProcessManager.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QScopeGuard>
#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

using namespace BreezeDesk;

class WorkerProcessManagerTest final : public QObject {
    Q_OBJECT

  private slots:
    void launchesTwoIsolatedWorkers();
    void retriesUntilDelayedEndpointIsReady();
    void restartsAfterAuthenticatedProtocolFailure();
    void restartsAfterAuthenticatedPeerClose();
};

namespace {

auto useLivenessHelper(const QString& statePath, const QByteArray& mode,
                       const int listenDelayMs = 0) {
    const QByteArray previousWorkerPath = qgetenv("BREEZEDESK_ASR_WORKER_PATH");
    const QByteArray previousOverrideOnly = qgetenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY");
    const QByteArray previousState = qgetenv("BREEZEDESK_TEST_WORKER_LIVENESS_STATE");
    const QByteArray previousMode = qgetenv("BREEZEDESK_TEST_WORKER_LIVENESS_MODE");
    const QByteArray previousListenDelay =
        qgetenv("BREEZEDESK_TEST_WORKER_LIVENESS_LISTEN_DELAY_MS");
    qputenv("BREEZEDESK_ASR_WORKER_PATH", QByteArray(BREEZEDESK_TEST_WORKER_LIVENESS_PATH));
    qputenv("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", "1");
    qputenv("BREEZEDESK_TEST_WORKER_LIVENESS_STATE", statePath.toUtf8());
    qputenv("BREEZEDESK_TEST_WORKER_LIVENESS_MODE", mode);
    qputenv("BREEZEDESK_TEST_WORKER_LIVENESS_LISTEN_DELAY_MS",
            QByteArray::number(listenDelayMs));
    return qScopeGuard([previousWorkerPath, previousOverrideOnly, previousState, previousMode,
                        previousListenDelay] {
        const auto restore = [](const char* name, const QByteArray& value) {
            if (value.isNull()) {
                qunsetenv(name);
            } else {
                qputenv(name, value);
            }
        };
        restore("BREEZEDESK_ASR_WORKER_PATH", previousWorkerPath);
        restore("BREEZEDESK_TEST_ASR_WORKER_OVERRIDE_ONLY", previousOverrideOnly);
        restore("BREEZEDESK_TEST_WORKER_LIVENESS_STATE", previousState);
        restore("BREEZEDESK_TEST_WORKER_LIVENESS_MODE", previousMode);
        restore("BREEZEDESK_TEST_WORKER_LIVENESS_LISTEN_DELAY_MS", previousListenDelay);
    });
}

int launchCount(const QString& path) {
    QFile state(path);
    return state.open(QIODevice::ReadOnly) ? QString::fromUtf8(state.readAll()).trimmed().toInt() : 0;
}

void verifyCapabilities(WorkerProcessManager& manager) {
    QSignalSpy messages(manager.client(), &Ipc::AsrWorkerClient::envelopeReceived);
    const QString requestId =
        manager.client()->sendRequest(Ipc::MessageType::GetCapabilities, {}, {});
    QVERIFY(!requestId.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 2'000);
    const auto envelope = qvariant_cast<Ipc::Envelope>(messages.constFirst().constFirst());
    QCOMPARE(envelope.type, Ipc::MessageType::Capabilities);
    QCOMPARE(envelope.requestId, requestId);
}

} // namespace

void WorkerProcessManagerTest::launchesTwoIsolatedWorkers() {
    WorkerProcessManager first;
    WorkerProcessManager second;
    QSignalSpy firstReady(&first, &WorkerProcessManager::readyChanged);
    QSignalSpy secondReady(&second, &WorkerProcessManager::readyChanged);
    QVERIFY2(first.start(), qPrintable(first.lastError()));
    QVERIFY2(second.start(), qPrintable(second.lastError()));
    QTRY_VERIFY_WITH_TIMEOUT(first.isReady(), 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(second.isReady(), 5'000);
    QVERIFY(!firstReady.isEmpty());
    QVERIFY(!secondReady.isEmpty());

    QSignalSpy firstMessages(first.client(), &Ipc::AsrWorkerClient::envelopeReceived);
    QSignalSpy secondMessages(second.client(), &Ipc::AsrWorkerClient::envelopeReceived);
    const QString firstRequest = first.client()->sendRequest(Ipc::MessageType::GetCapabilities, {}, {});
    const QString secondRequest = second.client()->sendRequest(Ipc::MessageType::GetCapabilities, {}, {});
    QVERIFY(!firstRequest.isEmpty());
    QVERIFY(!secondRequest.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!firstMessages.isEmpty(), 2'000);
    QTRY_VERIFY_WITH_TIMEOUT(!secondMessages.isEmpty(), 2'000);
    QCOMPARE(qvariant_cast<Ipc::Envelope>(firstMessages.constFirst().constFirst()).requestId, firstRequest);
    QCOMPARE(qvariant_cast<Ipc::Envelope>(secondMessages.constFirst().constFirst()).requestId, secondRequest);

    first.stop();
    second.stop();
    QVERIFY(!first.isReady());
    QVERIFY(!second.isReady());
}

void WorkerProcessManagerTest::retriesUntilDelayedEndpointIsReady() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString statePath = directory.filePath(QStringLiteral("delayed-listen-launches.txt"));
    const auto restoreEnvironment =
        useLivenessHelper(statePath, QByteArrayLiteral("normal"), 300);

    WorkerProcessManager manager;
    QSignalSpy interruptions(&manager, &WorkerProcessManager::workerInterrupted);
    QVERIFY2(manager.start(), qPrintable(manager.lastError()));
    QTRY_VERIFY_WITH_TIMEOUT(manager.isReady(), 5'000);
    QCOMPARE(launchCount(statePath), 1);
    QCOMPARE(interruptions.size(), 0);
    QVERIFY(manager.lastError().isEmpty());
    verifyCapabilities(manager);
}

void WorkerProcessManagerTest::restartsAfterAuthenticatedProtocolFailure() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString statePath = directory.filePath(QStringLiteral("protocol-launches.txt"));
    const auto restoreEnvironment = useLivenessHelper(statePath, QByteArrayLiteral("malformed"));

    WorkerProcessManager manager;
    QSignalSpy interruptions(&manager, &WorkerProcessManager::workerInterrupted);
    QVERIFY2(manager.start(), qPrintable(manager.lastError()));
    QTRY_VERIFY_WITH_TIMEOUT(manager.isReady(), 5'000);
    QCOMPARE(launchCount(statePath), 1);

    QVERIFY(!manager.client()
                 ->sendRequest(Ipc::MessageType::GetCapabilities, {},
                               {{QStringLiteral("triggerMalformed"), true}})
                 .isEmpty());

    QTRY_COMPARE_WITH_TIMEOUT(interruptions.size(), 1, 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(manager.isReady(), 5'000);
    QTRY_COMPARE_WITH_TIMEOUT(launchCount(statePath), 2, 2'000);
    QTest::qWait(300);
    QCOMPARE(interruptions.size(), 1);
    verifyCapabilities(manager);
}

void WorkerProcessManagerTest::restartsAfterAuthenticatedPeerClose() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString statePath = directory.filePath(QStringLiteral("disconnect-launches.txt"));
    const auto restoreEnvironment = useLivenessHelper(statePath, QByteArrayLiteral("peer-close"));

    WorkerProcessManager manager;
    QSignalSpy interruptions(&manager, &WorkerProcessManager::workerInterrupted);
    QVERIFY2(manager.start(), qPrintable(manager.lastError()));
    QTRY_VERIFY_WITH_TIMEOUT(manager.isReady(), 5'000);
    QCOMPARE(launchCount(statePath), 1);
    QVERIFY(!manager.client()
                 ->sendRequest(Ipc::MessageType::GetCapabilities, {},
                               {{QStringLiteral("triggerPeerClose"), true}})
                 .isEmpty());

    QTRY_COMPARE_WITH_TIMEOUT(interruptions.size(), 1, 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(manager.isReady(), 5'000);
    QTRY_COMPARE_WITH_TIMEOUT(launchCount(statePath), 2, 2'000);
    QTest::qWait(300);
    QCOMPARE(interruptions.size(), 1);
    verifyCapabilities(manager);
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    WorkerProcessManagerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_WorkerProcessManager.moc"
