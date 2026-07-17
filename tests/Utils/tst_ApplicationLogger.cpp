#include "breezedesk/core/ApplicationLogger.h"
#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/core/TemporaryFileJanitor.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QtTest>

using namespace BreezeDesk;

namespace {

class EnvironmentGuard final {
  public:
    explicit EnvironmentGuard(const QByteArray& value)
        : previous(qgetenv("BREEZEDESK_DATA_ROOT")),
          wasSet(qEnvironmentVariableIsSet("BREEZEDESK_DATA_ROOT")) {
        qputenv("BREEZEDESK_DATA_ROOT", value);
    }

    ~EnvironmentGuard() {
        if (wasSet) {
            qputenv("BREEZEDESK_DATA_ROOT", previous);
        } else {
            qunsetenv("BREEZEDESK_DATA_ROOT");
        }
    }

  private:
    QByteArray previous;
    bool wasSet;
};

[[nodiscard]] QByteArray readAll(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

[[nodiscard]] bool writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

} // namespace

class ApplicationLoggerTest final : public QObject {
    Q_OBJECT

  private slots:
    void sanitizerRedactsSensitiveFieldsAndPaths();
    void loggerWritesStructuredSanitizedEntries();
    void loggerRotatesAndRetainsBoundedFiles();
    void onlyOneLoggerCanOwnTheQtHandler();
    void janitorRemovesOnlyExpiredApplicationTemporaryFiles();
    void janitorRejectsBroadCleanupTargets();
};

void ApplicationLoggerTest::sanitizerRedactsSensitiveFieldsAndPaths() {
    const QString sanitized = LogSanitizer::sanitize(
        QStringLiteral("source=/Users/alice/Meetings/My Private Recording.wav transcript=customer roadmap; "
                       "\"sessionToken\":\"top-secret\" [private]speaker text[/private]"));
    QVERIFY(!sanitized.contains(QStringLiteral("alice")));
    QVERIFY(!sanitized.contains(QStringLiteral("Private Recording.wav")));
    QVERIFY(!sanitized.contains(QStringLiteral("customer roadmap")));
    QVERIFY(!sanitized.contains(QStringLiteral("top-secret")));
    QVERIFY(!sanitized.contains(QStringLiteral("speaker text")));
    QVERIFY(sanitized.contains(QStringLiteral("<redacted>")));
    QVERIFY(sanitized.contains(QStringLiteral("<path>")));
}

void ApplicationLoggerTest::loggerWritesStructuredSanitizedEntries() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LoggingConfiguration configuration;
    configuration.processName = QStringLiteral("BreezeDesk Test");
    configuration.logDirectory = directory.path();
    configuration.mirrorToStandardError = false;
    ApplicationLogger logger(configuration);
    const auto installed = logger.install();
    const QString installationError = installed ? QString() : installed.error().diagnosticString();
    QVERIFY2(installed, qPrintable(installationError));

    QLoggingCategory category("breezedesk.test.privacy");
    const QString privateMessage =
        QStringLiteral("path=/Users/alice/audio.wav, editedText=confidential words");
    qCWarning(category, "%s", qUtf8Printable(privateMessage));
    logger.uninstall();

    const QByteArray content = readAll(logger.activeLogPath());
    QVERIFY(content.contains("[BreezeDesk_Test:"));
    QVERIFY(content.contains("[warning] [breezedesk.test.privacy]"));
    QVERIFY(content.contains("<path>"));
    QVERIFY(content.contains("editedText=<redacted>"));
    QVERIFY(!content.contains("alice"));
    QVERIFY(!content.contains("confidential"));
    const QRegularExpression timestamp(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T"));
    QVERIFY(timestamp.match(QString::fromUtf8(content)).hasMatch());
}

void ApplicationLoggerTest::loggerRotatesAndRetainsBoundedFiles() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LoggingConfiguration configuration;
    configuration.processName = QStringLiteral("rotation");
    configuration.logDirectory = directory.path();
    configuration.maximumFileBytes = 1024;
    configuration.retainedFileCount = 3;
    configuration.mirrorToStandardError = false;
    ApplicationLogger logger(configuration);
    QVERIFY(logger.install());
    for (int index = 0; index < 30; ++index) {
        qWarning().noquote() << QString(240, QLatin1Char('x')) << index;
    }
    qWarning().noquote() << QString(5'000, QLatin1Char('y'));
    logger.uninstall();

    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("rotation.log"))));
    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("rotation.1.log"))));
    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("rotation.2.log"))));
    QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("rotation.3.log"))));
    QVERIFY(QFileInfo(directory.filePath(QStringLiteral("rotation.log"))).size() <= 1024);
    QVERIFY(QFileInfo(directory.filePath(QStringLiteral("rotation.1.log"))).size() <= 1024);
}

