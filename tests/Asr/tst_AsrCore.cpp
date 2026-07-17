#include <breezedesk/asr/CancellationFlag.h>
#include <breezedesk/asr/LongFormChunkPlanner.h>
#include <breezedesk/asr/ModelFileIntegrity.h>
#include <breezedesk/asr/OverlapDeduplicator.h>
#include <breezedesk/asr/PresetRegistry.h>
#include <breezedesk/asr/PromptTokenBudget.h>
#include <breezedesk/asr/SegmentMetrics.h>
#include <breezedesk/asr/StreamingVadSegmenter.h>
#include <breezedesk/asr/WhisperBackendInfo.h>
#include <breezedesk/asr/WhisperParameterMapper.h>
#include <breezedesk/asr/WhisperTranscriptionEngine.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtTest/QTest>

using namespace BreezeDesk::Asr;

class AsrCoreTest final : public QObject {
    Q_OBJECT

  private slots:
    void mapsPresets();
    void budgetsPromptInPriorityOrder();
    void convertsTimestampAndConfidence();
    void cancellationFlagIsAtomicBoundary();
    void plansSilenceBoundedChunks();
    void plansFourHourContinuousSpeechWithOverlap();
    void deduplicatesEnglishAndChinese();
    void retainsAmbiguousOverlap();
    void segmentsStreamingVadProbabilities();
    void disabledRuntimeReturnsTypedError();
    void rejectsChecksumMismatchBeforeModelLoad();
};

void AsrCoreTest::mapsPresets() {
    const auto fast = PresetRegistry::configuration(TranscriptionPreset::Fast);
    QCOMPARE(fast.strategy, SamplingStrategy::Greedy);
    QCOMPARE(fast.bestOf, 1);
    QCOMPARE(fast.temperatureIncrement, 0.0F);
    QVERIFY(fast.noContext);

    const auto balanced = PresetRegistry::configuration(TranscriptionPreset::Balanced);
    QCOMPARE(balanced.bestOf, 2);
    QCOMPARE(balanced.temperatureIncrement, 0.2F);
    QVERIFY(!balanced.noContext);

    const auto accurate = PresetRegistry::configuration(TranscriptionPreset::Accurate);
    QCOMPARE(accurate.strategy, SamplingStrategy::BeamSearch);
    QCOMPARE(accurate.beamSize, 5);

    TranscriptionOptions options;
    options.language = QStringLiteral("auto");
    options.preset = TranscriptionPreset::Accurate;
    options.threadCount = 0;
    const auto mapped = WhisperParameterMapper::map(options, 7);
    QVERIFY(mapped.detectLanguage);
    QCOMPARE(mapped.threadCount, 7);
    QCOMPARE(mapped.beamSize, 5);
    QVERIFY(!mapped.translate);
    QVERIFY(!mapped.noTimestamps);
}

void AsrCoreTest::budgetsPromptInPriorityOrder() {
    QList<PromptPart> parts{
        {QStringLiteral("previous tail"), PromptPartKind::PreviousTranscript, 100},
        {QStringLiteral("BreezeDesk"), PromptPartKind::Glossary, 10},
        {QStringLiteral("meeting context"), PromptPartKind::MeetingContext, 100},
        {QStringLiteral("lower term"), PromptPartKind::Glossary, 1},
    };
    const auto countCharacters = [](const QString& text) { return text.size(); };
    const QString first = QStringLiteral("Important terminology: BreezeDesk.");
    const auto result =
        PromptTokenBudget::compose(parts, static_cast<int>(first.size()) + 1, countCharacters);
    QCOMPARE(result.prompt, first);
    QCOMPARE(result.tokenCount, first.size());
    QCOMPARE(result.omitted.size(), 3);
}

