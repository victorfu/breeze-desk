#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

class QMediaDevices;

namespace BreezeDesk {

class AppearanceSettingsManager;
class AudioSettingsManager;
class GeneralSettingsManager;
class ModelSettingsManager;
class SettingsManagerBase;
class StorageSettingsManager;
class TranscriptionSettingsManager;
class UpdateSettingsManager;

struct SettingsManagerDependencies {
    GeneralSettingsManager* general{nullptr};
    AppearanceSettingsManager* appearance{nullptr};
    TranscriptionSettingsManager* transcription{nullptr};
    AudioSettingsManager* audio{nullptr};
    ModelSettingsManager* models{nullptr};
    StorageSettingsManager* storage{nullptr};
    UpdateSettingsManager* updates{nullptr};
};

class SettingsViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString closeBehavior READ closeBehavior WRITE setCloseBehavior NOTIFY generalChanged)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY generalChanged)
    Q_PROPERTY(bool launchAtStartup READ launchAtStartup WRITE setLaunchAtStartup NOTIFY generalChanged)
    Q_PROPERTY(QString importBehavior READ importBehavior WRITE setImportBehavior NOTIFY generalChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY appearanceChanged)
    Q_PROPERTY(bool compactMode READ compactMode WRITE setCompactMode NOTIFY appearanceChanged)
    Q_PROPERTY(QString waveformDensity READ waveformDensity WRITE setWaveformDensity NOTIFY appearanceChanged)
    Q_PROPERTY(QString defaultModel READ defaultModel WRITE setDefaultModel NOTIFY transcriptionChanged)
    Q_PROPERTY(QString transcriptionLanguage READ transcriptionLanguage WRITE setTranscriptionLanguage NOTIFY
                   transcriptionChanged)
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY transcriptionChanged)
    Q_PROPERTY(bool vadEnabled READ vadEnabled WRITE setVadEnabled NOTIFY transcriptionChanged)
    Q_PROPERTY(QString initialPromptBehavior READ initialPromptBehavior WRITE setInitialPromptBehavior NOTIFY
                   transcriptionChanged)
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY transcriptionChanged)
    Q_PROPERTY(bool flashAttention READ flashAttention WRITE setFlashAttention NOTIFY transcriptionChanged)
    Q_PROPERTY(bool tokenTimestamps READ tokenTimestamps WRITE setTokenTimestamps NOTIFY transcriptionChanged)
    Q_PROPERTY(int threadCount READ threadCount WRITE setThreadCount NOTIFY transcriptionChanged)
    Q_PROPERTY(qreal lowConfidenceThreshold READ lowConfidenceThreshold WRITE setLowConfidenceThreshold NOTIFY
                   transcriptionChanged)
    Q_PROPERTY(QString microphoneDevice READ microphoneDevice WRITE setMicrophoneDevice NOTIFY audioChanged)
    Q_PROPERTY(QString playbackDevice READ playbackDevice WRITE setPlaybackDevice NOTIFY audioChanged)
    Q_PROPERTY(QVariantList microphoneDevices READ microphoneDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(QVariantList playbackDevices READ playbackDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(QString recordingFormat READ recordingFormat WRITE setRecordingFormat NOTIFY audioChanged)
    Q_PROPERTY(bool autoTranscribeRecording READ autoTranscribeRecording WRITE setAutoTranscribeRecording
                   NOTIFY audioChanged)
    Q_PROPERTY(
        QString managedMediaPolicy READ managedMediaPolicy WRITE setManagedMediaPolicy NOTIFY storageChanged)
    Q_PROPERTY(QString storagePath READ storagePath NOTIFY storageChanged)
    Q_PROPERTY(QString exportPath READ exportPath NOTIFY storageChanged)
    Q_PROPERTY(bool automaticUpdates READ automaticUpdates WRITE setAutomaticUpdates NOTIFY updatesChanged)
    Q_PROPERTY(QString updateChannel READ updateChannel WRITE setUpdateChannel NOTIFY updatesChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

  public:
    explicit SettingsViewModel(QObject* parent = nullptr);

    // Managers remain owned by the application composition root. Passing an
    // empty bundle keeps the isolated QML smoke-test behavior.
    void installManagers(SettingsManagerDependencies dependencies);

    [[nodiscard]] QString language() const;
    [[nodiscard]] QString theme() const;
    [[nodiscard]] QString closeBehavior() const;
    [[nodiscard]] bool minimizeToTray() const noexcept;
    [[nodiscard]] bool launchAtStartup() const noexcept;
    [[nodiscard]] QString importBehavior() const;
    [[nodiscard]] qreal textScale() const noexcept;
    [[nodiscard]] bool compactMode() const noexcept;
    [[nodiscard]] QString waveformDensity() const;
    [[nodiscard]] QString defaultModel() const;
    [[nodiscard]] QString transcriptionLanguage() const;
    [[nodiscard]] QString preset() const;
    [[nodiscard]] bool vadEnabled() const noexcept;
    [[nodiscard]] QString initialPromptBehavior() const;
    [[nodiscard]] QString backend() const;
    [[nodiscard]] bool flashAttention() const noexcept;
    [[nodiscard]] bool tokenTimestamps() const noexcept;
    [[nodiscard]] int threadCount() const noexcept;
    [[nodiscard]] qreal lowConfidenceThreshold() const noexcept;
    [[nodiscard]] QString microphoneDevice() const;
    [[nodiscard]] QString playbackDevice() const;
    [[nodiscard]] QVariantList microphoneDevices() const;
    [[nodiscard]] QVariantList playbackDevices() const;
    [[nodiscard]] QString recordingFormat() const;
    [[nodiscard]] bool autoTranscribeRecording() const noexcept;
    [[nodiscard]] QString managedMediaPolicy() const;
    [[nodiscard]] QString storagePath() const;
    [[nodiscard]] QString exportPath() const;
    [[nodiscard]] bool automaticUpdates() const noexcept;
    [[nodiscard]] QString updateChannel() const;
    [[nodiscard]] QString appVersion() const;

    Q_INVOKABLE void chooseStoragePath();
    Q_INVOKABLE void chooseExportPath();
    Q_INVOKABLE void clearCache();
    Q_INVOKABLE void backupDatabase();
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void setStoragePath(const QString& path);
    Q_INVOKABLE void setExportPath(const QString& path);
    Q_INVOKABLE void setStorageFolder(const QUrl& folder);
    Q_INVOKABLE void setExportFolder(const QUrl& folder);

  public slots:
    void setLanguage(const QString& value);
    void setTheme(const QString& value);
    void setCloseBehavior(const QString& value);
    void setMinimizeToTray(bool value);
    void setLaunchAtStartup(bool value);
    void setImportBehavior(const QString& value);
    void setTextScale(qreal value);
    void setCompactMode(bool value);
    void setWaveformDensity(const QString& value);
    void setDefaultModel(const QString& value);
    void setTranscriptionLanguage(const QString& value);
    void setPreset(const QString& value);
    void setVadEnabled(bool value);
    void setInitialPromptBehavior(const QString& value);
    void setBackend(const QString& value);
    void setFlashAttention(bool value);
    void setTokenTimestamps(bool value);
    void setThreadCount(int value);
    void setLowConfidenceThreshold(qreal value);
    void setMicrophoneDevice(const QString& value);
    void setPlaybackDevice(const QString& value);
    void setRecordingFormat(const QString& value);
    void setAutoTranscribeRecording(bool value);
    void setManagedMediaPolicy(const QString& value);
    void setAutomaticUpdates(bool value);
    void setUpdateChannel(const QString& value);

  signals:
    void languageChanged();
    void themeChanged();
    void generalChanged();
    void appearanceChanged();
    void transcriptionChanged();
    void audioChanged();
    void audioDevicesChanged();
    void storageChanged();
    void updatesChanged();
    void storagePathRequested();
    void exportPathRequested();
    void clearCacheRequested();
    void backupDatabaseRequested();
    void updateCheckRequested();
    void persistenceError(const QString& message);

  private:
    void loadFromManagers();
    void syncManager(SettingsManagerBase* manager);

    SettingsManagerDependencies m_managers;
    QString m_language{"zh_TW"};
    QString m_theme{"System"};
    QString m_closeBehavior{"MinimizeToTray"};
    bool m_minimizeToTray{true};
    bool m_launchAtStartup{false};
    QString m_importBehavior{"ReferenceOriginal"};
    qreal m_textScale{1.0};
    bool m_compactMode{false};
    QString m_waveformDensity{"Balanced"};
    QString m_defaultModel{"breeze-asr-25-q5"};
    QString m_transcriptionLanguage{"zh"};
    QString m_preset{"Balanced"};
    bool m_vadEnabled{true};
    QString m_initialPromptBehavior{"GlossaryAndContext"};
    QString m_backend{"Auto"};
    bool m_flashAttention{true};
    bool m_tokenTimestamps{true};
    int m_threadCount{0};
    qreal m_lowConfidenceThreshold{0.35};
    QString m_microphoneDevice{"Default"};
    QString m_playbackDevice{"Default"};
    QString m_recordingFormat{"PCM WAV"};
    bool m_autoTranscribeRecording{false};
    QString m_managedMediaPolicy{"ReferenceOriginal"};
    bool m_automaticUpdates{false};
    QString m_updateChannel{"Stable"};
    QMediaDevices* m_mediaDevices{nullptr};
};

} // namespace BreezeDesk
