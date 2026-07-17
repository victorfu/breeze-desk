#include <breezedesk/ipc/ApplicationCommand.h>
#include <breezedesk/ipc/LocalEndpoint.h>
#include <breezedesk/ipc/SingleInstanceGuard.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QUuid>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <vector>

using namespace BreezeDesk::Ipc;

class SingleInstanceGuardTest final : public QObject {
    Q_OBJECT

  private slots:
    void forwardsUnicodeFileList();
    void forwardsApplicationCommandResult();
    void declinedCommandAllowsLocalFallback();
    void missingGuiAllowsLocalFallback();
    void startupBeforeHandlerAllowsLocalFallback();
    void disconnectAfterDeliveryIsIndeterminate();
    void startupRaceHasOnePrimary();

  private:
    static QString helperPath();
};

QString SingleInstanceGuardTest::helperPath() {
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("single_instance_race_helper"),
                                                              {QCoreApplication::applicationDirPath()});
    return executable;
}

void SingleInstanceGuardTest::forwardsUnicodeFileList() {
    const QString helper = helperPath();
    QVERIFY2(!helper.isEmpty(), "single_instance_race_helper was not found");
    const QString applicationId =
        QStringLiteral("test.single.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    SingleInstanceGuard primary(applicationId);
    QCOMPARE(primary.acquire(), SingleInstanceGuard::AcquireResult::Primary);
    QSignalSpy activations(&primary, &SingleInstanceGuard::activationRequested);

    const QStringList paths{QStringLiteral("/tmp/會議 錄音.m4a"),
                            QStringLiteral("C:/Users/Test/產品 demo.mp4")};
    QProcess secondary;
    QStringList arguments{QStringLiteral("--application-id"), applicationId};
    arguments.append(paths);
    secondary.start(helper, arguments);
    QVERIFY(secondary.waitForStarted());
    QTRY_COMPARE_WITH_TIMEOUT(activations.size(), 1, 3'000);
    QTRY_COMPARE_WITH_TIMEOUT(secondary.state(), QProcess::NotRunning, 3'000);
    QCOMPARE(secondary.exitCode(), 0);
    QCOMPARE(activations.constFirst().constFirst().toStringList(), paths);
}

void SingleInstanceGuardTest::forwardsApplicationCommandResult() {
    const QString helper = helperPath();
    QVERIFY(!helper.isEmpty());
    const QString applicationId =
        QStringLiteral("test.command.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    SingleInstanceGuard primary(applicationId);
    QCOMPARE(primary.acquire(), SingleInstanceGuard::AcquireResult::Primary);
    QStringList received;
    primary.setCommandHandler([&received](const QStringList& arguments) {
        received = arguments;
        ApplicationCommandReply reply;
        reply.handled = true;
        reply.exitCode = 7;
        reply.standardOutput = QByteArrayLiteral("machine-output\n");
        reply.standardError = QByteArrayLiteral("diagnostic-output\n");
        return reply;
    });

    QProcess client;
    client.start(helper, {QStringLiteral("--application-id"), applicationId, QStringLiteral("--send-command"),
                          QStringLiteral("library"), QStringLiteral("search"), QStringLiteral("台灣 會議")});
    QVERIFY(client.waitForStarted());
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QProcess::NotRunning, 3'000);
    QCOMPARE(client.exitStatus(), QProcess::NormalExit);
    QCOMPARE(client.exitCode(), 7);
    QCOMPARE(client.readAllStandardOutput(), QByteArrayLiteral("machine-output\n"));
    QCOMPARE(client.readAllStandardError(), QByteArrayLiteral("diagnostic-output\n"));
    QCOMPARE(received,
             QStringList({QStringLiteral("library"), QStringLiteral("search"), QStringLiteral("台灣 會議")}));
}

void SingleInstanceGuardTest::declinedCommandAllowsLocalFallback() {
    const QString helper = helperPath();
    QVERIFY(!helper.isEmpty());
    const QString applicationId =
        QStringLiteral("test.declined.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    SingleInstanceGuard primary(applicationId);
    QCOMPARE(primary.acquire(), SingleInstanceGuard::AcquireResult::Primary);
    primary.setCommandHandler([](const QStringList&) { return ApplicationCommandReply{}; });

    QProcess client;
    client.start(helper, {QStringLiteral("--application-id"), applicationId, QStringLiteral("--send-command"),
                          QStringLiteral("library"), QStringLiteral("list")});
    QVERIFY(client.waitForStarted());
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QProcess::NotRunning, 3'000);
    QCOMPARE(client.exitCode(), 41);
}

void SingleInstanceGuardTest::missingGuiAllowsLocalFallback() {
    const QString helper = helperPath();
    QVERIFY(!helper.isEmpty());
    QProcess client;
    client.start(helper,
                 {QStringLiteral("--application-id"),
                  QStringLiteral("test.missing.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)),
                  QStringLiteral("--send-command"), QStringLiteral("--timeout-ms"), QStringLiteral("150"),
                  QStringLiteral("models"), QStringLiteral("list")});
    QVERIFY(client.waitForStarted());
    QVERIFY(client.waitForFinished(2'000));
    QCOMPARE(client.exitCode(), 42);
}

void SingleInstanceGuardTest::startupBeforeHandlerAllowsLocalFallback() {
    const QString helper = helperPath();
    QVERIFY(!helper.isEmpty());
    const QString applicationId =
        QStringLiteral("test.not-ready.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    SingleInstanceGuard primary(applicationId);
    QCOMPARE(primary.acquire(), SingleInstanceGuard::AcquireResult::Primary);

    QProcess client;
    client.start(helper, {QStringLiteral("--application-id"), applicationId, QStringLiteral("--send-command"),
                          QStringLiteral("--timeout-ms"), QStringLiteral("200"), QStringLiteral("import"),
                          QStringLiteral("/tmp/not-executed.wav")});
    QVERIFY(client.waitForStarted());
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QProcess::NotRunning, 2'000);
    QCOMPARE(client.exitCode(), 41);
}

void SingleInstanceGuardTest::disconnectAfterDeliveryIsIndeterminate() {
    const QString helper = helperPath();
    QVERIFY(!helper.isEmpty());
    const QString applicationId =
        QStringLiteral("test.indeterminate.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString endpoint = LocalEndpoint::userScopedName(applicationId, QStringLiteral("application"));
    QLocalServer::removeServer(endpoint);
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY2(server.listen(endpoint), qPrintable(server.errorString()));
    connect(&server, &QLocalServer::newConnection, &server, [&server] {
        QLocalSocket* socket = server.nextPendingConnection();
        QVERIFY(socket != nullptr);
        connect(socket, &QLocalSocket::readyRead, socket, [socket] {
            (void)socket->readAll();
            socket->abort();
        });
    });

    QProcess client;
    client.start(helper, {QStringLiteral("--application-id"), applicationId, QStringLiteral("--send-command"),
                          QStringLiteral("--timeout-ms"), QStringLiteral("500"), QStringLiteral("import"),
                          QStringLiteral("/tmp/maybe-delivered.wav")});
    QVERIFY(client.waitForStarted());
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QProcess::NotRunning, 2'000);
    QCOMPARE(client.exitCode(), 43);
    server.close();
    QLocalServer::removeServer(endpoint);
}

void SingleInstanceGuardTest::startupRaceHasOnePrimary() {
    const QString helper = helperPath();
    QVERIFY(!helper.isEmpty());
    const QString applicationId =
        QStringLiteral("test.race.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    std::vector<std::unique_ptr<QProcess>> processes;
    processes.reserve(6);
    for (int index = 0; index < 6; ++index) {
        auto process = std::make_unique<QProcess>();
        process->start(helper,
                       {QStringLiteral("--application-id"), applicationId, QStringLiteral("--hold-ms"),
                        QStringLiteral("900"), QStringLiteral("file-%1-中文.wav").arg(index)});
        QVERIFY(process->waitForStarted());
        processes.push_back(std::move(process));
    }
    for (const auto& process : processes) {
        QVERIFY(process->waitForFinished(5'000));
        QCOMPARE(process->exitStatus(), QProcess::NormalExit);
        QCOMPARE(process->exitCode(), 0);
    }
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    SingleInstanceGuardTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_SingleInstanceGuard.moc"
