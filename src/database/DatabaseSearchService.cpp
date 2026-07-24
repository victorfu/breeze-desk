#include "breezedesk/database/DatabaseSearchService.h"

#include "breezedesk/database/DatabaseManager.h"

#include <QSqlError>
#include <QSqlQuery>

namespace BreezeDesk {
namespace {
UserFacingError searchError(const QString& message, const QSqlQuery& query) {
    return UserFacingError::database(ErrorCode::DatabaseQueryFailed, message, query.lastError().text(), true);
}

QStringList searchTokens(const QString& queryText) {
    return queryText.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

// The trigram tokenizer cannot match phrases shorter than three code points, so shorter
// tokens (most two-character Chinese words) are matched with escaped LIKE patterns instead.
bool trigramMatchable(const QString& token) {
    return token.toUcs4().size() >= 3;
}

QString ftsPhrase(QString token) {
    token.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + token + QLatin1Char('"');
}

QString likePattern(const QString& token) {
    QString escaped = token;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    escaped.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return QLatin1Char('%') + escaped + QLatin1Char('%');
}
} // namespace

DatabaseSearchService::DatabaseSearchService(DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager) {}

Result<void> DatabaseSearchService::rebuildRecording(const QString& recordingId) const {
    return m_databaseManager.transaction(
        [&](QSqlDatabase& database) { return rebuildRecording(database, recordingId); });
}

Result<void> DatabaseSearchService::rebuildRecording(QSqlDatabase& database,
                                                     const QString& recordingId) const {
    QSqlQuery source(database);
    source.prepare(QStringLiteral(
        "SELECT r.title,r.notes,COALESCE((SELECT group_concat(t.name,' ') FROM tags t "
        "JOIN recording_tags rt ON rt.tag_id=t.id WHERE rt.recording_id=r.id),''),"
        "COALESCE((SELECT group_concat(CASE WHEN s.edited_text='' THEN s.original_text ELSE "
        "s.edited_text END,' ') "
        "FROM transcript_segments s WHERE s.recording_id=r.id), '') FROM recordings r WHERE r.id=?"));
    source.addBindValue(recordingId);
    if (!source.exec())
        return Result<void>::failure(
            searchError(QStringLiteral("Search content could not be collected."), source));
    if (!source.next())
        return Result<void>::success();
    const QVariantList values = {recordingId, source.value(0), source.value(1), source.value(2),
                                 source.value(3)};
    QSqlQuery fallbackDelete(database);
    fallbackDelete.prepare(QStringLiteral("DELETE FROM search_index_fallback WHERE recording_id=?"));
    fallbackDelete.addBindValue(recordingId);
    if (!fallbackDelete.exec())
        return Result<void>::failure(
            searchError(QStringLiteral("The fallback search entry could not be refreshed."), fallbackDelete));
    QSqlQuery fallbackInsert(database);
    fallbackInsert.prepare(QStringLiteral(
        "INSERT INTO search_index_fallback(recording_id,title,notes,tags,transcript) VALUES(?,?,?,?,?)"));
    for (const QVariant& value : values)
        fallbackInsert.addBindValue(value);
    if (!fallbackInsert.exec())
        return Result<void>::failure(
            searchError(QStringLiteral("The fallback search entry could not be written."), fallbackInsert));
    if (m_databaseManager.hasFts5()) {
        QSqlQuery ftsDelete(database);
        ftsDelete.prepare(QStringLiteral("DELETE FROM search_index WHERE recording_id=?"));
        ftsDelete.addBindValue(recordingId);
        if (!ftsDelete.exec())
            return Result<void>::failure(
                searchError(QStringLiteral("The full-text entry could not be refreshed."), ftsDelete));
        QSqlQuery ftsInsert(database);
        ftsInsert.prepare(QStringLiteral(
            "INSERT INTO search_index(recording_id,title,notes,tags,transcript) VALUES(?,?,?,?,?)"));
        for (const QVariant& value : values)
            ftsInsert.addBindValue(value);
        if (!ftsInsert.exec())
            return Result<void>::failure(
                searchError(QStringLiteral("The full-text entry could not be written."), ftsInsert));
    }
    return Result<void>::success();
}

Result<void> DatabaseSearchService::rebuildAll() const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    if (!query.exec(QStringLiteral("SELECT id FROM recordings"))) {
        return Result<void>::failure(
            searchError(QStringLiteral("Recordings could not be enumerated for indexing."), query));
    }
    QStringList ids;
    while (query.next())
        ids.append(query.value(0).toString());
    for (const QString& id : ids) {
        auto result = rebuildRecording(id);
        if (!result)
            return result;
    }
    return Result<void>::success();
}

Result<QList<SearchResult>> DatabaseSearchService::search(const QString& queryText, const int limit,
                                                          const int offset) const {
    const QStringList tokens = searchTokens(queryText);
    if (tokens.isEmpty())
        return Result<QList<SearchResult>>::success({});
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<QList<SearchResult>>::failure(connectionResult.error());
    QStringList matchTokens;
    QStringList likeTokens;
    if (m_databaseManager.hasFts5()) {
        for (const QString& token : tokens)
            (trigramMatchable(token) ? matchTokens : likeTokens).append(token);
    } else {
        likeTokens = tokens;
    }
    const QString columnFilter = QStringLiteral(
        " AND (s.title LIKE ? ESCAPE '\\' OR s.notes LIKE ? ESCAPE '\\' OR s.tags LIKE ? ESCAPE '\\' "
        "OR s.transcript LIKE ? ESCAPE '\\')");
    QSqlQuery query(connectionResult.value());
    if (!matchTokens.isEmpty()) {
        QString sql = QStringLiteral(
            "SELECT s.recording_id,r.title,snippet(search_index,4,'<mark>','</mark>',' … ',18) "
            "FROM search_index s JOIN recordings r ON r.id=s.recording_id "
            "WHERE search_index MATCH ? AND r.deleted_at IS NULL");
        for (int i = 0; i < likeTokens.size(); ++i)
            sql += columnFilter;
        sql += QStringLiteral(" ORDER BY bm25(search_index) LIMIT ? OFFSET ?");
        query.prepare(sql);
        QStringList phrases;
        for (const QString& token : matchTokens)
            phrases.append(ftsPhrase(token));
        query.addBindValue(phrases.join(QLatin1Char(' ')));
    } else {
        QString sql = QStringLiteral(
            "SELECT s.recording_id,r.title,substr(s.transcript,1,240) FROM search_index_fallback s "
            "JOIN recordings r ON r.id=s.recording_id WHERE r.deleted_at IS NULL");
        for (int i = 0; i < likeTokens.size(); ++i)
            sql += columnFilter;
        sql += QStringLiteral(" ORDER BY r.updated_at DESC LIMIT ? OFFSET ?");
        query.prepare(sql);
    }
    for (const QString& token : likeTokens) {
        const QString pattern = likePattern(token);
        for (int i = 0; i < 4; ++i)
            query.addBindValue(pattern);
    }
    query.addBindValue(qBound(1, limit, 500));
    query.addBindValue(qMax(0, offset));
    if (!query.exec())
        return Result<QList<SearchResult>>::failure(
            searchError(QStringLiteral("The library search could not be completed."), query));
    QString segmentSql =
        QStringLiteral("SELECT id,start_ms,CASE WHEN edited_text='' THEN original_text ELSE edited_text END "
                       "FROM transcript_segments WHERE recording_id=? AND (");
    QStringList segmentClauses;
    for (int i = 0; i < tokens.size(); ++i) {
        segmentClauses.append(
            QStringLiteral("original_text LIKE ? ESCAPE '\\' OR edited_text LIKE ? ESCAPE '\\'"));
    }
    segmentSql += segmentClauses.join(QStringLiteral(" OR ")) + QStringLiteral(") ORDER BY start_ms LIMIT 1");
    QList<SearchResult> results;
    while (query.next()) {
        SearchResult result;
        result.recordingId = query.value(0).toString();
        result.title = query.value(1).toString();
        result.snippet = query.value(2).toString();
        QSqlQuery segment(connectionResult.value());
        segment.prepare(segmentSql);
        segment.addBindValue(result.recordingId);
        for (const QString& token : tokens) {
            const QString pattern = likePattern(token);
            segment.addBindValue(pattern);
            segment.addBindValue(pattern);
        }
        if (!segment.exec()) {
            return Result<QList<SearchResult>>::failure(
                searchError(QStringLiteral("Transcript search locations could not be loaded."), segment));
        }
        if (segment.next()) {
            result.segmentId = segment.value(0).toString();
            result.startMs = segment.value(1).toLongLong();
            result.snippet = segment.value(2).toString();
        }
        results.append(result);
    }
    return Result<QList<SearchResult>>::success(results);
}

} // namespace BreezeDesk
