#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/DatabaseSearchService.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/glossary/GlossaryPostProcessor.h"
#include "breezedesk/jobs/SqliteJobRepository.h"
#include "breezedesk/transcript/SqliteTranscriptRepository.h"
#include "breezedesk/transcript/TranscriptAutosave.h"
#include "breezedesk/transcript/TranscriptEditor.h"
#include "breezedesk/transcript/TranscriptExporter.h"
#include "breezedesk/ui/TranscriptViewModel.h"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace BreezeDesk;

class TranscriptTest final : public QObject {
    Q_OBJECT

  private slots:
    void editingSupportsSplitMergeAndUndo();
    void invalidTimeOverlapIsRejected();
    void allExportFormatsAreValid();
    void repositoryKeepsRevisionsAndAutosaves();
    void viewModelPreservesMetadataAndControlsGlossaryAudit();
};

static QList<TranscriptSegment> fixtureSegments() {
    TranscriptSegment first;
    first.id = QStringLiteral("s1");
    first.startMs = 0;
    first.endMs = 2'000;
    first.originalText = QStringLiteral("Hello BreezeDesk");
    first.editedText = first.originalText;
    first.averageProbability = .9;
    TranscriptSegment second;
    second.id = QStringLiteral("s2");
    second.ordinal = 1;
    second.startMs = 2'200;
    second.endMs = 4'000;
    second.originalText = QStringLiteral("這是測試");
    second.editedText = second.originalText;
    return {first, second};
}

