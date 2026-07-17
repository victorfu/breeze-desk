#pragma once

#include <QString>

namespace BreezeDesk {

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
    [[nodiscard]] static bool ensureLayout(QString* error = nullptr);

  private:
    [[nodiscard]] static QString child(const QString& name);
};

} // namespace BreezeDesk
