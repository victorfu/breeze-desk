#pragma once

#include "breezedesk/core/Result.h"

#include <QMutex>
#include <QSettings>
#include <QVariant>

#include <memory>

namespace BreezeDesk {

class SettingsStore final {
  public:
    explicit SettingsStore(const QString& iniFilePath);
    SettingsStore(QSettings::Scope scope, const QString& organization, const QString& application);
    ~SettingsStore();

    SettingsStore(const SettingsStore&) = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;

    [[nodiscard]] QVariant value(const QString& key, const QVariant& defaultValue = {}) const;
    [[nodiscard]] bool contains(const QString& key) const;
    void setValue(const QString& key, const QVariant& value);
    void remove(const QString& key);
    [[nodiscard]] Result<void> sync();
    [[nodiscard]] QString fileName() const;

  private:
    mutable QMutex m_mutex;
    std::unique_ptr<QSettings> m_settings;
};

class SettingsMigrationService final {
  public:
    static constexpr int CurrentSchemaVersion = 2;
    [[nodiscard]] static Result<void> migrate(SettingsStore& store);
};

} // namespace BreezeDesk
