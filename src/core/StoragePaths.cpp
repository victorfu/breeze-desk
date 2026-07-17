#include "breezedesk/core/StoragePaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace BreezeDesk {

QString StoragePaths::root() {
    const QString overridePath = qEnvironmentVariable("BREEZEDESK_DATA_ROOT").trimmed();
    if (!overridePath.isEmpty()) {
        return QFileInfo(overridePath).absoluteFilePath();
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString StoragePaths::models() {
    return child(QStringLiteral("models"));
}

QString StoragePaths::cache() {
    return child(QStringLiteral("cache"));
}

QString StoragePaths::logs() {
    return child(QStringLiteral("logs"));
}

QString StoragePaths::exports() {
    return child(QStringLiteral("exports"));
}

QString StoragePaths::recordings() {
    return child(QStringLiteral("recordings"));
}

QString StoragePaths::database() {
    return child(QStringLiteral("database"));
}

QString StoragePaths::databaseFile() {
    return QDir(database()).filePath(QStringLiteral("breezedesk.sqlite3"));
}

QString StoragePaths::temporary() {
    return child(QStringLiteral("temp"));
}

bool StoragePaths::ensureLayout(QString* error) {
    const QStringList directories{root(),    models(),     cache(),    logs(),
                                  exports(), recordings(), database(), temporary()};
    for (const QString& directory : directories) {
        if (!QDir().mkpath(directory)) {
            if (error != nullptr) {
                *error = QStringLiteral("Unable to create application data directory: %1").arg(directory);
            }
            return false;
        }
    }
    return true;
}

QString StoragePaths::child(const QString& name) {
    return QDir(root()).filePath(name);
}

} // namespace BreezeDesk
