#include <breezedesk/cli/CliForwardingPolicy.h>

#include <QtTest/QTest>

using namespace BreezeDesk;

class CliForwardingPolicyTest final : public QObject {
    Q_OBJECT

  private slots:
    void defaultsToGuiForwarding();
    void headlessAlwaysBypassesForwarding();
};

void CliForwardingPolicyTest::defaultsToGuiForwarding() {
    QStringList arguments{QStringLiteral("library"), QStringLiteral("list"), QStringLiteral("--json")};
    QVERIFY(!CliForwardingPolicy::consumeHeadlessFlag(&arguments));
    QCOMPARE(arguments,
             QStringList({QStringLiteral("library"), QStringLiteral("list"), QStringLiteral("--json")}));
}

void CliForwardingPolicyTest::headlessAlwaysBypassesForwarding() {
    QStringList arguments{QStringLiteral("transcribe"), QStringLiteral("會議.wav"),
                          QStringLiteral("--headless"), QStringLiteral("--json"),
                          QStringLiteral("--headless")};
    QVERIFY(CliForwardingPolicy::consumeHeadlessFlag(&arguments));
    QCOMPARE(arguments, QStringList({QStringLiteral("transcribe"), QStringLiteral("會議.wav"),
                                     QStringLiteral("--json")}));
}

QTEST_GUILESS_MAIN(CliForwardingPolicyTest)
#include "tst_CliForwardingPolicy.moc"
