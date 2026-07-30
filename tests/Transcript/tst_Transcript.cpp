#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/DatabaseSearchService.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/glossary/GlossaryPostProcessor.h"
#include "breezedesk/jobs/JobQueue.h"
#include "breezedesk/jobs/SqliteJobRepository.h"
#include "breezedesk/transcript/SqliteTranscriptRepository.h"
#include "breezedesk/transcript/TranscriptAutosave.h"
#include "breezedesk/transcript/TranscriptEditor.h"
#include "breezedesk/transcript/TranscriptExporter.h"
#include "breezedesk/ui/TranscriptViewModel.h"

#include <QJsonDocument>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace BreezeDesk;

class TranscriptTest final : public QObject {
    Q_OBJECT

  private slots:
    void editingSupportsSplitMergeAndUndo();
    void invalidTimeOverlapIsRejected();
    void allExportFormatsAreValid();
    void repositoryPersistsTranscriptAndAutosaves();
    void activeTranscriptRequiresCompletedJobAndRefreshesSearch();
    void repositoryDistinguishesGlossaryReplacementsFromManualEdits();
    void replaceChunkRejectsCrossLinkedTargetsWithoutMutation();
    void ownerlessEditsRejectActiveExecutionWithoutMutation();
    void staleOwnerCannotReplaceReclaimedChunkTranscript();
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

void TranscriptTest::repositoryPersistsTranscriptAndAutosaves() {
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
    const auto databaseConnection = database.connection();
    QVERIFY(databaseConnection);
    QSqlQuery completeFirstJob(databaseConnection.value());
    completeFirstJob.prepare(QStringLiteral(
        "UPDATE transcription_jobs SET state='Completed',stage='Completed',progress=1 WHERE id=?"));
    completeFirstJob.addBindValue(firstJob.id);
    QVERIFY(completeFirstJob.exec());
    QVERIFY(recordingRepository.setActiveTranscriptJob(recording.id, firstJob.id));
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
    QVERIFY(repository.replaceTranscript(recording.id, firstJob.id, first));
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
    TranscriptSegment edited = repository.segmentsForJob(firstJob.id).value().first();
    edited.editedText = QStringLiteral("Manually edited");
    edited.reviewed = false;
    TranscriptAutosave autosave(repository, 500);
    autosave.schedule(edited);
    QVERIFY(autosave.flush());
    QCOMPARE(repository.segment(edited.id).value()->editedText, QStringLiteral("Manually edited"));
    QVERIFY(!repository.segment(edited.id).value()->reviewed);
    QVERIFY(!repository.replaceTranscript(recording.id, firstJob.id, fixtureSegments()));
    QCOMPARE(recordingRepository.findById(recording.id).value()->activeJobId, firstJob.id);
}

void TranscriptTest::activeTranscriptRequiresCompletedJobAndRefreshesSearch() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordings(database);
    SqliteJobRepository jobs(database);
    SqliteTranscriptRepository transcripts(database);

    Recording recording;
    recording.id = QStringLiteral("active-transcript-recording");
    recording.title = QStringLiteral("Active transcript invariant");
    QVERIFY(recordings.create(recording));
    TranscriptionJob job;
    job.id = QStringLiteral("active-transcript-job");
    job.recordingId = recording.id;
    QVERIFY(jobs.createQueued(job));

    QList<TranscriptSegment> segments = fixtureSegments();
    segments[0].originalText = QStringLiteral("ActivationIndexNeedle");
    segments[0].editedText = segments[0].originalText;
    QVERIFY(transcripts.replaceTranscript(recording.id, job.id, segments));
    const auto beforeActivation =
        DatabaseSearchService(database).search(QStringLiteral("ActivationIndexNeedle"));
    QVERIFY(beforeActivation);
    QVERIFY(beforeActivation.value().isEmpty());

