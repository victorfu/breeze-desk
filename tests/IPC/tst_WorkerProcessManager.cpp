#include <breezedesk/app/WorkerProcessManager.h>

#include <QtCore/QCoreApplication>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

using namespace BreezeDesk;

class WorkerProcessManagerTest final : public QObject {
    Q_OBJECT

  private slots:
    void launchesTwoIsolatedWorkers();
};

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

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    WorkerProcessManagerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_WorkerProcessManager.moc"