void TranscriptTest::editingSupportsSplitMergeAndUndo() {
    TranscriptEditor editor;
    editor.setSegments(fixtureSegments());
    QVERIFY(editor.editText(0, QStringLiteral("Hello Breeze Desk")));
    QVERIFY(editor.split(0, 1'000, 5));
    QCOMPARE(editor.segments().size(), 3);
    QVERIFY(editor.mergeWithNext(0));
    QCOMPARE(editor.segments().size(), 2);
    QVERIFY(editor.undo());
    QCOMPARE(editor.segments().size(), 3);
    QVERIFY(editor.redo());
    QCOMPARE(editor.segments().size(), 2);
    QVERIFY(editor.validate());
}

void TranscriptTest::invalidTimeOverlapIsRejected() {
    TranscriptEditor editor;
    editor.setSegments(fixtureSegments());
    QVERIFY(!editor.setTimeRange(1, 1'500, 4'000));
    QCOMPARE(editor.segments().at(1).startMs, 2'200);
}

void TranscriptTest::allExportFormatsAreValid() {
    TranscriptExportMetadata metadata;
    metadata.recordingId = QStringLiteral("rec");
    metadata.title = QStringLiteral("會議");
    metadata.modelId = QStringLiteral("breeze-q5");
    metadata.language = QStringLiteral("zh");
    for (const auto format :
         {TranscriptExportFormat::Txt, TranscriptExportFormat::Markdown, TranscriptExportFormat::Srt,
          TranscriptExportFormat::Vtt, TranscriptExportFormat::Json, TranscriptExportFormat::Csv}) {
        auto rendered = TranscriptExporter::render(format, metadata, fixtureSegments());
        QVERIFY(rendered);
        QVERIFY(!rendered.value().isEmpty());
        if (format == TranscriptExportFormat::Vtt)
            QVERIFY(rendered.value().startsWith("WEBVTT"));
        if (format == TranscriptExportFormat::Srt)
            QVERIFY(rendered.value().contains("00:00:00,000 --> 00:00:02,000"));
        if (format == TranscriptExportFormat::Json) {
            const QJsonDocument json = QJsonDocument::fromJson(rendered.value());
            QCOMPARE(json.object().value(QStringLiteral("schemaVersion")).toInt(), 1);
            QCOMPARE(json.object().value(QStringLiteral("segments")).toArray().size(), 2);
        }
    }
}

void TranscriptTest::repositoryKeepsRevisionsAndAutosaves() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordingRepository(database);
    Recording recording;
    recording.id = QStringLiteral("rec");
    recording.title = QStringLiteral("Meeting");
    const auto createRecording = recordingRepository.create(recording);
    if (!createRecording)
        QFAIL(qPrintable(createRecording.error().diagnosticString()));
    SqliteJobRepository jobRepository(database);
    TranscriptionJob firstJob;
    firstJob.id = QStringLiteral("job-1");
    firstJob.recordingId = recording.id;
    QVERIFY(jobRepository.create(firstJob));
    TranscriptionJob secondJob = firstJob;
    secondJob.id = QStringLiteral("job-2");
    secondJob.revisionNumber = 2;
    QVERIFY(jobRepository.create(secondJob));
    SqliteTranscriptRepository repository(database);
    auto first = fixtureSegments();
    first[0].minimumProbability = .72;
    first[0].noSpeechProbability = .04;
    first[0].lowConfidence = true;
    first[0].reviewed = true;
    first[0].attempt = 3;
    first[0].createdAt = QDateTime::fromString(QStringLiteral("2026-07-17T01:02:03.000Z"), Qt::ISODateWithMs);
    GlossaryReplacement persistedReplacement;
    persistedReplacement.termId = QStringLiteral("term-1");
    persistedReplacement.alias = QStringLiteral("Breeze Desk");
    persistedReplacement.canonicalText = QStringLiteral("BreezeDesk");
    persistedReplacement.originalText = QStringLiteral("Breeze Desk");
    persistedReplacement.start = 0;
    persistedReplacement.length = persistedReplacement.canonicalText.size();
    first[0].replacementAudit = GlossaryPostProcessor::auditToJson({persistedReplacement});
    QVERIFY(repository.replaceRevision(recording.id, firstJob.id, first));
    const auto persisted = repository.segmentsForJob(firstJob.id).value().first();
    QCOMPARE(persisted.minimumProbability, .72);
    QCOMPARE(persisted.noSpeechProbability, .04);
    QVERIFY(persisted.lowConfidence);
    QVERIFY(persisted.reviewed);
    QCOMPARE(persisted.attempt, 3);
    QCOMPARE(persisted.createdAt, first[0].createdAt);
    QCOMPARE(persisted.replacementAudit, first[0].replacementAudit);
    const auto locatedSearch = DatabaseSearchService(database).search(QStringLiteral("BreezeDesk"));
    QVERIFY(locatedSearch);
    QCOMPARE(locatedSearch.value().size(), 1);
    QVERIFY(!locatedSearch.value().first().segmentId.isEmpty());
    QCOMPARE(locatedSearch.value().first().startMs, 0);
    auto second = fixtureSegments();
    second[0].id = QStringLiteral("r2-s1");
    second[1].id = QStringLiteral("r2-s2");
    second[0].originalText = QStringLiteral("New recognition");
    QVERIFY(repository.replaceRevision(recording.id, secondJob.id, second));
    QCOMPARE(repository.segmentsForJob(firstJob.id).value().first().originalText,
             QStringLiteral("Hello BreezeDesk"));
    TranscriptSegment edited = repository.segmentsForJob(firstJob.id).value().first();
    edited.editedText = QStringLiteral("Manually edited");
    edited.reviewed = false;
    TranscriptAutosave autosave(repository, 500);
    autosave.schedule(edited);
    QVERIFY(autosave.flush());
    QCOMPARE(repository.segment(edited.id).value()->editedText, QStringLiteral("Manually edited"));
    QVERIFY(!repository.segment(edited.id).value()->reviewed);
    QVERIFY(!repository.replaceRevision(recording.id, firstJob.id, fixtureSegments()));
    QVERIFY(recordingRepository.setActiveTranscriptJob(recording.id, secondJob.id));
    QCOMPARE(recordingRepository.findById(recording.id).value()->activeJobId, secondJob.id);
}

void TranscriptTest::viewModelPreservesMetadataAndControlsGlossaryAudit() {
    GlossaryTerm term;
    term.id = QStringLiteral("term");
    term.canonicalText = QStringLiteral("BreezeDesk");
    term.aliases = {QStringLiteral("Breeze Desk")};
    const QString original = QStringLiteral("Breeze Desk meeting");
    const auto processed = GlossaryPostProcessor().applyExplicitAliases(original, {term});

    TranscriptSegmentModel::Segment segment;
    segment.id = QStringLiteral("segment");
    segment.recordingId = QStringLiteral("recording");
    segment.jobId = QStringLiteral("job");
    segment.chunkId = QStringLiteral("chunk");
    segment.startMs = 1'000;
    segment.endMs = 2'500;
    segment.originalText = original;
    segment.editedText = processed.text;
    segment.averageProbability = .91;
    segment.minimumProbability = .73;
    segment.noSpeechProbability = .02;
    segment.lowConfidence = false;
    segment.reviewed = true;
    segment.replacementAudit = GlossaryPostProcessor::auditToJson(processed.replacements);
    segment.provisional = true;
    segment.attempt = 4;
    segment.createdAt = QDateTime::currentDateTimeUtc().addSecs(-10);
    segment.updatedAt = QDateTime::currentDateTimeUtc();

    TranscriptViewModel viewModel;
    viewModel.replaceSegments({segment});
    viewModel.setGlossaryReplacementApplied(0, 0, false);
    auto snapshot = viewModel.snapshot();
    QCOMPARE(snapshot.first().editedText, original);
    QVERIFY(!GlossaryPostProcessor::auditFromJson(snapshot.first().replacementAudit).first().applied);
    QCOMPARE(snapshot.first().chunkId, segment.chunkId);
    QCOMPARE(snapshot.first().averageProbability, segment.averageProbability);
    QCOMPARE(snapshot.first().minimumProbability, segment.minimumProbability);
    QCOMPARE(snapshot.first().noSpeechProbability, segment.noSpeechProbability);
    QCOMPARE(snapshot.first().provisional, segment.provisional);
    QCOMPARE(snapshot.first().attempt, segment.attempt);
    QVERIFY(viewModel.canUndo());

    viewModel.undo();
    snapshot = viewModel.snapshot();
    QCOMPARE(snapshot.first().editedText, processed.text);
    QVERIFY(GlossaryPostProcessor::auditFromJson(snapshot.first().replacementAudit).first().applied);
    viewModel.redo();
    QCOMPARE(viewModel.snapshot().first().editedText, original);

    viewModel.editText(0, QStringLiteral("Manual text"));
    QSignalSpy validationSpy(&viewModel, &TranscriptViewModel::validationError);
    viewModel.setGlossaryReplacementApplied(0, 0, true);
    QCOMPARE(validationSpy.size(), 1);
    QCOMPARE(viewModel.snapshot().first().editedText, QStringLiteral("Manual text"));

    TranscriptSegmentModel::Segment filteredOut = segment;
    filteredOut.id = QStringLiteral("filtered-out");
    filteredOut.originalText = QStringLiteral("Unrelated text");
    filteredOut.editedText = filteredOut.originalText;
    TranscriptSegmentModel::Segment filteredIn = segment;
    filteredIn.id = QStringLiteral("filtered-in");
    filteredIn.startMs = 3'000;
    filteredIn.endMs = 4'000;
    filteredIn.originalText = QStringLiteral("Needle result");
    filteredIn.editedText = filteredIn.originalText;

    TranscriptViewModel filteredViewModel;
    filteredViewModel.replaceSegments({filteredOut, filteredIn});
    filteredViewModel.setSelectedIndex(1);
    filteredViewModel.updatePlaybackPosition(3'500);
    filteredViewModel.setSearchText(QStringLiteral("Needle"));
    QAbstractItemModel* filteredModel = filteredViewModel.segments();
    QCOMPARE(filteredModel->rowCount(), 1);
    QCOMPARE(filteredViewModel.visibleSegmentCount(), 1);
    QCOMPARE(filteredViewModel.selectedIndex(), 0);
    QCOMPARE(filteredViewModel.activePlaybackIndex(), 0);
    QCOMPARE(filteredModel->roleNames().value(TranscriptFilterProxyModel::ProxyRowRole),
             QByteArrayLiteral("proxyRow"));
    QCOMPARE(filteredModel->data(filteredModel->index(0, 0), TranscriptFilterProxyModel::ProxyRowRole)
                 .toInt(),
             0);
    filteredViewModel.editText(0, QStringLiteral("Edited filtered result"));
    QCOMPARE(filteredViewModel.snapshot().at(0).editedText, filteredOut.editedText);
    QCOMPARE(filteredViewModel.snapshot().at(1).editedText, QStringLiteral("Edited filtered result"));
    filteredViewModel.setSearchText(QStringLiteral("Unrelated"));
    QCOMPARE(filteredViewModel.visibleSegmentCount(), 1);
    QCOMPARE(filteredViewModel.selectedIndex(), -1);
    QCOMPARE(filteredViewModel.activePlaybackIndex(), -1);
    QCOMPARE(filteredViewModel.findPrevious(12), 0);
}

QTEST_GUILESS_MAIN(TranscriptTest)
#include "tst_Transcript.moc"
