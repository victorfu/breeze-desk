#include "breezedesk/app/MaintenanceController.h"

#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/ipc/IAsrWorkerClient.h"
#include "breezedesk/update/UpdateCoordinator.h"

#include <QCborMap>
#include <QFile>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtTest>

using namespace BreezeDesk;

namespace {

class FakeWorkerClient final : public Ipc::IAsrWorkerClient {
    Q_OBJECT

  public:
    using IAsrWorkerClient::IAsrWorkerClient;

    void connectToWorker(const QString&, const QByteArray&) override { m_ready = true; }
    void disconnectFromWorker() override {
        m_ready = false;
        emit disconnected();
    }
    [[nodiscard]] bool isReady() const noexcept override { return m_ready; }

    QString sendRequest(Ipc::MessageType type, const QString&, const QCborMap&) override {
        if (!m_ready || type != Ipc::MessageType::GetCapabilities) {
            return {};
        }
        const QString requestId = QStringLiteral("capabilities-request");
        QTimer::singleShot(0, this, [this, requestId] {
            Ipc::Envelope response;
            response.type = Ipc::MessageType::Capabilities;
            response.requestId = requestId;
            response.workerVersion = QStringLiteral("test-worker-1");
            response.payload.insert(QStringLiteral("whisperVersion"), QStringLiteral("test-whisper-1"));
            response.payload.insert(QStringLiteral("compiledBackend"), QStringLiteral("metal"));
            response.payload.insert(QStringLiteral("runtimeAvailable"), true);
            emit envelopeReceived(response);
        });
        return requestId;
    }

  private:
    bool m_ready{true};
};

MaintenancePaths pathsFor(const QTemporaryDir& directory) {
    return {directory.path(), directory.filePath(QStringLiteral("cache")),
            directory.filePath(QStringLiteral("logs")), directory.filePath(QStringLiteral("exports"))};
}

bool writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

} // namespace

class MaintenanceControllerTest final : public QObject {
    Q_OBJECT

  private slots:
    void clearCacheRemovesOnlyCacheContents();
    void clearCacheRefusesBusyOrBroadLocation();
    void databaseBackupIsConsistentAndAtomic();
    void diagnosticsRefreshUsesWorkerCapabilities();
    void diagnosticsArchiveRedactsSensitiveContent();
    void updateCoordinatorSignalsAreForwarded();
};

