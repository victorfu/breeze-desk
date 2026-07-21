#pragma once

#include <QIcon>
#include <QList>
#include <QSize>

namespace BreezeDesk {

[[nodiscard]] QList<QSize> nativeBrandIconSizes();
[[nodiscard]] QIcon brandIcon();

} // namespace BreezeDesk
