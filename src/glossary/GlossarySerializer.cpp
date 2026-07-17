#include "breezedesk/glossary/GlossarySerializer.h"

#include "breezedesk/core/TextUtils.h"
#include "breezedesk/core/TimeUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace BreezeDesk {
namespace {
QJsonObject termObject(const GlossaryTerm& term) {
    QJsonArray aliases;
    for (const QString& alias : term.aliases)
        aliases.append(alias);
    return {{QStringLiteral("id"), term.id},
            {QStringLiteral("canonicalText"), term.canonicalText},
            {QStringLiteral("aliases"), aliases},
            {QStringLiteral("category"), term.category},
            {QStringLiteral("language"), term.language},
            {QStringLiteral("priority"), term.priority},
            {QStringLiteral("caseSensitive"), term.caseSensitive},
            {QStringLiteral("enabled"), term.enabled},
            {QStringLiteral("notes"), term.notes}};
}
QList<QStringList> parseCsv(const QString& text, bool* valid) {
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            if (c == QLatin1Char('"') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                field += c;
                ++i;
            } else if (c == QLatin1Char('"'))
                quoted = false;
            else
                field += c;
        } else if (c == QLatin1Char('"') && field.isEmpty())
            quoted = true;
        else if (c == QLatin1Char(',')) {
            row.append(field);
            field.clear();
        } else if (c == QLatin1Char('\n')) {
            row.append(field);
            rows.append(row);
            field.clear();
            row.clear();
        } else if (c != QLatin1Char('\r'))
            field += c;
    }
    if (!field.isEmpty() || !row.isEmpty()) {
        row.append(field);
        rows.append(row);
    }
    *valid = !quoted;
    return rows;
}
} // namespace

QByteArray GlossarySerializer::toJson(const GlossaryDocument& document) {
    QJsonArray terms;
    for (const GlossaryTerm& term : document.terms)
        terms.append(termObject(term));
    QJsonObject profile{{QStringLiteral("id"), document.profile.id},
                        {QStringLiteral("name"), document.profile.name},
                        {QStringLiteral("description"), document.profile.description},
                        {QStringLiteral("projectContext"), document.profile.projectContext}};
    return QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                     {QStringLiteral("profile"), profile},
                                     {QStringLiteral("terms"), terms}})
        .toJson(QJsonDocument::Indented);
}

Result<GlossaryDocument> GlossarySerializer::fromJson(const QByteArray& data) {
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject())
        return Result<GlossaryDocument>::failure(UserFacingError::validation(
            ErrorCode::SerializationFailed, QStringLiteral("The glossary JSON is invalid."),
            error.errorString()));
    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != 1)
        return Result<GlossaryDocument>::failure(
            UserFacingError::validation(ErrorCode::SerializationFailed,
                                        QStringLiteral("This glossary schema version is not supported.")));
    const QJsonObject object = root.value(QStringLiteral("profile")).toObject();
    GlossaryDocument result;
    result.profile = {object.value(QStringLiteral("id")).toString(),
                      object.value(QStringLiteral("name")).toString(),
                      object.value(QStringLiteral("description")).toString(),
                      object.value(QStringLiteral("projectContext")).toString(),
                      {},
                      {}};
    if (result.profile.name.trimmed().isEmpty())
        return Result<GlossaryDocument>::failure(UserFacingError::validation(
            ErrorCode::SerializationFailed, QStringLiteral("The glossary profile name is missing.")));
    for (const QJsonValue& value : root.value(QStringLiteral("terms")).toArray()) {
        const QJsonObject item = value.toObject();
        GlossaryTerm term;
        term.id = item.value(QStringLiteral("id")).toString();
        term.profileId = result.profile.id;
        term.canonicalText = item.value(QStringLiteral("canonicalText")).toString();
        for (const QJsonValue& alias : item.value(QStringLiteral("aliases")).toArray())
            term.aliases.append(alias.toString());
        term.category = item.value(QStringLiteral("category")).toString();
        term.language = item.value(QStringLiteral("language")).toString();
        term.priority = item.value(QStringLiteral("priority")).toInt();
        term.caseSensitive = item.value(QStringLiteral("caseSensitive")).toBool();
        term.enabled = item.value(QStringLiteral("enabled")).toBool(true);
        term.notes = item.value(QStringLiteral("notes")).toString();
        if (!term.canonicalText.trimmed().isEmpty())
            result.terms.append(term);
    }
    return Result<GlossaryDocument>::success(result);
}

