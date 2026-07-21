#include "breezedesk/ui/BrandIcons.h"

#include <QString>

namespace BreezeDesk {

QList<QSize> nativeBrandIconSizes() {
    return {{16, 16}, {20, 20}, {24, 24}, {28, 28}, {32, 32},   {40, 40},  {48, 48},
            {56, 56}, {64, 64}, {80, 80}, {96, 96}, {128, 128}, {256, 256}};
}

QIcon brandIcon() {
    return QIcon(QStringLiteral(":/qt/qml/BreezeDesk/icons/breezedesk-sidebar.png"));
}

} // namespace BreezeDesk
