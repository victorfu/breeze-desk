#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace BreezeDesk {

struct InstalledModelRecord {
    QString id;
    QString path;
    QString checksum;
    QString quantization;
    QDateTime installedAt;
    QDateTime lastUsedAt;
    bool verified = false;
};

class IModelRepository {
  public:
    virtual ~IModelRepository() = default;
    [[nodiscard]] virtual QVector<InstalledModelRecord> installedModels(QString* error = nullptr) const = 0;
    [[nodiscard]] virtual bool saveInstalledModel(const InstalledModelRecord& model,
                                                  QString* error = nullptr) = 0;
    [[nodiscard]] virtual bool removeInstalledModel(const QString& id, QString* error = nullptr) = 0;
};

} // namespace BreezeDesk
