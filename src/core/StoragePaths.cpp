#include "breezedesk/core/StoragePaths.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryFile>

namespace BreezeDesk {
namespace {

constexpr auto DataRootEnvironment = "BREEZEDESK_DATA_ROOT";

bool restoreDataRootEnvironment(const bool wasSet, const QByteArray& value) {
    return wasSet ? qputenv(DataRootEnvironment, value) : qunsetenv(DataRootEnvironment);
}

} // namespace

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

StorageLayoutInitializationResult StoragePaths::initializeLayout(const QString& legacyConfiguredRoot) {
    StorageLayoutInitializationResult result;
    const bool inheritedRootWasSet = qEnvironmentVariableIsSet(DataRootEnvironment);
    const QByteArray inheritedRoot = qgetenv(DataRootEnvironment);
    if (!qEnvironmentVariable(DataRootEnvironment).trimmed().isEmpty()) {
        result.succeeded = ensureLayout(&result.error);
        return result;
    }

    const QString requestedRoot = legacyConfiguredRoot.trimmed();
    if (requestedRoot.isEmpty()) {
        result.succeeded = ensureLayout(&result.error);
        return result;
    }

    QString configuredError;
    if (qputenv(DataRootEnvironment, requestedRoot.toUtf8()) && ensureLayout(&configuredError)) {
        result.succeeded = true;
        return result;
    }
    if (configuredError.isEmpty()) {
        configuredError = QStringLiteral("Unable to activate the configured application data directory.");
    }

    if (!restoreDataRootEnvironment(inheritedRootWasSet, inheritedRoot)) {
        result.error = QStringLiteral(
                           "The configured application data directory failed (%1), and the default "
                           "data-root environment could not be restored.")
                           .arg(configuredError);
        return result;
    }

    QString fallbackError;
    if (ensureLayout(&fallbackError)) {
        result.succeeded = true;
        result.recoveredFromLegacyOverride = true;
        return result;
    }

    result.error =
        QStringLiteral("The configured application data directory failed (%1), and the default "
                       "data directory also failed (%2).")
            .arg(configuredError, fallbackError);
    return result;
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

        QTemporaryFile probe(
            QDir(directory).filePath(QStringLiteral(".breezedesk-write-probe-XXXXXX")));
        if (!probe.open() || probe.write("ok", 2) != 2 || !probe.flush()) {
            if (error != nullptr) {
                *error = QStringLiteral("Application data directory is not writable: %1").arg(directory);
            }
            return false;
        }
        probe.close();
        if (!probe.remove()) {
            if (error != nullptr) {
                *error =
                    QStringLiteral("Application data write probe could not be removed: %1").arg(directory);
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
