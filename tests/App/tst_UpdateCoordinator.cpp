#include "breezedesk/platform/IPlatformService.h"
#include "breezedesk/update/UpdateCoordinator.h"

#include <QSignalSpy>
#include <QtTest>

using namespace BreezeDesk;

namespace {

class DirectInstallPlatform final : public IPlatformService {
  public:
    [[nodiscard]] PlatformCapabilities capabilities() const override { return {}; }
    [[nodiscard]] bool revealInFileManager(const QString&, QString*) const override { return false; }
    [[nodiscard]] bool setLaunchAtLogin(bool, QString*) override { return false; }
    [[nodiscard]] bool launchAtLogin(QString*) const override { return false; }
    [[nodiscard]] bool requestMicrophonePermission(QString*) override { return false; }
    [[nodiscard]] QString installSource() const override { return QStringLiteral("direct"); }
    [[nodiscard]] QString gpuDescription() const override { return {}; }
    void activateApplication() override {}
};

} // namespace

class UpdateCoordinatorTest final : public QObject {
    Q_OBJECT

  private slots:
    void missingNativeFrameworkFallsBackSafely();
    void nativeFrameworkLoadsWhenSupplied();
};

void UpdateCoordinatorTest::missingNativeFrameworkFallsBackSafely() {
    const QByteArray previousPath = qgetenv("BREEZEDESK_SPARKLE_FRAMEWORK_PATH");
    qunsetenv("BREEZEDESK_SPARKLE_FRAMEWORK_PATH");
    DirectInstallPlatform platform;
    UpdateCoordinator coordinator(&platform);
    QVERIFY(!coordinator.isAvailable());
    QSignalSpy errors(&coordinator, &UpdateCoordinator::error);
    coordinator.checkForUpdates(true);
    QCOMPARE(errors.size(), 1);
    QVERIFY(!errors.constFirst().constFirst().toString().trimmed().isEmpty());
    if (!previousPath.isEmpty()) {
        qputenv("BREEZEDESK_SPARKLE_FRAMEWORK_PATH", previousPath);
    }
}

void UpdateCoordinatorTest::nativeFrameworkLoadsWhenSupplied() {
    const QByteArray frameworkPath = qgetenv("BREEZEDESK_TEST_SPARKLE_FRAMEWORK_PATH");
    if (frameworkPath.isEmpty()) {
        QSKIP("Set BREEZEDESK_TEST_SPARKLE_FRAMEWORK_PATH for the optional Sparkle integration test.", 0);
    }
    qputenv("BREEZEDESK_SPARKLE_FRAMEWORK_PATH", frameworkPath);
    DirectInstallPlatform platform;
    UpdateCoordinator coordinator(&platform);
    QVERIFY2(coordinator.isAvailable(), "The supplied Sparkle 2 framework did not initialize.");
    qunsetenv("BREEZEDESK_SPARKLE_FRAMEWORK_PATH");
}

QTEST_GUILESS_MAIN(UpdateCoordinatorTest)

#include "tst_UpdateCoordinator.moc"
