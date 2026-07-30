#pragma once

#include <QString>

#include <atomic>

namespace BreezeDesk {

class FileHash final {
  public:
    [[nodiscard]] static QString sha256(const QString& path, QString* error = nullptr,
                                        const std::atomic_bool* cancellation = nullptr);
};

} // namespace BreezeDesk