void MaintenanceControllerTest::clearCacheRemovesOnlyCacheContents() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const MaintenancePaths paths = pathsFor(directory);
    QVERIFY(QDir().mkpath(QDir(paths.cacheDirectory).filePath(QStringLiteral("nested"))));
    QVERIFY(writeFile(QDir(paths.cacheDirectory).filePath(QStringLiteral("nested/cache.bin")),
                      QByteArray(4'096, 'x')));
    const QString outside = directory.filePath(QStringLiteral("recording.wav"));
    QVERIFY(writeFile(outside, QByteArrayLiteral("keep")));
    const QString linkedOutside = QDir(paths.cacheDirectory).filePath(QStringLiteral("outside-link"));
    const bool linkCreated = QFile::link(outside, linkedOutside);

    MaintenanceController controller({nullptr, nullptr, nullptr, paths});
    QSignalSpy cleared(&controller, &MaintenanceController::cacheCleared);
    controller.clearCache();
    QVERIFY(cleared.wait(5'000));
    QVERIFY(cleared.first().first().toLongLong() >= 4'096);
    QCOMPARE(
        QDir(paths.cacheDirectory).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).size(),
        0);
    QVERIFY(QFileInfo::exists(outside));
    if (linkCreated) {
        QCOMPARE(QFile(outside).size(), 4);
    }
}

void MaintenanceControllerTest::clearCacheRefusesBusyOrBroadLocation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MaintenancePaths paths = pathsFor(directory);
    QVERIFY(QDir().mkpath(paths.cacheDirectory));
    MaintenanceController controller({nullptr, nullptr, nullptr, paths});
    QSignalSpy errors(&controller, &MaintenanceController::operationFailed);

    controller.setCacheBusy(true);
    controller.clearCache();
    QCOMPARE(errors.size(), 1);

    controller.setCacheBusy(false);
    paths.cacheDirectory = directory.path();
    MaintenanceController broadController({nullptr, nullptr, nullptr, paths});
    QSignalSpy broadErrors(&broadController, &MaintenanceController::operationFailed);
    broadController.clearCache();
    QVERIFY(broadErrors.wait(5'000));
    QCOMPARE(broadErrors.first().first().toString(), QStringLiteral("ClearCache"));
}

void MaintenanceControllerTest::databaseBackupIsConsistentAndAtomic() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("database/library.sqlite3"));
    DatabaseManager database({databasePath});
    QVERIFY(database.initialize());
    auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery insert(connection.value());
    QVERIFY(insert.exec(
        QStringLiteral("INSERT INTO tags(id,name,created_at) VALUES('tag-1','Backup test','now')")));

    const MaintenancePaths paths = pathsFor(directory);
    MaintenanceController controller({&database, nullptr, nullptr, paths});
    const QString destination = directory.filePath(QStringLiteral("exports/backup.sqlite3"));
    QSignalSpy completed(&controller, &MaintenanceController::databaseBackupCreated);
    controller.backupDatabaseToUrl(QUrl::fromLocalFile(destination));
    QVERIFY(completed.wait(10'000));
    QCOMPARE(completed.first().first().toString(), QFileInfo(destination).absoluteFilePath());

    DatabaseManager backup({destination, 5'000, true, false});
    QVERIFY(backup.initialize());
    QVERIFY(backup.integrityCheck());
    auto backupConnection = backup.connection();
    QVERIFY(backupConnection);
    QSqlQuery count(backupConnection.value());
    QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM tags WHERE id='tag-1'")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 1);

    QSignalSpy rejected(&controller, &MaintenanceController::operationFailed);
    controller.backupDatabaseTo(destination);
    QVERIFY(rejected.wait(5'000));
    QVERIFY(backup.integrityCheck());
    const QStringList temporarySnapshots =
        QDir(QFileInfo(destination).absolutePath())
            .entryList({QStringLiteral(".breezedesk-snapshot-*")}, QDir::Files | QDir::Hidden);
    QVERIFY(temporarySnapshots.isEmpty());
}

void MaintenanceControllerTest::diagnosticsRefreshUsesWorkerCapabilities() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FakeWorkerClient worker;
    MaintenanceController controller({nullptr, &worker, nullptr, pathsFor(directory)});
    QSignalSpy changed(&controller, &MaintenanceController::diagnosticsChanged);
    controller.refreshDiagnostics();
    QTRY_COMPARE_WITH_TIMEOUT(
        controller.diagnosticsSnapshot().value(QStringLiteral("whisperVersion")).toString(),
        QStringLiteral("test-whisper-1"), 5'000);
    QVERIFY(!changed.isEmpty());
    const QJsonObject snapshot = controller.diagnosticsSnapshot();
    QCOMPARE(snapshot.value(QStringLiteral("workerVersion")).toString(), QStringLiteral("test-worker-1"));
    QCOMPARE(snapshot.value(QStringLiteral("compiledBackend")).toString(), QStringLiteral("metal"));
    QCOMPARE(snapshot.value(QStringLiteral("workerStatus")).toString(), QStringLiteral("Ready"));
}

void MaintenanceControllerTest::diagnosticsArchiveRedactsSensitiveContent() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const MaintenancePaths paths = pathsFor(directory);
    QVERIFY(QDir().mkpath(paths.logDirectory));
    QVERIFY(QDir().mkpath(paths.exportDirectory));
    QVERIFY(writeFile(
        QDir(paths.logDirectory).filePath(QStringLiteral("breezedesk.log")),
        QByteArrayLiteral("source /Users/private/meeting.wav\noriginal_text=sensitive transcript\nready\n")));
    MaintenanceController controller({nullptr, nullptr, nullptr, paths});
    controller.setSanitizedSettings(
        QJsonObject{{QStringLiteral("theme"), QStringLiteral("dark")},
                    {QStringLiteral("glossaryProfile"), QStringLiteral("secret-person")},
                    {QStringLiteral("exportPath"), QStringLiteral("/Users/private/exports")}});
    const QString destination = QDir(paths.exportDirectory).filePath(QStringLiteral("diagnostics.zip"));
    QSignalSpy exported(&controller, &MaintenanceController::diagnosticsExported);
    controller.exportDiagnosticsToUrl(QUrl::fromLocalFile(destination), false);
    QVERIFY(exported.wait(5'000));

    QFile archiveFile(destination);
    QVERIFY(archiveFile.open(QIODevice::ReadOnly));
    const QByteArray archive = archiveFile.readAll();
    QVERIFY(archive.contains(QByteArrayLiteral("diagnostics.json")));
    QVERIFY(archive.contains(QByteArray::fromHex("504b0506")));
    QVERIFY(archive.contains(QByteArrayLiteral("<redacted>")));
    QVERIFY(archive.contains(QByteArrayLiteral("<redacted-path>")));
    QVERIFY(!archive.contains(QByteArrayLiteral("secret-person")));
    QVERIFY(!archive.contains(QByteArrayLiteral("sensitive transcript")));
    QVERIFY(!archive.contains(QByteArrayLiteral("/Users/private")));

    QSignalSpy rejected(&controller, &MaintenanceController::operationFailed);
    controller.exportDiagnosticsToUrl(QUrl(QStringLiteral("https://example.invalid/report.zip")), false);
    QCOMPARE(rejected.size(), 1);
    QCOMPARE(rejected.first().first().toString(), QStringLiteral("ExportDiagnostics"));
}

void MaintenanceControllerTest::updateCoordinatorSignalsAreForwarded() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    UpdateCoordinator updates(nullptr);
    MaintenanceController controller({nullptr, nullptr, &updates, pathsFor(directory)});
    QSignalSpy errors(&controller, &MaintenanceController::updateError);
    controller.checkForUpdates();
    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.first().first().toString().contains(QStringLiteral("unavailable"), Qt::CaseInsensitive));
}

QTEST_GUILESS_MAIN(MaintenanceControllerTest)

#include "tst_MaintenanceController.moc"
