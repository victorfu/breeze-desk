#include "breezedesk/settings/SettingsManagers.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace BreezeDesk;

class SettingsTest final : public QObject {
    Q_OBJECT

  private slots:
    void defaultsArePrivacyPreserving();
    void valuesPersistAndAreClamped();
    void legacySettingsMigrate();
    void debugAndReleaseFilesAreIsolated();
};

void SettingsTest::defaultsArePrivacyPreserving() {
    QTemporaryDir directory;
    SettingsStore store(directory.filePath(QStringLiteral("settings.ini")));
    AppearanceSettingsManager appearance(store);
    TranscriptionSettingsManager transcription(store);
    UpdateSettingsManager updates(store);
    PrivacySettingsManager privacy(store);
    QCOMPARE(appearance.theme(), ThemeMode::Light);
    QCOMPARE(transcription.defaultModelId(), QStringLiteral("breeze-q5"));
    QCOMPARE(transcription.language(), QStringLiteral("zh"));
    QCOMPARE(transcription.preset(), QStringLiteral("balanced"));
    QCOMPARE(transcription.backend(), BackendPreference::Automatic);
    QVERIFY(transcription.vadEnabled());
    QVERIFY(!updates.automaticCheck());
    QVERIFY(privacy.redactPathsInLogs());
    QVERIFY(!privacy.includePathsInDiagnostics());
}

void SettingsTest::valuesPersistAndAreClamped() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("settings.ini"));
    {
        SettingsStore store(path);
        AppearanceSettingsManager appearance(store);
        TranscriptionSettingsManager transcription(store);
        appearance.setTheme(ThemeMode::Dark);
        appearance.setTextScale(8.0);
        transcription.setBackend(BackendPreference::Cpu);
        transcription.setLowConfidenceThreshold(-1.0);
        QVERIFY(store.sync());
    }
    SettingsStore store(path);
    AppearanceSettingsManager appearance(store);
    TranscriptionSettingsManager transcription(store);
    QCOMPARE(appearance.theme(), ThemeMode::Dark);
    QCOMPARE(appearance.textScale(), 2.0);
    QCOMPARE(transcription.backend(), BackendPreference::Cpu);
    QCOMPARE(transcription.lowConfidenceThreshold(), 0.0);
}

void SettingsTest::legacySettingsMigrate() {
    QTemporaryDir directory;
    SettingsStore store(directory.filePath(QStringLiteral("settings.ini")));
    store.setValue(QStringLiteral("appearance/darkMode"), true);
    store.setValue(QStringLiteral("transcription/useGpu"), false);
    QVERIFY(SettingsMigrationService::migrate(store));
    QCOMPARE(store.value(QStringLiteral("settings/schemaVersion")).toInt(),
             SettingsMigrationService::CurrentSchemaVersion);
    QCOMPARE(AppearanceSettingsManager(store).theme(), ThemeMode::Dark);
    QCOMPARE(TranscriptionSettingsManager(store).backend(), BackendPreference::Cpu);
    QVERIFY(!store.contains(QStringLiteral("appearance/darkMode")));
}

void SettingsTest::debugAndReleaseFilesAreIsolated() {
    QTemporaryDir directory;
    SettingsStore release(directory.filePath(QStringLiteral("BreezeDesk.ini")));
    SettingsStore debug(directory.filePath(QStringLiteral("BreezeDesk-Debug.ini")));
    GeneralSettingsManager releaseGeneral(release);
    GeneralSettingsManager debugGeneral(debug);
    releaseGeneral.setLanguage(QStringLiteral("en"));
    debugGeneral.setLanguage(QStringLiteral("zh_TW"));
    QVERIFY(release.sync());
    QVERIFY(debug.sync());
    QCOMPARE(releaseGeneral.language(), QStringLiteral("en"));
    QCOMPARE(debugGeneral.language(), QStringLiteral("zh_TW"));
    QVERIFY(release.fileName() != debug.fileName());
}

QTEST_GUILESS_MAIN(SettingsTest)
#include "tst_Settings.moc"
