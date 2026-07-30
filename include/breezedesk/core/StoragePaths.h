#pragma once

#include <QString>

namespace BreezeDesk {

struct StorageLayoutInitializationResult final {
    bool succeeded{false};
    bool recoveredFromLegacyOverride{false};
    QString error;
};

class StoragePaths final {
  public:
    [[nodiscard]] static QString root();
    [[nodiscard]] static QString models();
    [[nodiscard]] static QString cache();
    [[nodiscard]] static QString logs();
    [[nodiscard]] static QString exports();
    [[nodiscard]] static QString recordings();
    [[nodiscard]] static QString database();
    [[nodiscard]] static QString databaseFile();
    [[nodiscard]] static QString temporary();
    [[nodiscard]] static StorageLayoutInitializationResult
    initializeLayout(const QString& legacyConfiguredRoot = {});
    [[nodiscard]] static bool ensureLayout(QString* error = nullptr);

  private:
    [[nodiscard]] static QString child(const QString& name);
};

} // namespace BreezeDesk
