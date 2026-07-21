#pragma once

#include <QIcon>
#include <QList>
#include <QSize>

namespace BreezeDesk {

[[nodiscard]] QList<QSize> nativeBrandIconSizes();
[[nodiscard]] QIcon brandIcon();
[[nodiscard]] QIcon windowsTrayIcon();
[[nodiscard]] QIcon macMenuBarIcon();

} // namespace BreezeDesk