QByteArray GlossarySerializer::termsToCsv(const QList<GlossaryTerm>& terms, const bool withBom) {
    QString output =
        QStringLiteral("canonical_text,aliases,category,language,priority,case_sensitive,enabled,notes\n");
    for (const GlossaryTerm& term : terms) {
        output += QStringList{TextUtils::csvField(term.canonicalText),
                              TextUtils::csvField(term.aliases.join(QLatin1Char('|'))),
                              TextUtils::csvField(term.category),
                              TextUtils::csvField(term.language),
                              QString::number(term.priority),
                              term.caseSensitive ? QStringLiteral("true") : QStringLiteral("false"),
                              term.enabled ? QStringLiteral("true") : QStringLiteral("false"),
                              TextUtils::csvField(term.notes)}
                      .join(QLatin1Char(','));
        output += QLatin1Char('\n');
    }
    QByteArray bytes = output.toUtf8();
    if (withBom)
        bytes.prepend("\xEF\xBB\xBF", 3);
    return bytes;
}

Result<QList<GlossaryTerm>> GlossarySerializer::termsFromCsv(const QByteArray& data,
                                                             const QString& profileId) {
    QString text = QString::fromUtf8(data);
    if (!text.isEmpty() && text.front() == QChar(0xfeff))
        text.remove(0, 1);
    bool valid = false;
    const auto rows = parseCsv(text, &valid);
    if (!valid || rows.isEmpty())
        return Result<QList<GlossaryTerm>>::failure(UserFacingError::validation(
            ErrorCode::SerializationFailed, QStringLiteral("The glossary CSV is malformed.")));
    const QStringList expected{QStringLiteral("canonical_text"), QStringLiteral("aliases"),
                               QStringLiteral("category"),       QStringLiteral("language"),
                               QStringLiteral("priority"),       QStringLiteral("case_sensitive"),
                               QStringLiteral("enabled"),        QStringLiteral("notes")};
    if (rows.first() != expected)
        return Result<QList<GlossaryTerm>>::failure(UserFacingError::validation(
            ErrorCode::SerializationFailed, QStringLiteral("The glossary CSV header is not recognized.")));
    QList<GlossaryTerm> terms;
    for (int i = 1; i < rows.size(); ++i) {
        const QStringList& row = rows.at(i);
        if (row.size() == 1 && row.first().isEmpty())
            continue;
        if (row.size() != expected.size())
            return Result<QList<GlossaryTerm>>::failure(UserFacingError::validation(
                ErrorCode::SerializationFailed,
                QStringLiteral("Glossary CSV row %1 has the wrong number of columns.").arg(i + 1)));
        GlossaryTerm term;
        term.profileId = profileId;
        term.canonicalText = row.at(0).trimmed();
        term.aliases = row.at(1).split(QLatin1Char('|'), Qt::SkipEmptyParts);
        term.category = row.at(2);
        term.language = row.at(3);
        bool priorityOk = false;
        term.priority = row.at(4).toInt(&priorityOk);
        term.caseSensitive = row.at(5).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
        term.enabled = row.at(6).compare(QStringLiteral("false"), Qt::CaseInsensitive) != 0;
        term.notes = row.at(7);
        if (term.canonicalText.isEmpty() || !priorityOk)
            return Result<QList<GlossaryTerm>>::failure(UserFacingError::validation(
                ErrorCode::SerializationFailed,
                QStringLiteral("Glossary CSV row %1 contains invalid data.").arg(i + 1)));
        terms.append(term);
    }
    return Result<QList<GlossaryTerm>>::success(terms);
}

} // namespace BreezeDesk
