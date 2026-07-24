#include "breezedesk/ui/SettingsViewModel.h"

#include "breezedesk/platform/PlatformCapabilities.h"
#include "breezedesk/settings/SettingsManagers.h"

#include <QAudioDevice>
#include <QCoreApplication>
#include <QMediaDevices>
#include <QStandardPaths>
#include <QThread>

namespace BreezeDesk {
namespace {

QString canonicalModelId(const QString& id) {
    if (id == QLatin1String("breeze-q5")) {
        return QStringLiteral("breeze-asr-25-q5");
    }
    if (id == QLatin1String("breeze-q8")) {
        return QStringLiteral("breeze-asr-25-q8");
    }
    return id;
}

QString uiTheme(ThemeMode value) {
    switch (value) {
    case ThemeMode::System:
        return QStringLiteral("System");
    case ThemeMode::Light:
        return QStringLiteral("Light");
    case ThemeMode::Dark:
        return QStringLiteral("Dark");
    }
    return QStringLiteral("System");
}

ThemeMode storedTheme(const QString& value) {
    if (value == QLatin1String("Light")) {
        return ThemeMode::Light;
    }
    if (value == QLatin1String("Dark")) {
        return ThemeMode::Dark;
    }
    return ThemeMode::System;
}

QString uiCloseBehavior(CloseBehavior value) {
    switch (value) {
    case CloseBehavior::CloseWindow:
        return QStringLiteral("CloseWindow");
    case CloseBehavior::MinimizeToTray:
        return QStringLiteral("MinimizeToTray");
    case CloseBehavior::Quit:
        return QStringLiteral("Quit");
    }
    return QStringLiteral("MinimizeToTray");
}

CloseBehavior storedCloseBehavior(const QString& value) {
    if (value == QLatin1String("CloseWindow")) {
        return CloseBehavior::CloseWindow;
    }
    if (value == QLatin1String("Quit")) {
        return CloseBehavior::Quit;
    }
    return CloseBehavior::MinimizeToTray;
}

QString uiBackend(BackendPreference value) {
    switch (value) {
    case BackendPreference::Automatic:
        return QStringLiteral("Auto");
    case BackendPreference::Cpu:
        return QStringLiteral("CPU");
    case BackendPreference::Metal:
        return QStringLiteral("Metal");
    case BackendPreference::Vulkan:
        return QStringLiteral("Vulkan");
    }
    return QStringLiteral("Auto");
}

BackendPreference storedBackend(const QString& value) {
    if (value == QLatin1String("CPU")) {
        return BackendPreference::Cpu;
    }
    if (value == QLatin1String("Metal")) {
        return BackendPreference::Metal;
    }
    if (value == QLatin1String("Vulkan")) {
        return BackendPreference::Vulkan;
    }
    return BackendPreference::Automatic;
}

QString uiManagedMediaPolicy(ManagedMediaPolicy value) {
    return value == ManagedMediaPolicy::CopyIntoLibrary ? QStringLiteral("CopyManaged")
                                                        : QStringLiteral("ReferenceOriginal");
}

ManagedMediaPolicy storedManagedMediaPolicy(const QString& value) {
    return value == QLatin1String("CopyManaged") ? ManagedMediaPolicy::CopyIntoLibrary
                                                 : ManagedMediaPolicy::ReferenceOriginal;
}

QString uiUpdateChannel(UpdateChannel value) {
    return value == UpdateChannel::Beta ? QStringLiteral("Beta") : QStringLiteral("Stable");
}

UpdateChannel storedUpdateChannel(const QString& value) {
    return value == QLatin1String("Beta") ? UpdateChannel::Beta : UpdateChannel::Stable;
}

QString uiWaveformDensity(int value) {
    if (value <= 1) {
        return QStringLiteral("Sparse");
    }
    if (value >= 3) {
        return QStringLiteral("Dense");
    }
    return QStringLiteral("Balanced");
}

int storedWaveformDensity(const QString& value) {
    if (value == QLatin1String("Sparse")) {
        return 1;
    }
    if (value == QLatin1String("Dense")) {
        return 3;
    }
    return 2;
}

QString uiPreset(QString value) {
    if (value == QLatin1String("fast")) {
        return QStringLiteral("Fast");
    }
    if (value == QLatin1String("accurate")) {
        return QStringLiteral("Accurate");
    }
    return QStringLiteral("Balanced");
}

QString storedPreset(const QString& value) {
    return value.toLower();
}

} // namespace

SettingsViewModel::SettingsViewModel(QObject* parent)
    : QObject(parent), m_threadCount(qMax(1, QThread::idealThreadCount())),
      m_mediaDevices(new QMediaDevices(this)) {
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, this,
            &SettingsViewModel::audioDevicesChanged);
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this,
            &SettingsViewModel::audioDevicesChanged);
}

