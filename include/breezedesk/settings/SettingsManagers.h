#pragma once

#include "breezedesk/settings/SettingsStore.h"

#include <QByteArray>
#include <QDateTime>
#include <QSize>
#include <QStringList>

namespace BreezeDesk {

enum class ThemeMode { System, Light, Dark };
enum class CloseBehavior { CloseWindow, MinimizeToTray, Quit };
enum class BackendPreference { Automatic, Cpu, Metal, Vulkan, Cuda };
enum class ManagedMediaPolicy { ReferenceOriginal, CopyIntoLibrary };
enum class UpdateChannel { Stable, Beta };

[[nodiscard]] QString themeModeName(ThemeMode value);
[[nodiscard]] QString closeBehaviorName(CloseBehavior value);
[[nodiscard]] QString backendPreferenceName(BackendPreference value);
[[nodiscard]] QString managedMediaPolicyName(ManagedMediaPolicy value);
[[nodiscard]] QString updateChannelName(UpdateChannel value);

class SettingsManagerBase {
  public:
    virtual ~SettingsManagerBase() = default;
    [[nodiscard]] Result<void> sync() { return m_store.sync(); }

  protected:
    SettingsManagerBase(SettingsStore& store, QString prefix);
    [[nodiscard]] QVariant read(const QString& key, const QVariant& fallback) const;
    void write(const QString& key, const QVariant& value);

    SettingsStore& m_store;
    QString m_prefix;
};

class GeneralSettingsManager final : public SettingsManagerBase {
  public:
    explicit GeneralSettingsManager(SettingsStore& store);
    [[nodiscard]] QString language() const;
    void setLanguage(const QString& value);
    [[nodiscard]] bool launchAtStartup() const;
    void setLaunchAtStartup(bool value);
    [[nodiscard]] CloseBehavior closeBehavior() const;
    void setCloseBehavior(CloseBehavior value);
    [[nodiscard]] bool minimizeToTray() const;
    void setMinimizeToTray(bool value);
    [[nodiscard]] bool startTranscriptionAfterImport() const;
    void setStartTranscriptionAfterImport(bool value);
};

class AppearanceSettingsManager final : public SettingsManagerBase {
  public:
    explicit AppearanceSettingsManager(SettingsStore& store);
    [[nodiscard]] ThemeMode theme() const;
    void setTheme(ThemeMode value);
    [[nodiscard]] double textScale() const;
    void setTextScale(double value);
    [[nodiscard]] bool compactMode() const;
    void setCompactMode(bool value);
    [[nodiscard]] int waveformDensity() const;
    void setWaveformDensity(int value);
};

class TranscriptionSettingsManager final : public SettingsManagerBase {
  public:
    explicit TranscriptionSettingsManager(SettingsStore& store);
    [[nodiscard]] QString defaultModelId() const;
    void setDefaultModelId(const QString& value);
    [[nodiscard]] QString language() const;
    void setLanguage(const QString& value);
    [[nodiscard]] QString preset() const;
    void setPreset(const QString& value);
    [[nodiscard]] bool vadEnabled() const;
    void setVadEnabled(bool value);
    [[nodiscard]] QString initialPromptBehavior() const;
    void setInitialPromptBehavior(const QString& value);
    [[nodiscard]] QString glossaryProfileId() const;
    void setGlossaryProfileId(const QString& value);
    [[nodiscard]] int threadCount() const;
    void setThreadCount(int value);
    [[nodiscard]] BackendPreference backend() const;
    void setBackend(BackendPreference value);
    [[nodiscard]] bool flashAttention() const;
    void setFlashAttention(bool value);
    [[nodiscard]] bool tokenTimestamps() const;
    void setTokenTimestamps(bool value);
    [[nodiscard]] double lowConfidenceThreshold() const;
    void setLowConfidenceThreshold(double value);
};

class AudioSettingsManager final : public SettingsManagerBase {
  public:
    explicit AudioSettingsManager(SettingsStore& store);
    [[nodiscard]] QString inputDeviceId() const;
    void setInputDeviceId(const QString& value);
    [[nodiscard]] QString outputDeviceId() const;
    void setOutputDeviceId(const QString& value);
    [[nodiscard]] QString recordingFormat() const;
    void setRecordingFormat(const QString& value);
    [[nodiscard]] bool autoTranscribeRecording() const;
    void setAutoTranscribeRecording(bool value);
};

class ModelSettingsManager final : public SettingsManagerBase {
  public:
    explicit ModelSettingsManager(SettingsStore& store);
    [[nodiscard]] QString defaultModelId() const;
    void setDefaultModelId(const QString& value);
    [[nodiscard]] QString customModelDirectory() const;
    void setCustomModelDirectory(const QString& value);
    [[nodiscard]] int workerIdleTimeoutSeconds() const;
    void setWorkerIdleTimeoutSeconds(int value);
};

class StorageSettingsManager final : public SettingsManagerBase {
  public:
    explicit StorageSettingsManager(SettingsStore& store);
    [[nodiscard]] QString dataDirectoryOverride() const;
    void setDataDirectoryOverride(const QString& value);
    [[nodiscard]] QString cacheDirectoryOverride() const;
    void setCacheDirectoryOverride(const QString& value);
    [[nodiscard]] QString exportDirectory() const;
    void setExportDirectory(const QString& value);
    [[nodiscard]] qint64 cacheLimitBytes() const;
    void setCacheLimitBytes(qint64 value);
    [[nodiscard]] ManagedMediaPolicy managedMediaPolicy() const;
    void setManagedMediaPolicy(ManagedMediaPolicy value);
    [[nodiscard]] bool automaticDatabaseBackup() const;
    void setAutomaticDatabaseBackup(bool value);
};

class UpdateSettingsManager final : public SettingsManagerBase {
  public:
    explicit UpdateSettingsManager(SettingsStore& store);
    [[nodiscard]] bool automaticCheck() const;
    void setAutomaticCheck(bool value);
    [[nodiscard]] UpdateChannel channel() const;
    void setChannel(UpdateChannel value);
    [[nodiscard]] QDateTime lastCheck() const;
    void setLastCheck(const QDateTime& value);
};

class PrivacySettingsManager final : public SettingsManagerBase {
  public:
    explicit PrivacySettingsManager(SettingsStore& store);
    [[nodiscard]] bool redactPathsInLogs() const;
    void setRedactPathsInLogs(bool value);
    [[nodiscard]] bool includePathsInDiagnostics() const;
    void setIncludePathsInDiagnostics(bool value);
};

class WindowSettingsManager final : public SettingsManagerBase {
  public:
    explicit WindowSettingsManager(SettingsStore& store);
    [[nodiscard]] QByteArray geometry() const;
    void setGeometry(const QByteArray& value);
    [[nodiscard]] QByteArray state() const;
    void setState(const QByteArray& value);
    [[nodiscard]] bool inspectorVisible() const;
    void setInspectorVisible(bool value);
    [[nodiscard]] bool autoScrollTranscript() const;
    void setAutoScrollTranscript(bool value);
};

} // namespace BreezeDesk