void AsrCoreTest::convertsTimestampAndConfidence() {
    QCOMPARE(SegmentMetrics::timestampTicksToMilliseconds(123, 4'000), 5'230);
    const auto confidence = SegmentMetrics::confidence({0.9F, 0.7F, 0.5F}, 0.45F);
    QCOMPARE(confidence.averageProbability, 0.7F);
    QCOMPARE(confidence.minimumProbability, 0.5F);
    QVERIFY(!confidence.lowConfidence);
    QVERIFY(SegmentMetrics::confidence({0.9F, 0.2F}, 0.45F).lowConfidence);
    QVERIFY(SegmentMetrics::confidence({}, 0.45F).lowConfidence);
}

void AsrCoreTest::cancellationFlagIsAtomicBoundary() {
    CancellationFlag flag;
    QVERIFY(!flag.isRequested());
    flag.request();
    QVERIFY(flag.isRequested());
    flag.reset();
    QVERIFY(!flag.isRequested());
}

void AsrCoreTest::plansSilenceBoundedChunks() {
    constexpr qint64 minute = 60 * 1000;
    LongFormChunkPlanner planner;
    const QList<SpeechRegion> speech{
        {0, 9 * minute + 30'000},
        {10 * minute + 30'000, 19 * minute + 30'000},
        {20 * minute + 30'000, 30 * minute},
    };
    const auto chunks = planner.plan(30 * minute, speech);
    QCOMPARE(chunks.size(), 3);
    QCOMPARE(chunks.at(0).endMs, 10 * minute);
    QCOMPARE(chunks.at(1).endMs, 20 * minute);
    QCOMPARE(chunks.at(0).overlapAfterMs, 0);
    QCOMPARE(chunks.at(1).startMs, chunks.at(0).endMs);
}

void AsrCoreTest::plansFourHourContinuousSpeechWithOverlap() {
    constexpr qint64 minute = 60 * 1000;
    const qint64 duration = 4 * 60 * minute;
    LongFormChunkPlanner planner;
    const auto chunks = planner.plan(duration, {{0, duration}});
    QVERIFY(chunks.size() > 16);
    QCOMPARE(chunks.constFirst().startMs, 0);
    QCOMPARE(chunks.constLast().endMs, duration);
    for (qsizetype index = 0; index < chunks.size(); ++index) {
        const auto& chunk = chunks.at(index);
        QVERIFY(chunk.endMs > chunk.startMs);
        QVERIFY(chunk.endMs - chunk.startMs <= 15 * minute);
        if (index + 1 < chunks.size()) {
            QCOMPARE(chunk.overlapAfterMs, 900);
            QCOMPARE(chunks.at(index + 1).overlapBeforeMs, 900);
            QCOMPARE(chunks.at(index + 1).startMs, chunk.endMs - 900);
        }
    }
}

void AsrCoreTest::deduplicatesEnglishAndChinese() {
    const auto english = OverlapDeduplicator::deduplicate(QStringLiteral("We discussed the release plan"),
                                                          QStringLiteral("release plan and testing"));
    QCOMPARE(english.text, QStringLiteral("and testing"));
    QCOMPARE(english.matchedUnits, 2);

    const auto chinese = OverlapDeduplicator::deduplicate(QStringLiteral("今天討論產品發布"),
                                                          QStringLiteral("產品發布，還有測試"));
    QCOMPARE(chinese.text, QStringLiteral("還有測試"));
    QCOMPARE(chinese.matchedUnits, 4);
}

void AsrCoreTest::retainsAmbiguousOverlap() {
    const QString incoming = QStringLiteral("品需要測試");
    const auto result = OverlapDeduplicator::deduplicate(QStringLiteral("新產品"), incoming);
    QCOMPARE(result.text, incoming);
    QCOMPARE(result.matchedUnits, 0);
    QVERIFY(result.ambiguous);
    QVERIFY(!result.diagnostic.isEmpty());
}

void AsrCoreTest::segmentsStreamingVadProbabilities() {
    StreamingVadConfiguration configuration;
    configuration.probabilityFrameMs = 100;
    configuration.minimumSpeechMs = 200;
    configuration.minimumSilenceMs = 200;
    configuration.speechPaddingMs = 50;
    StreamingVadSegmenter segmenter(configuration);
    segmenter.appendProbabilities(
        {0.1F, 0.8F, 0.9F, 0.8F, 0.1F, 0.1F, 0.1F, 0.9F, 0.9F, 0.9F, 0.1F, 0.1F, 0.1F});
    const auto regions = segmenter.finish(1'300);
    QCOMPARE(regions.size(), 2);
    QCOMPARE(regions.at(0).startMs, 50);
    QCOMPARE(regions.at(0).endMs, 450);
    QCOMPARE(regions.at(1).startMs, 650);
    QCOMPARE(regions.at(1).endMs, 1'050);
}

void AsrCoreTest::disabledRuntimeReturnsTypedError() {
    if (WhisperBackendInfo::runtimeAvailable()) {
        QSKIP("Runtime-enabled model loading is covered by the optional model integration test", 0);
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("fixture.bin"));
    const QByteArray payload("model-integrity-fixture");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(payload), payload.size());
    file.close();
    ModelLoadOptions options;
    options.modelPath = path;
    options.expectedSha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    WhisperTranscriptionEngine engine;
    const auto result = engine.loadModel(options);
    QCOMPARE(result.error.code, AsrErrorCode::RuntimeUnavailable);
}

void AsrCoreTest::rejectsChecksumMismatchBeforeModelLoad() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("corrupt-model.bin"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArrayLiteral("not-the-trusted-model")), 21);
    file.close();

    QByteArray actual;
    QCOMPARE(ModelFileIntegrity::verifySha256(path, {}).code, AsrErrorCode::InvalidRequest);
    const AsrError integrity = ModelFileIntegrity::verifySha256(path, QByteArray(64, '0'), &actual);
    QCOMPARE(integrity.code, AsrErrorCode::ModelChecksumMismatch);
    QCOMPARE(actual.size(), 64);

    ModelLoadOptions options;
    options.modelPath = path;
    options.expectedSha256 = QByteArray(64, '0');
    WhisperTranscriptionEngine engine;
    const auto result = engine.loadModel(options);
    QCOMPARE(result.error.code, AsrErrorCode::ModelChecksumMismatch);
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    AsrCoreTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_AsrCore.moc"
