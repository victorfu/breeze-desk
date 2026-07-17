#include "breezedesk/models/ModelManifest.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUrl>

namespace BreezeDesk {

namespace {
qint64 jsonInteger(const QJsonObject& object, const QString& key) {
    return object.value(key).toVariant().toLongLong();
}

ModelManifestEntry parseEntry(const QJsonObject& object) {
    ModelManifestEntry entry;
    entry.id = object.value(QStringLiteral("id")).toString();
    entry.displayName = object.value(QStringLiteral("displayName")).toString();
    entry.description = object.value(QStringLiteral("description")).toString();
    entry.engine = object.value(QStringLiteral("engine")).toString();
    entry.architecture = object.value(QStringLiteral("architecture")).toString();
    entry.quantization = object.value(QStringLiteral("quantization")).toString();
    entry.downloadUrl = object.value(QStringLiteral("downloadUrl")).toString();
    entry.fileName = object.value(QStringLiteral("fileName")).toString();
    entry.fileSize = jsonInteger(object, QStringLiteral("fileSize"));
    entry.sha256 = object.value(QStringLiteral("sha256")).toString().toLatin1().toLower();
    entry.sourceRepository = object.value(QStringLiteral("sourceRepository")).toString();
    entry.sourceRevision = object.value(QStringLiteral("sourceRevision")).toString();
    entry.licenseName = object.value(QStringLiteral("licenseName")).toString();
    entry.licenseUrl = object.value(QStringLiteral("licenseUrl")).toString();
    entry.recommendedMemoryBytes = jsonInteger(object, QStringLiteral("recommendedMemoryBytes"));
    const QJsonArray capabilities = object.value(QStringLiteral("capabilities")).toArray();
    for (const QJsonValue& capability : capabilities) {
        entry.capabilities.push_back(capability.toString());
    }
    entry.defaultLanguage = object.value(QStringLiteral("defaultLanguage")).toString();
    entry.isRecommended = object.value(QStringLiteral("isRecommended")).toBool();
    return entry;
}
} // namespace

bool ModelManifestEntry::isValid(QString* error) const {
    const bool validHash =
        sha256.size() == 64 && std::all_of(sha256.cbegin(), sha256.cend(), [](char character) {
            return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
        });
    if (id.isEmpty() || displayName.isEmpty() || engine != QStringLiteral("whisper.cpp") ||
        fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\')) || fileSize <= 0 ||
        !validHash || !QUrl(downloadUrl).isValid() || sourceRevision.size() < 7 || licenseName.isEmpty() ||
        licenseUrl.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("Invalid model manifest entry: %1").arg(id);
        }
        return false;
    }
    return true;
}

ModelManifest ModelManifest::fromJson(const QByteArray& json, QString* error) {
    ModelManifest result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("Model manifest is not valid JSON: %1").arg(parseError.errorString());
        }
        return result;
    }
    const QJsonArray models = document.object().value(QStringLiteral("models")).toArray();
    result.m_entries.reserve(models.size());
    for (const QJsonValue& value : models) {
        result.m_entries.push_back(parseEntry(value.toObject()));
    }
    if (!result.isValid(error)) {
        result.m_entries.clear();
    }
    return result;
}

ModelManifest ModelManifest::loadBundled(QString* error) {
    QFile file(QStringLiteral(":/models/models.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return {};
    }
    return fromJson(file.readAll(), error);
}

QVector<ModelManifestEntry> ModelManifest::entries() const {
    return m_entries;
}

const ModelManifestEntry* ModelManifest::find(const QString& id) const {
    const auto iterator = std::find_if(m_entries.cbegin(), m_entries.cend(),
                                       [&id](const ModelManifestEntry& entry) { return entry.id == id; });
    return iterator == m_entries.cend() ? nullptr : &(*iterator);
}

bool ModelManifest::isValid(QString* error) const {
    if (m_entries.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("Model manifest contains no models.");
        }
        return false;
    }
    QSet<QString> identifiers;
    int recommendedCount = 0;
    for (const ModelManifestEntry& entry : m_entries) {
        if (!entry.isValid(error) || identifiers.contains(entry.id)) {
            if (error != nullptr && identifiers.contains(entry.id)) {
                *error = QStringLiteral("Duplicate model id: %1").arg(entry.id);
            }
            return false;
        }
        identifiers.insert(entry.id);
        recommendedCount += entry.isRecommended ? 1 : 0;
    }
    if (recommendedCount != 1) {
        if (error != nullptr) {
            *error = QStringLiteral("Exactly one model must be recommended.");
        }
        return false;
    }
    return true;
}

} // namespace BreezeDesk
