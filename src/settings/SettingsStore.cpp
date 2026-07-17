#include "breezedesk/settings/SettingsStore.h"

#include "breezedesk/app_config.h"

#include <QMutexLocker>

namespace BreezeDesk {

SettingsStore::SettingsStore(const QString& iniFilePath)
    : m_settings(std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat)) {}

SettingsStore::SettingsStore(const QSettings::Scope scope, const QString& organization,
                             const QString& application)
    : m_settings(std::make_unique<QSettings>(QSettings::NativeFormat, scope, organization, application)) {}

SettingsStore::~SettingsStore() = default;

QVariant SettingsStore::value(const QString& key, const QVariant& defaultValue) const {
    QMutexLocker locker(&m_mutex);
    return m_settings->value(key, defaultValue);
}

bool SettingsStore::contains(const QString& key) const {
    QMutexLocker locker(&m_mutex);
    return m_settings->contains(key);
}

void SettingsStore::setValue(const QString& key, const QVariant& value) {
    QMutexLocker locker(&m_mutex);
    m_settings->setValue(key, value);
}

void SettingsStore::remove(const QString& key) {
    QMutexLocker locker(&m_mutex);
    m_settings->remove(key);
}

Result<void> SettingsStore::sync() {
    QMutexLocker locker(&m_mutex);
    m_settings->sync();
    if (m_settings->status() == QSettings::NoError)
        return Result<void>::success();
    const ErrorCode code = m_settings->status() == QSettings::AccessError ? ErrorCode::SettingsWriteFailed
                                                                          : ErrorCode::SerializationFailed;
    const QString settingsFile = m_settings->fileName();
    return Result<void>::failure(
        {ErrorDomain::Settings,
         code,
         QStringLiteral("Settings could not be saved"),
         QStringLiteral("The application could not persist one or more preferences."),
         QStringLiteral("Check the settings file permissions and try again."),
         settingsFile,
         true,
         {}});
}

QString SettingsStore::fileName() const {
    QMutexLocker locker(&m_mutex);
    return m_settings->fileName();
}

Result<void> SettingsMigrationService::migrate(SettingsStore& store) {
    int version = store.value(QStringLiteral("settings/schemaVersion"), 0).toInt();
    if (version > CurrentSchemaVersion)
        return Result<void>::failure(
            UserFacingError::validation(ErrorCode::SerializationFailed,
                                        QStringLiteral("These settings were created by a newer %1 version.")
                                            .arg(QString::fromLatin1(AppConfig::ProductName))));
    if (version < 1) {
        if (store.contains(QStringLiteral("appearance/darkMode"))) {
            store.setValue(QStringLiteral("appearance/theme"),
                           store.value(QStringLiteral("appearance/darkMode")).toBool()
                               ? QStringLiteral("dark")
                               : QStringLiteral("light"));
            store.remove(QStringLiteral("appearance/darkMode"));
        }
        version = 1;
        store.setValue(QStringLiteral("settings/schemaVersion"), version);
    }
    if (version < 2) {
        if (store.contains(QStringLiteral("transcription/useGpu"))) {
            store.setValue(QStringLiteral("transcription/backend"),
                           store.value(QStringLiteral("transcription/useGpu")).toBool()
                               ? QStringLiteral("auto")
                               : QStringLiteral("cpu"));
            store.remove(QStringLiteral("transcription/useGpu"));
        }
        version = 2;
        store.setValue(QStringLiteral("settings/schemaVersion"), version);
    }
    return store.sync();
}

} // namespace BreezeDesk