void SettingsViewModel::installManagers(SettingsManagerDependencies dependencies) {
    m_managers = dependencies;
    loadFromManagers();
}

QString SettingsViewModel::language() const {
    return m_language;
}

QString SettingsViewModel::theme() const {
    return m_theme;
}

QString SettingsViewModel::closeBehavior() const {
    return m_closeBehavior;
}

bool SettingsViewModel::minimizeToTray() const noexcept {
    return m_minimizeToTray;
}

bool SettingsViewModel::launchAtStartup() const noexcept {
    return m_launchAtStartup;
}

QString SettingsViewModel::importBehavior() const {
    return m_importBehavior;
}

qreal SettingsViewModel::textScale() const noexcept {
    return m_textScale;
}

bool SettingsViewModel::compactMode() const noexcept {
    return m_compactMode;
}

QString SettingsViewModel::waveformDensity() const {
    return m_waveformDensity;
}

QString SettingsViewModel::defaultModel() const {
    return m_defaultModel;
}

QString SettingsViewModel::transcriptionLanguage() const {
    return m_transcriptionLanguage;
}

QString SettingsViewModel::preset() const {
    return m_preset;
}

bool SettingsViewModel::vadEnabled() const noexcept {
    return m_vadEnabled;
}

QString SettingsViewModel::initialPromptBehavior() const {
    return m_initialPromptBehavior;
}

QString SettingsViewModel::backend() const {
    return m_backend;
}

QStringList SettingsViewModel::availableBackends() const {
    // The compiled worker only ships accelerators the platform can host, so the
    // picker must not offer Metal on Windows or Vulkan on macOS. Auto and CPU are
    // always present; Auto falls back to CPU when no accelerator initializes.
    QStringList backends{QStringLiteral("Auto"), QStringLiteral("CPU")};
    const PlatformCapabilities capabilities = PlatformCapabilities::current();
    if (capabilities.supportsMetal) {
        backends.append(QStringLiteral("Metal"));
    }
    if (capabilities.supportsVulkan) {
        backends.append(QStringLiteral("Vulkan"));
    }
    return backends;
}

bool SettingsViewModel::flashAttention() const noexcept {
    return m_flashAttention;
}

bool SettingsViewModel::tokenTimestamps() const noexcept {
    return m_tokenTimestamps;
}

int SettingsViewModel::threadCount() const noexcept {
    return m_threadCount;
}

qreal SettingsViewModel::lowConfidenceThreshold() const noexcept {
    return m_lowConfidenceThreshold;
}

QString SettingsViewModel::microphoneDevice() const {
    return m_microphoneDevice;
}

QString SettingsViewModel::playbackDevice() const {
    return m_playbackDevice;
}

QVariantList SettingsViewModel::microphoneDevices() const {
    QVariantList result{QVariantMap{{QStringLiteral("id"), QStringLiteral("Default")},
                                    {QStringLiteral("description"), tr("System default")},
                                    {QStringLiteral("default"), true}}};
    for (const QAudioDevice& device : QMediaDevices::audioInputs()) {
        result.append(QVariantMap{{QStringLiteral("id"), QString::fromUtf8(device.id())},
                                  {QStringLiteral("description"), device.description()},
                                  {QStringLiteral("default"), device == QMediaDevices::defaultAudioInput()}});
    }
    return result;
}

QVariantList SettingsViewModel::playbackDevices() const {
    QVariantList result{QVariantMap{{QStringLiteral("id"), QStringLiteral("Default")},
                                    {QStringLiteral("description"), tr("System default")},
                                    {QStringLiteral("default"), true}}};
    for (const QAudioDevice& device : QMediaDevices::audioOutputs()) {
        result.append(
            QVariantMap{{QStringLiteral("id"), QString::fromUtf8(device.id())},
                        {QStringLiteral("description"), device.description()},
                        {QStringLiteral("default"), device == QMediaDevices::defaultAudioOutput()}});
    }
    return result;
}

