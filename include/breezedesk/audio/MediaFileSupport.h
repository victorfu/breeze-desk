#pragma once

#include <QString>
#include <QStringList>

namespace BreezeDesk {

class MediaFileSupport final {
  public:
    [[nodiscard]] static QStringList supportedExtensions();
    [[nodiscard]] static bool isSupportedPath(const QString& path);
};

} // namespace BreezeDesk
