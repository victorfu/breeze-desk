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

namespace {
bool insertTranscript(DatabaseManager& manager, const QString& recordingId, const QStringList& segments) {
    auto connection = manager.connection();
    if (!connection)
        return false;
    QSqlQuery job(connection.value());
    job.prepare(QStringLiteral(
        "INSERT INTO transcription_jobs(id,recording_id,state,stage,progress,queue_position,created_at) "
        "VALUES(?,?,'Completed','Finalizing',1,0,'2026-01-01T00:00:00.000Z')"));
    job.addBindValue(recordingId + QStringLiteral("-job"));
    job.addBindValue(recordingId);
    if (!job.exec())
        return false;
    QSqlQuery activate(connection.value());
    activate.prepare(QStringLiteral("UPDATE recordings SET active_job_id=? WHERE id=?"));
    activate.addBindValue(recordingId + QStringLiteral("-job"));
    activate.addBindValue(recordingId);
    if (!activate.exec())
        return false;
    for (int i = 0; i < segments.size(); ++i) {
        QSqlQuery segment(connection.value());
        segment.prepare(QStringLiteral(
            "INSERT INTO transcript_segments(id,recording_id,job_id,ordinal,start_ms,end_ms,original_text,"
            "created_at,updated_at) VALUES(?,?,?,?,?,?,?,'2026-01-01T00:00:00.000Z',"
            "'2026-01-01T00:00:00.000Z')"));
        segment.addBindValue(recordingId + QStringLiteral("-seg-") + QString::number(i));
        segment.addBindValue(recordingId);
        segment.addBindValue(recordingId + QStringLiteral("-job"));
        segment.addBindValue(i);
        segment.addBindValue(i * 1000);
        segment.addBindValue(i * 1000 + 900);
        segment.addBindValue(segments.at(i));
        if (!segment.exec())
            return false;
    }
    return static_cast<bool>(DatabaseSearchService(manager).rebuildRecording(recordingId));
}

bool createRecordingWithTranscript(DatabaseManager& manager, const QString& id, const QStringList& segments) {
    SqliteRecordingRepository repository(manager);
    Recording recording;
    recording.id = id;
    recording.title = QStringLiteral("Recording ") + id;
    if (!repository.create(recording))
        return false;
    return insertTranscript(manager, id, segments);
}
} // namespace

class DatabaseTest final : public QObject {
    Q_OBJECT

  private slots:
    void cleanMigrationConfiguresSQLite();
    void failedTransactionRollsBack();
    void connectionsAreThreadLocal();
    void recordingTrashAndSearchWork();
    void permanentDeletePurgesRawSearchIndexes();
    void permanentDeleteRollsBackWhenSearchIndexDeleteFails();
    void chineseSubstringSearchFindsRecordings();
    void multiKeywordSearchRequiresAllTerms();
    void searchEscapesLikeWildcards();
    void searchIndexMigrationRebuildsWithTrigram();
    void migrationBackupAndIntegrityCheckWork();
    void upgradeMigrationCreatesBackup();
    void executionLeaseAndSingleTranscriptMigrationsNormalizeLegacyData();
    void singleGlossaryMigrationConsolidatesProfiles();
    void migrationChecksumMismatchIsRejected();
};

