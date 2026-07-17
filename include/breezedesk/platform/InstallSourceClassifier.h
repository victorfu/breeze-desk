#pragma once

#include <QString>

namespace BreezeDesk {

[[nodiscard]] QString classifyWindowsInstallSource(const QString& applicationDirectory,
                                                   const QString& registeredInstallDirectory);

} // namespace BreezeDesk
