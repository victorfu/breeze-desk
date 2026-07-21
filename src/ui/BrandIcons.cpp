#include "breezedesk/ui/BrandIcons.h"

#include <QString>

namespace BreezeDesk {

QList<QSize> nativeBrandIconSizes() {
    return {{16, 16}, {20, 20}, {24, 24}, {28, 28}, {30, 30},   {32, 32},  {36, 36},
            {40, 40}, {48, 48}, {56, 56}, {60, 60}, {64, 64},   {72, 72},  {80, 80},
            {96, 96}, {128, 128}, {256, 256}};
}

QIcon brandIcon() {
#ifdef Q_OS_WIN
    return QIcon(QStringLiteral(":/qt/qml/BreezeDesk/icons/breezedesk.ico"));
#else
    return QIcon(QStringLiteral(":/qt/qml/BreezeDesk/icons/breezedesk.png"));
#endif
}

QIcon windowsTrayIcon() {
    return QIcon(QStringLiteral(":/qt/qml/BreezeDesk/icons/breezedesk-tray.png"));
}

QIcon macMenuBarIcon() {
    QIcon icon(QStringLiteral(":/qt/qml/BreezeDesk/icons/breezedesk-menubar-Template.png"));
    icon.setIsMask(true);
    return icon;
}

} // namespace BreezeDesk
