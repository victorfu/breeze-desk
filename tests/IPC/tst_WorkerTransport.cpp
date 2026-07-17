#include <breezedesk/ipc/AsrWorkerClient.h>
#include <breezedesk/ipc/LocalEndpoint.h>
#include <breezedesk/ipc/WorkerServer.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QUuid>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

using namespace BreezeDesk::Ipc;

class WorkerTransportTest final : public QObject {
    Q_OBJECT

  private slots:
    void handshakeAndRequest();
    void rejectsWrongSessionToken();
};

void WorkerTransportTest::handshakeAndRequest() {
    const QString endpoint = LocalEndpoint::userScopedName(
        QStringLiteral("test.%1").arg(QUuid::createUuid().toString()), QStringLiteral("worker"));
    const QByteArray token(32, 'a');
    WorkerServer server;
    QVERIFY2(server.listen(endpoint, token), qPrintable(server.errorString()));
    QSignalSpy serverRequest(&server, &WorkerServer::envelopeReceived);

    AsrWorkerClient client(QStringLiteral("test-client"));
    QSignalSpy ready(&client, &AsrWorkerClient::ready);
    QSignalSpy clientErrors(&client, &AsrWorkerClient::protocolError);
    client.connectToWorker(endpoint, token);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 2'000);
    QVERIFY(client.isReady());
    QCOMPARE(clientErrors.size(), 0);

    const QString requestId =
        client.sendRequest(MessageType::GetCapabilities, {}, {{QStringLiteral("detail"), true}});
    QVERIFY(!requestId.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(serverRequest.size(), 1, 2'000);
    const auto arguments = serverRequest.takeFirst();
    const auto envelope = qvariant_cast<Envelope>(arguments.at(1));
    QCOMPARE(envelope.type, MessageType::GetCapabilities);
    QCOMPARE(envelope.requestId, requestId);
}

void WorkerTransportTest::rejectsWrongSessionToken() {
    const QString endpoint = LocalEndpoint::userScopedName(
        QStringLiteral("test.%1").arg(QUuid::createUuid().toString()), QStringLiteral("worker"));
    WorkerServer server;
    QVERIFY(server.listen(endpoint, QByteArray(32, 'a')));
    QSignalSpy serverErrors(&server, &WorkerServer::protocolError);

    AsrWorkerClient client(QStringLiteral("test-client"));
    QSignalSpy ready(&client, &AsrWorkerClient::ready);
    client.connectToWorker(endpoint, QByteArray(32, 'b'));
    QTRY_VERIFY_WITH_TIMEOUT(serverErrors.size() >= 1, 2'000);
    QCOMPARE(ready.size(), 0);
    const auto error = qvariant_cast<ProtocolError>(serverErrors.constFirst().at(1));
    QCOMPARE(error.code, ProtocolErrorCode::AuthenticationFailed);
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    WorkerTransportTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_WorkerTransport.moc"