QString SettingsViewModel::recordingFormat() const {
    return m_recordingFormat;
}

bool SettingsViewModel::autoTranscribeRecording() const noexcept {
    return m_autoTranscribeRecording;
}

QString SettingsViewModel::managedMediaPolicy() const {
    return m_managedMediaPolicy;
}

QString SettingsViewModel::storagePath() const {
    if (m_managers.storage != nullptr && !m_managers.storage->dataDirectoryOverride().isEmpty()) {
        return m_managers.storage->dataDirectoryOverride();
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString SettingsViewModel::exportPath() const {
    if (m_managers.storage != nullptr && !m_managers.storage->exportDirectory().isEmpty()) {
        return m_managers.storage->exportDirectory();
    }
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

bool SettingsViewModel::automaticUpdates() const noexcept {
    return m_automaticUpdates;
}

QString SettingsViewModel::updateChannel() const {
    return m_updateChannel;
}

QString SettingsViewModel::appVersion() const {
    return QCoreApplication::applicationVersion();
}

void SettingsViewModel::chooseStoragePath() {
    emit storagePathRequested();
}

void SettingsViewModel::chooseExportPath() {
    emit exportPathRequested();
}

void SettingsViewModel::clearCache() {
    emit clearCacheRequested();
}

void SettingsViewModel::backupDatabase() {
    emit backupDatabaseRequested();
}

void SettingsViewModel::checkForUpdates() {
    emit updateCheckRequested();
}

void SettingsViewModel::setStoragePath(const QString& path) {
    if (m_managers.storage == nullptr || path.trimmed().isEmpty() || storagePath() == path) {
        return;
    }
    m_managers.storage->setDataDirectoryOverride(path);
    syncManager(m_managers.storage);
    emit storageChanged();
}

void SettingsViewModel::setExportPath(const QString& path) {
    if (m_managers.storage == nullptr || path.trimmed().isEmpty() || exportPath() == path) {
        return;
    }
    m_managers.storage->setExportDirectory(path);
    syncManager(m_managers.storage);
    emit storageChanged();
}

void SettingsViewModel::setStorageFolder(const QUrl& folder) {
    if (folder.isLocalFile()) {
        setStoragePath(folder.toLocalFile());
    }
}

void SettingsViewModel::setExportFolder(const QUrl& folder) {
    if (folder.isLocalFile()) {
        setExportPath(folder.toLocalFile());
    }
}

void SettingsViewModel::setLanguage(const QString& value) {
    if ((value != QLatin1String("en") && value != QLatin1String("zh_TW")) || m_language == value) {
        return;
    }
    m_language = value;
    if (m_managers.general != nullptr) {
        m_managers.general->setLanguage(value);
        syncManager(m_managers.general);
    }
    emit languageChanged();
}

void SettingsViewModel::setTheme(const QString& value) {
    static const QStringList allowed{QStringLiteral("System"), QStringLiteral("Light"),
                                     QStringLiteral("Dark")};
    if (!allowed.contains(value) || m_theme == value) {
        return;
    }
    m_theme = value;
    if (m_managers.appearance != nullptr) {
        m_managers.appearance->setTheme(storedTheme(value));
        syncManager(m_managers.appearance);
    }
    emit themeChanged();
}

void SettingsViewModel::setCloseBehavior(const QString& value) {
    static const QStringList allowed{QStringLiteral("CloseWindow"), QStringLiteral("MinimizeToTray"),
                                     QStringLiteral("Quit")};
    if (!allowed.contains(value) || m_closeBehavior == value) {
        return;
    }
    m_closeBehavior = value;
    if (m_managers.general != nullptr) {
        m_managers.general->setCloseBehavior(storedCloseBehavior(value));
        syncManager(m_managers.general);
    }
    emit generalChanged();
}

void SettingsViewModel::setMinimizeToTray(bool value) {
    if (m_minimizeToTray == value) {
        return;
    }
    m_minimizeToTray = value;
    if (m_managers.general != nullptr) {
        m_managers.general->setMinimizeToTray(value);
        syncManager(m_managers.general);
    }
    emit generalChanged();
}

void SettingsViewModel::setLaunchAtStartup(bool value) {
    if (m_launchAtStartup == value) {
        return;
    }
    m_launchAtStartup = value;
    if (m_managers.general != nullptr) {
        m_managers.general->setLaunchAtStartup(value);
        syncManager(m_managers.general);
    }
    emit generalChanged();
}

void SettingsViewModel::setImportBehavior(const QString& value) {
    static const QStringList allowed{QStringLiteral("ReferenceOriginal"), QStringLiteral("CopyManaged")};
    if (!allowed.contains(value) || m_importBehavior == value) {
        return;
    }
    m_importBehavior = value;
    m_managedMediaPolicy = value;
    if (m_managers.storage != nullptr) {
        m_managers.storage->setManagedMediaPolicy(storedManagedMediaPolicy(value));
        syncManager(m_managers.storage);
    }
    emit generalChanged();
    emit storageChanged();
}

void SettingsViewModel::setTextScale(qreal value) {
    const qreal bounded = qBound(0.8, value, 1.5);
    if (qFuzzyCompare(m_textScale, bounded)) {
        return;
    }
    m_textScale = bounded;
    if (m_managers.appearance != nullptr) {
        m_managers.appearance->setTextScale(bounded);
        syncManager(m_managers.appearance);
    }
    emit appearanceChanged();
}

void SettingsViewModel::setCompactMode(bool value) {
    if (m_compactMode == value) {
        return;
    }
    m_compactMode = value;
    if (m_managers.appearance != nullptr) {
        m_managers.appearance->setCompactMode(value);
        syncManager(m_managers.appearance);
    }
    emit appearanceChanged();
}

void SettingsViewModel::setWaveformDensity(const QString& value) {
    static const QStringList allowed{QStringLiteral("Sparse"), QStringLiteral("Balanced"),
                                     QStringLiteral("Dense")};
    if (!allowed.contains(value) || m_waveformDensity == value) {
        return;
    }
    m_waveformDensity = value;
    if (m_managers.appearance != nullptr) {
        m_managers.appearance->setWaveformDensity(storedWaveformDensity(value));
        syncManager(m_managers.appearance);
    }
    emit appearanceChanged();
}

void SettingsViewModel::setDefaultModel(const QString& value) {
    const QString canonical = canonicalModelId(value);
    if (canonical.isEmpty() || m_defaultModel == canonical) {
        return;
    }
    m_defaultModel = canonical;
    if (m_managers.transcription != nullptr) {
        m_managers.transcription->setDefaultModelId(canonical);
        syncManager(m_managers.transcription);
    }
    if (m_managers.models != nullptr) {
        m_managers.models->setDefaultModelId(canonical);
        syncManager(m_managers.models);
    }
    emit transcriptionChanged();
}

void SettingsViewModel::setTranscriptionLanguage(const QString& value) {
    if ((value != QLatin1String("zh") && value != QLatin1String("auto")) ||
        m_transcriptionLanguage == value) {
        return;
    }
    m_transcriptionLanguage = value;
    if (m_managers.transcription != nullptr) {
        m_managers.transcription->setLanguage(value);
        syncManager(m_managers.transcription);
    }
    emit transcriptionChanged();
}

void SettingsViewModel::setPreset(const QString& value) {
    static const QStringList allowed{QStringLiteral("Fast"), QStringLiteral("Balanced"),
                                     QStringLiteral("Accurate")};
    if (!allowed.contains(value) || m_preset == value) {
        return;
    }
    m_preset = value;
    if (m_managers.transcription != nullptr) {
        m_managers.transcription->setPreset(storedPreset(value));
        syncManager(m_managers.transcription);
    }
    emit transcriptionChanged();
}

void SettingsViewModel::setVadEnabled(bool value) {
    if (m_vadEnabled == value) {
        return;
    }
    m_vadEnabled = value;
    if (m_managers.transcription != nullptr) {
        m_managers.transcription->setVadEnabled(value);
        syncManager(m_managers.transcription);
    }
    emit transcriptionChanged();
}

void SettingsViewModel::setInitialPromptBehavior(const QString& value) {
    if ((value != QLatin1String("GlossaryAndContext") && value != QLatin1String("Disabled")) ||
        m_initialPromptBehavior == value) {
        return;
    }
    m_initialPromptBehavior = value;
    if (m_managers.transcription != nullptr) {
        m_managers.transcription->setInitialPromptBehavior(value == QLatin1String("Disabled")
                                                               ? QStringLiteral("disabled")
                                                               : QStringLiteral("glossary-and-context"));
        syncManager(m_managers.transcription);
    }
    emit transcriptionChanged();
}

void SettingsViewModel::setBackend(const QString& value) {
    if (!availableBackends().contains(value) || m_backend == value) {
        return;
    }
    m_backend = value;
    if (m_managers.transcription != nullptr) {
        m_managers.transcription->setBackend(storedBackend(value));
        syncManager(m_managers.transcription);
    }
    emit transcriptionChanged();
}

void SettingsViewModel::setFlashAttention(bool value) {
    if (m_flashAttention == value) {
        return;
    }
    m_flashAttention = value;
    if (m_managers.transcription != nullptr) {
        m_managers.transcription->setFlashAttention(value);
        syncManager(m_managers.transcription);
    }
    emit transcriptionChanged();
}

void SettingsViewModel::setTokenTimestamps(const bool value) {
    if (m_tokenTimestamps == value) {
        return;
    }
    m_tokenTimestamps = value;
    if (m_managers.transcription != nullptr) {
        m_managers.transcription->setTokenTimestamps(value);
        syncManager(m_managers.transcription);
    }
    emit transcriptionChanged();
}

void SettingsViewModel::setThreadCount(int value) {
    const int bounded = qBound(1, value, qMax(1, QThread::idealThreadCount()));
    if (m_threadCount == bounded) {
        return;
    }
    m_threadCount = bounded;
    if (m_managers.transcription != nullptr) {
        m_managers.transcription->setThreadCount(bounded);
        syncManager(m_managers.transcription);
    }
    emit transcriptionChanged();
}

void SettingsViewModel::setLowConfidenceThreshold(qreal value) {
    const qreal bounded = qBound(0.0, value, 1.0);
    if (qFuzzyCompare(m_lowConfidenceThreshold, bounded)) {
        return;
    }
    m_lowConfidenceThreshold = bounded;
    if (m_managers.transcription != nullptr) {
        m_managers.transcription->setLowConfidenceThreshold(bounded);
        syncManager(m_managers.transcription);
    }
    emit transcriptionChanged();
}

void SettingsViewModel::setMicrophoneDevice(const QString& value) {
    if (value.isEmpty() || m_microphoneDevice == value) {
        return;
    }
    m_microphoneDevice = value;
    if (m_managers.audio != nullptr) {
        m_managers.audio->setInputDeviceId(value == QLatin1String("Default") ? QString{} : value);
        syncManager(m_managers.audio);
    }
    emit audioChanged();
}

void SettingsViewModel::setPlaybackDevice(const QString& value) {
    if (value.isEmpty() || m_playbackDevice == value) {
        return;
    }
    m_playbackDevice = value;
    if (m_managers.audio != nullptr) {
        m_managers.audio->setOutputDeviceId(value == QLatin1String("Default") ? QString{} : value);
        syncManager(m_managers.audio);
    }
    emit audioChanged();
}

void SettingsViewModel::setRecordingFormat(const QString& value) {
    if (value != QLatin1String("PCM WAV") || m_recordingFormat == value) {
        return;
    }
    m_recordingFormat = value;
    if (m_managers.audio != nullptr) {
        m_managers.audio->setRecordingFormat(QStringLiteral("pcm16-wav"));
        syncManager(m_managers.audio);
    }
    emit audioChanged();
}

void SettingsViewModel::setAutoTranscribeRecording(const bool value) {
    if (m_autoTranscribeRecording == value) {
        return;
    }
    m_autoTranscribeRecording = value;
    if (m_managers.audio != nullptr) {
        m_managers.audio->setAutoTranscribeRecording(value);
        syncManager(m_managers.audio);
    }
    emit audioChanged();
}

void SettingsViewModel::setManagedMediaPolicy(const QString& value) {
    static const QStringList allowed{QStringLiteral("ReferenceOriginal"), QStringLiteral("CopyManaged")};
    if (!allowed.contains(value) || m_managedMediaPolicy == value) {
        return;
    }
    m_managedMediaPolicy = value;
    m_importBehavior = value;
    if (m_managers.storage != nullptr) {
        m_managers.storage->setManagedMediaPolicy(storedManagedMediaPolicy(value));
        syncManager(m_managers.storage);
    }
    emit generalChanged();
    emit storageChanged();
}

void SettingsViewModel::setAutomaticUpdates(bool value) {
    if (m_automaticUpdates == value) {
        return;
    }
    m_automaticUpdates = value;
    if (m_managers.updates != nullptr) {
        m_managers.updates->setAutomaticCheck(value);
        syncManager(m_managers.updates);
    }
    emit updatesChanged();
}

void SettingsViewModel::setUpdateChannel(const QString& value) {
    if ((value != QLatin1String("Stable") && value != QLatin1String("Beta")) || m_updateChannel == value) {
        return;
    }
    m_updateChannel = value;
    if (m_managers.updates != nullptr) {
        m_managers.updates->setChannel(storedUpdateChannel(value));
        syncManager(m_managers.updates);
    }
    emit updatesChanged();
}

void SettingsViewModel::loadFromManagers() {
    if (m_managers.general != nullptr) {
        const QString storedLanguage = m_managers.general->language();
        m_language = storedLanguage == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("zh_TW");
        m_closeBehavior = uiCloseBehavior(m_managers.general->closeBehavior());
        m_minimizeToTray = m_managers.general->minimizeToTray();
        m_launchAtStartup = m_managers.general->launchAtStartup();
    }
    if (m_managers.appearance != nullptr) {
        m_theme = uiTheme(m_managers.appearance->theme());
        m_textScale = qBound(0.8, m_managers.appearance->textScale(), 1.5);
        m_compactMode = m_managers.appearance->compactMode();
        m_waveformDensity = uiWaveformDensity(m_managers.appearance->waveformDensity());
    }
    if (m_managers.transcription != nullptr) {
        const QString storedModel = m_managers.transcription->defaultModelId();
        m_defaultModel = canonicalModelId(storedModel);
        if (storedModel != m_defaultModel) {
            m_managers.transcription->setDefaultModelId(m_defaultModel);
            syncManager(m_managers.transcription);
        }
        m_transcriptionLanguage = m_managers.transcription->language();
        m_preset = uiPreset(m_managers.transcription->preset());
        m_vadEnabled = m_managers.transcription->vadEnabled();
        m_initialPromptBehavior =
            m_managers.transcription->initialPromptBehavior() == QLatin1String("disabled")
                ? QStringLiteral("Disabled")
                : QStringLiteral("GlossaryAndContext");
        m_backend = uiBackend(m_managers.transcription->backend());
        m_flashAttention = m_managers.transcription->flashAttention();
        m_tokenTimestamps = m_managers.transcription->tokenTimestamps();
        const int storedThreads = m_managers.transcription->threadCount();
        m_threadCount = storedThreads > 0 ? qMin(storedThreads, qMax(1, QThread::idealThreadCount()))
                                          : qMax(1, QThread::idealThreadCount());
        m_lowConfidenceThreshold = m_managers.transcription->lowConfidenceThreshold();
    }
    if (m_managers.models != nullptr) {
        const QString storedModel = m_managers.models->defaultModelId();
        m_defaultModel = canonicalModelId(storedModel);
        if (storedModel != m_defaultModel) {
            m_managers.models->setDefaultModelId(m_defaultModel);
            syncManager(m_managers.models);
        }
    }
    if (m_managers.audio != nullptr) {
        m_microphoneDevice = m_managers.audio->inputDeviceId();
        if (m_microphoneDevice.isEmpty()) {
            m_microphoneDevice = QStringLiteral("Default");
        }
        m_playbackDevice = m_managers.audio->outputDeviceId();
        if (m_playbackDevice.isEmpty()) {
            m_playbackDevice = QStringLiteral("Default");
        }
        m_recordingFormat = QStringLiteral("PCM WAV");
        m_autoTranscribeRecording = m_managers.audio->autoTranscribeRecording();
    }
    if (m_managers.storage != nullptr) {
        m_managedMediaPolicy = uiManagedMediaPolicy(m_managers.storage->managedMediaPolicy());
        m_importBehavior = m_managedMediaPolicy;
    }
    if (m_managers.updates != nullptr) {
        m_automaticUpdates = m_managers.updates->automaticCheck();
        m_updateChannel = uiUpdateChannel(m_managers.updates->channel());
    }
    emit languageChanged();
    emit themeChanged();
    emit generalChanged();
    emit appearanceChanged();
    emit transcriptionChanged();
    emit audioChanged();
    emit storageChanged();
    emit updatesChanged();
}

void SettingsViewModel::syncManager(SettingsManagerBase* manager) {
    if (manager == nullptr) {
        return;
    }
    const auto result = manager->sync();
    if (!result) {
        emit persistenceError(result.error().message);
    }
}

} // namespace BreezeDesk
