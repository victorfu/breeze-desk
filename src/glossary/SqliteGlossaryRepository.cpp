#include "breezedesk/glossary/SqliteGlossaryRepository.h"

#include "breezedesk/core/TimeUtils.h"
#include "breezedesk/database/DatabaseManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace BreezeDesk {
namespace {
UserFacingError queryError(const QString& message, const QSqlQuery& query) {
    return UserFacingError::database(ErrorCode::DatabaseQueryFailed, message, query.lastError().text(), true);
}
QString aliasesToJson(const QStringList& aliases) {
    QJsonArray array;
    for (const QString& alias : aliases)
        if (!alias.trimmed().isEmpty())
            array.append(alias.trimmed());
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
QStringList aliasesFromJson(const QString& json) {
    QStringList result;
    for (const QJsonValue& value : QJsonDocument::fromJson(json.toUtf8()).array())
        result.append(value.toString());
    return result;
}
GlossaryProfile readProfile(QSqlQuery& query) {
    return {query.value(QStringLiteral("id")).toString(),
            query.value(QStringLiteral("name")).toString(),
            query.value(QStringLiteral("description")).toString(),
            query.value(QStringLiteral("project_context")).toString(),
            TimeUtils::fromStorageString(query.value(QStringLiteral("created_at")).toString()),
            TimeUtils::fromStorageString(query.value(QStringLiteral("updated_at")).toString())};
}
GlossaryTerm readTerm(QSqlQuery& query) {
    GlossaryTerm term;
    term.id = query.value(QStringLiteral("id")).toString();
    term.profileId = query.value(QStringLiteral("profile_id")).toString();
    term.canonicalText = query.value(QStringLiteral("canonical_text")).toString();
    term.aliases = aliasesFromJson(query.value(QStringLiteral("aliases_json")).toString());
    term.category = query.value(QStringLiteral("category")).toString();
    term.language = query.value(QStringLiteral("language")).toString();
    term.priority = query.value(QStringLiteral("priority")).toInt();
    term.caseSensitive = query.value(QStringLiteral("case_sensitive")).toBool();
    term.enabled = query.value(QStringLiteral("enabled")).toBool();
    term.notes = query.value(QStringLiteral("notes")).toString();
    term.createdAt = TimeUtils::fromStorageString(query.value(QStringLiteral("created_at")).toString());
    term.updatedAt = TimeUtils::fromStorageString(query.value(QStringLiteral("updated_at")).toString());
    return term;
}
QString nonNull(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value;
}
} // namespace

SqliteGlossaryRepository::SqliteGlossaryRepository(DatabaseManager& databaseManager)
    : m_databaseManager(databaseManager) {}

Result<QString> SqliteGlossaryRepository::createProfile(GlossaryProfile profile) {
    if (profile.name.trimmed().isEmpty())
        return Result<QString>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A glossary profile name is required.")));
    if (profile.id.isEmpty())
        profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = TimeUtils::nowStorageString();
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<QString>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral(
        "INSERT INTO glossary_profiles(id,name,description,project_context,created_at,updated_at) "
        "VALUES(?,?,?,?,?,?)"));
    query.addBindValue(profile.id);
    query.addBindValue(profile.name.trimmed());
    query.addBindValue(nonNull(profile.description));
    query.addBindValue(nonNull(profile.projectContext));
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec())
        return Result<QString>::failure(
            queryError(QStringLiteral("The glossary profile could not be created."), query));
    return Result<QString>::success(profile.id);
}

Result<void> SqliteGlossaryRepository::updateProfile(const GlossaryProfile& profile) {
    if (profile.id.isEmpty() || profile.name.trimmed().isEmpty())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("Glossary profile ID and name are required.")));
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral(
        "UPDATE glossary_profiles SET name=?,description=?,project_context=?,updated_at=? WHERE id=?"));
    query.addBindValue(profile.name.trimmed());
    query.addBindValue(nonNull(profile.description));
    query.addBindValue(nonNull(profile.projectContext));
    query.addBindValue(TimeUtils::nowStorageString());
    query.addBindValue(profile.id);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The glossary profile could not be updated."), query));
    if (query.numRowsAffected() == 0)
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The glossary profile no longer exists.")));
    return Result<void>::success();
}

Result<void> SqliteGlossaryRepository::deleteProfile(const QString& profileId) {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral("DELETE FROM glossary_profiles WHERE id=?"));
    query.addBindValue(profileId);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The glossary profile could not be deleted."), query));
    return Result<void>::success();
}

Result<QString> SqliteGlossaryRepository::duplicateProfile(const QString& profileId, const QString& newName) {
    auto source = profile(profileId);
    if (!source)
        return Result<QString>::failure(source.error());
    if (!source.value())
        return Result<QString>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The glossary profile does not exist.")));
    GlossaryProfile copy = *source.value();
    copy.id.clear();
    copy.name = newName;
    copy.createdAt = {};
    copy.updatedAt = {};
    auto idResult = createProfile(copy);
    if (!idResult)
        return idResult;
    auto sourceTerms = terms(profileId);
    if (!sourceTerms)
        return Result<QString>::failure(sourceTerms.error());
    for (GlossaryTerm term : sourceTerms.value()) {
        term.id.clear();
        term.profileId = idResult.value();
        term.createdAt = {};
        term.updatedAt = {};
        auto termResult = createTerm(term);
        if (!termResult) {
            const auto cleanupResult = deleteProfile(idResult.value());
            Q_UNUSED(cleanupResult)
            return Result<QString>::failure(termResult.error());
        }
    }
    return idResult;
}

