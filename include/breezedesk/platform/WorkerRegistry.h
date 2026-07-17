#pragma once

#include <QStringList>

namespace BreezeDesk {

enum class WorkerHostPlatform {
    Windows,
    MacOS,
    Unix,
};

class WorkerRegistry final {
  public:
    [[nodiscard]] static QStringList executableCandidates(const QString& applicationDirectory,
                                                          const QString& preferredBackend,
                                                          const QString& overridePath = {},
                                                          const QString& developmentPath = {});
    [[nodiscard]] static QStringList executableCandidatesForHost(WorkerHostPlatform hostPlatform,
                                                                 const QString& applicationDirectory,
                                                                 const QString& preferredBackend,
                                                                 const QString& overridePath = {},
                                                                 const QString& developmentPath = {});
};

} // namespace BreezeDesk
