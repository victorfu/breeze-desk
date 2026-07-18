#include "breezedesk/database/DatabaseManager.h"

#include "breezedesk/app_config.h"
#include "breezedesk/core/TimeUtils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryFile>
#include <QThread>
#include <QUuid>

namespace BreezeDesk {
namespace {

class ScopedFileRemoval final {
  public:
    explicit ScopedFileRemoval(QString path) : m_path(std::move(path)) {}
    ~ScopedFileRemoval() { QFile::remove(m_path); }

    ScopedFileRemoval(const ScopedFileRemoval&) = delete;
    ScopedFileRemoval& operator=(const ScopedFileRemoval&) = delete;

  private:
    QString m_path;
};

UserFacingError sqlError(const ErrorCode code, const QString& message, const QSqlError& error,
                         const bool retryable = false) {
    return UserFacingError::database(code, message, error.text(), retryable);
}

bool execute(QSqlDatabase& database, const QString& statement, UserFacingError* error) {
    QSqlQuery query(database);
    if (query.exec(statement)) {
        return true;
    }
    if (error != nullptr) {
        *error =
            sqlError(ErrorCode::DatabaseMigrationFailed,
                     QStringLiteral("A database schema update could not be applied."), query.lastError());
    }
    return false;
}

QStringList initialSchema() {
    return {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS recordings ("
            "id TEXT PRIMARY KEY, title TEXT NOT NULL, source_path TEXT NOT NULL DEFAULT '', "
            "managed_media_path TEXT NOT NULL DEFAULT '', normalized_pcm_path TEXT NOT NULL DEFAULT '', "
            "source_hash TEXT NOT NULL DEFAULT '', media_type TEXT NOT NULL DEFAULT '', "
            "duration_ms INTEGER NOT NULL DEFAULT 0 CHECK(duration_ms >= 0), "
            "sample_rate INTEGER NOT NULL DEFAULT 0 CHECK(sample_rate >= 0), "
            "channel_count INTEGER NOT NULL DEFAULT 0 CHECK(channel_count >= 0), "
            "waveform_path TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL, updated_at TEXT NOT NULL, "
            "deleted_at TEXT, notes TEXT NOT NULL DEFAULT '', review_state TEXT NOT NULL DEFAULT "
            "'unreviewed', "
            "active_job_id TEXT)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS transcription_jobs ("
            "id TEXT PRIMARY KEY, recording_id TEXT NOT NULL REFERENCES recordings(id) ON DELETE CASCADE, "
            "state TEXT NOT NULL, stage TEXT NOT NULL, progress REAL NOT NULL DEFAULT 0 "
            "CHECK(progress >= 0 AND progress <= 1), model_id TEXT NOT NULL DEFAULT '', "
            "model_checksum TEXT NOT NULL DEFAULT '', engine_version TEXT NOT NULL DEFAULT '', "
            "worker_version TEXT NOT NULL DEFAULT '', backend TEXT NOT NULL DEFAULT '', "
            "language TEXT NOT NULL DEFAULT 'zh', preset TEXT NOT NULL DEFAULT 'balanced', "
            "glossary_profile_id TEXT, meeting_context TEXT NOT NULL DEFAULT '', "
            "vad_enabled INTEGER NOT NULL DEFAULT 1, error_code TEXT NOT NULL DEFAULT '', "
            "error_message TEXT NOT NULL DEFAULT '', diagnostics_json TEXT NOT NULL DEFAULT '{}', "
            "parameters_json TEXT NOT NULL DEFAULT '{}', queue_position INTEGER NOT NULL DEFAULT 0, "
            "revision_number INTEGER NOT NULL DEFAULT 1, retry_count INTEGER NOT NULL DEFAULT 0, "
            "created_at TEXT NOT NULL, started_at TEXT, completed_at TEXT, interrupted_at TEXT, "
            "last_completed_chunk INTEGER NOT NULL DEFAULT -1)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS job_chunks ("
            "id TEXT PRIMARY KEY, job_id TEXT NOT NULL REFERENCES transcription_jobs(id) ON DELETE CASCADE, "
            "ordinal INTEGER NOT NULL, start_ms INTEGER NOT NULL, end_ms INTEGER NOT NULL, "
            "overlap_before_ms INTEGER NOT NULL DEFAULT 0, overlap_after_ms INTEGER NOT NULL DEFAULT 0, "
            "state TEXT NOT NULL, attempts INTEGER NOT NULL DEFAULT 0, started_at TEXT, completed_at TEXT, "
            "error TEXT NOT NULL DEFAULT '', result_hash TEXT NOT NULL DEFAULT '', diagnostics_json TEXT NOT "
            "NULL DEFAULT '{}', "
            "UNIQUE(job_id, ordinal), CHECK(start_ms >= 0), CHECK(end_ms > start_ms))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS transcript_segments ("
            "id TEXT PRIMARY KEY, recording_id TEXT NOT NULL REFERENCES recordings(id) ON DELETE CASCADE, "
            "job_id TEXT NOT NULL REFERENCES transcription_jobs(id) ON DELETE CASCADE, "
            "chunk_id TEXT REFERENCES job_chunks(id) ON DELETE SET NULL, ordinal INTEGER NOT NULL, "
            "start_ms INTEGER NOT NULL, end_ms INTEGER NOT NULL, original_text TEXT NOT NULL DEFAULT '', "
            "edited_text TEXT NOT NULL DEFAULT '', average_probability REAL NOT NULL DEFAULT 0, "
            "minimum_probability REAL NOT NULL DEFAULT 0, no_speech_probability REAL NOT NULL DEFAULT 0, "
            "low_confidence INTEGER NOT NULL DEFAULT 0, replacement_audit_json TEXT NOT NULL DEFAULT '[]', "
            "is_provisional INTEGER NOT NULL DEFAULT 0, attempt INTEGER NOT NULL DEFAULT 1, "
            "created_at TEXT NOT NULL, updated_at TEXT NOT NULL, UNIQUE(job_id, ordinal), "
            "CHECK(start_ms >= 0), CHECK(end_ms > start_ms))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tags (id TEXT PRIMARY KEY, name TEXT NOT NULL COLLATE NOCASE UNIQUE, "
            "created_at TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS recording_tags (recording_id TEXT NOT NULL REFERENCES recordings(id) "
            "ON DELETE CASCADE, tag_id TEXT NOT NULL REFERENCES tags(id) ON DELETE CASCADE, "
            "PRIMARY KEY(recording_id, tag_id))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS glossary_profiles (id TEXT PRIMARY KEY, name TEXT NOT NULL, "
            "description TEXT NOT NULL DEFAULT '', project_context TEXT NOT NULL DEFAULT '', "
            "created_at TEXT NOT NULL, updated_at TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS glossary_terms (id TEXT PRIMARY KEY, "
                       "profile_id TEXT NOT NULL REFERENCES glossary_profiles(id) ON DELETE CASCADE, "
                       "canonical_text TEXT NOT NULL, aliases_json TEXT NOT NULL DEFAULT '[]', "
                       "category TEXT NOT NULL DEFAULT '', language TEXT NOT NULL DEFAULT '', "
                       "priority INTEGER NOT NULL DEFAULT 0, case_sensitive INTEGER NOT NULL DEFAULT 0, "
                       "enabled INTEGER NOT NULL DEFAULT 1, notes TEXT NOT NULL DEFAULT '', "
                       "created_at TEXT NOT NULL, updated_at TEXT NOT NULL, UNIQUE(profile_id, "
                       "canonical_text COLLATE NOCASE))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS installed_models (id TEXT PRIMARY KEY, display_name TEXT NOT NULL, "
            "file_path TEXT NOT NULL UNIQUE, file_size INTEGER NOT NULL DEFAULT 0, sha256 TEXT NOT NULL "
            "DEFAULT '', "
            "quantization TEXT NOT NULL DEFAULT '', source_revision TEXT NOT NULL DEFAULT '', "
            "license_name TEXT NOT NULL DEFAULT '', verified_at TEXT, last_used_at TEXT, "
            "is_custom INTEGER NOT NULL DEFAULT 0, metadata_json TEXT NOT NULL DEFAULT '{}')"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS database_features (name TEXT PRIMARY KEY, enabled INTEGER NOT NULL, "
            "detail TEXT NOT NULL DEFAULT '')"),
    };
}

QStringList indexes() {
    return {
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_recordings_updated ON recordings(updated_at DESC)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_recordings_deleted ON recordings(deleted_at)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_jobs_recording ON transcription_jobs(recording_id, "
                       "created_at DESC)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_jobs_state_queue ON transcription_jobs(state, queue_position)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_chunks_job_state ON job_chunks(job_id, state, ordinal)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_segments_job_time ON transcript_segments(job_id, "
                       "start_ms, ordinal)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_segments_recording ON transcript_segments(recording_id, job_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_glossary_terms_profile ON glossary_terms(profile_id, "
                       "enabled, priority DESC)"),
    };
}

QStringList revisionHistorySchema() {
    return {
        QStringLiteral("UPDATE transcription_jobs AS current SET revision_number=(SELECT COUNT(*) FROM "
                       "transcription_jobs AS ordered WHERE ordered.recording_id=current.recording_id AND "
                       "(ordered.created_at<current.created_at OR (ordered.created_at=current.created_at AND "
                       "ordered.id<=current.id)))"),
        QStringLiteral(
            "WITH ranked(id,new_position) AS (SELECT id,ROW_NUMBER() OVER (ORDER BY queue_position,"
            "created_at,id)-1 FROM transcription_jobs WHERE state='Queued') UPDATE transcription_jobs SET "
            "queue_position=(SELECT new_position FROM ranked WHERE ranked.id=transcription_jobs.id) WHERE "
            "id IN (SELECT id FROM ranked)"),
        QStringLiteral(
            "UPDATE transcription_jobs SET "
            "state='Interrupted',interrupted_at=COALESCE(interrupted_at,created_at),"
            "error_code=CASE WHEN error_code='' THEN 'WorkerCrashed' ELSE error_code END,"
            "error_message=CASE WHEN error_message='' THEN 'The previous transcription owner is no longer "
            "active.' ELSE error_message END WHERE state IN ('Preparing','Normalizing','WaitingForModel',"
            "'LoadingModel','AnalyzingSpeech','Transcribing','Finalizing','Cancelling')"),
        QStringLiteral(
            "UPDATE recordings SET active_job_id=(SELECT candidate.id FROM transcription_jobs candidate "
            "WHERE candidate.recording_id=recordings.id AND candidate.state='Completed' ORDER BY "
            "candidate.revision_number DESC,candidate.id DESC LIMIT 1)"),
        QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_jobs_recording_revision ON "
                       "transcription_jobs(recording_id,revision_number)"),
        QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_jobs_single_execution ON transcription_jobs((1)) WHERE "
            "state IN ('Preparing','Normalizing','WaitingForModel','LoadingModel','AnalyzingSpeech',"
            "'Transcribing','Finalizing','Cancelling')"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS transcription_job_events ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, job_id TEXT NOT NULL REFERENCES transcription_jobs(id) "
            "ON DELETE CASCADE, event_type TEXT NOT NULL, severity TEXT NOT NULL DEFAULT 'info', state TEXT, "
            "stage TEXT, progress REAL CHECK(progress IS NULL OR (progress>=0 AND progress<=1)), "
            "code TEXT NOT NULL DEFAULT '', message TEXT NOT NULL DEFAULT '', payload_json TEXT NOT NULL "
            "DEFAULT '{}', created_at TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_job_events_job_id ON transcription_job_events(job_id,id)"),
        QStringLiteral(
            "INSERT INTO "
            "transcription_job_events(job_id,event_type,severity,state,stage,progress,code,message,"
            "payload_json,created_at) SELECT "
            "id,'enqueued','info','Queued','Preparing',0,'','','{}',created_at "
            "FROM transcription_jobs"),
        QStringLiteral(
            "INSERT INTO "
            "transcription_job_events(job_id,event_type,severity,state,stage,progress,code,message,"
            "payload_json,created_at) SELECT id,CASE state WHEN 'Completed' THEN 'completed' WHEN "
            "'Cancelled' "
            "THEN 'cancelled' WHEN 'Failed' THEN 'failed' ELSE 'interrupted' END,CASE state WHEN 'Failed' "
            "THEN "
            "'error' WHEN 'Completed' THEN 'info' ELSE 'warning' "
            "END,state,stage,progress,error_code,error_message,"
            "'{}',COALESCE(completed_at,interrupted_at,started_at,created_at) FROM transcription_jobs WHERE "
            "state IN ('Completed','Cancelled','Failed','Interrupted')"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS asr_execution_lease ("
            "resource TEXT PRIMARY KEY CHECK(resource='asr'), owner_token TEXT NOT NULL, "
            "job_id TEXT NOT NULL REFERENCES transcription_jobs(id) ON DELETE CASCADE, acquired_at TEXT NOT "
            "NULL, heartbeat_at TEXT NOT NULL, expires_at TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS trg_jobs_active_revision_before_delete BEFORE DELETE ON "
            "transcription_jobs WHEN EXISTS(SELECT 1 FROM recordings WHERE active_job_id=OLD.id) BEGIN "
            "UPDATE recordings SET active_job_id=(SELECT candidate.id FROM transcription_jobs candidate "
            "WHERE candidate.recording_id=OLD.recording_id AND candidate.id<>OLD.id AND "
            "candidate.state='Completed' ORDER BY candidate.revision_number DESC,candidate.id DESC LIMIT 1),"
            "updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE active_job_id=OLD.id; END"),
    };
}

QString migrationChecksum(const QStringList& statements) {
    return QString::fromLatin1(
        QCryptographicHash::hash(statements.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256)
            .toHex());
}

} // namespace

DatabaseManager::DatabaseManager(DatabaseOptions options)
    : m_options(std::move(options)), m_instanceId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

DatabaseManager::~DatabaseManager() {
    QSet<QString> names;
    {
        QMutexLocker locker(&m_mutex);
        names = m_connectionNames;
        m_connectionNames.clear();
    }
    for (const QString& name : names) {
        QSqlDatabase::removeDatabase(name);
    }
}

Result<void> DatabaseManager::initialize() {
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        return Result<void>::success();
    }
    if (m_options.filePath.isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("The database file path is empty.")));
    }
    const QFileInfo fileInfo(m_options.filePath);
    if (!fileInfo.absoluteDir().mkpath(QStringLiteral("."))) {
        return Result<void>::failure(UserFacingError::database(
            ErrorCode::DatabaseOpenFailed, QStringLiteral("The database directory could not be created."),
            fileInfo.absolutePath()));
    }
    locker.unlock();
    auto connectionResult = createThreadConnection();
    if (!connectionResult) {
        return Result<void>::failure(connectionResult.error());
    }
    QSqlDatabase database = connectionResult.value();
    auto migrationResult = applyMigrations(database);
    if (!migrationResult) {
        return migrationResult;
    }
    locker.relock();
    m_initialized = true;
    return Result<void>::success();
}

