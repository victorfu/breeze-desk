#pragma once

#include <QString>

namespace BreezeDesk {

class FileHash final {
  public:
    [[nodiscard]] static QString sha256(const QString& path, QString* error = nullptr);
};

} // namespace BreezeDesk
