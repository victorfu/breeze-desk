#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/DatabaseSearchService.h"
#include "breezedesk/database/SqliteRecordingRepository.h"

#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <thread>

using namespace BreezeDesk;

class DatabaseTest final : public QObject {
    Q_OBJECT

  private slots:
    void cleanMigrationConfiguresSQLite();
    void failedTransactionRollsBack();
    void connectionsAreThreadLocal();
    void recordingTrashAndSearchWork();
    void migrationBackupAndIntegrityCheckWork();
    void upgradeMigrationCreatesBackup();
    void migrationChecksumMismatchIsRejected();
};

void DatabaseTest::cleanMigrationConfiguresSQLite() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DatabaseManager manager({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(manager.initialize());
    QCOMPARE(manager.schemaVersion(), 6);
    auto connection = manager.connection();
    QVERIFY(connection);
    QSqlQuery foreignKeys(connection.value());
    QVERIFY(foreignKeys.exec(QStringLiteral("PRAGMA foreign_keys")));
    QVERIFY(foreignKeys.next());
    QCOMPARE(foreignKeys.value(0).toInt(), 1);
    QSqlQuery journal(connection.value());
    QVERIFY(journal.exec(QStringLiteral("PRAGMA journal_mode")));
    QVERIFY(journal.next());
    QCOMPARE(journal.value(0).toString().toLower(), QStringLiteral("wal"));
    QVERIFY(manager.integrityCheck());
}

void DatabaseTest::failedTransactionRollsBack() {
    QTemporaryDir directory;
    DatabaseManager manager({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(manager.initialize());
    const auto operation = manager.transaction([](QSqlDatabase& database) -> Result<void> {
        QSqlQuery query(database);
        if (!query.exec(
                QStringLiteral("INSERT INTO tags(id,name,created_at) VALUES('id','temporary','now')"))) {
            return Result<void>::failure(UserFacingError::database(
                ErrorCode::DatabaseQueryFailed, QStringLiteral("Test setup insert failed."),
                query.lastError().text()));
        }
        return Result<void>::failure(
            UserFacingError::validation(ErrorCode::OperationCancelled, QStringLiteral("cancel")));
    });
    QVERIFY(!operation);
    auto connection = manager.connection();
    QVERIFY(connection);
    QSqlQuery count(connection.value());
    QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM tags")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 0);
}

void DatabaseTest::connectionsAreThreadLocal() {
    QTemporaryDir directory;
    DatabaseManager manager({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(manager.initialize());
    const QString mainName = manager.connection().value().connectionName();
    QString workerName;
    bool workerOk = false;
    std::thread worker([&]() {
        auto connection = manager.connection();
        workerOk = connection.hasValue();
        if (connection) {
            workerName = connection.value().connectionName();
            QSqlQuery query(connection.value());
            workerOk = query.exec(QStringLiteral("SELECT 1")) && query.next();
        }
    });
    worker.join();
    QVERIFY(workerOk);
    QVERIFY(!workerName.isEmpty());
    QVERIFY(workerName != mainName);
}

void DatabaseTest::recordingTrashAndSearchWork() {
    QTemporaryDir directory;
    DatabaseManager manager({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(manager.initialize());
    SqliteRecordingRepository repository(manager);
    Recording recording;
    recording.id = QStringLiteral("rec-1");
    recording.title = QStringLiteral("Breeze planning");
    recording.sourcePath = directory.filePath(QStringLiteral("會議 source.wav"));
    recording.notes = QStringLiteral("Taiwan product meeting");
    recording.tags = {QStringLiteral("Planning"), QStringLiteral("產品")};
    const auto createResult = repository.create(recording);
    if (!createResult)
        QFAIL(qPrintable(createResult.error().diagnosticString()));
    auto found = repository.findById(recording.id);
    QVERIFY(found && found.value());
    QCOMPARE(found.value()->tags.size(), 2);
    auto bySource = repository.findBySourcePath(recording.sourcePath);
    QVERIFY(bySource);
    QVERIFY(bySource.value().has_value());
    QCOMPARE(bySource.value()->id, recording.id);
    DatabaseSearchService search(manager);
    auto results = search.search(QStringLiteral("Taiwan"));
    QVERIFY(results);
    QCOMPARE(results.value().size(), 1);
    QVERIFY(repository.moveToTrash(recording.id));
    RecordingQuery trash;
    trash.deletedOnly = true;
    auto trashPage = repository.list(trash);
    QVERIFY(trashPage);
    QCOMPARE(trashPage.value().items.size(), 1);
    QVERIFY(repository.restore(recording.id));
    QVERIFY(!repository.permanentlyDelete(recording.id));
    QVERIFY(repository.moveToTrash(recording.id));
    QVERIFY(repository.permanentlyDelete(recording.id));
    QCOMPARE(repository.findById(recording.id).value().has_value(), false);
}

void DatabaseTest::migrationBackupAndIntegrityCheckWork() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager manager({path});
    QVERIFY(manager.initialize());
    const QString backupPath = directory.filePath(QStringLiteral("manual.sqlite"));
    auto backup = manager.createBackup(backupPath);
    QVERIFY(backup);
    QVERIFY(QFileInfo::exists(backup.value()));
    QVERIFY(manager.integrityCheck());
}

void DatabaseTest::upgradeMigrationCreatesBackup() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite"));
    {
        DatabaseManager manager({path});
        QVERIFY(manager.initialize());
        auto connection = manager.connection();
        QVERIFY(connection);
        QSqlQuery removeColumn(connection.value());
        QVERIFY(removeColumn.exec(QStringLiteral("ALTER TABLE transcription_jobs DROP COLUMN queue_hidden")));
        QSqlQuery removeVersion(connection.value());
        QSqlQuery removeReviewed(connection.value());
        QVERIFY(removeReviewed.exec(QStringLiteral("ALTER TABLE transcript_segments DROP COLUMN reviewed")));
        QVERIFY(removeVersion.exec(QStringLiteral("DELETE FROM schema_migrations WHERE version IN (4,5,6)")));
        QSqlQuery removeIndex(connection.value());
        QVERIFY(removeIndex.exec(QStringLiteral("DROP INDEX idx_recordings_source_path")));
    }
    DatabaseManager upgraded({path});
    QVERIFY(upgraded.initialize());
    QCOMPARE(upgraded.schemaVersion(), 6);
    const QStringList backups =
        QDir(directory.path()).entryList({QStringLiteral("library.sqlite.backup-*")}, QDir::Files);
    QCOMPARE(backups.size(), 1);
}

void DatabaseTest::migrationChecksumMismatchIsRejected() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite"));
    {
        DatabaseManager manager({path});
        QVERIFY(manager.initialize());
        auto connection = manager.connection();
        QVERIFY(connection);
        QSqlQuery tamper(connection.value());
        QVERIFY(
            tamper.exec(QStringLiteral("UPDATE schema_migrations SET checksum='tampered' WHERE version=1")));
    }
    DatabaseManager reopened({path});
    const auto result = reopened.initialize();
    QVERIFY(!result);
    QCOMPARE(result.error().code, ErrorCode::DatabaseMigrationFailed);
}

QTEST_GUILESS_MAIN(DatabaseTest)
#include "tst_Database.moc"
