#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/glossary/GlossaryPostProcessor.h"
#include "breezedesk/glossary/GlossarySerializer.h"
#include "breezedesk/glossary/PromptComposer.h"
#include "breezedesk/glossary/SqliteGlossaryRepository.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace BreezeDesk;

class GlossaryTest final : public QObject {
    Q_OBJECT

  private slots:
    void profileAndTermCrud();
    void serializerRoundTripsJsonAndCsv();
    void promptHonorsPriorityAndTokenBudget();
    void explicitAliasesAreAuditableAndReversible();
};

void GlossaryTest::profileAndTermCrud() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteGlossaryRepository repository(database);
    GlossaryProfile profile;
    profile.name = QStringLiteral("Breeze project");
    auto profileId = repository.createProfile(profile);
    if (!profileId)
        QFAIL(qPrintable(profileId.error().diagnosticString()));
    GlossaryTerm term;
    term.profileId = profileId.value();
    term.canonicalText = QStringLiteral("BreezeDesk");
    term.aliases = {QStringLiteral("Breeze Desk")};
    term.priority = 100;
    auto termId = repository.createTerm(term);
    QVERIFY(termId);
    QCOMPARE(repository.terms(profileId.value()).value().size(), 1);
    QVERIFY(repository.setTermsEnabled({termId.value()}, false));
    QCOMPARE(repository.terms(profileId.value()).value().first().enabled, false);
    auto duplicate = repository.duplicateProfile(profileId.value(), QStringLiteral("Copy"));
    QVERIFY(duplicate);
    QCOMPARE(repository.terms(duplicate.value()).value().size(), 1);
    QVERIFY(repository.deleteTerm(termId.value()));
    QVERIFY(repository.deleteProfile(profileId.value()));
}

void GlossaryTest::serializerRoundTripsJsonAndCsv() {
    GlossaryDocument document;
    document.profile.id = QStringLiteral("p");
    document.profile.name = QStringLiteral("Project");
    GlossaryTerm term;
    term.profileId = document.profile.id;
    term.canonicalText = QStringLiteral("Breeze, ASR");
    term.aliases = {QStringLiteral("微風 ASR"), QStringLiteral("Breeze speech")};
    term.priority = 8;
    term.notes = QStringLiteral("quoted \"note\"");
    document.terms.append(term);
    auto json = GlossarySerializer::fromJson(GlossarySerializer::toJson(document));
    QVERIFY(json);
    QCOMPARE(json.value().terms.first().aliases, term.aliases);
    auto csv = GlossarySerializer::termsFromCsv(GlossarySerializer::termsToCsv(document.terms, true),
                                                QStringLiteral("new"));
    QVERIFY(csv);
    QCOMPARE(csv.value().first().canonicalText, term.canonicalText);
    QCOMPARE(csv.value().first().notes, term.notes);
}

void GlossaryTest::promptHonorsPriorityAndTokenBudget() {
    GlossaryTerm high;
    high.id = QStringLiteral("high");
    high.canonicalText = QStringLiteral("BreezeDesk");
    high.priority = 100;
    GlossaryTerm low;
    low.id = QStringLiteral("low");
    low.canonicalText = QStringLiteral("Extremely Long Low Priority Product Name");
    low.priority = 1;
    PromptCompositionRequest request;
    request.terms = {low, high};
    request.maximumTokens = 12;
    PromptComposer composer;
    auto result = composer.compose(
        request, [](const QString& text) { return text.split(QLatin1Char(' '), Qt::SkipEmptyParts).size(); });
    QVERIFY(result);
    QVERIFY(result.value().includedTermIds.contains(high.id));
    QVERIFY(result.value().omittedTermIds.contains(low.id));
    QVERIFY(result.value().tokenCount <= request.maximumTokens);
}

void GlossaryTest::explicitAliasesAreAuditableAndReversible() {
    GlossaryTerm product;
    product.id = QStringLiteral("1");
    product.canonicalText = QStringLiteral("BreezeDesk");
    product.aliases = {QStringLiteral("Breeze Desk")};
    GlossaryTerm chinese;
    chinese.id = QStringLiteral("2");
    chinese.canonicalText = QStringLiteral("聯發科技");
    chinese.aliases = {QStringLiteral("聯發科")};
    GlossaryPostProcessor processor;
    const QString original = QStringLiteral("Breeze Desk 使用聯發科模型，但 MyBreeze Desk 不變");
    auto result = processor.applyExplicitAliases(original, {product, chinese});
    QCOMPARE(result.text, QStringLiteral("BreezeDesk 使用聯發科技模型，但 MyBreeze Desk 不變"));
    QCOMPARE(result.replacements.size(), 2);
    const QJsonArray audit = GlossaryPostProcessor::auditToJson(result.replacements);
    QList<GlossaryReplacement> restored = GlossaryPostProcessor::auditFromJson(audit);
    QCOMPARE(restored.size(), 2);
    auto rendered = processor.renderAudit(original, restored);
    QVERIFY(rendered.has_value());
    QCOMPARE(*rendered, result.text);
    restored[0].applied = false;
    rendered = processor.renderAudit(original, restored);
    QVERIFY(rendered.has_value());
    QCOMPARE(*rendered, QStringLiteral("Breeze Desk 使用聯發科技模型，但 MyBreeze Desk 不變"));
    QCOMPARE(processor.revert(result.text, result.replacements), original);
}

QTEST_GUILESS_MAIN(GlossaryTest)
#include "tst_Glossary.moc"
