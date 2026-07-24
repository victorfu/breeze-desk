#include "breezedesk/platform/WorkerRegistry.h"

#include "breezedesk/app_config.h"

#include <QDir>
#include <QtTest>

using namespace BreezeDesk;

namespace {

QString workerName(const QString& suffix = {}) {
    return QString::fromLatin1(AppConfig::WorkerExecutableName) + suffix;
}

} // namespace

class WorkerRegistryTest final : public QObject {
    Q_OBJECT

  private slots:
    void windowsVariantOrder_data();
    void windowsVariantOrder();
    void overrideAndDevelopmentPathsAreDeduplicated();
    void macBundleCandidatesAreStable();
};

void WorkerRegistryTest::windowsVariantOrder_data() {
    QTest::addColumn<QString>("preference");
    QTest::addColumn<QStringList>("expectedNames");
    QTest::newRow("automatic") << QStringLiteral("Auto")
                               << QStringList{workerName(QStringLiteral("-vulkan.exe")),
                                              workerName(QStringLiteral("-cpu.exe")),
                                              workerName(QStringLiteral(".exe")),
                                              workerName(QStringLiteral(".exe"))};
    QTest::newRow("cpu-case-insensitive")
        << QStringLiteral("cpu")
        << QStringList{workerName(QStringLiteral("-cpu.exe")), workerName(QStringLiteral(".exe")),
                       workerName(QStringLiteral(".exe"))};
    QTest::newRow("vulkan-prefers-cpu-fallback")
        << QStringLiteral("Vulkan")
        << QStringList{workerName(QStringLiteral("-vulkan.exe")), workerName(QStringLiteral("-cpu.exe")),
                       workerName(QStringLiteral(".exe")), workerName(QStringLiteral(".exe"))};
}

void WorkerRegistryTest::windowsVariantOrder() {
    QFETCH(QString, preference);
    QFETCH(QStringList, expectedNames);
    const QStringList candidates = WorkerRegistry::executableCandidatesForHost(
        WorkerHostPlatform::Windows, QStringLiteral("/opt/BreezeDesk/bin"), preference);
    QCOMPARE(candidates.size(), expectedNames.size());
    for (qsizetype index = 0; index < candidates.size(); ++index) {
        QCOMPARE(QFileInfo(candidates.at(index)).fileName(), expectedNames.at(index));
    }
    QVERIFY(candidates.constLast().contains(QStringLiteral("/worker/")));
}

void WorkerRegistryTest::overrideAndDevelopmentPathsAreDeduplicated() {
    const QString generic = QStringLiteral("/opt/BreezeDesk/bin/%1.exe").arg(workerName());
    const QStringList candidates = WorkerRegistry::executableCandidatesForHost(
        WorkerHostPlatform::Windows, QStringLiteral("/opt/BreezeDesk/bin"), QStringLiteral("Auto"),
        QStringLiteral("/opt/BreezeDesk/bin/../bin/%1.exe").arg(workerName()), generic);
    QCOMPARE(candidates.constFirst(), generic);
    QCOMPARE(candidates.count(generic), 1);
}

void WorkerRegistryTest::macBundleCandidatesAreStable() {
    const QString applicationDirectory = QStringLiteral("/Applications/BreezeDesk.app/Contents/MacOS");
    const QString developmentPath = QStringLiteral("/tmp/%1").arg(workerName());
    const QStringList candidates = WorkerRegistry::executableCandidatesForHost(
        WorkerHostPlatform::MacOS, applicationDirectory, QStringLiteral("Metal"), {}, developmentPath);
    QCOMPARE(
        candidates,
        QStringList({QStringLiteral("/Applications/BreezeDesk.app/Contents/MacOS/%1").arg(workerName()),
                     QStringLiteral("/Applications/BreezeDesk.app/Contents/Helpers/%1").arg(workerName()),
                     QStringLiteral("/worker/%1").arg(workerName()), developmentPath}));
}

QTEST_GUILESS_MAIN(WorkerRegistryTest)

#include "tst_WorkerRegistry.moc"
