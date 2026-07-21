#pragma once

#include "breezedesk/models/ModelDownloadOperation.h"
#include "breezedesk/models/ModelManifest.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

class QNetworkAccessManager;

namespace BreezeDesk {

struct CustomModelInfo {
    QString id;
    QString displayName;
    QString path;
    qint64 fileSize{0};
    QByteArray sha256;
};

class ModelManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString modelsDirectory READ modelsDirectory CONSTANT)
    Q_PROPERTY(
        QString defaultModelId READ defaultModelId WRITE setDefaultModelId NOTIFY defaultModelIdChanged)

  public:
    explicit ModelManager(QObject* parent = nullptr);

    [[nodiscard]] const ModelManifest& manifest() const;
    [[nodiscard]] QString modelsDirectory() const;
    [[nodiscard]] QString defaultModelId() const;
    void setDefaultModelId(const QString& id);
    [[nodiscard]] bool isInstalled(const QString& id) const;
    [[nodiscard]] QString modelPath(const QString& id) const;
    [[nodiscard]] QByteArray expectedSha256(const QString& id) const;
    [[nodiscard]] QList<CustomModelInfo> customModels() const;
    [[nodiscard]] bool verify(const QString& id, QString* error = nullptr) const;
    [[nodiscard]] bool importCustomModel(const QString& sourcePath, const QString& displayName,
                                         QString* modelId, QString* error = nullptr);
    [[nodiscard]] bool removeModel(const QString& id, QString* error = nullptr);
    void setModelInUse(const QString& id, bool inUse);

    Q_INVOKABLE ModelDownloadOperation* download(const QString& id);

  signals:
    void defaultModelIdChanged();
    void modelsChanged();

  private:
    void discoverCustomModels();

    ModelManifest m_manifest;
    QNetworkAccessManager* m_network = nullptr;
    QHash<QString, QPointer<ModelDownloadOperation>> m_downloads;
    QSet<QString> m_modelsInUse;
    QHash<QString, QString> m_customModels;
    QHash<QString, QString> m_customModelNames;
    QHash<QString, QByteArray> m_customModelSha256;
    QString m_defaultModelId = QStringLiteral("breeze-asr-25-q5");
};

} // namespace BreezeDesk
