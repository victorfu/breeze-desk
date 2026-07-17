#include "breezedesk/core/Result.h"
#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/core/TextUtils.h"
#include "breezedesk/core/TimeUtils.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace BreezeDesk;

class CoreUtilitiesTest final : public QObject {
    Q_OBJECT

  private slots:
    void resultCarriesTypedErrors();
    void textNormalizationHandlesChineseAndEnglish();
    void fileNamesAndCsvAreSafe();
    void clockFormattingUsesMilliseconds();
    void storageLayoutSupportsAnExplicitTestRoot();
};

void CoreUtilitiesTest::resultCarriesTypedErrors() {
    auto result = Result<int>::failure(
        UserFacingError::validation(ErrorCode::InvalidArgument, QStringLiteral("bad value")));
    QVERIFY(!result);
    QCOMPARE(result.error().domain, ErrorDomain::Validation);
    QCOMPARE(result.error().code, ErrorCode::InvalidArgument);
    QVERIFY(result.error().diagnosticString().contains(QStringLiteral("InvalidArgument")));
}

void CoreUtilitiesTest::textNormalizationHandlesChineseAndEnglish() {
    QCOMPARE(TextUtils::normalizedForMatching(QStringLiteral("  Breeze—ASR，測試！ ")),
             QStringLiteral("breeze asr 測試"));
    QCOMPARE(TextUtils::wordAndCharacterTokens(QStringLiteral("Breeze ASR測試")),
             QStringList({QStringLiteral("breeze"), QStringLiteral("asr"), QStringLiteral("測"),
                          QStringLiteral("試")}));
}

void CoreUtilitiesTest::fileNamesAndCsvAreSafe() {
    QCOMPARE(TextUtils::sanitizedFileName(QStringLiteral(" meeting: 01?.txt ")),
             QStringLiteral("meeting_ 01_.txt"));
    QCOMPARE(TextUtils::csvField(QStringLiteral("a,\"b\"")), QStringLiteral("\"a,\"\"b\"\"\""));
}

void CoreUtilitiesTest::clockFormattingUsesMilliseconds() {
    QCOMPARE(TimeUtils::formatClock(3'723'045), QStringLiteral("01:02:03.045"));
    QCOMPARE(TimeUtils::formatClock(-1), QStringLiteral("00:00:00.000"));
}

void CoreUtilitiesTest::storageLayoutSupportsAnExplicitTestRoot() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray previous = qgetenv("BREEZEDESK_DATA_ROOT");
    qputenv("BREEZEDESK_DATA_ROOT", directory.filePath(QStringLiteral("Breeze Data")).toUtf8());
    QString error;
    QVERIFY2(StoragePaths::ensureLayout(&error), qPrintable(error));
    QVERIFY(QFileInfo(StoragePaths::models()).isDir());
    QVERIFY(QFileInfo(StoragePaths::cache()).isDir());
    QVERIFY(StoragePaths::databaseFile().startsWith(directory.path()));
    if (previous.isNull()) {
        qunsetenv("BREEZEDESK_DATA_ROOT");
    } else {
        qputenv("BREEZEDESK_DATA_ROOT", previous);
    }
}

QTEST_GUILESS_MAIN(CoreUtilitiesTest)
#include "tst_CoreUtilities.moc"
