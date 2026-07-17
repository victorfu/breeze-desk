#include <breezedesk/ipc/LocalEndpoint.h>

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QStandardPaths>

namespace BreezeDesk::Ipc {

QString LocalEndpoint::userScopedName(const QString& applicationId, const QString& channel) {
    QString scope = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (scope.isEmpty()) {
        scope = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    scope = QDir::cleanPath(scope);
    const QByteArray digest =
        QCryptographicHash::hash(
            (scope + QLatin1Char('|') + applicationId + QLatin1Char('|') + channel).toUtf8(),
            QCryptographicHash::Sha256)
            .toHex()
            .left(24);
    // Unix-domain socket paths are commonly limited to roughly 100 bytes.
    // Keep the visible prefix fixed and place all identity in the digest.
    return QStringLiteral("breezedesk-%1").arg(QString::fromLatin1(digest));
}

} // namespace BreezeDesk::Ipc
