#include "breezedesk/platform/InstallSourceClassifier.h"

#include <QtTest>

using namespace BreezeDesk;

class InstallSourceClassifierTest final : public QObject {
    Q_OBJECT

  private slots:
    void recognizesMsixInstall();
    void recognizesDevelopmentInstall();
};

void InstallSourceClassifierTest::recognizesMsixInstall() {
    QCOMPARE(classifyWindowsInstallSource(
                 QStringLiteral("C:\\Program Files\\WindowsApps\\PartnerCenter.BreezeDesk_1.0.0.0_x64\\bin")),
             QStringLiteral("msix"));
}

void InstallSourceClassifierTest::recognizesDevelopmentInstall() {
    QCOMPARE(classifyWindowsInstallSource(
                 QStringLiteral("C:/Users/Alice/AppData/Local/build/BreezeDesk/bin")),
             QStringLiteral("development"));
}

QTEST_GUILESS_MAIN(InstallSourceClassifierTest)

#include "tst_InstallSourceClassifier.moc"
