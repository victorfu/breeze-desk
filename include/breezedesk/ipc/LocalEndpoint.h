#pragma once

#include <QtCore/QString>

namespace BreezeDesk::Ipc {

class LocalEndpoint final {
  public:
    [[nodiscard]] static QString userScopedName(const QString& applicationId,
                                                const QString& channel = QStringLiteral("worker"));
};

} // namespace BreezeDesk::Ipc