    const auto queuedActivation = recordings.setActiveTranscriptJob(recording.id, job.id);
    QVERIFY(!queuedActivation);
    QCOMPARE(queuedActivation.error().code, ErrorCode::InvalidStateTransition);

    const QString owner = QStringLiteral("active-transcript-owner");
    QVERIFY(jobs.claimQueued(job.id, owner, 10'000).value().claimed);
    const auto runningActivation = recordings.setActiveTranscriptJob(recording.id, job.id);
    QVERIFY(!runningActivation);
    QCOMPARE(runningActivation.error().code, ErrorCode::InvalidStateTransition);

    const auto connection = database.connection();
    QVERIFY(connection);
    QSqlQuery complete(connection.value());
    complete.prepare(QStringLiteral(
        "UPDATE transcription_jobs SET state='Completed',stage='Completed',progress=1 WHERE id=?"));
    complete.addBindValue(job.id);
    QVERIFY(complete.exec());
    QSqlQuery release(connection.value());
    release.prepare(QStringLiteral("DELETE FROM asr_execution_lease WHERE job_id=?"));
    release.addBindValue(job.id);
    QVERIFY(release.exec());

    QVERIFY(recordings.setActiveTranscriptJob(recording.id, job.id));
    QCOMPARE(recordings.findById(recording.id).value()->activeJobId, job.id);
    const auto afterActivation =
        DatabaseSearchService(database).search(QStringLiteral("ActivationIndexNeedle"));
    QVERIFY(afterActivation);
    QCOMPARE(afterActivation.value().size(), 1);
    QCOMPARE(afterActivation.value().constFirst().recordingId, recording.id);

    Recording otherRecording;
    otherRecording.id = QStringLiteral("other-active-transcript-recording");
    otherRecording.title = QStringLiteral("Other recording");
    QVERIFY(recordings.create(otherRecording));
    TranscriptionJob otherJob;
    otherJob.id = QStringLiteral("other-active-transcript-job");
    otherJob.recordingId = otherRecording.id;
    QVERIFY(jobs.createQueued(otherJob));
    QSqlQuery completeOther(connection.value());
    completeOther.prepare(QStringLiteral(
        "UPDATE transcription_jobs SET state='Completed',stage='Completed',progress=1 WHERE id=?"));
    completeOther.addBindValue(otherJob.id);
    QVERIFY(completeOther.exec());
    const auto mismatchedActivation =
        recordings.setActiveTranscriptJob(recording.id, otherJob.id);
    QVERIFY(!mismatchedActivation);
    QCOMPARE(mismatchedActivation.error().code, ErrorCode::NotFound);
    QCOMPARE(recordings.findById(recording.id).value()->activeJobId, job.id);
}

void TranscriptTest::repositoryDistinguishesGlossaryReplacementsFromManualEdits() {
    QTemporaryDir directory;
    DatabaseManager database({directory.filePath(QStringLiteral("library.sqlite"))});
    QVERIFY(database.initialize());
    SqliteRecordingRepository recordingRepository(database);
    Recording recording;
    recording.id = QStringLiteral("recording-glossary-partial");
    recording.title = QStringLiteral("Glossary partials");
    QVERIFY(recordingRepository.create(recording));

    SqliteJobRepository jobRepository(database);
    TranscriptionJob job;
    job.id = QStringLiteral("job-glossary-partial");
    job.recordingId = recording.id;
    QVERIFY(jobRepository.create(job));
    JobChunk chunk;
    chunk.id = QStringLiteral("chunk-1");
    chunk.jobId = job.id;
    chunk.ordinal = 0;
    chunk.startMs = 0;
    chunk.endMs = 1'000;
    QVERIFY(jobRepository.replaceChunks(job.id, {chunk}));
    const QString owner = QStringLiteral("glossary-transcript-owner");
    QVERIFY(jobRepository.claimQueued(job.id, owner).value().claimed);

    GlossaryTerm term;
    term.id = QStringLiteral("term-breezedesk");
    term.canonicalText = QStringLiteral("BreezeDesk");
    term.aliases = {QStringLiteral("Breeze Desk")};
    const QString originalText = QStringLiteral("Breeze Desk meeting");
    const GlossaryPostProcessResult processed =
        GlossaryPostProcessor().applyExplicitAliases(originalText, {term});
    QCOMPARE(processed.text, QStringLiteral("BreezeDesk meeting"));
    QCOMPARE(processed.replacements.size(), 1);

    TranscriptSegment automatic;
    automatic.id = QStringLiteral("segment-glossary-partial");
    automatic.recordingId = recording.id;
    automatic.jobId = job.id;
    automatic.chunkId = QStringLiteral("chunk-1");
    automatic.startMs = 0;
    automatic.endMs = 1'000;
    automatic.originalText = originalText;
    automatic.editedText = processed.text;
    automatic.replacementAudit = GlossaryPostProcessor::auditToJson(processed.replacements);

    SqliteTranscriptRepository repository(database);
    QVERIFY(repository.replaceChunk(recording.id, job.id, automatic.chunkId, {automatic}, true, 1,
                                    owner));
    QVERIFY(repository.replaceChunk(recording.id, job.id, automatic.chunkId, {automatic}, true, 1,
                                    owner));
    QVERIFY(repository.replaceChunk(recording.id, job.id, automatic.chunkId, {automatic}, false, 1,
                                    owner));
    const TranscriptSegment finalized = repository.segmentsForJob(job.id).value().first();
    QCOMPARE(finalized.editedText, processed.text);
    QVERIFY(!finalized.provisional);
    QVERIFY(jobRepository.transition(job.id, JobState::Interrupted, {}, {}, owner));

    TranscriptSegment manual = finalized;
    manual.editedText = QStringLiteral("Manually corrected meeting");
    QVERIFY(repository.saveEditedSegment(manual));
    QVERIFY(JobQueue(jobRepository).resume(job.id));
    QVERIFY(jobRepository.claimQueued(job.id, owner).value().claimed);
    const auto rejected =
        repository.replaceChunk(recording.id, job.id, automatic.chunkId, {automatic}, true, 2, owner);
    QVERIFY(!rejected);
    QCOMPARE(rejected.error().code, ErrorCode::InvalidStateTransition);
    QCOMPARE(repository.segment(manual.id).value()->editedText,
             QStringLiteral("Manually corrected meeting"));
}

void TranscriptTest::replaceChunkRejectsCrossLinkedTargetsWithoutMutation() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager databaseA({path});
    DatabaseManager databaseB({path});
    QVERIFY(databaseA.initialize());
    QVERIFY(databaseB.initialize());
    SqliteRecordingRepository recordings(databaseA);
    SqliteJobRepository jobs(databaseA);
    SqliteTranscriptRepository transcriptsA(databaseA);
    SqliteTranscriptRepository transcriptsB(databaseB);

    Recording recordingA;
    recordingA.id = QStringLiteral("linked-recording-a");
    recordingA.title = QStringLiteral("Recording A");
    QVERIFY(recordings.create(recordingA));
    Recording recordingB;
    recordingB.id = QStringLiteral("linked-recording-b");
    recordingB.title = QStringLiteral("Recording B");
    QVERIFY(recordings.create(recordingB));

    TranscriptionJob jobA;
    jobA.id = QStringLiteral("linked-job-a");
    jobA.recordingId = recordingA.id;
    QVERIFY(jobs.createQueued(jobA));
    TranscriptionJob jobB;
    jobB.id = QStringLiteral("linked-job-b");
    jobB.recordingId = recordingB.id;
    QVERIFY(jobs.createQueued(jobB));
    JobChunk chunkA;
    chunkA.id = QStringLiteral("linked-chunk-a");
    chunkA.jobId = jobA.id;
    chunkA.startMs = 0;
    chunkA.endMs = 1'000;
    QVERIFY(jobs.replaceChunks(jobA.id, {chunkA}));
    JobChunk chunkB = chunkA;
    chunkB.id = QStringLiteral("linked-chunk-b");
    chunkB.jobId = jobB.id;
    QVERIFY(jobs.replaceChunks(jobB.id, {chunkB}));

    const QString owner = QStringLiteral("linked-owner-a");
    QVERIFY(jobs.claimQueued(jobA.id, owner, 10'000).value().claimed);
    TranscriptSegment original;
    original.id = QStringLiteral("linked-segment-original");
    original.recordingId = recordingA.id;
    original.jobId = jobA.id;
    original.chunkId = chunkA.id;
    original.startMs = 0;
    original.endMs = 1'000;
    original.originalText = QStringLiteral("durable original");
    original.editedText = original.originalText;
    QVERIFY(transcriptsA.replaceChunk(recordingA.id, jobA.id, chunkA.id, {original}, false, 1,
                                      owner));
    const auto beforeResult = transcriptsB.segmentsForJob(jobA.id, true);
    QVERIFY(beforeResult);
    QCOMPARE(beforeResult.value().size(), 1);
    const TranscriptSegment before = beforeResult.value().constFirst();

    TranscriptSegment spoof = original;
    spoof.id = QStringLiteral("linked-segment-spoof");
    spoof.originalText = QStringLiteral("spoofed replacement");
    spoof.editedText = spoof.originalText;
    const auto wrongRecording = transcriptsB.replaceChunk(
        recordingB.id, jobA.id, chunkA.id, {spoof}, false, 2, owner);
    QVERIFY(!wrongRecording);
    QCOMPARE(wrongRecording.error().code, ErrorCode::InvalidArgument);
    const auto wrongChunk = transcriptsB.replaceChunk(recordingA.id, jobA.id, chunkB.id, {spoof},
                                                       false, 2, owner);
    QVERIFY(!wrongChunk);
    QCOMPARE(wrongChunk.error().code, ErrorCode::InvalidArgument);

    const auto durable = transcriptsB.segmentsForJob(jobA.id, true);
    QVERIFY(durable);
    QCOMPARE(durable.value().size(), 1);
    const TranscriptSegment after = durable.value().constFirst();
    QCOMPARE(after.id, before.id);
    QCOMPARE(after.recordingId, before.recordingId);
    QCOMPARE(after.jobId, before.jobId);
    QCOMPARE(after.chunkId, before.chunkId);
    QCOMPARE(after.ordinal, before.ordinal);
    QCOMPARE(after.startMs, before.startMs);
    QCOMPARE(after.endMs, before.endMs);
    QCOMPARE(after.originalText, before.originalText);
    QCOMPARE(after.editedText, before.editedText);
    QCOMPARE(after.averageProbability, before.averageProbability);
    QCOMPARE(after.minimumProbability, before.minimumProbability);
    QCOMPARE(after.noSpeechProbability, before.noSpeechProbability);
    QCOMPARE(after.lowConfidence, before.lowConfidence);
    QCOMPARE(after.reviewed, before.reviewed);
    QCOMPARE(after.replacementAudit, before.replacementAudit);
    QCOMPARE(after.provisional, before.provisional);
    QCOMPARE(after.attempt, before.attempt);
    QCOMPARE(after.createdAt, before.createdAt);
    QCOMPARE(after.updatedAt, before.updatedAt);
}

void TranscriptTest::ownerlessEditsRejectActiveExecutionWithoutMutation() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager databaseA({path});
    DatabaseManager databaseB({path});
    QVERIFY(databaseA.initialize());
    QVERIFY(databaseB.initialize());
    SqliteRecordingRepository recordings(databaseA);
    SqliteJobRepository jobs(databaseA);
    SqliteTranscriptRepository transcriptsA(databaseA);
    SqliteTranscriptRepository transcriptsB(databaseB);

    Recording recording;
    recording.id = QStringLiteral("ownerless-edit-recording");
    recording.title = QStringLiteral("Ownerless edit fencing");
    QVERIFY(recordings.create(recording));
    TranscriptionJob job;
    job.id = QStringLiteral("ownerless-edit-job");
    job.recordingId = recording.id;
    QVERIFY(jobs.createQueued(job));
    TranscriptSegment original;
    original.id = QStringLiteral("ownerless-edit-segment");
    original.recordingId = recording.id;
    original.jobId = job.id;
    original.startMs = 0;
    original.endMs = 1'000;
    original.originalText = QStringLiteral("durable before execution");
    original.editedText = original.originalText;
    QVERIFY(transcriptsA.replaceTranscript(recording.id, job.id, {original}));

    const QString owner = QStringLiteral("ownerless-edit-owner");
    QVERIFY(jobs.claimQueued(job.id, owner, 10'000).value().claimed);
    TranscriptSegment replacement = original;
    replacement.id = QStringLiteral("ownerless-edit-replacement");
    replacement.originalText = QStringLiteral("ownerless replacement");
    replacement.editedText = replacement.originalText;
    const auto replace = transcriptsB.replaceTranscript(recording.id, job.id, {replacement});
    QVERIFY(!replace);
    QCOMPARE(replace.error().code, ErrorCode::InvalidStateTransition);
    const auto saveAll = transcriptsB.saveEditedTranscript(recording.id, job.id, {replacement});
    QVERIFY(!saveAll);
    QCOMPARE(saveAll.error().code, ErrorCode::InvalidStateTransition);
    const auto existing = transcriptsB.segment(original.id);
    QVERIFY(existing);
    QVERIFY(existing.value());
    const TranscriptSegment beforeExecution = *existing.value();
    TranscriptSegment edited = beforeExecution;
    edited.editedText = QStringLiteral("ownerless segment edit");
    const auto saveOne = transcriptsB.saveEditedSegment(edited);
    QVERIFY(!saveOne);
    QCOMPARE(saveOne.error().code, ErrorCode::InvalidStateTransition);
    const auto remove = transcriptsB.deleteSegment(original.id);
    QVERIFY(!remove);
    QCOMPARE(remove.error().code, ErrorCode::InvalidStateTransition);

    auto durable = transcriptsB.segmentsForJob(job.id, true);
    QVERIFY(durable);
    QCOMPARE(durable.value().size(), 1);
    const TranscriptSegment afterRejectedWrites = durable.value().constFirst();
    QCOMPARE(afterRejectedWrites.id, beforeExecution.id);
    QCOMPARE(afterRejectedWrites.recordingId, beforeExecution.recordingId);
    QCOMPARE(afterRejectedWrites.jobId, beforeExecution.jobId);
    QCOMPARE(afterRejectedWrites.originalText, beforeExecution.originalText);
    QCOMPARE(afterRejectedWrites.editedText, beforeExecution.editedText);
    QCOMPARE(afterRejectedWrites.startMs, beforeExecution.startMs);
    QCOMPARE(afterRejectedWrites.endMs, beforeExecution.endMs);
    QCOMPARE(afterRejectedWrites.createdAt, beforeExecution.createdAt);
    QCOMPARE(afterRejectedWrites.updatedAt, beforeExecution.updatedAt);

    const auto connection = databaseA.connection();
    QVERIFY(connection);
    QSqlQuery state(connection.value());
    state.prepare(QStringLiteral("UPDATE transcription_jobs SET state=? WHERE id=?"));
    state.addBindValue(jobStateName(JobState::Interrupted));
    state.addBindValue(job.id);
    QVERIFY(state.exec());
    const auto leasedInterruptedEdit = transcriptsB.saveEditedSegment(edited);
    QVERIFY(!leasedInterruptedEdit);
    QCOMPARE(leasedInterruptedEdit.error().code, ErrorCode::InvalidStateTransition);

    QSqlQuery expire(connection.value());
    expire.prepare(QStringLiteral(
        "UPDATE asr_execution_lease SET expires_at='2000-01-01T00:00:00.000Z' "
        "WHERE resource='asr' AND job_id=?"));
    expire.addBindValue(job.id);
    QVERIFY(expire.exec());
    QVERIFY(transcriptsB.saveEditedSegment(edited));
    QCOMPARE(transcriptsB.segment(original.id).value()->editedText,
             QStringLiteral("ownerless segment edit"));

    QSqlQuery release(connection.value());
    release.prepare(QStringLiteral("DELETE FROM asr_execution_lease WHERE resource='asr' AND job_id=?"));
    release.addBindValue(job.id);
    QVERIFY(release.exec());

    state.bindValue(0, jobStateName(JobState::Completed));
    state.bindValue(1, job.id);
    QVERIFY(state.exec());
    edited.editedText = QStringLiteral("completed job edit");
    QVERIFY(transcriptsB.saveEditedSegment(edited));
    QCOMPARE(transcriptsB.segment(original.id).value()->editedText,
             QStringLiteral("completed job edit"));

    state.bindValue(0, jobStateName(JobState::Preparing));
    state.bindValue(1, job.id);
    QVERIFY(state.exec());
    const auto runningWithoutLeaseDelete = transcriptsB.deleteSegment(original.id);
    QVERIFY(!runningWithoutLeaseDelete);
    QCOMPARE(runningWithoutLeaseDelete.error().code, ErrorCode::InvalidStateTransition);
    durable = transcriptsB.segmentsForJob(job.id, true);
    QVERIFY(durable);
    QCOMPARE(durable.value().size(), 1);
    QCOMPARE(durable.value().constFirst().editedText, QStringLiteral("completed job edit"));
}

void TranscriptTest::staleOwnerCannotReplaceReclaimedChunkTranscript() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite"));
    DatabaseManager databaseA({path});
    DatabaseManager databaseB({path});
    QVERIFY(databaseA.initialize());
    QVERIFY(databaseB.initialize());
    SqliteRecordingRepository recordings(databaseA);
    Recording recording;
    recording.id = QStringLiteral("fenced-transcript-recording");
    recording.title = QStringLiteral("Fenced transcript");
    QVERIFY(recordings.create(recording));
    SqliteJobRepository jobsA(databaseA);
    SqliteJobRepository jobsB(databaseB);
    SqliteTranscriptRepository transcriptsA(databaseA);
    SqliteTranscriptRepository transcriptsB(databaseB);

    TranscriptionJob job;
    job.id = QStringLiteral("fenced-transcript-job");
    job.recordingId = recording.id;
    QVERIFY(jobsA.createQueued(job));
    JobChunk chunk;
    chunk.id = QStringLiteral("fenced-transcript-chunk");
    chunk.jobId = job.id;
    chunk.ordinal = 0;
    chunk.startMs = 0;
    chunk.endMs = 1'000;
    QVERIFY(jobsA.replaceChunks(job.id, {chunk}));

    const QString ownerA = QStringLiteral("transcript-owner-a");
    QVERIFY(jobsA.claimQueued(job.id, ownerA, 10'000).value().claimed);
    QVERIFY(jobsA.transition(job.id, JobState::LoadingModel, {}, {}, ownerA));
    QVERIFY(jobsA.transition(job.id, JobState::Transcribing, {}, {}, ownerA));
    TranscriptSegment segmentA;
    segmentA.id = QStringLiteral("segment-a");
    segmentA.recordingId = recording.id;
    segmentA.jobId = job.id;
    segmentA.chunkId = chunk.id;
    segmentA.startMs = 0;
    segmentA.endMs = 1'000;
    segmentA.originalText = QStringLiteral("owner A output");
    QVERIFY(transcriptsA.replaceChunk(recording.id, job.id, chunk.id, {segmentA}, true, 1, ownerA));

    const auto connection = databaseA.connection();
    QVERIFY(connection);
    QSqlQuery expire(connection.value());
    QVERIFY(expire.exec(QStringLiteral(
        "UPDATE asr_execution_lease SET expires_at='2000-01-01T00:00:00.000Z' WHERE resource='asr'")));
    QVERIFY(jobsA.markRunningJobsInterrupted(QStringLiteral("transcript handoff")));
    QVERIFY(JobQueue(jobsA).resume(job.id));

    const QString ownerB = QStringLiteral("transcript-owner-b");
    QVERIFY(jobsB.claimQueued(job.id, ownerB, 10'000).value().claimed);
    QVERIFY(jobsB.transition(job.id, JobState::LoadingModel, {}, {}, ownerB));
    QVERIFY(jobsB.transition(job.id, JobState::Transcribing, {}, {}, ownerB));
    TranscriptSegment segmentB = segmentA;
    segmentB.id = QStringLiteral("segment-b");
    segmentB.originalText = QStringLiteral("owner B output");
    QVERIFY(transcriptsB.replaceChunk(recording.id, job.id, chunk.id, {segmentB}, false, 2, ownerB));

    segmentA.originalText = QStringLiteral("stale overwrite");
    const auto staleProvisional =
        transcriptsA.replaceChunk(recording.id, job.id, chunk.id, {segmentA}, true, 3, ownerA);
    QVERIFY(!staleProvisional);
    QCOMPARE(staleProvisional.error().code, ErrorCode::ExecutionLeaseLost);
    const auto staleFinal =
        transcriptsA.replaceChunk(recording.id, job.id, chunk.id, {segmentA}, false, 3, ownerA);
    QVERIFY(!staleFinal);
    QCOMPARE(staleFinal.error().code, ErrorCode::ExecutionLeaseLost);

    const auto durable = transcriptsB.segmentsForJob(job.id, true);
    QVERIFY(durable);
    QCOMPARE(durable.value().size(), 1);
    QCOMPARE(durable.value().constFirst().id, QStringLiteral("segment-b"));
    QCOMPARE(durable.value().constFirst().originalText, QStringLiteral("owner B output"));
    QVERIFY(!durable.value().constFirst().provisional);
    QCOMPARE(durable.value().constFirst().attempt, 2);
    QVERIFY(jobsB.renewLease(job.id, ownerB, 10'000));
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
    QCOMPARE(
        filteredModel->data(filteredModel->index(0, 0), TranscriptFilterProxyModel::ProxyRowRole).toInt(), 0);
    filteredViewModel.editText(0, QStringLiteral("Edited filtered result"));
    QCOMPARE(filteredViewModel.snapshot().at(0).editedText, filteredOut.editedText);
    QCOMPARE(filteredViewModel.snapshot().at(1).editedText, QStringLiteral("Edited filtered result"));
    QVERIFY(filteredViewModel.editTextById(QStringLiteral("filtered-in"),
                                           QStringLiteral("No longer matches the filter")));
    QCOMPARE(filteredViewModel.visibleSegmentCount(), 0);
    QCOMPARE(filteredViewModel.snapshot().at(0).editedText, filteredOut.editedText);
    QCOMPARE(filteredViewModel.snapshot().at(1).editedText,
             QStringLiteral("No longer matches the filter"));
    QVERIFY(filteredViewModel.editTextById(QStringLiteral("filtered-in"),
                                           QStringLiteral("No longer matches the filter")));
    filteredViewModel.setSearchText(QStringLiteral("Unrelated"));
    QCOMPARE(filteredViewModel.visibleSegmentCount(), 1);
    QCOMPARE(filteredViewModel.selectedIndex(), -1);
    QCOMPARE(filteredViewModel.activePlaybackIndex(), -1);
    QCOMPARE(filteredViewModel.findPrevious(12), 0);
}

QTEST_GUILESS_MAIN(TranscriptTest)
#include "tst_Transcript.moc"