Result<std::optional<GlossaryProfile>> SqliteGlossaryRepository::profile(const QString& profileId) const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<std::optional<GlossaryProfile>>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral("SELECT * FROM glossary_profiles WHERE id=?"));
    query.addBindValue(profileId);
    if (!query.exec())
        return Result<std::optional<GlossaryProfile>>::failure(
            queryError(QStringLiteral("The glossary profile could not be loaded."), query));
    if (!query.next())
        return Result<std::optional<GlossaryProfile>>::success(std::nullopt);
    return Result<std::optional<GlossaryProfile>>::success(readProfile(query));
}

Result<QList<GlossaryProfile>> SqliteGlossaryRepository::profiles() const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<QList<GlossaryProfile>>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    if (!query.exec(QStringLiteral("SELECT * FROM glossary_profiles ORDER BY name COLLATE NOCASE")))
        return Result<QList<GlossaryProfile>>::failure(
            queryError(QStringLiteral("Glossary profiles could not be loaded."), query));
    QList<GlossaryProfile> result;
    while (query.next())
        result.append(readProfile(query));
    return Result<QList<GlossaryProfile>>::success(result);
}

Result<QString> SqliteGlossaryRepository::createTerm(GlossaryTerm term) {
    if (term.profileId.isEmpty() || term.canonicalText.trimmed().isEmpty())
        return Result<QString>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A profile and canonical term are required.")));
    if (term.id.isEmpty())
        term.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = TimeUtils::nowStorageString();
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<QString>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral(
        "INSERT INTO glossary_terms(id,profile_id,canonical_text,aliases_json,category,language,priority,"
        "case_sensitive,enabled,notes,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(term.id);
    query.addBindValue(term.profileId);
    query.addBindValue(term.canonicalText.trimmed());
    query.addBindValue(aliasesToJson(term.aliases));
    query.addBindValue(nonNull(term.category));
    query.addBindValue(nonNull(term.language));
    query.addBindValue(term.priority);
    query.addBindValue(term.caseSensitive);
    query.addBindValue(term.enabled);
    query.addBindValue(nonNull(term.notes));
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec())
        return Result<QString>::failure(
            queryError(QStringLiteral("The glossary term could not be created."), query));
    return Result<QString>::success(term.id);
}

Result<void> SqliteGlossaryRepository::updateTerm(const GlossaryTerm& term) {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral(
        "UPDATE glossary_terms SET canonical_text=?,aliases_json=?,category=?,language=?,priority=?,"
        "case_sensitive=?,enabled=?,notes=?,updated_at=? WHERE id=? AND profile_id=?"));
    query.addBindValue(term.canonicalText.trimmed());
    query.addBindValue(aliasesToJson(term.aliases));
    query.addBindValue(nonNull(term.category));
    query.addBindValue(nonNull(term.language));
    query.addBindValue(term.priority);
    query.addBindValue(term.caseSensitive);
    query.addBindValue(term.enabled);
    query.addBindValue(nonNull(term.notes));
    query.addBindValue(TimeUtils::nowStorageString());
    query.addBindValue(term.id);
    query.addBindValue(term.profileId);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The glossary term could not be updated."), query));
    return Result<void>::success();
}

Result<void> SqliteGlossaryRepository::deleteTerm(const QString& termId) {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral("DELETE FROM glossary_terms WHERE id=?"));
    query.addBindValue(termId);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The glossary term could not be deleted."), query));
    return Result<void>::success();
}

Result<QList<GlossaryTerm>> SqliteGlossaryRepository::terms(const QString& profileId,
                                                            const QString& search) const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<QList<GlossaryTerm>>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    if (search.trimmed().isEmpty()) {
        query.prepare(QStringLiteral("SELECT * FROM glossary_terms WHERE profile_id=? ORDER BY priority "
                                     "DESC,canonical_text COLLATE NOCASE"));
        query.addBindValue(profileId);
    } else {
        query.prepare(QStringLiteral("SELECT * FROM glossary_terms WHERE profile_id=? AND (canonical_text "
                                     "LIKE ? OR aliases_json LIKE ? OR notes LIKE ?) "
                                     "ORDER BY priority DESC,canonical_text COLLATE NOCASE"));
        const QString pattern = QLatin1Char('%') + search.trimmed() + QLatin1Char('%');
        query.addBindValue(profileId);
        query.addBindValue(pattern);
        query.addBindValue(pattern);
        query.addBindValue(pattern);
    }
    if (!query.exec())
        return Result<QList<GlossaryTerm>>::failure(
            queryError(QStringLiteral("Glossary terms could not be loaded."), query));
    QList<GlossaryTerm> result;
    while (query.next())
        result.append(readTerm(query));
    return Result<QList<GlossaryTerm>>::success(result);
}

Result<void> SqliteGlossaryRepository::setTermsEnabled(const QStringList& termIds, const bool enabled) {
    return m_databaseManager.transaction([&](QSqlDatabase& database) {
        for (const QString& termId : termIds) {
            QSqlQuery query(database);
            query.prepare(QStringLiteral("UPDATE glossary_terms SET enabled=?,updated_at=? WHERE id=?"));
            query.addBindValue(enabled);
            query.addBindValue(TimeUtils::nowStorageString());
            query.addBindValue(termId);
            if (!query.exec())
                return Result<void>::failure(
                    queryError(QStringLiteral("Glossary terms could not be changed."), query));
        }
        return Result<void>::success();
    });
}

} // namespace BreezeDesk
