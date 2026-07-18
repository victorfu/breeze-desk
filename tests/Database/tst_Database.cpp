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
    void revisionMigrationNormalizesLegacyHistory();
    void migrationChecksumMismatchIsRejected();
};

void DatabaseTest::cleanMigrationConfiguresSQLite() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DatabaseManager manager({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(manager.initialize());
    QCOMPARE(manager.schemaVersion(), 7);
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
        QVERIFY(
            removeVersion.exec(QStringLiteral("DELETE FROM schema_migrations WHERE version IN (4,5,6,7)")));
        QSqlQuery removeIndex(connection.value());
        QVERIFY(removeIndex.exec(QStringLiteral("DROP INDEX idx_recordings_source_path")));
    }
    DatabaseManager upgraded({path});
    QVERIFY(upgraded.initialize());
    QCOMPARE(upgraded.schemaVersion(), 7);
    const QStringList backups =
        QDir(directory.path()).entryList({QStringLiteral("library.sqlite.backup-*")}, QDir::Files);
    QCOMPARE(backups.size(), 1);
}

void DatabaseTest::revisionMigrationNormalizesLegacyHistory() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite"));
    {
        DatabaseManager manager({path});
        QVERIFY(manager.initialize());
        SqliteRecordingRepository recordings(manager);
        Recording recording;
        recording.id = QStringLiteral("rec");
        recording.title = QStringLiteral("Legacy history");
        QVERIFY(recordings.create(recording));

        auto connection = manager.connection();
        QVERIFY(connection);
        QSqlQuery query(connection.value());
        QVERIFY(query.exec(QStringLiteral("DROP TRIGGER trg_jobs_active_revision_before_delete")));
        QVERIFY(query.exec(QStringLiteral("DROP INDEX idx_jobs_recording_revision")));
        QVERIFY(query.exec(QStringLiteral("DROP INDEX idx_jobs_single_execution")));
        QVERIFY(query.exec(QStringLiteral("DROP TABLE asr_execution_lease")));
        QVERIFY(query.exec(QStringLiteral("DROP TABLE transcription_job_events")));
        QVERIFY(query.exec(QStringLiteral("DELETE FROM schema_migrations WHERE version=7")));

        const auto insertJob = [&](const QString& id, const QString& state, const int queuePosition,
                                   const QString& createdAt) {
            QSqlQuery insert(connection.value());
            insert.prepare(QStringLiteral(
                "INSERT INTO transcription_jobs(id,recording_id,state,stage,progress,queue_position,"
                "revision_number,created_at) VALUES(?,'rec',?,'Preparing',0,?,9,?)"));
            insert.addBindValue(id);
            insert.addBindValue(state);
            insert.addBindValue(queuePosition);
            insert.addBindValue(createdAt);
            return insert.exec();
        };
        QVERIFY(insertJob(QStringLiteral("old-completed"), QStringLiteral("Completed"), 7,
                          QStringLiteral("2026-01-01T00:00:00.000Z")));
        QVERIFY(insertJob(QStringLiteral("new-completed"), QStringLiteral("Completed"), 7,
                          QStringLiteral("2026-01-01T00:01:00.000Z")));
        QVERIFY(insertJob(QStringLiteral("running"), QStringLiteral("Transcribing"), 2,
                          QStringLiteral("2026-01-01T00:02:00.000Z")));
        QVERIFY(insertJob(QStringLiteral("queued-a"), QStringLiteral("Queued"), 0,
                          QStringLiteral("2026-01-01T00:03:00.000Z")));
        QVERIFY(insertJob(QStringLiteral("queued-b"), QStringLiteral("Queued"), 0,
                          QStringLiteral("2026-01-01T00:04:00.000Z")));
        QVERIFY(
            query.exec(QStringLiteral("UPDATE recordings SET active_job_id='old-completed' WHERE id='rec'")));
    }

    DatabaseManager upgraded({path});
    QVERIFY(upgraded.initialize());
    QCOMPARE(upgraded.schemaVersion(), 7);
    auto connection = upgraded.connection();
    QVERIFY(connection);
    QSqlQuery jobs(connection.value());
    QVERIFY(jobs.exec(QStringLiteral(
        "SELECT id,state,revision_number,queue_position FROM transcription_jobs ORDER BY revision_number")));
    QStringList ids;
    QList<int> revisions;
    QMap<QString, QString> states;
    QMap<QString, int> queuePositions;
    while (jobs.next()) {
        const QString id = jobs.value(0).toString();
        ids.append(id);
        states.insert(id, jobs.value(1).toString());
        revisions.append(jobs.value(2).toInt());
        queuePositions.insert(id, jobs.value(3).toInt());
    }
    QCOMPARE(ids, QStringList({QStringLiteral("old-completed"), QStringLiteral("new-completed"),
                               QStringLiteral("running"), QStringLiteral("queued-a"),
                               QStringLiteral("queued-b")}));
    QCOMPARE(revisions, QList<int>({1, 2, 3, 4, 5}));
    QCOMPARE(states.value(QStringLiteral("running")), QStringLiteral("Interrupted"));
    QCOMPARE(queuePositions.value(QStringLiteral("queued-a")), 0);
    QCOMPARE(queuePositions.value(QStringLiteral("queued-b")), 1);

    QSqlQuery active(connection.value());
    QVERIFY(active.exec(QStringLiteral("SELECT active_job_id FROM recordings WHERE id='rec'")));
    QVERIFY(active.next());
    QCOMPARE(active.value(0).toString(), QStringLiteral("new-completed"));

    QSqlQuery events(connection.value());
    QVERIFY(events.exec(QStringLiteral(
        "SELECT event_type,COUNT(*) FROM transcription_job_events GROUP BY event_type ORDER BY event_type")));
    QMap<QString, int> eventCounts;
    while (events.next()) {
        eventCounts.insert(events.value(0).toString(), events.value(1).toInt());
    }
    QCOMPARE(eventCounts.value(QStringLiteral("enqueued")), 5);
    QCOMPARE(eventCounts.value(QStringLiteral("completed")), 2);
    QCOMPARE(eventCounts.value(QStringLiteral("interrupted")), 1);
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
