#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace BreezeDesk {

struct ModelManifestEntry {
    QString id;
    QString displayName;
    QString description;
    QString engine;
    QString architecture;
    QString quantization;
    QString downloadUrl;
    QString fileName;
    qint64 fileSize = 0;
    QByteArray sha256;
    QString sourceRepository;
    QString sourceRevision;
    QString licenseName;
    QString licenseUrl;
    qint64 recommendedMemoryBytes = 0;
    QStringList capabilities;
    QString defaultLanguage;
    bool isRecommended = false;

    [[nodiscard]] bool isValid(QString* error = nullptr) const;
};

class ModelManifest final {
  public:
    [[nodiscard]] static ModelManifest fromJson(const QByteArray& json, QString* error = nullptr);
    [[nodiscard]] static ModelManifest loadBundled(QString* error = nullptr);
    [[nodiscard]] QVector<ModelManifestEntry> entries() const;
    [[nodiscard]] const ModelManifestEntry* find(const QString& id) const;
    [[nodiscard]] bool isValid(QString* error = nullptr) const;

  private:
    QVector<ModelManifestEntry> m_entries;
};

} // namespace BreezeDesk
