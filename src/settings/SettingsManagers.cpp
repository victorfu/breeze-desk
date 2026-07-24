#include "breezedesk/settings/SettingsManagers.h"

#include <QHash>

namespace BreezeDesk {
namespace {
template <typename Enum>
QString enumName(const QHash<Enum, QString>& values, const Enum value, const QString& fallback) {
    return values.value(value, fallback);
}
template <typename Enum>
Enum enumValue(const QHash<Enum, QString>& values, const QString& name, const Enum fallback) {
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        if (it.value() == name)
            return it.key();
    return fallback;
}
const QHash<ThemeMode, QString>& themes() {
    static const QHash<ThemeMode, QString> v{{ThemeMode::System, QStringLiteral("system")},
                                             {ThemeMode::Light, QStringLiteral("light")},
                                             {ThemeMode::Dark, QStringLiteral("dark")}};
    return v;
}
const QHash<CloseBehavior, QString>& closes() {
    static const QHash<CloseBehavior, QString> v{
        {CloseBehavior::CloseWindow, QStringLiteral("close-window")},
        {CloseBehavior::MinimizeToTray, QStringLiteral("minimize-to-tray")},
        {CloseBehavior::Quit, QStringLiteral("quit")}};
    return v;
}
const QHash<BackendPreference, QString>& backends() {
    static const QHash<BackendPreference, QString> v{{BackendPreference::Automatic, QStringLiteral("auto")},
                                                     {BackendPreference::Cpu, QStringLiteral("cpu")},
                                                     {BackendPreference::Metal, QStringLiteral("metal")},
                                                     {BackendPreference::Vulkan, QStringLiteral("vulkan")}};
    return v;
}
const QHash<ManagedMediaPolicy, QString>& policies() {
    static const QHash<ManagedMediaPolicy, QString> v{
        {ManagedMediaPolicy::ReferenceOriginal, QStringLiteral("reference")},
        {ManagedMediaPolicy::CopyIntoLibrary, QStringLiteral("copy")}};
    return v;
}
const QHash<UpdateChannel, QString>& channels() {
    static const QHash<UpdateChannel, QString> v{{UpdateChannel::Stable, QStringLiteral("stable")},
                                                 {UpdateChannel::Beta, QStringLiteral("beta")}};
    return v;
}
} // namespace

QString themeModeName(ThemeMode value) {
    return enumName(themes(), value, QStringLiteral("system"));
}
QString closeBehaviorName(CloseBehavior value) {
    return enumName(closes(), value, QStringLiteral("minimize-to-tray"));
}
QString backendPreferenceName(BackendPreference value) {
    return enumName(backends(), value, QStringLiteral("auto"));
}
QString managedMediaPolicyName(ManagedMediaPolicy value) {
    return enumName(policies(), value, QStringLiteral("reference"));
}
QString updateChannelName(UpdateChannel value) {
    return enumName(channels(), value, QStringLiteral("stable"));
}

SettingsManagerBase::SettingsManagerBase(SettingsStore& store, QString prefix)
    : m_store(store), m_prefix(std::move(prefix)) {}
QVariant SettingsManagerBase::read(const QString& key, const QVariant& fallback) const {
    return m_store.value(m_prefix + QLatin1Char('/') + key, fallback);
}
void SettingsManagerBase::write(const QString& key, const QVariant& value) {
    m_store.setValue(m_prefix + QLatin1Char('/') + key, value);
}

GeneralSettingsManager::GeneralSettingsManager(SettingsStore& s)
    : SettingsManagerBase(s, QStringLiteral("general")) {}
QString GeneralSettingsManager::language() const {
    return read(QStringLiteral("language"), QStringLiteral("system")).toString();
}
void GeneralSettingsManager::setLanguage(const QString& v) {
    static const QStringList allowed{QStringLiteral("system"), QStringLiteral("en"), QStringLiteral("zh_TW")};
    write(QStringLiteral("language"), allowed.contains(v) ? v : QStringLiteral("system"));
}
bool GeneralSettingsManager::launchAtStartup() const {
    return read(QStringLiteral("launchAtStartup"), false).toBool();
}
void GeneralSettingsManager::setLaunchAtStartup(bool v) {
    write(QStringLiteral("launchAtStartup"), v);
}
CloseBehavior GeneralSettingsManager::closeBehavior() const {
    return enumValue(closes(),
                     read(QStringLiteral("closeBehavior"), QStringLiteral("minimize-to-tray")).toString(),
                     CloseBehavior::MinimizeToTray);
}
void GeneralSettingsManager::setCloseBehavior(CloseBehavior v) {
    write(QStringLiteral("closeBehavior"), closeBehaviorName(v));
}
bool GeneralSettingsManager::minimizeToTray() const {
    return read(QStringLiteral("minimizeToTray"), true).toBool();
}
void GeneralSettingsManager::setMinimizeToTray(bool v) {
    write(QStringLiteral("minimizeToTray"), v);
}
bool GeneralSettingsManager::startTranscriptionAfterImport() const {
    return read(QStringLiteral("startAfterImport"), false).toBool();
}
void GeneralSettingsManager::setStartTranscriptionAfterImport(bool v) {
    write(QStringLiteral("startAfterImport"), v);
}

AppearanceSettingsManager::AppearanceSettingsManager(SettingsStore& s)
    : SettingsManagerBase(s, QStringLiteral("appearance")) {}
ThemeMode AppearanceSettingsManager::theme() const {
    return enumValue(themes(), read(QStringLiteral("theme"), QStringLiteral("light")).toString(),
                     ThemeMode::Light);
}
void AppearanceSettingsManager::setTheme(ThemeMode v) {
    write(QStringLiteral("theme"), themeModeName(v));
}
double AppearanceSettingsManager::textScale() const {
    return qBound(.75, read(QStringLiteral("textScale"), 1.0).toDouble(), 2.0);
}
void AppearanceSettingsManager::setTextScale(double v) {
    write(QStringLiteral("textScale"), qBound(.75, v, 2.0));
}
bool AppearanceSettingsManager::compactMode() const {
    return read(QStringLiteral("compactMode"), false).toBool();
}
void AppearanceSettingsManager::setCompactMode(bool v) {
    write(QStringLiteral("compactMode"), v);
}
int AppearanceSettingsManager::waveformDensity() const {
    return qBound(1, read(QStringLiteral("waveformDensity"), 2).toInt(), 4);
}
void AppearanceSettingsManager::setWaveformDensity(int v) {
    write(QStringLiteral("waveformDensity"), qBound(1, v, 4));
}

TranscriptionSettingsManager::TranscriptionSettingsManager(SettingsStore& s)
    : SettingsManagerBase(s, QStringLiteral("transcription")) {}
QString TranscriptionSettingsManager::defaultModelId() const {
    return read(QStringLiteral("defaultModel"), QStringLiteral("breeze-q5")).toString();
}
void TranscriptionSettingsManager::setDefaultModelId(const QString& v) {
    write(QStringLiteral("defaultModel"), v);
}
QString TranscriptionSettingsManager::language() const {
    return read(QStringLiteral("language"), QStringLiteral("zh")).toString();
}
void TranscriptionSettingsManager::setLanguage(const QString& v) {
    write(QStringLiteral("language"), v == QStringLiteral("auto") ? v : QStringLiteral("zh"));
}
QString TranscriptionSettingsManager::preset() const {
    return read(QStringLiteral("preset"), QStringLiteral("balanced")).toString();
}
void TranscriptionSettingsManager::setPreset(const QString& v) {
    static const QStringList a{QStringLiteral("fast"), QStringLiteral("balanced"),
                               QStringLiteral("accurate")};
    write(QStringLiteral("preset"), a.contains(v) ? v : QStringLiteral("balanced"));
}
bool TranscriptionSettingsManager::vadEnabled() const {
    return read(QStringLiteral("vadEnabled"), true).toBool();
}
void TranscriptionSettingsManager::setVadEnabled(bool v) {
    write(QStringLiteral("vadEnabled"), v);
}
QString TranscriptionSettingsManager::initialPromptBehavior() const {
    return read(QStringLiteral("initialPromptBehavior"), QStringLiteral("glossary-and-context")).toString();
}
void TranscriptionSettingsManager::setInitialPromptBehavior(const QString& v) {
    write(QStringLiteral("initialPromptBehavior"), v);
}
QString TranscriptionSettingsManager::glossaryProfileId() const {
    return read(QStringLiteral("glossaryProfile"), QString()).toString();
}
void TranscriptionSettingsManager::setGlossaryProfileId(const QString& v) {
    write(QStringLiteral("glossaryProfile"), v);
}
int TranscriptionSettingsManager::threadCount() const {
    return qBound(0, read(QStringLiteral("threadCount"), 0).toInt(), 256);
}
void TranscriptionSettingsManager::setThreadCount(int v) {
    write(QStringLiteral("threadCount"), qBound(0, v, 256));
}
BackendPreference TranscriptionSettingsManager::backend() const {
    return enumValue(backends(), read(QStringLiteral("backend"), QStringLiteral("auto")).toString(),
                     BackendPreference::Automatic);
}
void TranscriptionSettingsManager::setBackend(BackendPreference v) {
    write(QStringLiteral("backend"), backendPreferenceName(v));
}
bool TranscriptionSettingsManager::flashAttention() const {
    return read(QStringLiteral("flashAttention"), true).toBool();
}
void TranscriptionSettingsManager::setFlashAttention(bool v) {
    write(QStringLiteral("flashAttention"), v);
}
bool TranscriptionSettingsManager::tokenTimestamps() const {
    return read(QStringLiteral("tokenTimestamps"), true).toBool();
}
void TranscriptionSettingsManager::setTokenTimestamps(bool v) {
    write(QStringLiteral("tokenTimestamps"), v);
}
double TranscriptionSettingsManager::lowConfidenceThreshold() const {
    return qBound(0.0, read(QStringLiteral("lowConfidenceThreshold"), .35).toDouble(), 1.0);
}
void TranscriptionSettingsManager::setLowConfidenceThreshold(double v) {
    write(QStringLiteral("lowConfidenceThreshold"), qBound(0.0, v, 1.0));
}

AudioSettingsManager::AudioSettingsManager(SettingsStore& s)
    : SettingsManagerBase(s, QStringLiteral("audio")) {}
QString AudioSettingsManager::inputDeviceId() const {
    return read(QStringLiteral("inputDevice"), QString()).toString();
}
void AudioSettingsManager::setInputDeviceId(const QString& v) {
    write(QStringLiteral("inputDevice"), v);
}
QString AudioSettingsManager::outputDeviceId() const {
    return read(QStringLiteral("outputDevice"), QString()).toString();
}
void AudioSettingsManager::setOutputDeviceId(const QString& v) {
    write(QStringLiteral("outputDevice"), v);
}
QString AudioSettingsManager::recordingFormat() const {
    return read(QStringLiteral("recordingFormat"), QStringLiteral("pcm16-wav")).toString();
}
void AudioSettingsManager::setRecordingFormat(const QString& v) {
    write(QStringLiteral("recordingFormat"),
          v == QStringLiteral("pcm16-wav") ? v : QStringLiteral("pcm16-wav"));
}
bool AudioSettingsManager::autoTranscribeRecording() const {
    return read(QStringLiteral("autoTranscribe"), false).toBool();
}
void AudioSettingsManager::setAutoTranscribeRecording(bool v) {
    write(QStringLiteral("autoTranscribe"), v);
}

ModelSettingsManager::ModelSettingsManager(SettingsStore& s)
    : SettingsManagerBase(s, QStringLiteral("models")) {}
QString ModelSettingsManager::defaultModelId() const {
    return m_store.value(QStringLiteral("transcription/defaultModel"), QStringLiteral("breeze-q5"))
        .toString();
}
void ModelSettingsManager::setDefaultModelId(const QString& v) {
    m_store.setValue(QStringLiteral("transcription/defaultModel"), v);
}
QString ModelSettingsManager::customModelDirectory() const {
    return read(QStringLiteral("customDirectory"), QString()).toString();
}
void ModelSettingsManager::setCustomModelDirectory(const QString& v) {
    write(QStringLiteral("customDirectory"), v);
}
int ModelSettingsManager::workerIdleTimeoutSeconds() const {
    return qBound(0, read(QStringLiteral("workerIdleTimeout"), 600).toInt(), 86400);
}
void ModelSettingsManager::setWorkerIdleTimeoutSeconds(int v) {
    write(QStringLiteral("workerIdleTimeout"), qBound(0, v, 86400));
}

StorageSettingsManager::StorageSettingsManager(SettingsStore& s)
    : SettingsManagerBase(s, QStringLiteral("storage")) {}
QString StorageSettingsManager::dataDirectoryOverride() const {
    return read(QStringLiteral("dataDirectory"), QString()).toString();
}
void StorageSettingsManager::setDataDirectoryOverride(const QString& v) {
    write(QStringLiteral("dataDirectory"), v);
}
QString StorageSettingsManager::cacheDirectoryOverride() const {
    return read(QStringLiteral("cacheDirectory"), QString()).toString();
}
void StorageSettingsManager::setCacheDirectoryOverride(const QString& v) {
    write(QStringLiteral("cacheDirectory"), v);
}
QString StorageSettingsManager::exportDirectory() const {
    return read(QStringLiteral("exportDirectory"), QString()).toString();
}
void StorageSettingsManager::setExportDirectory(const QString& v) {
    write(QStringLiteral("exportDirectory"), v);
}
qint64 StorageSettingsManager::cacheLimitBytes() const {
    return qMax<qint64>(
        0, read(QStringLiteral("cacheLimitBytes"), QVariant::fromValue<qint64>(20LL * 1024 * 1024 * 1024))
               .toLongLong());
}
void StorageSettingsManager::setCacheLimitBytes(qint64 v) {
    write(QStringLiteral("cacheLimitBytes"), qMax<qint64>(0, v));
}
ManagedMediaPolicy StorageSettingsManager::managedMediaPolicy() const {
    return enumValue(policies(),
                     read(QStringLiteral("managedMediaPolicy"), QStringLiteral("reference")).toString(),
                     ManagedMediaPolicy::ReferenceOriginal);
}
void StorageSettingsManager::setManagedMediaPolicy(ManagedMediaPolicy v) {
    write(QStringLiteral("managedMediaPolicy"), managedMediaPolicyName(v));
}
bool StorageSettingsManager::automaticDatabaseBackup() const {
    return read(QStringLiteral("automaticDatabaseBackup"), true).toBool();
}
void StorageSettingsManager::setAutomaticDatabaseBackup(bool v) {
    write(QStringLiteral("automaticDatabaseBackup"), v);
}

UpdateSettingsManager::UpdateSettingsManager(SettingsStore& s)
    : SettingsManagerBase(s, QStringLiteral("updates")) {}
bool UpdateSettingsManager::automaticCheck() const {
    return read(QStringLiteral("automaticCheck"), false).toBool();
}
void UpdateSettingsManager::setAutomaticCheck(bool v) {
    write(QStringLiteral("automaticCheck"), v);
}
UpdateChannel UpdateSettingsManager::channel() const {
    return enumValue(channels(), read(QStringLiteral("channel"), QStringLiteral("stable")).toString(),
                     UpdateChannel::Stable);
}
void UpdateSettingsManager::setChannel(UpdateChannel v) {
    write(QStringLiteral("channel"), updateChannelName(v));
}
QDateTime UpdateSettingsManager::lastCheck() const {
    return read(QStringLiteral("lastCheck"), QDateTime()).toDateTime();
}
void UpdateSettingsManager::setLastCheck(const QDateTime& v) {
    write(QStringLiteral("lastCheck"), v.toUTC());
}

PrivacySettingsManager::PrivacySettingsManager(SettingsStore& s)
    : SettingsManagerBase(s, QStringLiteral("privacy")) {}
bool PrivacySettingsManager::redactPathsInLogs() const {
    return read(QStringLiteral("redactPaths"), true).toBool();
}
void PrivacySettingsManager::setRedactPathsInLogs(bool v) {
    write(QStringLiteral("redactPaths"), v);
}
bool PrivacySettingsManager::includePathsInDiagnostics() const {
    return read(QStringLiteral("includePathsInDiagnostics"), false).toBool();
}
void PrivacySettingsManager::setIncludePathsInDiagnostics(bool v) {
    write(QStringLiteral("includePathsInDiagnostics"), v);
}

WindowSettingsManager::WindowSettingsManager(SettingsStore& s)
    : SettingsManagerBase(s, QStringLiteral("window")) {}
QByteArray WindowSettingsManager::geometry() const {
    return read(QStringLiteral("geometry"), QByteArray()).toByteArray();
}
void WindowSettingsManager::setGeometry(const QByteArray& v) {
    write(QStringLiteral("geometry"), v);
}
QByteArray WindowSettingsManager::state() const {
    return read(QStringLiteral("state"), QByteArray()).toByteArray();
}
void WindowSettingsManager::setState(const QByteArray& v) {
    write(QStringLiteral("state"), v);
}
bool WindowSettingsManager::inspectorVisible() const {
    return read(QStringLiteral("inspectorVisible"), true).toBool();
}
void WindowSettingsManager::setInspectorVisible(bool v) {
    write(QStringLiteral("inspectorVisible"), v);
}
bool WindowSettingsManager::autoScrollTranscript() const {
    return read(QStringLiteral("autoScrollTranscript"), true).toBool();
}
void WindowSettingsManager::setAutoScrollTranscript(bool v) {
    write(QStringLiteral("autoScrollTranscript"), v);
}

} // namespace BreezeDesk