void DatabaseTest::cleanMigrationConfiguresSQLite() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DatabaseManager manager({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(manager.initialize());
    QCOMPARE(manager.schemaVersion(), 10);
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

void DatabaseTest::permanentDeletePurgesRawSearchIndexes() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite"));
    const QString recordingId = QStringLiteral("raw-index-delete-recording");
    const QString title = QStringLiteral("TitleSecret-6c701a");
    const QString notes = QStringLiteral("NotesSecret-3f940b");
    const QString tag = QStringLiteral("TagSecret-9a55d2");
    const QString transcript = QStringLiteral("TranscriptSecret-187ec4");
    bool ftsTableExists = false;

    {
        DatabaseManager setup({path});
        QVERIFY(setup.initialize());
        SqliteRecordingRepository repository(setup);
        Recording recording;
        recording.id = recordingId;
        recording.title = title;
        recording.notes = notes;
        recording.tags = {tag};
        QVERIFY(repository.create(recording));
        QVERIFY(insertTranscript(setup, recordingId, {transcript}));

        const auto connection = setup.connection();
        QVERIFY(connection);
        QSqlQuery fallback(connection.value());
        fallback.prepare(QStringLiteral(
            "SELECT title,notes,tags,transcript FROM search_index_fallback WHERE recording_id=?"));
        fallback.addBindValue(recordingId);
        QVERIFY(fallback.exec());
        QVERIFY(fallback.next());
        QCOMPARE(fallback.value(0).toString(), title);
        QCOMPARE(fallback.value(1).toString(), notes);
        QCOMPARE(fallback.value(2).toString(), tag);
        QCOMPARE(fallback.value(3).toString(), transcript);

        ftsTableExists = setup.hasFts5();
        if (ftsTableExists) {
            QSqlQuery fts(connection.value());
            fts.prepare(QStringLiteral(
                "SELECT title,notes,tags,transcript FROM search_index WHERE recording_id=?"));
            fts.addBindValue(recordingId);
            QVERIFY(fts.exec());
            QVERIFY(fts.next());
            QCOMPARE(fts.value(0).toString(), title);
            QCOMPARE(fts.value(1).toString(), notes);
            QCOMPARE(fts.value(2).toString(), tag);
            QCOMPARE(fts.value(3).toString(), transcript);

            // Simulate a legacy database whose dormant FTS table is not advertised by the cached
            // feature flag. Permanent deletion must inspect the schema rather than trusting the flag.
            QSqlQuery disableFeature(connection.value());
            QVERIFY(disableFeature.exec(QStringLiteral(
                "UPDATE database_features SET enabled=0,detail='legacy dormant FTS table' "
                "WHERE name='fts5'")));
            QCOMPARE(disableFeature.numRowsAffected(), 1);
        }
    }

    DatabaseManager manager({path});
    QVERIFY(manager.initialize());
    if (ftsTableExists) {
        QVERIFY(!manager.hasFts5());
    }
    SqliteRecordingRepository repository(manager);
    QVERIFY(repository.moveToTrash(recordingId));
    QVERIFY(repository.permanentlyDelete(recordingId));

    const auto connection = manager.connection();
    QVERIFY(connection);
    QSqlQuery fallbackCount(connection.value());
    fallbackCount.prepare(
        QStringLiteral("SELECT COUNT(*) FROM search_index_fallback WHERE recording_id=?"));
    fallbackCount.addBindValue(recordingId);
    QVERIFY(fallbackCount.exec());
    QVERIFY(fallbackCount.next());
    QCOMPARE(fallbackCount.value(0).toInt(), 0);
    if (ftsTableExists) {
        QSqlQuery ftsCount(connection.value());
        ftsCount.prepare(QStringLiteral("SELECT COUNT(*) FROM search_index WHERE recording_id=?"));
        ftsCount.addBindValue(recordingId);
        QVERIFY(ftsCount.exec());
        QVERIFY(ftsCount.next());
        QCOMPARE(ftsCount.value(0).toInt(), 0);
    }
}

void DatabaseTest::permanentDeleteRollsBackWhenSearchIndexDeleteFails() {
    QTemporaryDir directory;
    DatabaseManager manager({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(manager.initialize());
    SqliteRecordingRepository repository(manager);
    const QString recordingId = QStringLiteral("atomic-index-delete-recording");
    Recording recording;
    recording.id = recordingId;
    recording.title = QStringLiteral("AtomicTitleSecret-a77bf1");
    recording.notes = QStringLiteral("AtomicNotesSecret-8cb053");
    QVERIFY(repository.create(recording));
    QVERIFY(insertTranscript(manager, recordingId,
                             {QStringLiteral("AtomicTranscriptSecret-2706ce")}));
    QVERIFY(repository.moveToTrash(recordingId));

    const auto connection = manager.connection();
    QVERIFY(connection);
    QSqlQuery replaceFts(connection.value());
    QVERIFY(replaceFts.exec(QStringLiteral("DROP TABLE IF EXISTS search_index")));
    QVERIFY(replaceFts.exec(QStringLiteral(
        "CREATE TABLE search_index(recording_id TEXT PRIMARY KEY,title TEXT NOT NULL,notes TEXT NOT "
        "NULL,tags TEXT NOT NULL,transcript TEXT NOT NULL)")));
    QSqlQuery legacyIndex(connection.value());
    legacyIndex.prepare(QStringLiteral(
        "INSERT INTO search_index(recording_id,title,notes,tags,transcript) VALUES(?,?,?,?,?)"));
    legacyIndex.addBindValue(recordingId);
    legacyIndex.addBindValue(recording.title);
    legacyIndex.addBindValue(recording.notes);
    legacyIndex.addBindValue(QStringLiteral("AtomicTagSecret-66dac1"));
    legacyIndex.addBindValue(QStringLiteral("AtomicTranscriptSecret-2706ce"));
    QVERIFY(legacyIndex.exec());
    QSqlQuery injectFailure(connection.value());
    QVERIFY(injectFailure.exec(QStringLiteral(
        "CREATE TRIGGER fail_search_index_delete BEFORE DELETE ON search_index "
        "WHEN OLD.recording_id='atomic-index-delete-recording' BEGIN "
        "SELECT RAISE(ABORT,'injected search index delete failure'); END")));

    const auto permanentlyDeleted = repository.permanentlyDelete(recordingId);
    QVERIFY(!permanentlyDeleted);

    QSqlQuery recordingCount(connection.value());
    recordingCount.prepare(
        QStringLiteral("SELECT COUNT(*) FROM recordings WHERE id=? AND deleted_at IS NOT NULL"));
    recordingCount.addBindValue(recordingId);
    QVERIFY(recordingCount.exec());
    QVERIFY(recordingCount.next());
    QCOMPARE(recordingCount.value(0).toInt(), 1);

    QSqlQuery fallbackCount(connection.value());
    fallbackCount.prepare(
        QStringLiteral("SELECT COUNT(*) FROM search_index_fallback WHERE recording_id=?"));
    fallbackCount.addBindValue(recordingId);
    QVERIFY(fallbackCount.exec());
    QVERIFY(fallbackCount.next());
    QCOMPARE(fallbackCount.value(0).toInt(), 1);

    QSqlQuery searchIndexCount(connection.value());
    searchIndexCount.prepare(
        QStringLiteral("SELECT COUNT(*) FROM search_index WHERE recording_id=?"));
    searchIndexCount.addBindValue(recordingId);
    QVERIFY(searchIndexCount.exec());
    QVERIFY(searchIndexCount.next());
    QCOMPARE(searchIndexCount.value(0).toInt(), 1);
}

void DatabaseTest::chineseSubstringSearchFindsRecordings() {
    QTemporaryDir directory;
    DatabaseManager manager({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(manager.initialize());
    QVERIFY(createRecordingWithTranscript(manager, QStringLiteral("rec-zh"),
                                          {QStringLiteral("開場寒暄"), QStringLiteral("今天討論預算分配")}));
    DatabaseSearchService search(manager);

    auto twoCharacter = search.search(QStringLiteral("預算"));
    QVERIFY(twoCharacter);
    QCOMPARE(twoCharacter.value().size(), 1);
    QCOMPARE(twoCharacter.value().first().recordingId, QStringLiteral("rec-zh"));
    QCOMPARE(twoCharacter.value().first().segmentId, QStringLiteral("rec-zh-seg-1"));
    QCOMPARE(twoCharacter.value().first().startMs, 1000);

    auto fourCharacter = search.search(QStringLiteral("預算分配"));
    QVERIFY(fourCharacter);
    QCOMPARE(fourCharacter.value().size(), 1);
    QCOMPARE(fourCharacter.value().first().recordingId, QStringLiteral("rec-zh"));

    auto missing = search.search(QStringLiteral("不存在"));
    QVERIFY(missing);
    QCOMPARE(missing.value().size(), 0);
}

void DatabaseTest::multiKeywordSearchRequiresAllTerms() {
    QTemporaryDir directory;
    DatabaseManager manager({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(manager.initialize());
    QVERIFY(createRecordingWithTranscript(
        manager, QStringLiteral("rec-multi"),
        {QStringLiteral("會議結束後我們散會"), QStringLiteral("我們確認了預算數字")}));
    SqliteRecordingRepository repository(manager);
    Recording english;
    english.id = QStringLiteral("rec-en");
    english.title = QStringLiteral("Weekly sync");
    english.notes = QStringLiteral("Taiwan product meeting");
    QVERIFY(repository.create(english));
    DatabaseSearchService search(manager);

    auto mixedLengths = search.search(QStringLiteral("會議 預算數字"));
    QVERIFY(mixedLengths);
    QCOMPARE(mixedLengths.value().size(), 1);
    QCOMPARE(mixedLengths.value().first().recordingId, QStringLiteral("rec-multi"));

    auto nonAdjacent = search.search(QStringLiteral("Taiwan meeting"));
    QVERIFY(nonAdjacent);
    QCOMPARE(nonAdjacent.value().size(), 1);
    QCOMPARE(nonAdjacent.value().first().recordingId, QStringLiteral("rec-en"));

    auto shortMiss = search.search(QStringLiteral("會議 火箭"));
    QVERIFY(shortMiss);
    QCOMPARE(shortMiss.value().size(), 0);

    auto mixedMiss = search.search(QStringLiteral("預算數字 火箭"));
    QVERIFY(mixedMiss);
    QCOMPARE(mixedMiss.value().size(), 0);
}

void DatabaseTest::searchEscapesLikeWildcards() {
    QTemporaryDir directory;
    DatabaseManager manager({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(manager.initialize());
    QVERIFY(createRecordingWithTranscript(manager, QStringLiteral("rec-percent"),
                                          {QStringLiteral("折扣5%活動開跑")}));
    QVERIFY(createRecordingWithTranscript(manager, QStringLiteral("rec-plain"),
                                          {QStringLiteral("折扣5折活動開跑")}));
    QVERIFY(createRecordingWithTranscript(manager, QStringLiteral("rec-underscore"),
                                          {QStringLiteral("檔案A_B版本")}));
    QVERIFY(createRecordingWithTranscript(manager, QStringLiteral("rec-letter"),
                                          {QStringLiteral("檔案AXB版本")}));
    DatabaseSearchService search(manager);

    auto percent = search.search(QStringLiteral("5%"));
    QVERIFY(percent);
    QCOMPARE(percent.value().size(), 1);
    QCOMPARE(percent.value().first().recordingId, QStringLiteral("rec-percent"));

    auto underscore = search.search(QStringLiteral("A_"));
    QVERIFY(underscore);
    QCOMPARE(underscore.value().size(), 1);
    QCOMPARE(underscore.value().first().recordingId, QStringLiteral("rec-underscore"));
}

void DatabaseTest::searchIndexMigrationRebuildsWithTrigram() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite"));
    {
        DatabaseManager manager({path});
        QVERIFY(manager.initialize());
        QVERIFY(manager.hasFts5());
        QVERIFY(createRecordingWithTranscript(manager, QStringLiteral("rec-migrate"),
                                              {QStringLiteral("今天討論預算分配")}));
        auto connection = manager.connection();
        QVERIFY(connection);
        QSqlQuery query(connection.value());
        QVERIFY(query.exec(QStringLiteral("DROP TABLE search_index")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE VIRTUAL TABLE search_index USING fts5(recording_id UNINDEXED, title, notes, tags, "
            "transcript, tokenize='unicode61 remove_diacritics 2')")));
        QVERIFY(query.exec(QStringLiteral(
            "ALTER TABLE transcription_jobs ADD COLUMN revision_number INTEGER NOT NULL DEFAULT 1")));
        QVERIFY(query.exec(QStringLiteral("DELETE FROM schema_migrations WHERE version IN (8,9,10)")));
    }
    DatabaseManager upgraded({path});
    QVERIFY(upgraded.initialize());
    QCOMPARE(upgraded.schemaVersion(), 10);
    QVERIFY(upgraded.hasFts5());
    auto connection = upgraded.connection();
    QVERIFY(connection);
    QSqlQuery ddl(connection.value());
    QVERIFY(ddl.exec(QStringLiteral("SELECT sql FROM sqlite_master WHERE name='search_index'")));
    QVERIFY(ddl.next());
    QVERIFY(ddl.value(0).toString().contains(QStringLiteral("trigram")));
    DatabaseSearchService search(upgraded);
    auto rebuilt = search.search(QStringLiteral("預算分配"));
    QVERIFY(rebuilt);
    QCOMPARE(rebuilt.value().size(), 1);
    QCOMPARE(rebuilt.value().first().recordingId, QStringLiteral("rec-migrate"));
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
        QVERIFY(removeVersion.exec(QStringLiteral(
            "ALTER TABLE transcription_jobs ADD COLUMN revision_number INTEGER NOT NULL DEFAULT 1")));
        QVERIFY(removeVersion.exec(
            QStringLiteral("DELETE FROM schema_migrations WHERE version IN (4,5,6,7,8,9,10)")));
        QSqlQuery removeIndex(connection.value());
        QVERIFY(removeIndex.exec(QStringLiteral("DROP INDEX idx_recordings_source_path")));
    }
    DatabaseManager upgraded({path});
    QVERIFY(upgraded.initialize());
    QCOMPARE(upgraded.schemaVersion(), 10);
    const QStringList backups =
        QDir(directory.path()).entryList({QStringLiteral("library.sqlite.backup-*")}, QDir::Files);
    QCOMPARE(backups.size(), 1);
}

void DatabaseTest::executionLeaseAndSingleTranscriptMigrationsNormalizeLegacyData() {
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
        QVERIFY(query.exec(QStringLiteral(
            "ALTER TABLE transcription_jobs ADD COLUMN revision_number INTEGER NOT NULL DEFAULT 1")));
        QVERIFY(query.exec(QStringLiteral("DROP TRIGGER IF EXISTS trg_jobs_active_revision_before_delete")));
        QVERIFY(query.exec(QStringLiteral("DROP INDEX IF EXISTS idx_jobs_recording_revision")));
        QVERIFY(query.exec(QStringLiteral("DROP INDEX idx_jobs_single_execution")));
        QVERIFY(query.exec(QStringLiteral("DROP TABLE asr_execution_lease")));
        QVERIFY(query.exec(QStringLiteral("DROP TABLE transcription_job_events")));
        QVERIFY(query.exec(QStringLiteral("DELETE FROM schema_migrations WHERE version IN (7,8,9,10)")));

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
        const auto insertSegment = [&](const QString& id, const QString& jobId, const QString& text) {
            QSqlQuery insert(connection.value());
            insert.prepare(QStringLiteral(
                "INSERT INTO transcript_segments(id,recording_id,job_id,ordinal,start_ms,end_ms,"
                "original_text,created_at,updated_at) VALUES(?,'rec',?,0,0,1000,?,'now','now')"));
            insert.addBindValue(id);
            insert.addBindValue(jobId);
            insert.addBindValue(text);
            return insert.exec();
        };
        QVERIFY(insertSegment(QStringLiteral("old-segment"), QStringLiteral("old-completed"),
                              QStringLiteral("Old transcript")));
        QVERIFY(insertSegment(QStringLiteral("new-segment"), QStringLiteral("new-completed"),
                              QStringLiteral("New transcript")));
        QVERIFY(
            query.exec(QStringLiteral("UPDATE recordings SET active_job_id='old-completed' WHERE id='rec'")));
    }

    DatabaseManager upgraded({path});
    QVERIFY(upgraded.initialize());
    QCOMPARE(upgraded.schemaVersion(), 10);
    auto connection = upgraded.connection();
    QVERIFY(connection);
    QSqlQuery jobs(connection.value());
    QVERIFY(jobs.exec(
        QStringLiteral("SELECT id,state,queue_position,queue_hidden FROM transcription_jobs ORDER BY "
                       "created_at")));
    QStringList ids;
    QMap<QString, QString> states;
    QMap<QString, int> queuePositions;
    QMap<QString, bool> queueHidden;
    while (jobs.next()) {
        const QString id = jobs.value(0).toString();
        ids.append(id);
        states.insert(id, jobs.value(1).toString());
        queuePositions.insert(id, jobs.value(2).toInt());
        queueHidden.insert(id, jobs.value(3).toBool());
    }
    QCOMPARE(ids, QStringList({QStringLiteral("old-completed"), QStringLiteral("new-completed"),
                               QStringLiteral("running"), QStringLiteral("queued-a"),
                               QStringLiteral("queued-b")}));
    QCOMPARE(states.value(QStringLiteral("running")), QStringLiteral("Interrupted"));
    QCOMPARE(queuePositions.value(QStringLiteral("queued-a")), 0);
    QCOMPARE(queuePositions.value(QStringLiteral("queued-b")), 1);
    QVERIFY(queueHidden.value(QStringLiteral("old-completed")));
    QVERIFY(!queueHidden.value(QStringLiteral("new-completed")));

    QSqlQuery active(connection.value());
    QVERIFY(active.exec(QStringLiteral("SELECT active_job_id FROM recordings WHERE id='rec'")));
    QVERIFY(active.next());
    QCOMPARE(active.value(0).toString(), QStringLiteral("new-completed"));

    QSqlQuery segments(connection.value());
    QVERIFY(segments.exec(QStringLiteral("SELECT id,original_text FROM transcript_segments")));
    QVERIFY(segments.next());
    QCOMPARE(segments.value(0).toString(), QStringLiteral("new-segment"));
    QCOMPARE(segments.value(1).toString(), QStringLiteral("New transcript"));
    QVERIFY(!segments.next());

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

void DatabaseTest::singleGlossaryMigrationConsolidatesProfiles() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite"));
    {
        DatabaseManager manager({path});
        QVERIFY(manager.initialize());
        auto connection = manager.connection();
        QVERIFY(connection);
        QSqlQuery setup(connection.value());
        QVERIFY(setup.exec(QStringLiteral(
            "ALTER TABLE transcription_jobs ADD COLUMN revision_number INTEGER NOT NULL DEFAULT 1")));
        QVERIFY(setup.exec(
            QStringLiteral("INSERT INTO glossary_profiles(id,name,created_at,updated_at) VALUES"
                           "('product','Product','now','now'),('customer','Customer','now','now')")));
        QVERIFY(setup.exec(QStringLiteral(
            "INSERT INTO glossary_terms(id,profile_id,canonical_text,enabled,created_at,updated_at) "
            "VALUES('term-1','product','BreezeDesk',1,'now','now'),"
            "('term-duplicate','customer','breezedesk',0,'now','now'),"
            "('term-2','customer','MediaTek',0,'now','now')")));
        QVERIFY(setup.exec(QStringLiteral("DELETE FROM schema_migrations WHERE version IN (9,10)")));
    }

    DatabaseManager upgraded({path});
    QVERIFY(upgraded.initialize());
    QCOMPARE(upgraded.schemaVersion(), 10);
    auto connection = upgraded.connection();
    QVERIFY(connection);
    QSqlQuery profiles(connection.value());
    QVERIFY(profiles.exec(QStringLiteral("SELECT id FROM glossary_profiles")));
    QVERIFY(profiles.next());
    QCOMPARE(profiles.value(0).toString(), QStringLiteral("default"));
    QVERIFY(!profiles.next());
    QSqlQuery terms(connection.value());
    QVERIFY(terms.exec(QStringLiteral("SELECT id,profile_id,enabled FROM glossary_terms ORDER BY id")));
    QVERIFY(terms.next());
    QCOMPARE(terms.value(0).toString(), QStringLiteral("term-1"));
    QCOMPARE(terms.value(1).toString(), QStringLiteral("default"));
    QVERIFY(terms.value(2).toBool());
    QVERIFY(terms.next());
    QCOMPARE(terms.value(0).toString(), QStringLiteral("term-2"));
    QCOMPARE(terms.value(1).toString(), QStringLiteral("default"));
    QVERIFY(!terms.value(2).toBool());
    QVERIFY(!terms.next());
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