void ApplicationLoggerTest::onlyOneLoggerCanOwnTheQtHandler() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LoggingConfiguration firstConfiguration;
    firstConfiguration.processName = QStringLiteral("first");
    firstConfiguration.logDirectory = directory.path();
    firstConfiguration.mirrorToStandardError = false;
    LoggingConfiguration secondConfiguration = firstConfiguration;
    secondConfiguration.processName = QStringLiteral("second");
    ApplicationLogger first(firstConfiguration);
    ApplicationLogger second(secondConfiguration);
    QVERIFY(first.install());
    const auto rejected = second.install();
    QVERIFY(!rejected);
    first.uninstall();
    QVERIFY(second.install());
    second.uninstall();
}

void ApplicationLoggerTest::janitorRemovesOnlyExpiredApplicationTemporaryFiles() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    EnvironmentGuard environment(root.path().toUtf8());
    QVERIFY(StoragePaths::ensureLayout());
    const QString stale = QDir(StoragePaths::temporary()).filePath(QStringLiteral("stale.tmp"));
    const QString recent = QDir(StoragePaths::temporary()).filePath(QStringLiteral("recent.tmp"));
    const QString protectedFile =
        QDir(StoragePaths::temporary()).filePath(QStringLiteral("active-session.tmp"));
    QVERIFY(writeFile(stale, QByteArrayLiteral("old")));
    QVERIFY(writeFile(recent, QByteArrayLiteral("new")));
    QVERIFY(writeFile(protectedFile, QByteArrayLiteral("active")));

    QFile staleFile(stale);
    QVERIFY(staleFile.open(QIODevice::ReadWrite));
    QVERIFY(staleFile.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2),
                                  QFileDevice::FileModificationTime));
    staleFile.close();
    QFile protectedHandle(protectedFile);
    QVERIFY(protectedHandle.open(QIODevice::ReadWrite));
    QVERIFY(protectedHandle.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2),
                                        QFileDevice::FileModificationTime));
    protectedHandle.close();

    TemporaryCleanupPolicy policy;
    policy.maximumAgeMs = 60LL * 60LL * 1000LL;
    policy.protectedPaths = {protectedFile};
    const TemporaryCleanupReport report = TemporaryFileJanitor::clean(policy);
    QVERIFY2(report.succeeded(), qPrintable(report.error));
    QCOMPARE(report.filesRemoved, 1);
    QCOMPARE(report.bytesReleased, 3);
    QVERIFY(!QFileInfo::exists(stale));
    QVERIFY(QFileInfo::exists(recent));
    QVERIFY(QFileInfo::exists(protectedFile));
}

void ApplicationLoggerTest::janitorRejectsBroadCleanupTargets() {
    TemporaryCleanupPolicy policy;
    policy.directory = QDir::rootPath();
    policy.allowOutsideApplicationTemporaryDirectory = true;
    const TemporaryCleanupReport report = TemporaryFileJanitor::clean(policy);
    QVERIFY(!report.succeeded());
    QVERIFY(!report.error.isEmpty());
    QCOMPARE(report.filesRemoved, 0);
}

QTEST_GUILESS_MAIN(ApplicationLoggerTest)
#include "tst_ApplicationLogger.moc"