Result<QSqlDatabase> DatabaseManager::connection() const {
    {
        QMutexLocker locker(&m_mutex);
        if (!m_initialized) {
            return Result<QSqlDatabase>::failure(UserFacingError::database(
                ErrorCode::DatabaseOpenFailed, QStringLiteral("The database has not been initialized.")));
        }
    }
    return createThreadConnection();
}

Result<QSqlDatabase> DatabaseManager::createThreadConnection() const {
    const QString name = connectionNameForCurrentThread();
    if (QSqlDatabase::contains(name)) {
        QSqlDatabase existing = QSqlDatabase::database(name, false);
        if (existing.isOpen()) {
            return Result<QSqlDatabase>::success(existing);
        }
    }
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    database.setDatabaseName(m_options.filePath);
    if (!database.open()) {
        const UserFacingError error =
            sqlError(ErrorCode::DatabaseOpenFailed,
                     QStringLiteral("The transcript library could not be opened."), database.lastError());
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
        return Result<QSqlDatabase>::failure(error);
    }
    auto configureResult = configureConnection(database);
    if (!configureResult) {
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
        return Result<QSqlDatabase>::failure(configureResult.error());
    }
    {
        QMutexLocker locker(&m_mutex);
        m_connectionNames.insert(name);
    }
    return Result<QSqlDatabase>::success(database);
}

