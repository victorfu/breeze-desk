#include "breezedesk/platform/InstallSourceClassifier.h"

#include <QtTest>

using namespace BreezeDesk;

class InstallSourceClassifierTest final : public QObject {
    Q_OBJECT

  private slots:
    void recognizesPerUserNsisInstall();
    void keepsMsixUpdatesDisabled();
    void recognizesProgramFilesFallback();
    void rejectsUnregisteredDevelopmentPath();
    void rejectsSiblingPrefix();
};

void InstallSourceClassifierTest::recognizesPerUserNsisInstall() {
    QCOMPARE(classifyWindowsInstallSource(
                 QStringLiteral("C:\\Users\\Alice\\AppData\\Local\\Programs\\BreezeDesk\\bin"),
                 QStringLiteral("c:\\users\\alice\\appdata\\local\\programs\\breezedesk")),
             QStringLiteral("direct"));
}

void InstallSourceClassifierTest::keepsMsixUpdatesDisabled() {
    QCOMPARE(classifyWindowsInstallSource(
                 QStringLiteral("C:\\Program Files\\WindowsApps\\VictorFu.BreezeDesk_1.0.0.0_x64\\bin"),
                 QStringLiteral("C:\\Program Files\\WindowsApps\\VictorFu.BreezeDesk_1.0.0.0_x64")),
             QStringLiteral("msix"));
}

void InstallSourceClassifierTest::recognizesProgramFilesFallback() {
    QCOMPARE(classifyWindowsInstallSource(QStringLiteral("C:/Program Files/BreezeDesk/bin"), {}),
             QStringLiteral("direct"));
}

void InstallSourceClassifierTest::rejectsUnregisteredDevelopmentPath() {
    QCOMPARE(
        classifyWindowsInstallSource(QStringLiteral("C:/Users/Alice/AppData/Local/build/BreezeDesk/bin"), {}),
        QStringLiteral("development"));
}

void InstallSourceClassifierTest::rejectsSiblingPrefix() {
    QCOMPARE(classifyWindowsInstallSource(
                 QStringLiteral("C:/Users/Alice/AppData/Local/Programs/BreezeDesk-Preview/bin"),
                 QStringLiteral("C:/Users/Alice/AppData/Local/Programs/BreezeDesk")),
             QStringLiteral("development"));
}

QTEST_GUILESS_MAIN(InstallSourceClassifierTest)

#include "tst_InstallSourceClassifier.moc"
