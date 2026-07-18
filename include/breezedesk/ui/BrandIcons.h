#pragma once

#include <QIcon>
#include <QList>
#include <QSize>

namespace BreezeDesk {

[[nodiscard]] QList<QSize> nativeBrandIconSizes();
[[nodiscard]] QIcon brandSymbolIcon();
[[nodiscard]] QIcon windowsTrayIcon();

} // namespace BreezeDesk