Result<void> DatabaseManager::configureConnection(QSqlDatabase& database) const {
    const QStringList pragmas = {
        QStringLiteral("PRAGMA foreign_keys = ON"),
        QStringLiteral("PRAGMA busy_timeout = %1").arg(qMax(0, m_options.busyTimeoutMs)),
        QStringLiteral("PRAGMA synchronous = NORMAL"),
    };
    for (const QString& pragma : pragmas) {
        QSqlQuery query(database);
        if (!query.exec(pragma)) {
            return Result<void>::failure(sqlError(
                ErrorCode::DatabaseOpenFailed,
                QStringLiteral("The database connection could not be configured."), query.lastError()));
        }
    }
    if (m_options.enableWriteAheadLog) {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("PRAGMA journal_mode = WAL")) || !query.next() ||
            query.value(0).toString().compare(QStringLiteral("wal"), Qt::CaseInsensitive) != 0) {
            return Result<void>::failure(sqlError(ErrorCode::DatabaseOpenFailed,
                                                  QStringLiteral("Write-ahead logging could not be enabled."),
                                                  query.lastError()));
        }
    }
    return Result<void>::success();
}

Result<void> DatabaseManager::applyMigrations(QSqlDatabase& database) {
    UserFacingError error;
    if (!execute(database,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS schema_migrations ("
                                "version INTEGER PRIMARY KEY, name TEXT NOT NULL, checksum TEXT NOT NULL, "
                                "applied_at TEXT NOT NULL)"),
                 &error)) {
        return Result<void>::failure(error);
    }
    int currentVersion = 0;
    {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("SELECT COALESCE(MAX(version), 0) FROM schema_migrations")) ||
            !query.next()) {
            return Result<void>::failure(sqlError(
                ErrorCode::DatabaseMigrationFailed,
                QStringLiteral("The database schema version could not be read."), query.lastError()));
        }
        currentVersion = query.value(0).toInt();
    }
    constexpr int latestSchemaVersion = 7;
    if (currentVersion > latestSchemaVersion) {
        return Result<void>::failure(
            UserFacingError::database(ErrorCode::DatabaseMigrationFailed,
                                      QStringLiteral("This library was created by a newer %1 version.")
                                          .arg(QString::fromLatin1(AppConfig::ProductName)),
                                      QStringLiteral("schema version %1").arg(currentVersion)));
    }
    const QMap<int, QPair<QString, QString>> expectedMigrations = {
        {1, {QStringLiteral("initial_schema"), migrationChecksum(initialSchema())}},
        {2, {QStringLiteral("query_indexes"), migrationChecksum(indexes())}},
        {3,
         {QStringLiteral("search_index"),
          QString::fromLatin1(
              QCryptographicHash::hash(QByteArrayLiteral("fts5-or-fallback-v1"), QCryptographicHash::Sha256)
                  .toHex())}},
        {4,
         {QStringLiteral("queue_visibility"),
          migrationChecksum({QStringLiteral(
              "ALTER TABLE transcription_jobs ADD COLUMN queue_hidden INTEGER NOT NULL DEFAULT 0")})}},
        {5,
         {QStringLiteral("recording_source_index"),
          migrationChecksum({QStringLiteral(
              "CREATE INDEX IF NOT EXISTS idx_recordings_source_path ON recordings(source_path)")})}},
        {6,
         {QStringLiteral("segment_review_state"),
          migrationChecksum({QStringLiteral(
              "ALTER TABLE transcript_segments ADD COLUMN reviewed INTEGER NOT NULL DEFAULT 0")})}},
        {7,
         {QStringLiteral("revision_history_and_execution_lease"),
          migrationChecksum(revisionHistorySchema())}},
    };
    QSet<int> appliedVersions;
    QSqlQuery applied(database);
    if (!applied.exec(
            QStringLiteral("SELECT version,name,checksum FROM schema_migrations ORDER BY version"))) {
        return Result<void>::failure(sqlError(ErrorCode::DatabaseMigrationFailed,
                                              QStringLiteral("Applied migrations could not be verified."),
                                              applied.lastError()));
    }
    while (applied.next()) {
        const int version = applied.value(0).toInt();
        appliedVersions.insert(version);
        const auto expected = expectedMigrations.constFind(version);
        if (expected == expectedMigrations.cend() || expected->first != applied.value(1).toString() ||
            expected->second != applied.value(2).toString()) {
            return Result<void>::failure(UserFacingError::database(
                ErrorCode::DatabaseMigrationFailed,
                QStringLiteral("A database migration checksum does not match this %1 build.")
                    .arg(QString::fromLatin1(AppConfig::ProductName)),
                QStringLiteral("migration version %1").arg(version)));
        }
    }
    for (int version = 1; version <= currentVersion; ++version) {
        if (!appliedVersions.contains(version)) {
            return Result<void>::failure(
                UserFacingError::database(ErrorCode::DatabaseMigrationFailed,
                                          QStringLiteral("The database migration history is incomplete."),
                                          QStringLiteral("missing migration version %1").arg(version)));
        }
    }
    if (currentVersion > 0 && currentVersion < latestSchemaVersion && m_options.createMigrationBackup) {
        auto backupResult = createBackup();
        if (!backupResult) {
            return Result<void>::failure(backupResult.error());
        }
    }

    const auto applyStatements = [&database](const int version, const QString& name,
                                             const QStringList& statements) -> Result<void> {
        if (!database.transaction()) {
            return Result<void>::failure(sqlError(
                ErrorCode::DatabaseMigrationFailed,
                QStringLiteral("A migration transaction could not be started."), database.lastError()));
        }
        UserFacingError statementError;
        for (const QString& statement : statements) {
            if (!execute(database, statement, &statementError)) {
                database.rollback();
                return Result<void>::failure(statementError);
            }
        }
        const QString checksum = migrationChecksum(statements);
        QSqlQuery record(database);
        record.prepare(QStringLiteral(
            "INSERT INTO schema_migrations(version, name, checksum, applied_at) VALUES(?, ?, ?, ?)"));
        record.addBindValue(version);
        record.addBindValue(name);
        record.addBindValue(checksum);
        record.addBindValue(TimeUtils::nowStorageString());
        if (!record.exec()) {
            const UserFacingError recordError =
                sqlError(ErrorCode::DatabaseMigrationFailed,
                         QStringLiteral("The migration result could not be recorded."), record.lastError());
            database.rollback();
            return Result<void>::failure(recordError);
        }
        if (!database.commit()) {
            database.rollback();
            return Result<void>::failure(sqlError(
                ErrorCode::DatabaseMigrationFailed,
                QStringLiteral("The migration transaction could not be committed."), database.lastError()));
        }
        return Result<void>::success();
    };

    if (currentVersion < 1) {
        auto result = applyStatements(1, QStringLiteral("initial_schema"), initialSchema());
        if (!result)
            return result;
        currentVersion = 1;
    }
    if (currentVersion < 2) {
        auto result = applyStatements(2, QStringLiteral("query_indexes"), indexes());
        if (!result)
            return result;
        currentVersion = 2;
    }
    if (currentVersion < 3) {
        if (!database.transaction()) {
            return Result<void>::failure(
                sqlError(ErrorCode::DatabaseMigrationFailed,
                         QStringLiteral("The search migration could not be started."), database.lastError()));
        }
        QSqlQuery fts(database);
        m_hasFts5 = fts.exec(QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS search_index USING fts5("
                                            "recording_id UNINDEXED, title, notes, tags, transcript, "
                                            "tokenize='unicode61 remove_diacritics 2')"));
        QSqlQuery fallback(database);
        if (!fallback.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS search_index_fallback ("
                "recording_id TEXT PRIMARY KEY REFERENCES recordings(id) ON DELETE CASCADE, "
                "title TEXT NOT NULL, notes TEXT NOT NULL, tags TEXT NOT NULL, transcript TEXT NOT NULL)"))) {
            database.rollback();
            return Result<void>::failure(sqlError(
                ErrorCode::DatabaseMigrationFailed,
                QStringLiteral("The fallback search index could not be created."), fallback.lastError()));
        }
        QSqlQuery feature(database);
        feature.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO database_features(name, enabled, detail) VALUES('fts5', ?, ?)"));
        feature.addBindValue(m_hasFts5 ? 1 : 0);
        feature.addBindValue(m_hasFts5 ? QStringLiteral("SQLite FTS5 virtual table")
                                       : fts.lastError().text());
        if (!feature.exec()) {
            database.rollback();
            return Result<void>::failure(sqlError(
                ErrorCode::DatabaseMigrationFailed,
                QStringLiteral("The database capabilities could not be recorded."), feature.lastError()));
        }
        QSqlQuery record(database);
        record.prepare(QStringLiteral("INSERT INTO schema_migrations(version, name, checksum, applied_at) "
                                      "VALUES(3, 'search_index', ?, ?)"));
        record.addBindValue(QString::fromLatin1(
            QCryptographicHash::hash(QByteArrayLiteral("fts5-or-fallback-v1"), QCryptographicHash::Sha256)
                .toHex()));
        record.addBindValue(TimeUtils::nowStorageString());
        if (!record.exec() || !database.commit()) {
            database.rollback();
            return Result<void>::failure(
                sqlError(ErrorCode::DatabaseMigrationFailed,
                         QStringLiteral("The search migration could not be committed."), record.lastError()));
        }
        currentVersion = 3;
    } else {
        QSqlQuery feature(database);
        if (feature.exec(QStringLiteral("SELECT enabled FROM database_features WHERE name='fts5'")) &&
            feature.next()) {
            m_hasFts5 = feature.value(0).toBool();
        }
    }
    if (currentVersion < 4) {
        auto result = applyStatements(
            4, QStringLiteral("queue_visibility"),
            {QStringLiteral(
                "ALTER TABLE transcription_jobs ADD COLUMN queue_hidden INTEGER NOT NULL DEFAULT 0")});
        if (!result)
            return result;
        currentVersion = 4;
    }
    if (currentVersion < 5) {
        auto result = applyStatements(
            5, QStringLiteral("recording_source_index"),
            {QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_recordings_source_path ON recordings(source_path)")});
        if (!result)
            return result;
        currentVersion = 5;
    }
    if (currentVersion < 6) {
        auto result = applyStatements(
            6, QStringLiteral("segment_review_state"),
            {QStringLiteral(
                "ALTER TABLE transcript_segments ADD COLUMN reviewed INTEGER NOT NULL DEFAULT 0")});
        if (!result)
            return result;
        currentVersion = 6;
    }
    if (currentVersion < 7) {
        auto result = applyStatements(7, QStringLiteral("revision_history_and_execution_lease"),
                                      revisionHistorySchema());
        if (!result)
            return result;
        currentVersion = 7;
    }
    m_schemaVersion = currentVersion;
    return Result<void>::success();
}

Result<void> DatabaseManager::transaction(const std::function<Result<void>(QSqlDatabase&)>& operation) const {
    auto connectionResult = connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlDatabase database = connectionResult.value();
    if (!database.transaction()) {
        return Result<void>::failure(sqlError(
            ErrorCode::DatabaseQueryFailed, QStringLiteral("The database transaction could not be started."),
            database.lastError(), true));
    }
    const Result<void> operationResult = operation(database);
    if (!operationResult) {
        database.rollback();
        return operationResult;
    }
    if (!database.commit()) {
        database.rollback();
        return Result<void>::failure(sqlError(
            ErrorCode::DatabaseQueryFailed,
            QStringLiteral("The database transaction could not be committed."), database.lastError(), true));
    }
    return Result<void>::success();
}

Result<void>
DatabaseManager::immediateTransaction(const std::function<Result<void>(QSqlDatabase&)>& operation) const {
    auto connectionResult = connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlDatabase database = connectionResult.value();
    QSqlQuery begin(database);
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        return Result<void>::failure(
            sqlError(ErrorCode::DatabaseQueryFailed,
                     QStringLiteral("The immediate database transaction could not be started."),
                     begin.lastError(), true));
    }
    const Result<void> operationResult = operation(database);
    if (!operationResult) {
        QSqlQuery rollback(database);
        rollback.exec(QStringLiteral("ROLLBACK"));
        return operationResult;
    }
    QSqlQuery commit(database);
    if (!commit.exec(QStringLiteral("COMMIT"))) {
        QSqlQuery rollback(database);
        rollback.exec(QStringLiteral("ROLLBACK"));
        return Result<void>::failure(
            sqlError(ErrorCode::DatabaseQueryFailed,
                     QStringLiteral("The immediate database transaction could not be committed."),
                     commit.lastError(), true));
    }
    return Result<void>::success();
}

Result<void> DatabaseManager::integrityCheck() const {
    auto connectionResult = connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    if (!query.exec(QStringLiteral("PRAGMA quick_check")) || !query.next()) {
        return Result<void>::failure(sqlError(ErrorCode::DatabaseCorrupt,
                                              QStringLiteral("The library integrity check could not run."),
                                              query.lastError()));
    }
    const QString result = query.value(0).toString();
    if (result != QStringLiteral("ok")) {
        return Result<void>::failure(UserFacingError::database(
            ErrorCode::DatabaseCorrupt, QStringLiteral("The transcript library is damaged."), result));
    }
    return Result<void>::success();
}

Result<QString> DatabaseManager::createBackup(const QString& destinationPath) const {
    if (!QFileInfo::exists(m_options.filePath)) {
        return Result<QString>::failure(UserFacingError::database(
            ErrorCode::NotFound, QStringLiteral("There is no database file to back up."),
            m_options.filePath));
    }
    const QString target =
        destinationPath.isEmpty()
            ? m_options.filePath + QStringLiteral(".backup-") +
                  QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz"))
            : destinationPath;
    if (QFileInfo::exists(target) || QFileInfo(target).isSymLink()) {
        return Result<QString>::failure(UserFacingError::validation(
            ErrorCode::AlreadyExists, QStringLiteral("The backup destination already exists."), target));
    }

    const QFileInfo targetInfo(target);
    if (!QDir().mkpath(targetInfo.absolutePath())) {
        return Result<QString>::failure(
            UserFacingError::database(ErrorCode::DatabaseQueryFailed,
                                      QStringLiteral("The database backup directory could not be created."),
                                      targetInfo.absolutePath()));
    }

    QTemporaryFile snapshotFile(
        QDir(targetInfo.absolutePath()).filePath(QStringLiteral(".breezedesk-snapshot-XXXXXX.sqlite3")));
    snapshotFile.setAutoRemove(true);
    if (!snapshotFile.open()) {
        return Result<QString>::failure(
            UserFacingError::database(ErrorCode::DatabaseQueryFailed,
                                      QStringLiteral("A temporary database snapshot could not be created."),
                                      snapshotFile.errorString(), true));
    }
    const QString snapshotPath = snapshotFile.fileName();
    const ScopedFileRemoval removeSnapshot(snapshotPath);
    snapshotFile.close();
    if (!snapshotFile.remove()) {
        return Result<QString>::failure(UserFacingError::database(
            ErrorCode::DatabaseQueryFailed,
            QStringLiteral("The temporary database snapshot could not be prepared."),
            snapshotFile.errorString(), true));
    }

    auto connectionResult = createThreadConnection();
    if (!connectionResult) {
        return Result<QString>::failure(connectionResult.error());
    }
    QString escapedSnapshotPath = snapshotPath;
    escapedSnapshotPath.replace(QLatin1Char('\''), QStringLiteral("''"));
    QSqlQuery snapshot(connectionResult.value());
    if (!snapshot.exec(QStringLiteral("VACUUM INTO '%1'").arg(escapedSnapshotPath))) {
        return Result<QString>::failure(
            UserFacingError::database(ErrorCode::DatabaseQueryFailed,
                                      QStringLiteral("A consistent database snapshot could not be created."),
                                      snapshot.lastError().text(), true));
    }

    QFile source(snapshotPath);
    QSaveFile destination(target);
    if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) {
        return Result<QString>::failure(UserFacingError::database(
            ErrorCode::DatabaseQueryFailed, QStringLiteral("The database backup file could not be opened."),
            source.errorString().isEmpty() ? destination.errorString() : source.errorString(), true));
    }
    constexpr qint64 CopyBlockSize = 1024 * 1024;
    while (!source.atEnd()) {
        const QByteArray block = source.read(CopyBlockSize);
        if (block.isEmpty() && source.error() != QFileDevice::NoError) {
            destination.cancelWriting();
            return Result<QString>::failure(UserFacingError::database(
                ErrorCode::DatabaseQueryFailed, QStringLiteral("The database snapshot could not be read."),
                source.errorString(), true));
        }
        if (destination.write(block) != block.size()) {
            destination.cancelWriting();
            return Result<QString>::failure(UserFacingError::database(
                ErrorCode::DatabaseQueryFailed, QStringLiteral("The database backup could not be written."),
                destination.errorString(), true));
        }
    }
    if (!destination.commit()) {
        return Result<QString>::failure(UserFacingError::database(
            ErrorCode::DatabaseQueryFailed,
            QStringLiteral("The database backup could not be committed atomically."),
            destination.errorString(), true));
    }
    return Result<QString>::success(target);
}

QString DatabaseManager::connectionNameForCurrentThread() const {
    const auto threadAddress = reinterpret_cast<quintptr>(QThread::currentThread());
    return QStringLiteral("breezedesk-%1-%2").arg(m_instanceId, QString::number(threadAddress, 16));
}

int DatabaseManager::schemaVersion() const {
    QMutexLocker locker(&m_mutex);
    return m_schemaVersion;
}

bool DatabaseManager::hasFts5() const {
    QMutexLocker locker(&m_mutex);
    return m_hasFts5;
}

} // namespace BreezeDesk
