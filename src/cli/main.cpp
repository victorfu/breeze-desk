#include "breezedesk/app/WorkerProcessManager.h"
#include "breezedesk/app_config.h"
#include "breezedesk/asr/LongFormChunkPlanner.h"
#include "breezedesk/asr/OverlapDeduplicator.h"
#include "breezedesk/audio/FFmpegLocator.h"
#include "breezedesk/audio/FFmpegNormalizationService.h"
#include "breezedesk/audio/FFprobeService.h"
#include "breezedesk/cli/CliExitCode.h"
#include "breezedesk/cli/CliForwardingPolicy.h"
#include "breezedesk/cli/CliTranscriptionPersistence.h"
#include "breezedesk/core/ApplicationLogger.h"
#include "breezedesk/core/LoggingCategories.h"
#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/core/TemporaryFileJanitor.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/DatabaseSearchService.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/glossary/GlossaryPostProcessor.h"
#include "breezedesk/glossary/SqliteGlossaryRepository.h"
#include "breezedesk/ipc/ApplicationCommand.h"
#include "breezedesk/ipc/Protocol.h"
#include "breezedesk/jobs/JobQueue.h"
#include "breezedesk/jobs/SqliteJobRepository.h"
#include "breezedesk/models/ModelManager.h"
#include "breezedesk/transcript/SqliteTranscriptRepository.h"
#include "breezedesk/transcript/TranscriptExporter.h"
#include "breezedesk/version.h"

#include <QCborArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <optional>
#include <utility>

namespace BreezeDesk {
namespace {

volatile std::sig_atomic_t g_interrupted = 0;

void signalHandler(int) {
    g_interrupted = 1;
}

void writeJson(const QJsonObject& object) {
    QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact) << '\n';
}

void writeError(const QString& message) {
    QTextStream(stderr) << message << '\n';
}

void writeForwardedBytes(FILE* destination, const QByteArray& bytes) {
    if (!bytes.isEmpty()) {
        (void)std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), destination);
    }
    std::fflush(destination);
}

void writeProgress(const QString& message) {
    QTextStream stream(stderr);
    stream << '\r' << message;
    stream.flush();
}

void finishProgress() {
    QTextStream(stderr) << '\n';
}

QByteArray sha256File(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(4 * 1024 * 1024);
        if (block.isEmpty() && !file.atEnd()) {
            return {};
        }
        hash.addData(block);
    }
    return file.error() == QFileDevice::NoError ? hash.result().toHex() : QByteArray{};
}

struct CliPromptConfiguration {
    QCborArray parts;
    QList<GlossaryTerm> terms;
    QString profileId;
    QString profileContext;
    QString meetingContext;
};

QJsonArray encodeGlossaryTerms(const QList<GlossaryTerm>& terms) {
    QJsonArray encoded;
    for (const GlossaryTerm& term : terms) {
        QJsonArray aliases;
        for (const QString& alias : term.aliases) {
            aliases.append(alias);
        }
        encoded.append(QJsonObject{{QStringLiteral("id"), term.id},
                                   {QStringLiteral("canonicalText"), term.canonicalText},
                                   {QStringLiteral("aliases"), aliases},
                                   {QStringLiteral("priority"), term.priority},
                                   {QStringLiteral("caseSensitive"), term.caseSensitive},
                                   {QStringLiteral("enabled"), term.enabled}});
    }
    return encoded;
}

QList<GlossaryTerm> decodeGlossaryTerms(const QJsonArray& encoded) {
    QList<GlossaryTerm> terms;
    terms.reserve(encoded.size());
    for (const QJsonValue& value : encoded) {
        const QJsonObject item = value.toObject();
        GlossaryTerm term;
        term.id = item.value(QStringLiteral("id")).toString();
        term.canonicalText = item.value(QStringLiteral("canonicalText")).toString();
        for (const QJsonValue& alias : item.value(QStringLiteral("aliases")).toArray()) {
            term.aliases.append(alias.toString());
        }
        term.priority = item.value(QStringLiteral("priority")).toInt();
        term.caseSensitive = item.value(QStringLiteral("caseSensitive")).toBool();
        term.enabled = item.value(QStringLiteral("enabled")).toBool(true);
        if (!term.id.isEmpty() && !term.canonicalText.trimmed().isEmpty()) {
            terms.append(std::move(term));
        }
    }
    return terms;
}

QCborArray buildPromptParts(const CliPromptConfiguration& configuration) {
    QCborArray parts;
    for (const GlossaryTerm& term : configuration.terms) {
        if (!term.enabled) {
            continue;
        }
        QString text = term.canonicalText;
        if (!term.aliases.isEmpty()) {
            text += QStringLiteral(" (aliases: %1)").arg(term.aliases.join(QStringLiteral(", ")));
        }
        parts.append(QCborMap{{QStringLiteral("kind"), QStringLiteral("glossary")},
                              {QStringLiteral("text"), text},
                              {QStringLiteral("priority"), term.priority}});
    }
    if (!configuration.meetingContext.trimmed().isEmpty()) {
        parts.append(QCborMap{{QStringLiteral("kind"), QStringLiteral("meetingContext")},
                              {QStringLiteral("text"), configuration.meetingContext},
                              {QStringLiteral("priority"), 1}});
    }
    if (!configuration.profileContext.trimmed().isEmpty()) {
        parts.append(QCborMap{{QStringLiteral("kind"), QStringLiteral("meetingContext")},
                              {QStringLiteral("text"), configuration.profileContext},
                              {QStringLiteral("priority"), 0}});
    }
    return parts;
}

QString optionValue(const QStringList& arguments, const QString& name, const QString& fallback = {}) {
    const qsizetype index = arguments.indexOf(name);
    return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : fallback;
}

bool hasOption(const QStringList& arguments, const QString& name) {
    return arguments.contains(name);
}

TranscriptExportFormat exportFormat(const QString& name, bool* valid) {
    static const QHash<QString, TranscriptExportFormat> formats{
        {QStringLiteral("txt"), TranscriptExportFormat::Txt},
        {QStringLiteral("md"), TranscriptExportFormat::Markdown},
        {QStringLiteral("srt"), TranscriptExportFormat::Srt},
        {QStringLiteral("vtt"), TranscriptExportFormat::Vtt},
        {QStringLiteral("json"), TranscriptExportFormat::Json},
        {QStringLiteral("csv"), TranscriptExportFormat::Csv}};
    const auto iterator = formats.constFind(name.toLower());
    *valid = iterator != formats.cend();
    return *valid ? iterator.value() : TranscriptExportFormat::Txt;
}

QString extensionFor(TranscriptExportFormat format) {
    switch (format) {
    case TranscriptExportFormat::Txt:
        return QStringLiteral("txt");
    case TranscriptExportFormat::Markdown:
        return QStringLiteral("md");
    case TranscriptExportFormat::Srt:
        return QStringLiteral("srt");
    case TranscriptExportFormat::Vtt:
        return QStringLiteral("vtt");
    case TranscriptExportFormat::Json:
        return QStringLiteral("json");
    case TranscriptExportFormat::Csv:
        return QStringLiteral("csv");
    }
    return QStringLiteral("txt");
}

QJsonObject recordingJson(const Recording& recording) {
    QJsonArray tags;
    for (const QString& tag : recording.tags) {
        tags.push_back(tag);
    }
    return {{QStringLiteral("id"), recording.id},
            {QStringLiteral("title"), recording.title},
            {QStringLiteral("sourcePath"), recording.sourcePath},
            {QStringLiteral("durationMs"), recording.durationMs},
            {QStringLiteral("reviewState"), recording.reviewState},
            {QStringLiteral("tags"), tags},
            {QStringLiteral("activeJobId"), recording.activeJobId}};
}

CliExitCode initializeDatabase(DatabaseManager* database) {
    const auto result = database->initialize();
    if (!result) {
        writeError(result.error().diagnosticString());
        return CliExitCode::DatabaseFailure;
    }
    return CliExitCode::Success;
}

CliExitCode importFiles(const QStringList& paths, SqliteRecordingRepository* repository, bool json) {
    const FFmpegLocator::Tools tools = FFmpegLocator::locate();
    if (!tools.isValid()) {
        writeError(tools.error);
        return CliExitCode::MediaFailure;
    }
    FFprobeService probe(tools.ffprobePath);
    QJsonArray imported;
    for (const QString& path : paths) {
        QString error;
        MediaMetadata metadata = probe.inspect(path, &error);
        if (!metadata.hasAudio) {
            writeError(QStringLiteral("%1: %2").arg(path, error));
            return CliExitCode::MediaFailure;
        }
        const QFileInfo info(path);
        Recording recording;
        recording.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        recording.title = info.completeBaseName();
        recording.sourcePath = info.absoluteFilePath();
        recording.mediaType = metadata.hasVideo ? QStringLiteral("video") : QStringLiteral("audio");
        recording.durationMs = metadata.durationMs;
        recording.sampleRate = metadata.sampleRate;
        recording.channelCount = metadata.channelCount;
        recording.createdAt = QDateTime::currentDateTimeUtc();
        recording.updatedAt = recording.createdAt;
        const auto result = repository->create(recording);
        if (!result) {
            writeError(result.error().diagnosticString());
            return CliExitCode::DatabaseFailure;
        }
        imported.push_back(recordingJson(recording));
        if (!json) {
            QTextStream(stdout) << recording.id << '\t' << recording.title << '\n';
        }
    }
    if (json) {
        writeJson({{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("recordings"), imported}});
    }
    return CliExitCode::Success;
}

CliExitCode listLibrary(SqliteRecordingRepository* repository, bool json) {
    RecordingQuery query;
    query.limit = 10000;
    const auto result = repository->list(query);
    if (!result) {
        writeError(result.error().diagnosticString());
        return CliExitCode::DatabaseFailure;
    }
    QJsonArray values;
    for (const Recording& recording : result.value().items) {
        values.push_back(recordingJson(recording));
        if (!json) {
            QTextStream(stdout) << recording.id << '\t' << recording.durationMs << '\t' << recording.title
                                << '\n';
        }
    }
    if (json) {
        writeJson({{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("recordings"), values}});
    }
    return CliExitCode::Success;
}

CliExitCode searchLibrary(DatabaseManager* database, const QString& query, bool json) {
    const auto result = DatabaseSearchService(*database).search(query);
    if (!result) {
        writeError(result.error().diagnosticString());
        return CliExitCode::DatabaseFailure;
    }
    QJsonArray values;
    for (const SearchResult& match : result.value()) {
        const QJsonObject value{{QStringLiteral("recordingId"), match.recordingId},
                                {QStringLiteral("segmentId"), match.segmentId},
                                {QStringLiteral("startMs"), match.startMs},
                                {QStringLiteral("title"), match.title},
                                {QStringLiteral("snippet"), match.snippet}};
        values.push_back(value);
        if (!json) {
            QTextStream(stdout) << match.recordingId << '\t' << match.startMs << '\t' << match.title << '\t'
                                << match.snippet << '\n';
        }
    }
    if (json) {
        writeJson({{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("results"), values}});
    }
    return CliExitCode::Success;
}

CliExitCode listJobs(SqliteJobRepository* repository, bool json) {
    const auto result = repository->list(true);
    if (!result) {
        writeError(result.error().diagnosticString());
        return CliExitCode::DatabaseFailure;
    }
    QJsonArray values;
    for (const TranscriptionJob& job : result.value()) {
        const QJsonObject value{{QStringLiteral("id"), job.id},
                                {QStringLiteral("recordingId"), job.recordingId},
                                {QStringLiteral("state"), jobStateName(job.state)},
                                {QStringLiteral("stage"), jobStageName(job.stage)},
                                {QStringLiteral("progress"), job.progress},
                                {QStringLiteral("modelId"), job.modelId}};
        values.push_back(value);
        if (!json) {
            QTextStream(stdout) << job.id << '\t' << jobStateName(job.state) << '\t' << job.progress << '\n';
        }
    }
    if (json) {
        writeJson({{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("jobs"), values}});
    }
    return CliExitCode::Success;
}

CliExitCode cancelJob(SqliteJobRepository* repository, const QString& id, bool json) {
    const auto job = repository->findById(id);
    if (!job || !job.value().has_value()) {
        writeError(job ? QStringLiteral("Job not found: %1").arg(id) : job.error().diagnosticString());
        return CliExitCode::DatabaseFailure;
    }
    const auto result = JobQueue(*repository).cancel(id);
    if (!result) {
        writeError(result.error().diagnosticString());
        return CliExitCode::DatabaseFailure;
    }
    if (json) {
        writeJson({{QStringLiteral("schemaVersion"), 1},
                   {QStringLiteral("jobId"), id},
                   {QStringLiteral("state"), QStringLiteral("Cancelled")}});
    }
    return CliExitCode::Success;
}

CliExitCode listModels(ModelManager* manager, bool json) {
    QJsonArray models;
    for (const ModelManifestEntry& entry : manager->manifest().entries()) {
        const QJsonObject value{{QStringLiteral("id"), entry.id},
                                {QStringLiteral("displayName"), entry.displayName},
                                {QStringLiteral("quantization"), entry.quantization},
                                {QStringLiteral("fileSize"), entry.fileSize},
                                {QStringLiteral("installed"), manager->isInstalled(entry.id)},
                                {QStringLiteral("recommended"), entry.isRecommended},
                                {QStringLiteral("license"), entry.licenseName}};
        models.push_back(value);
        if (!json) {
            QTextStream(stdout) << entry.id << '\t'
                                << (manager->isInstalled(entry.id) ? "installed" : "not-installed") << '\t'
                                << entry.displayName << '\n';
        }
    }
    for (const CustomModelInfo& custom : manager->customModels()) {
        models.push_back(QJsonObject{{QStringLiteral("id"), custom.id},
                                     {QStringLiteral("displayName"), custom.displayName},
                                     {QStringLiteral("quantization"), QStringLiteral("custom")},
                                     {QStringLiteral("fileSize"), custom.fileSize},
                                     {QStringLiteral("installed"), true},
                                     {QStringLiteral("recommended"), false},
                                     {QStringLiteral("license"), QStringLiteral("user-supplied")}});
        if (!json) {
            QTextStream(stdout) << custom.id << '\t' << "installed\t" << custom.displayName << '\n';
        }
    }
    if (json) {
        writeJson({{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("models"), models}});
    }
    return CliExitCode::Success;
}

CliExitCode verifyModel(ModelManager* manager, const QString& id, bool json) {
    QString error;
    const bool valid = manager->verify(id, &error);
    if (json) {
        writeJson({{QStringLiteral("schemaVersion"), 1},
                   {QStringLiteral("modelId"), id},
                   {QStringLiteral("valid"), valid},
                   {QStringLiteral("error"), error}});
    } else {
        QTextStream(stdout) << id << '\t' << (valid ? "verified" : "invalid") << '\n';
    }
    if (!valid) {
        writeError(error);
        return CliExitCode::ModelUnavailable;
    }
    return CliExitCode::Success;
}

CliExitCode downloadModel(ModelManager* manager, const QString& id, bool json) {
    ModelDownloadOperation* operation = manager->download(id);
    if (operation == nullptr) {
        writeError(QStringLiteral("Unknown model id: %1").arg(id));
        return CliExitCode::InvalidArguments;
    }
    QEventLoop loop;
    bool success = false;
    QString path;
    QObject::connect(operation, &ModelDownloadOperation::progressChanged, operation, [operation] {
        writeProgress(QStringLiteral("Downloading %1 %2%")
                          .arg(operation->modelId(), QString::number(operation->progress() * 100.0, 'f', 1)));
    });
    QObject::connect(operation, &ModelDownloadOperation::finished, &loop,
                     [&loop, &success, &path](bool completed, const QString& filePath) {
                         success = completed;
                         path = filePath;
                         loop.quit();
                     });
    loop.exec();
    finishProgress();
    if (!success) {
        writeError(operation->error());
        return CliExitCode::NetworkFailure;
    }
    if (json) {
        writeJson({{QStringLiteral("schemaVersion"), 1},
                   {QStringLiteral("modelId"), id},
                   {QStringLiteral("path"), path},
                   {QStringLiteral("verified"), true}});
    } else {
        QTextStream(stdout) << path << '\n';
    }
    return CliExitCode::Success;
}

CliExitCode exportRecording(SqliteRecordingRepository* recordings, SqliteTranscriptRepository* transcripts,
                            const QString& recordingId, const QStringList& arguments, bool jsonOutput) {
    const auto recordingResult = recordings->findById(recordingId);
    if (!recordingResult || !recordingResult.value().has_value()) {
        writeError(QStringLiteral("Recording not found: %1").arg(recordingId));
        return CliExitCode::DatabaseFailure;
    }
    const Recording& recording = *recordingResult.value();
    if (recording.activeJobId.isEmpty()) {
        writeError(QStringLiteral("Recording has no active transcript revision."));
        return CliExitCode::ExportFailure;
    }
    const auto segmentResult = transcripts->segmentsForJob(recording.activeJobId, false);
    if (!segmentResult) {
        writeError(segmentResult.error().diagnosticString());
        return CliExitCode::DatabaseFailure;
    }
    bool validFormat = false;
    const TranscriptExportFormat format =
        exportFormat(optionValue(arguments, QStringLiteral("--format"), QStringLiteral("txt")), &validFormat);
    if (!validFormat) {
        writeError(QStringLiteral("Unsupported export format."));
        return CliExitCode::InvalidArguments;
    }
    const QString fallback =
        QDir::current().filePath(recording.title + QLatin1Char('.') + extensionFor(format));
    const QString output = optionValue(arguments, QStringLiteral("--output"), fallback);
    TranscriptExportMetadata metadata;
    metadata.recordingId = recording.id;
    metadata.title = recording.title;
    metadata.durationMs = recording.durationMs;
    TranscriptExportOptions options;
    options.includeTimecodes = hasOption(arguments, QStringLiteral("--timecodes"));
    options.utf8Bom = hasOption(arguments, QStringLiteral("--bom"));
    const auto result =
        TranscriptExporter::writeFile(output, format, metadata, segmentResult.value(), options);
    if (!result) {
        writeError(result.error().diagnosticString());
        return CliExitCode::ExportFailure;
    }
    if (jsonOutput) {
        writeJson({{QStringLiteral("schemaVersion"), 1},
                   {QStringLiteral("recordingId"), recording.id},
                   {QStringLiteral("output"), QFileInfo(output).absoluteFilePath()}});
    } else {
        QTextStream(stdout) << QFileInfo(output).absoluteFilePath() << '\n';
    }
    return CliExitCode::Success;
}

struct TranscribeRunResult {
    CliExitCode exitCode = CliExitCode::InternalFailure;
    QList<TranscriptSegment> segments;
    QString error;
    QString recordingId;
    QString jobId;
    qint64 durationMs = 0;
    bool resumed = false;
};

QList<JobChunk> durableChunks(const QList<Asr::TranscriptionChunk>& chunks) {
    QList<JobChunk> values;
    values.reserve(chunks.size());
    for (const Asr::TranscriptionChunk& chunk : chunks) {
        JobChunk value;
        value.ordinal = chunk.ordinal;
        value.startMs = chunk.startMs;
        value.endMs = chunk.endMs;
        value.overlapBeforeMs = chunk.overlapBeforeMs;
        value.overlapAfterMs = chunk.overlapAfterMs;
        values.append(value);
    }
    return values;
}

QString normalizedAudioPath(const QString& recordingId) {
    const QString directory = QDir(StoragePaths::cache()).filePath(QStringLiteral("audio"));
    if (!QDir().mkpath(directory)) {
        return {};
    }
    return QDir(directory).filePath(recordingId + QStringLiteral(".wav"));
}

bool isReusableNormalizedAudio(const QString& path, qint64 durationMs) {
    const QFileInfo information(path);
    if (!information.isFile() || information.size() <= 44) {
        return false;
    }
    constexpr qint64 PcmBytesPerMillisecond = 32;
    const qint64 expectedBytes = qMax<qint64>(1, durationMs) * PcmBytesPerMillisecond;
    return information.size() >= (expectedBytes * 9) / 10;
}

TranscribeRunResult runHeadlessTranscription(const QString& source, const QString& modelPath,
                                             const QString& modelId, const QString& modelChecksum,
                                             const QString& vadModelPath, const QString& vadModelChecksum,
                                             const QStringList& arguments,
                                             const CliPromptConfiguration& promptConfiguration,
                                             SqliteRecordingRepository& recordings, SqliteJobRepository& jobs,
                                             SqliteTranscriptRepository& transcripts) {
    const FFmpegLocator::Tools tools = FFmpegLocator::locate();
    if (!tools.isValid()) {
        TranscribeRunResult result;
        result.exitCode = CliExitCode::MediaFailure;
        result.error = tools.error;
        return result;
    }
    FFprobeService probe(tools.ffprobePath);
    QString error;
    const MediaMetadata metadata = probe.inspect(source, &error);
    if (!metadata.hasAudio || metadata.durationMs <= 0) {
        TranscribeRunResult result;
        result.exitCode = CliExitCode::MediaFailure;
        result.error = error;
        return result;
    }

    const QString resumeJobId = optionValue(arguments, QStringLiteral("--resume-job"));
    QString recordingId;
    QString pcmPath;
    bool resumeHadReachedTranscription = false;
    if (!resumeJobId.isEmpty()) {
        const auto savedJob = jobs.findById(resumeJobId);
        if (!savedJob) {
            TranscribeRunResult result;
            result.exitCode = CliExitCode::DatabaseFailure;
            result.error = savedJob.error().diagnosticString();
            return result;
        }
        if (!savedJob.value()) {
            TranscribeRunResult result;
            result.exitCode = CliExitCode::InvalidArguments;
            result.error = QStringLiteral("The resume job does not exist: %1").arg(resumeJobId);
            return result;
        }
        const auto savedRecording = recordings.findById(savedJob.value()->recordingId);
        if (!savedRecording) {
            TranscribeRunResult result;
            result.exitCode = CliExitCode::DatabaseFailure;
            result.error = savedRecording.error().diagnosticString();
            return result;
        }
        if (!savedRecording.value()) {
            TranscribeRunResult result;
            result.exitCode = CliExitCode::InvalidArguments;
            result.error = QStringLiteral("The recording for the resume job no longer exists.");
            return result;
        }
        recordingId = savedRecording.value()->id;
        pcmPath = savedRecording.value()->normalizedPcmPath;
        if (pcmPath.isEmpty()) {
            pcmPath = normalizedAudioPath(recordingId);
        }
        resumeHadReachedTranscription =
            static_cast<int>(savedJob.value()->stage) >= static_cast<int>(JobStage::Transcribing);
    } else {
        const auto existing = recordings.findBySourcePath(source);
        if (!existing) {
            TranscribeRunResult result;
            result.exitCode = CliExitCode::DatabaseFailure;
            result.error = existing.error().diagnosticString();
            return result;
        }
        recordingId =
            existing.value() ? existing.value()->id : QUuid::createUuid().toString(QUuid::WithoutBraces);
        pcmPath = normalizedAudioPath(recordingId);
    }
    if (pcmPath.isEmpty()) {
        TranscribeRunResult result;
        result.exitCode = CliExitCode::MediaFailure;
        result.error = QStringLiteral("The normalized audio cache directory could not be created.");
        return result;
    }

    CliTranscriptionPersistence persistence(
        recordings, jobs, transcripts, {}, [] { return g_interrupted != 0; },
        [](const QString& message) { writeError(message); });
    Result<DurableTranscriptionIdentity> session =
        Result<DurableTranscriptionIdentity>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("The transcription session was not initialized.")));
    if (resumeJobId.isEmpty()) {
        Recording recording;
        recording.id = recordingId;
        recording.title = QFileInfo(source).completeBaseName();
        recording.sourcePath = source;
        recording.normalizedPcmPath = pcmPath;
        recording.mediaType = metadata.hasVideo ? QStringLiteral("video") : QStringLiteral("audio");
        recording.durationMs = metadata.durationMs;
        recording.sampleRate = metadata.sampleRate;
        recording.channelCount = metadata.channelCount;
        recording.createdAt = QDateTime::currentDateTimeUtc();
        recording.updatedAt = recording.createdAt;

        TranscriptionJob job;
        job.recordingId = recordingId;
        job.modelId = modelId;
        job.modelChecksum = modelChecksum;
        job.engineVersion = QStringLiteral("whisper.cpp-v1.9.1");
        job.workerVersion = QString::fromLatin1(BREEZEDESK_VERSION_STRING);
        job.backend = optionValue(arguments, QStringLiteral("--backend"), QStringLiteral("auto"));
        job.language = optionValue(arguments, QStringLiteral("--language"), QStringLiteral("zh"));
        job.preset = optionValue(arguments, QStringLiteral("--preset"), QStringLiteral("balanced"));
        job.glossaryProfileId = promptConfiguration.profileId;
        job.meetingContext = promptConfiguration.meetingContext;
        job.vadEnabled = hasOption(arguments, QStringLiteral("--vad"));
        job.parameters = {
            {QStringLiteral("flashAttention"), !hasOption(arguments, QStringLiteral("--no-flash-attention"))},
            {QStringLiteral("initialPrompt"), job.meetingContext},
            {QStringLiteral("glossaryProjectContext"), promptConfiguration.profileContext},
            {QStringLiteral("glossaryTerms"), encodeGlossaryTerms(promptConfiguration.terms)},
        };
        DurableTranscriptionDescriptor descriptor;
        descriptor.recording = recording;
        descriptor.job = job;
        descriptor.chunks = durableChunks(Asr::LongFormChunkPlanner().plan(metadata.durationMs, {}));
        session = persistence.beginNew(std::move(descriptor));
    } else {
        session = persistence.resume(resumeJobId, source, pcmPath);
    }
    if (!session) {
        TranscribeRunResult result;
        if (session.error().code == ErrorCode::OperationCancelled ||
            session.error().code == ErrorCode::JobCancelled) {
            result.exitCode = CliExitCode::Cancelled;
        } else {
            result.exitCode = session.error().domain == ErrorDomain::Database
                                  ? CliExitCode::DatabaseFailure
                                  : CliExitCode::InvalidArguments;
        }
        result.error = session.error().diagnosticString();
        return result;
    }

    const auto makeResult = [&persistence, &metadata](CliExitCode code, QList<TranscriptSegment> segments,
                                                      QString message) {
        TranscribeRunResult result;
        result.exitCode = code;
        result.segments = std::move(segments);
        result.error = std::move(message);
        result.recordingId = persistence.identity().recordingId;
        result.jobId = persistence.identity().jobId;
        result.durationMs = metadata.durationMs;
        result.resumed = persistence.identity().resumed;
        return result;
    };
    const auto interruptSession = [&persistence](const QString& reason, const QString& errorCode) {
        const auto checkpoint = persistence.interrupt(reason, errorCode);
        Q_UNUSED(checkpoint)
    };
    QString leaseFailure;
    QTimer leaseHeartbeat;
    leaseHeartbeat.setInterval(2'000);
    QObject::connect(&leaseHeartbeat, &QTimer::timeout, &leaseHeartbeat,
                     [&persistence, &leaseFailure, &leaseHeartbeat] {
                         if (!persistence.isActive()) {
                             leaseHeartbeat.stop();
                             return;
                         }
                         const auto renewed = persistence.renewExecutionLease();
                         if (!renewed && leaseFailure.isEmpty()) {
                             leaseFailure = QStringLiteral(
                                 "The global transcription lease was lost: %1")
                                                .arg(renewed.error().diagnosticString());
                             g_interrupted = 1;
                         }
                     });
    leaseHeartbeat.start();
    const auto interruptionReason = [&leaseFailure] {
        return leaseFailure.isEmpty()
                   ? QStringLiteral("Transcription was interrupted by the user.")
                   : leaseFailure;
    };
    const auto interruptionCode = [&leaseFailure] {
        return leaseFailure.isEmpty() ? QStringLiteral("JobCancelled")
                                      : QStringLiteral("ExecutionLeaseLost");
    };
    const auto interruptionExitCode = [&leaseFailure] {
        return leaseFailure.isEmpty() ? CliExitCode::Cancelled : CliExitCode::DatabaseFailure;
    };

    if (g_interrupted != 0) {
        const QString reason = interruptionReason();
        interruptSession(reason, interruptionCode());
        return makeResult(interruptionExitCode(), {}, reason);
    }

    if (!isReusableNormalizedAudio(pcmPath, metadata.durationMs)) {
        const auto normalizationStarted = persistence.beginNormalization();
        if (!normalizationStarted) {
            interruptSession(normalizationStarted.error().diagnosticString(),
                             QStringLiteral("DatabaseQueryFailed"));
            return makeResult(CliExitCode::DatabaseFailure, {},
                              normalizationStarted.error().diagnosticString());
        }
        FFmpegNormalizationService normalizer(tools.ffmpegPath);
        NormalizationOperation* operation =
            normalizer.normalize(source, pcmPath, metadata.durationMs, &normalizer);
        QEventLoop normalizationLoop;
        bool normalized = false;
        QString checkpointError;
        QObject::connect(operation, &NormalizationOperation::progressChanged, operation,
                         [&persistence, operation, &checkpointError] {
                             writeProgress(QStringLiteral("Normalizing %1%")
                                               .arg(QString::number(operation->progress() * 100.0, 'f', 1)));
                             const auto saved =
                                 persistence.updateNormalizationProgress(operation->progress());
                             if (!saved && checkpointError.isEmpty()) {
                                 checkpointError = saved.error().diagnosticString();
                                 operation->cancel();
                             }
                         });
        QObject::connect(operation, &NormalizationOperation::finished, &normalizationLoop,
                         [&normalizationLoop, &normalized](bool success, const QString&) {
                             normalized = success;
                             normalizationLoop.quit();
                         });
        QTimer normalizationCancellation;
        normalizationCancellation.setInterval(100);
        QObject::connect(&normalizationCancellation, &QTimer::timeout, operation, [operation] {
            if (g_interrupted != 0) {
                operation->cancel();
            }
        });
        normalizationCancellation.start();
        normalizationLoop.exec();
        finishProgress();
        if (!checkpointError.isEmpty()) {
            interruptSession(checkpointError, QStringLiteral("DatabaseQueryFailed"));
            return makeResult(CliExitCode::DatabaseFailure, {}, checkpointError);
        }
        if (!normalized) {
            const QString reason =
                g_interrupted != 0 ? interruptionReason() : operation->error();
            if (g_interrupted != 0) {
                interruptSession(reason, interruptionCode());
                return makeResult(interruptionExitCode(), {}, reason);
            }
            const auto failed = persistence.fail(QStringLiteral("AudioDecodeFailed"), reason);
            Q_UNUSED(failed)
            return makeResult(CliExitCode::MediaFailure, {}, reason);
        }
    }

    WorkerProcessManager worker;
    QString workerInterruption;
    QObject::connect(&worker, &WorkerProcessManager::workerInterrupted, &worker,
                     [&workerInterruption](const QString& reason) { workerInterruption = reason; });
    QEventLoop readyLoop;
    QTimer readyTimeout;
    readyTimeout.setSingleShot(true);
    QObject::connect(&worker, &WorkerProcessManager::readyChanged, &readyLoop, [&worker, &readyLoop] {
        if (worker.isReady()) {
            readyLoop.quit();
        }
    });
    QObject::connect(&readyTimeout, &QTimer::timeout, &readyLoop, &QEventLoop::quit);
    QObject::connect(&worker, &WorkerProcessManager::workerInterrupted, &readyLoop, &QEventLoop::quit);
    if (!worker.start()) {
        interruptSession(worker.lastError(), QStringLiteral("WorkerCrashed"));
        return makeResult(CliExitCode::WorkerFailure, {}, worker.lastError());
    }
    readyTimeout.start(10000);
    readyLoop.exec();
    if (!worker.isReady()) {
        const QString reason = worker.lastError().isEmpty()
                                   ? QStringLiteral("Timed out connecting to the ASR worker.")
                                   : worker.lastError();
        interruptSession(reason, workerInterruption.isEmpty() ? QStringLiteral("WorkerTimeout")
                                                              : QStringLiteral("WorkerCrashed"));
        return makeResult(CliExitCode::WorkerFailure, {}, reason);
    }

    Ipc::AsrWorkerClient* client = worker.client();
    const QString jobId = persistence.identity().jobId;

    const auto modelLoadStarted = persistence.beginModelLoad();
    if (!modelLoadStarted) {
        worker.stop();
        interruptSession(modelLoadStarted.error().diagnosticString(), QStringLiteral("DatabaseQueryFailed"));
        return makeResult(CliExitCode::DatabaseFailure, {}, modelLoadStarted.error().diagnosticString());
    }
    QEventLoop loadLoop;
    QString loadError;
    QString actualBackend;
    QString runtimeVersion;
    QJsonObject runtimeDiagnostics;
    bool loaded = false;
    bool loadTimedOut = false;
    const QString loadRequest = client->sendRequest(
        Ipc::MessageType::LoadModel, {},
        {{QStringLiteral("modelPath"), modelPath},
         {QStringLiteral("modelSha256"), modelChecksum},
         {QStringLiteral("backend"),
          optionValue(arguments, QStringLiteral("--backend"), QStringLiteral("auto"))},
         {QStringLiteral("flashAttention"), !hasOption(arguments, QStringLiteral("--no-flash-attention"))}});
    QMetaObject::Connection loadConnection = QObject::connect(
        client, &Ipc::AsrWorkerClient::envelopeReceived, &loadLoop,
        [&loadLoop, &loadError, &actualBackend, &runtimeVersion, &runtimeDiagnostics, &loaded,
         loadRequest](const Ipc::Envelope& envelope) {
            if (envelope.requestId != loadRequest) {
                return;
            }
            if (envelope.type == Ipc::MessageType::ModelLoaded) {
                actualBackend = envelope.payload.value(QStringLiteral("actualBackend")).toString();
                runtimeVersion = envelope.payload.value(QStringLiteral("runtimeVersion")).toString();
                runtimeDiagnostics.insert(
                    QStringLiteral("requestedBackend"),
                    envelope.payload.value(QStringLiteral("selectedBackend")).toString());
                runtimeDiagnostics.insert(QStringLiteral("flashAttention"),
                                          envelope.payload.value(QStringLiteral("flashAttention")).toBool());
                runtimeDiagnostics.insert(QStringLiteral("usedFallback"),
                                          envelope.payload.value(QStringLiteral("usedFallback")).toBool());
                runtimeDiagnostics.insert(
                    QStringLiteral("loadTimeMs"),
                    static_cast<double>(envelope.payload.value(QStringLiteral("loadTimeMs")).toInteger()));
                runtimeDiagnostics.insert(QStringLiteral("systemInfo"),
                                          envelope.payload.value(QStringLiteral("systemInfo")).toString());
                loaded = true;
                loadLoop.quit();
            } else if (envelope.type == Ipc::MessageType::Error) {
                loadError = envelope.payload.value(QStringLiteral("message")).toString();
                loadLoop.quit();
            }
        });
    QTimer loadTimeout;
    loadTimeout.setSingleShot(true);
    QObject::connect(&loadTimeout, &QTimer::timeout, &loadLoop, [&loadLoop, &loadTimedOut] {
        loadTimedOut = true;
        loadLoop.quit();
    });
    QObject::connect(&worker, &WorkerProcessManager::workerInterrupted, &loadLoop, &QEventLoop::quit);
    QTimer loadCancellation;
    loadCancellation.setInterval(100);
    QObject::connect(&loadCancellation, &QTimer::timeout, &loadLoop, [&worker, &loadLoop, jobId] {
        if (g_interrupted != 0) {
            worker.forceCancelAfterGrace(jobId);
            loadLoop.quit();
        }
    });
    loadCancellation.start();
    loadTimeout.start(120000);
    loadLoop.exec();
    QObject::disconnect(loadConnection);
    if (!loaded) {
        QString reason = loadError;
        if (g_interrupted != 0) {
            reason = interruptionReason();
            interruptSession(reason, interruptionCode());
            worker.stop();
            return makeResult(interruptionExitCode(), {}, reason);
        }
        if (!workerInterruption.isEmpty() || loadTimedOut) {
            reason = workerInterruption.isEmpty() ? QStringLiteral("Model loading timed out.")
                                                  : workerInterruption;
            interruptSession(reason, loadTimedOut ? QStringLiteral("WorkerTimeout")
                                                  : QStringLiteral("WorkerCrashed"));
            worker.stop();
            return makeResult(CliExitCode::WorkerFailure, {}, reason);
        }
        if (reason.isEmpty()) {
            reason = QStringLiteral("The ASR model could not be loaded.");
        }
        const auto failed = persistence.fail(QStringLiteral("ModelLoadFailed"), reason);
        Q_UNUSED(failed)
        worker.stop();
        return makeResult(CliExitCode::ModelUnavailable, {}, reason);
    }
    const auto runtimeSaved =
        jobs.updateRuntimeInfo(jobId, actualBackend, runtimeVersion,
                               QString::fromLatin1(BREEZEDESK_VERSION_STRING), runtimeDiagnostics);
    if (!runtimeSaved) {
        const QString reason = runtimeSaved.error().diagnosticString();
        interruptSession(reason, QStringLiteral("DatabaseQueryFailed"));
        worker.stop();
        return makeResult(CliExitCode::DatabaseFailure, {}, reason);
    }

    const bool shouldAnalyzeSpeech =
        hasOption(arguments, QStringLiteral("--vad")) && !resumeHadReachedTranscription;
    if (shouldAnalyzeSpeech) {
        const auto analysisStarted = persistence.beginSpeechAnalysis();
        if (!analysisStarted) {
            interruptSession(analysisStarted.error().diagnosticString(),
                             QStringLiteral("DatabaseQueryFailed"));
            worker.stop();
            return makeResult(CliExitCode::DatabaseFailure, {}, analysisStarted.error().diagnosticString());
        }
        QEventLoop analysisLoop;
        QString analysisError;
        QString analysisCheckpointError;
        bool analysisCompleted = false;
        bool analysisTimedOut = false;
        QList<JobChunk> analyzedChunks;
        const QString requestId = client->sendRequest(Ipc::MessageType::AnalyzeSpeech, jobId,
                                                      {{QStringLiteral("pcmPath"), pcmPath},
                                                       {QStringLiteral("vadModelPath"), vadModelPath},
                                                       {QStringLiteral("vadModelSha256"), vadModelChecksum}});
        const QMetaObject::Connection analysisConnection = QObject::connect(
            client, &Ipc::AsrWorkerClient::envelopeReceived, &analysisLoop,
            [&analysisLoop, &analysisError, &analysisCheckpointError, &analysisCompleted, &analyzedChunks,
             &persistence, requestId, jobId, &worker](const Ipc::Envelope& envelope) {
                if (envelope.requestId != requestId || envelope.jobId != jobId) {
                    return;
                }
                if (envelope.type == Ipc::MessageType::Progress) {
                    const qint64 progress = envelope.payload.value(QStringLiteral("progress")).toInteger();
                    writeProgress(QStringLiteral("Analyzing speech %1%").arg(progress));
                    const auto saved =
                        persistence.updateSpeechAnalysisProgress(static_cast<double>(progress) / 100.0);
                    if (!saved) {
                        analysisCheckpointError = saved.error().diagnosticString();
                        worker.forceCancelAfterGrace(jobId);
                        analysisLoop.quit();
                    }
                } else if (envelope.type == Ipc::MessageType::SpeechAnalysisCompleted) {
                    const auto values = envelope.payload.value(QStringLiteral("chunks")).toArray();
                    analyzedChunks.reserve(values.size());
                    for (const auto& value : values) {
                        const auto map = value.toMap();
                        JobChunk chunk;
                        chunk.ordinal = static_cast<int>(map.value(QStringLiteral("ordinal")).toInteger());
                        chunk.startMs = map.value(QStringLiteral("startMs")).toInteger();
                        chunk.endMs = map.value(QStringLiteral("endMs")).toInteger();
                        chunk.overlapBeforeMs = map.value(QStringLiteral("overlapBeforeMs")).toInteger();
                        chunk.overlapAfterMs = map.value(QStringLiteral("overlapAfterMs")).toInteger();
                        analyzedChunks.append(chunk);
                    }
                    analysisCompleted = !analyzedChunks.isEmpty();
                    if (!analysisCompleted) {
                        analysisError = QStringLiteral("Speech analysis returned no transcription chunks.");
                    }
                    analysisLoop.quit();
                } else if (envelope.type == Ipc::MessageType::Error) {
                    analysisError = envelope.payload.value(QStringLiteral("message")).toString();
                    analysisLoop.quit();
                } else if (envelope.type == Ipc::MessageType::JobCancelled) {
                    analysisError = QStringLiteral("Speech analysis was cancelled.");
                    analysisLoop.quit();
                }
            });
        QTimer analysisTimeout;
        analysisTimeout.setSingleShot(true);
        QObject::connect(&analysisTimeout, &QTimer::timeout, &analysisLoop,
                         [&analysisLoop, &analysisTimedOut] {
                             analysisTimedOut = true;
                             analysisLoop.quit();
                         });
        QObject::connect(&worker, &WorkerProcessManager::workerInterrupted, &analysisLoop, &QEventLoop::quit);
        QTimer cancellationPoll;
        cancellationPoll.setInterval(100);
        QObject::connect(&cancellationPoll, &QTimer::timeout, &analysisLoop, [&worker, &analysisLoop, jobId] {
            if (g_interrupted != 0) {
                worker.forceCancelAfterGrace(jobId);
                analysisLoop.quit();
            }
        });
        cancellationPoll.start();
        analysisTimeout.start(60 * 60 * 1000);
        analysisLoop.exec();
        QObject::disconnect(analysisConnection);
        finishProgress();
        if (!analysisCheckpointError.isEmpty()) {
            interruptSession(analysisCheckpointError, QStringLiteral("DatabaseQueryFailed"));
            worker.stop();
            return makeResult(CliExitCode::DatabaseFailure, {}, analysisCheckpointError);
        }
        if (!analysisCompleted) {
            QString reason = analysisError;
            if (g_interrupted != 0) {
                reason = interruptionReason();
                interruptSession(reason, interruptionCode());
                worker.stop();
                return makeResult(interruptionExitCode(), {}, reason);
            }
            if (!workerInterruption.isEmpty() || analysisTimedOut) {
                reason = workerInterruption.isEmpty() ? QStringLiteral("Speech analysis timed out.")
                                                      : workerInterruption;
                interruptSession(reason, analysisTimedOut ? QStringLiteral("WorkerTimeout")
                                                          : QStringLiteral("WorkerCrashed"));
                worker.stop();
                return makeResult(CliExitCode::WorkerFailure, {}, reason);
            }
            if (reason.isEmpty()) {
                reason = QStringLiteral("Speech analysis failed.");
            }
            const auto failed = persistence.fail(QStringLiteral("AudioDecodeFailed"), reason);
            Q_UNUSED(failed)
            worker.stop();
            return makeResult(CliExitCode::TranscriptionFailure, {}, reason);
        }
        const auto replaced = persistence.replaceChunkPlan(std::move(analyzedChunks));
        if (!replaced) {
            interruptSession(replaced.error().diagnosticString(), QStringLiteral("DatabaseQueryFailed"));
            worker.stop();
            return makeResult(CliExitCode::DatabaseFailure, {}, replaced.error().diagnosticString());
        }
    }

    const auto transcriptionStarted = persistence.beginTranscription();
    if (!transcriptionStarted) {
        interruptSession(transcriptionStarted.error().diagnosticString(),
                         QStringLiteral("DatabaseQueryFailed"));
        worker.stop();
        return makeResult(CliExitCode::DatabaseFailure, {}, transcriptionStarted.error().diagnosticString());
    }
    auto storedSegments = transcripts.segmentsForJob(jobId, false);
    if (!storedSegments) {
        interruptSession(storedSegments.error().diagnosticString(), QStringLiteral("DatabaseQueryFailed"));
        worker.stop();
        return makeResult(CliExitCode::DatabaseFailure, {}, storedSegments.error().diagnosticString());
    }
    QList<TranscriptSegment> segments = storedSegments.value();
    QList<JobChunk> chunks = persistence.identity().chunks;
    std::sort(chunks.begin(), chunks.end(),
              [](const JobChunk& left, const JobChunk& right) { return left.ordinal < right.ordinal; });
    int completedChunks =
        static_cast<int>(std::count_if(chunks.cbegin(), chunks.cend(), [](const JobChunk& chunk) {
            return chunk.state == ChunkState::Completed;
        }));
    for (qsizetype chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex) {
        if (chunks.at(chunkIndex).state == ChunkState::Completed) {
            continue;
        }
        if (g_interrupted != 0) {
            worker.forceCancelAfterGrace(jobId);
            const QString reason = interruptionReason();
            interruptSession(reason, interruptionCode());
            worker.stop();
            return makeResult(interruptionExitCode(), segments, reason);
        }
        const JobChunk& chunk = chunks.at(chunkIndex);
        const auto chunkStarted = persistence.beginChunk(chunk.ordinal);
        if (!chunkStarted) {
            interruptSession(chunkStarted.error().diagnosticString(), QStringLiteral("DatabaseQueryFailed"));
            worker.stop();
            return makeResult(CliExitCode::DatabaseFailure, segments,
                              chunkStarted.error().diagnosticString());
        }
        const bool finalChunk = chunkIndex == chunks.size() - 1;
        QEventLoop chunkLoop;
        QString chunkError;
        QString chunkCheckpointError;
        bool completed = false;
        bool chunkTimedOut = false;
        QList<TranscriptSegment> incoming;
        QCborArray currentPromptParts = promptConfiguration.parts;
        QString previousContext;
        for (auto iterator = segments.crbegin();
             iterator != segments.crend() && previousContext.size() < 1'000; ++iterator) {
            if (!iterator->provisional && !iterator->displayText().trimmed().isEmpty()) {
                previousContext.prepend(iterator->displayText().trimmed() + QLatin1Char(' '));
            }
        }
        previousContext = previousContext.trimmed().right(1'000);
        if (!previousContext.isEmpty()) {
            currentPromptParts.append(QCborMap{{QStringLiteral("kind"), QStringLiteral("previousTranscript")},
                                               {QStringLiteral("text"), previousContext}});
        }
        QCborMap payload{{QStringLiteral("pcmPath"), pcmPath},
                         {QStringLiteral("startMs"), chunk.startMs},
                         {QStringLiteral("endMs"), chunk.endMs},
                         {QStringLiteral("finalChunk"), finalChunk},
                         {QStringLiteral("language"),
                          optionValue(arguments, QStringLiteral("--language"), QStringLiteral("zh"))},
                         {QStringLiteral("preset"),
                          optionValue(arguments, QStringLiteral("--preset"), QStringLiteral("balanced"))},
                         {QStringLiteral("vadEnabled"), hasOption(arguments, QStringLiteral("--vad"))},
                         {QStringLiteral("vadModelPath"), vadModelPath},
                         {QStringLiteral("vadModelSha256"), vadModelChecksum},
                         {QStringLiteral("promptParts"), currentPromptParts}};
        const QString requestId = client->sendRequest(Ipc::MessageType::StartTranscription, jobId, payload);
        QMetaObject::Connection connection = QObject::connect(
            client, &Ipc::AsrWorkerClient::envelopeReceived, &chunkLoop,
            [&chunkLoop, &chunkError, &chunkCheckpointError, &completed, &incoming, &persistence, &worker,
             requestId, jobId, &segments, &completedChunks, chunk, finalChunk,
             glossaryTerms = promptConfiguration.terms,
             chunkCount = chunks.size()](const Ipc::Envelope& envelope) {
                if (envelope.requestId != requestId || envelope.jobId != jobId) {
                    return;
                }
                if (envelope.type == Ipc::MessageType::Progress) {
                    const qint64 workerProgress =
                        envelope.payload.value(QStringLiteral("progress")).toInteger();
                    const double overallProgress =
                        (static_cast<double>(completedChunks) + static_cast<double>(workerProgress) / 100.0) /
                        static_cast<double>(qMax<qsizetype>(1, chunkCount));
                    writeProgress(QStringLiteral("Transcribing %1%")
                                      .arg(QString::number(overallProgress * 100.0, 'f', 1)));
                    const auto saved = persistence.updateTranscriptionProgress(overallProgress);
                    if (!saved) {
                        chunkCheckpointError = saved.error().diagnosticString();
                        worker.forceCancelAfterGrace(jobId);
                        chunkLoop.quit();
                    }
                } else if (envelope.type == Ipc::MessageType::PartialSegment) {
                    TranscriptSegment segment;
                    segment.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    segment.jobId = jobId;
                    segment.ordinal = static_cast<int>(segments.size() + incoming.size());
                    segment.startMs = envelope.payload.value(QStringLiteral("startMs")).toInteger();
                    segment.endMs = envelope.payload.value(QStringLiteral("endMs")).toInteger();
                    segment.originalText = envelope.payload.value(QStringLiteral("originalText")).toString();
                    const GlossaryPostProcessResult postProcessed =
                        GlossaryPostProcessor().applyExplicitAliases(segment.originalText, glossaryTerms);
                    if (!postProcessed.replacements.isEmpty()) {
                        segment.editedText = postProcessed.text;
                        segment.replacementAudit =
                            GlossaryPostProcessor::auditToJson(postProcessed.replacements);
                    }
                    segment.averageProbability =
                        envelope.payload.value(QStringLiteral("averageTokenProbability")).toDouble();
                    segment.minimumProbability =
                        envelope.payload.value(QStringLiteral("minimumTokenProbability")).toDouble();
                    segment.noSpeechProbability =
                        envelope.payload.value(QStringLiteral("noSpeechProbability")).toDouble();
                    segment.lowConfidence = envelope.payload.value(QStringLiteral("lowConfidence")).toBool();
                    incoming.push_back(std::move(segment));
                    const auto saved = persistence.saveChunkSegments(chunk.ordinal, incoming, true);
                    if (!saved) {
                        chunkCheckpointError = saved.error().diagnosticString();
                        worker.forceCancelAfterGrace(jobId);
                        chunkLoop.quit();
                    }
                } else if ((envelope.type == Ipc::MessageType::ChunkCompleted && !finalChunk) ||
                           (envelope.type == Ipc::MessageType::TranscriptionCompleted && finalChunk)) {
                    completed = true;
                    chunkLoop.quit();
                } else if (envelope.type == Ipc::MessageType::Error) {
                    chunkError = envelope.payload.value(QStringLiteral("message")).toString();
                    chunkLoop.quit();
                } else if (envelope.type == Ipc::MessageType::JobCancelled) {
                    chunkError = QStringLiteral("Transcription was cancelled.");
                    chunkLoop.quit();
                }
            });
        QTimer chunkTimeout;
        chunkTimeout.setSingleShot(true);
        QObject::connect(&chunkTimeout, &QTimer::timeout, &chunkLoop, [&chunkLoop, &chunkTimedOut] {
            chunkTimedOut = true;
            chunkLoop.quit();
        });
        QObject::connect(&worker, &WorkerProcessManager::workerInterrupted, &chunkLoop, &QEventLoop::quit);
        QTimer cancellationPoll;
        cancellationPoll.setInterval(100);
        QObject::connect(&cancellationPoll, &QTimer::timeout, &chunkLoop, [&worker, &chunkLoop, jobId] {
            if (g_interrupted != 0) {
                worker.forceCancelAfterGrace(jobId);
                chunkLoop.quit();
            }
        });
        cancellationPoll.start();
        chunkTimeout.start(12 * 60 * 60 * 1000);
        chunkLoop.exec();
        QObject::disconnect(connection);
        finishProgress();
        if (!completed) {
            if (!chunkCheckpointError.isEmpty()) {
                interruptSession(chunkCheckpointError, QStringLiteral("DatabaseQueryFailed"));
                worker.stop();
                return makeResult(CliExitCode::DatabaseFailure, segments, chunkCheckpointError);
            }
            if (g_interrupted != 0) {
                const QString reason = interruptionReason();
                interruptSession(reason, interruptionCode());
                worker.stop();
                return makeResult(interruptionExitCode(), segments, reason);
            }
            if (!workerInterruption.isEmpty() || chunkTimedOut) {
                const QString reason =
                    workerInterruption.isEmpty()
                        ? QStringLiteral("The ASR worker timed out while transcribing a chunk.")
                        : workerInterruption;
                interruptSession(reason, chunkTimedOut ? QStringLiteral("WorkerTimeout")
                                                       : QStringLiteral("WorkerCrashed"));
                worker.stop();
                return makeResult(CliExitCode::WorkerFailure, segments, reason);
            }
            if (chunkError.isEmpty()) {
                chunkError = QStringLiteral("The ASR worker stopped before completing the chunk.");
            }
            const auto failed = persistence.fail(QStringLiteral("Unknown"), chunkError);
            Q_UNUSED(failed)
            worker.stop();
            return makeResult(CliExitCode::TranscriptionFailure, segments, chunkError);
        }
        if (!segments.isEmpty() && !incoming.isEmpty() && chunk.overlapBeforeMs > 0) {
            const auto deduplicated = Asr::OverlapDeduplicator::deduplicate(
                segments.constLast().displayText(), incoming.first().originalText, true);
            if (deduplicated.text.trimmed().isEmpty()) {
                incoming.removeFirst();
            } else {
                incoming.first().originalText = deduplicated.text;
            }
        }
        for (TranscriptSegment& segment : incoming) {
            const GlossaryPostProcessResult postProcessed =
                GlossaryPostProcessor().applyExplicitAliases(segment.originalText, promptConfiguration.terms);
            segment.editedText = postProcessed.replacements.isEmpty() ? QString{} : postProcessed.text;
            segment.replacementAudit = GlossaryPostProcessor::auditToJson(postProcessed.replacements);
        }
        qint64 previousEnd = segments.isEmpty() ? 0 : segments.constLast().endMs;
        for (TranscriptSegment& segment : incoming) {
            segment.startMs = qMax(segment.startMs, previousEnd);
            if (segment.endMs <= segment.startMs) {
                segment.endMs = segment.startMs + 1;
            }
            previousEnd = segment.endMs;
        }
        const auto completedChunk = persistence.completeChunk(chunk.ordinal, incoming);
        if (!completedChunk) {
            interruptSession(completedChunk.error().diagnosticString(),
                             QStringLiteral("DatabaseQueryFailed"));
            worker.stop();
            return makeResult(CliExitCode::DatabaseFailure, segments,
                              completedChunk.error().diagnosticString());
        }
        segments.append(incoming);
        ++completedChunks;
    }
    const auto durableCompletion = persistence.complete();
    if (!durableCompletion) {
        interruptSession(durableCompletion.error().diagnosticString(), QStringLiteral("DatabaseQueryFailed"));
        worker.stop();
        return makeResult(CliExitCode::DatabaseFailure, segments,
                          durableCompletion.error().diagnosticString());
    }
    const auto durableSegments = persistence.persistedSegments();
    if (!durableSegments) {
        worker.stop();
        return makeResult(CliExitCode::DatabaseFailure, segments, durableSegments.error().diagnosticString());
    }
    worker.stop();
    return makeResult(CliExitCode::Success, durableSegments.value(), {});
}

CliExitCode transcribeFile(const QString& source, const QStringList& arguments, ModelManager* models,
                           SqliteRecordingRepository* recordings, SqliteJobRepository* jobs,
                           SqliteTranscriptRepository* transcripts, SqliteGlossaryRepository* glossaries,
                           bool jsonOutput) {
    if (!QFileInfo(source).isFile()) {
        writeError(QStringLiteral("Source file does not exist: %1").arg(source));
        return CliExitCode::SourceMissing;
    }
    QStringList runArguments = arguments;
    QString fallbackModelId = models->defaultModelId();
    const QString resumeJobId = optionValue(arguments, QStringLiteral("--resume-job"));
    TranscriptionJob resumeSettings;
    CliPromptConfiguration promptConfiguration;
    promptConfiguration.meetingContext = optionValue(arguments, QStringLiteral("--initial-prompt"));
    bool isResume = false;
    if (!resumeJobId.isEmpty()) {
        const auto savedJob = jobs->findById(resumeJobId);
        if (!savedJob) {
            writeError(savedJob.error().diagnosticString());
            return CliExitCode::DatabaseFailure;
        }
        if (!savedJob.value()) {
            writeError(QStringLiteral("The resume job does not exist: %1").arg(resumeJobId));
            return CliExitCode::InvalidArguments;
        }
        resumeSettings = *savedJob.value();
        if (resumeSettings.state != JobState::Interrupted && resumeSettings.state != JobState::Failed) {
            writeError(QStringLiteral("Only interrupted or failed transcription jobs can be resumed."));
            return CliExitCode::InvalidArguments;
        }
        isResume = true;
        fallbackModelId = resumeSettings.modelId;
        promptConfiguration.profileId = resumeSettings.glossaryProfileId;
        promptConfiguration.meetingContext = resumeSettings.meetingContext;
        promptConfiguration.profileContext =
            resumeSettings.parameters.value(QStringLiteral("glossaryProjectContext")).toString();
        promptConfiguration.terms =
            decodeGlossaryTerms(resumeSettings.parameters.value(QStringLiteral("glossaryTerms")).toArray());

        const auto conflicts = [&arguments](const QString& option, const QString& expected) {
            return hasOption(arguments, option) && optionValue(arguments, option) != expected;
        };
        const QString expectedLanguage =
            resumeSettings.language.isEmpty() ? QStringLiteral("zh") : resumeSettings.language;
        const QString expectedPreset =
            resumeSettings.preset.isEmpty() ? QStringLiteral("balanced") : resumeSettings.preset;
        const QString expectedBackend =
            resumeSettings.backend.isEmpty() ? QStringLiteral("auto") : resumeSettings.backend;
        if (conflicts(QStringLiteral("--model"), resumeSettings.modelId) ||
            conflicts(QStringLiteral("--language"), expectedLanguage) ||
            conflicts(QStringLiteral("--preset"), expectedPreset) ||
            conflicts(QStringLiteral("--backend"), expectedBackend) ||
            conflicts(QStringLiteral("--initial-prompt"), resumeSettings.meetingContext) ||
            conflicts(QStringLiteral("--glossary"), resumeSettings.glossaryProfileId) ||
            (hasOption(arguments, QStringLiteral("--vad")) && !resumeSettings.vadEnabled)) {
            writeError(
                QStringLiteral("A resumed job must use its original model and transcription settings."));
            return CliExitCode::InvalidArguments;
        }
        const bool flashAttention =
            resumeSettings.parameters.value(QStringLiteral("flashAttention")).toBool(true);
        if (hasOption(arguments, QStringLiteral("--no-flash-attention")) && flashAttention) {
            writeError(
                QStringLiteral("A resumed job must use its original model and transcription settings."));
            return CliExitCode::InvalidArguments;
        }
        const auto appendOption = [&runArguments](const QString& option, const QString& value) {
            if (!hasOption(runArguments, option)) {
                runArguments.append({option, value});
            }
        };
        appendOption(QStringLiteral("--language"), expectedLanguage);
        appendOption(QStringLiteral("--preset"), expectedPreset);
        appendOption(QStringLiteral("--backend"), expectedBackend);
        if (!resumeSettings.meetingContext.isEmpty()) {
            appendOption(QStringLiteral("--initial-prompt"), resumeSettings.meetingContext);
        }
        if (resumeSettings.vadEnabled && !hasOption(runArguments, QStringLiteral("--vad"))) {
            runArguments.append(QStringLiteral("--vad"));
        }
        if (!flashAttention && !hasOption(runArguments, QStringLiteral("--no-flash-attention"))) {
            runArguments.append(QStringLiteral("--no-flash-attention"));
        }
    } else {
        const QString requestedGlossary = optionValue(arguments, QStringLiteral("--glossary"));
        if (!requestedGlossary.isEmpty()) {
            const auto profileResult = glossaries->profile(requestedGlossary);
            if (!profileResult) {
                writeError(profileResult.error().diagnosticString());
                return CliExitCode::DatabaseFailure;
            }
            std::optional<GlossaryProfile> selected = profileResult.value();
            if (!selected.has_value()) {
                const auto profiles = glossaries->profiles();
                if (!profiles) {
                    writeError(profiles.error().diagnosticString());
                    return CliExitCode::DatabaseFailure;
                }
                const auto match =
                    std::find_if(profiles.value().cbegin(), profiles.value().cend(),
                                 [&requestedGlossary](const GlossaryProfile& profile) {
                                     return profile.name.compare(requestedGlossary, Qt::CaseInsensitive) == 0;
                                 });
                if (match != profiles.value().cend()) {
                    selected = *match;
                }
            }
            if (!selected.has_value()) {
                writeError(QStringLiteral("Glossary profile does not exist: %1").arg(requestedGlossary));
                return CliExitCode::InvalidArguments;
            }
            promptConfiguration.profileId = selected->id;
            promptConfiguration.profileContext = selected->projectContext;
            const auto terms = glossaries->terms(selected->id);
            if (!terms) {
                writeError(terms.error().diagnosticString());
                return CliExitCode::DatabaseFailure;
            }
            promptConfiguration.terms = terms.value();
        }
    }
    promptConfiguration.parts = buildPromptParts(promptConfiguration);
    const QString modelPathOption = optionValue(runArguments, QStringLiteral("--model-path"));
    const QString modelId = optionValue(runArguments, QStringLiteral("--model"), fallbackModelId);
    const QString modelPath = modelPathOption.isEmpty() ? models->modelPath(modelId) : modelPathOption;
    if (!QFileInfo(modelPath).isFile()) {
        writeError(QStringLiteral("Model is not installed: %1").arg(modelId));
        return CliExitCode::ModelUnavailable;
    }
    bool validFormat = false;
    const TranscriptExportFormat format = exportFormat(
        optionValue(runArguments, QStringLiteral("--format"), QStringLiteral("txt")), &validFormat);
    if (!validFormat) {
        writeError(QStringLiteral("Unsupported output format."));
        return CliExitCode::InvalidArguments;
    }
    const QFileInfo sourceInfo(source);
    const QString output = optionValue(
        runArguments, QStringLiteral("--output"),
        QDir::current().filePath(sourceInfo.completeBaseName() + QLatin1Char('.') + extensionFor(format)));
    const bool vadEnabled = hasOption(runArguments, QStringLiteral("--vad"));
    const QString vadModelPath =
        vadEnabled ? models->modelPath(QStringLiteral("silero-vad-v6.2.0")) : QString{};
    if (vadEnabled && !QFileInfo(vadModelPath).isFile()) {
        writeError(QStringLiteral("VAD model is not installed: silero-vad-v6.2.0"));
        return CliExitCode::ModelUnavailable;
    }
    const QString modelChecksum =
        isResume && !resumeSettings.modelChecksum.isEmpty()
            ? resumeSettings.modelChecksum
            : QString::fromLatin1(modelPathOption.isEmpty() ? models->expectedSha256(modelId)
                                                            : sha256File(modelPath));
    if (modelChecksum.size() != 64) {
        writeError(QStringLiteral("The ASR model does not have a trusted SHA-256."));
        return CliExitCode::ModelUnavailable;
    }
    const QString vadModelChecksum =
        vadEnabled ? QString::fromLatin1(models->expectedSha256(QStringLiteral("silero-vad-v6.2.0")))
                   : QString{};
    if (vadEnabled && vadModelChecksum.size() != 64) {
        writeError(QStringLiteral("The VAD model does not have a trusted SHA-256."));
        return CliExitCode::ModelUnavailable;
    }
    const TranscribeRunResult run = runHeadlessTranscription(
        sourceInfo.absoluteFilePath(), modelPath, modelId, modelChecksum, vadModelPath, vadModelChecksum,
        runArguments, promptConfiguration, *recordings, *jobs, *transcripts);
    if (run.exitCode != CliExitCode::Success) {
        writeError(run.error);
        if (!run.jobId.isEmpty()) {
            writeError(QStringLiteral("Durable job ID: %1").arg(run.jobId));
        }
        return run.exitCode;
    }
    TranscriptExportMetadata metadata;
    metadata.recordingId = run.recordingId;
    metadata.title = sourceInfo.completeBaseName();
    metadata.durationMs = run.durationMs;
    metadata.modelId = modelId;
    metadata.modelChecksum = modelChecksum;
    metadata.engineVersion = QStringLiteral("whisper.cpp-v1.9.1");
    metadata.workerVersion = QString::fromLatin1(BREEZEDESK_VERSION_STRING);
    metadata.language = optionValue(runArguments, QStringLiteral("--language"), QStringLiteral("zh"));
    metadata.preset = optionValue(runArguments, QStringLiteral("--preset"), QStringLiteral("balanced"));
    metadata.additional = {{QStringLiteral("jobId"), run.jobId}, {QStringLiteral("resumed"), run.resumed}};
    const auto exported = TranscriptExporter::writeFile(output, format, metadata, run.segments);
    if (!exported) {
        writeError(exported.error().diagnosticString());
        return CliExitCode::ExportFailure;
    }
    if (jsonOutput) {
        writeJson({{QStringLiteral("schemaVersion"), 1},
                   {QStringLiteral("recordingId"), metadata.recordingId},
                   {QStringLiteral("jobId"), run.jobId},
                   {QStringLiteral("resumed"), run.resumed},
                   {QStringLiteral("segments"), run.segments.size()},
                   {QStringLiteral("output"), QFileInfo(output).absoluteFilePath()}});
    } else {
        QTextStream(stdout) << QFileInfo(output).absoluteFilePath() << '\n';
    }
    return CliExitCode::Success;
}

void printHelp() {
    QTextStream(stdout)
        << QString::fromLatin1(AppConfig::ProductName) << " CLI " << BREEZEDESK_VERSION_STRING << "\n\n"
        << "Global options:\n"
        << "  --headless  Always execute locally and start an isolated native worker when needed.\n\n"
        << "Commands:\n"
        << "  transcribe <file> [--model id|--model-path path] [--language zh] [--preset balanced]\n"
        << "             [--glossary profile-id-or-name] [--initial-prompt context]\n"
        << "             [--vad] [--resume-job id] [--output path]\n"
        << "             [--format txt|md|srt|vtt|json|csv] [--json]\n"
        << "  import <file...> [--json]\n"
        << "  library list [--json]\n"
        << "  library search <query> [--json]\n"
        << "  jobs list [--json]\n"
        << "  jobs cancel <id> [--json]\n"
        << "  models list [--json]\n"
        << "  models download <id> [--json]\n"
        << "  models verify <id> [--json]\n"
        << "  export <recording-id> [--output path] [--format ...] [--json]\n\n"
        << "Exit codes: 0 success, 2 arguments, 3 source, 4 model, 5 media, 6 worker,\n"
        << "            7 transcription, 8 database, 9 export, 10 network, 11 cancelled, 12 internal.\n";
}

} // namespace
} // namespace BreezeDesk

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    application.setOrganizationName(QString::fromLatin1(BreezeDesk::AppConfig::OrganizationName));
    application.setOrganizationDomain(QString::fromLatin1(BreezeDesk::AppConfig::OrganizationDomain));
    application.setApplicationName(QString::fromLatin1(BreezeDesk::AppConfig::DataDirectoryName));
    std::signal(SIGINT, BreezeDesk::signalHandler);
    std::signal(SIGTERM, BreezeDesk::signalHandler);

    QStringList arguments = application.arguments().mid(1);
    const bool headless = BreezeDesk::CliForwardingPolicy::consumeHeadlessFlag(&arguments);
    if (arguments.isEmpty() || arguments.first() == QStringLiteral("--help") ||
        arguments.first() == QStringLiteral("-h")) {
        BreezeDesk::printHelp();
        return static_cast<int>(BreezeDesk::CliExitCode::Success);
    }
    const bool json = BreezeDesk::hasOption(arguments, QStringLiteral("--json"));
    if (!headless) {
        const BreezeDesk::Ipc::ApplicationCommandForwardResult forwarded =
            BreezeDesk::Ipc::ApplicationCommandClient::forward(
                QString::fromLatin1(BreezeDesk::AppConfig::BundleId), arguments);
        if (forwarded.completed()) {
            BreezeDesk::writeForwardedBytes(stdout, forwarded.standardOutput);
            BreezeDesk::writeForwardedBytes(stderr, forwarded.standardError);
            return forwarded.exitCode;
        }
        if (!forwarded.canExecuteLocally()) {
            BreezeDesk::writeError(forwarded.error.isEmpty()
                                       ? QStringLiteral("The running GUI did not confirm the command.")
                                       : forwarded.error);
            return static_cast<int>(BreezeDesk::CliExitCode::InternalFailure);
        }
    }
    const QString command = arguments.takeFirst();

    BreezeDesk::LoggingConfiguration loggingConfiguration;
    loggingConfiguration.processName = QString::fromLatin1(BreezeDesk::AppConfig::CliExecutableName);
    loggingConfiguration.logDirectory = BreezeDesk::StoragePaths::logs();
    BreezeDesk::ApplicationLogger logger(loggingConfiguration);
    const auto loggerResult = logger.install();
    if (!loggerResult) {
        const QByteArray safeError =
            BreezeDesk::LogSanitizer::sanitize(loggerResult.error().diagnosticString()).toUtf8();
        const QByteArray productName = QString::fromLatin1(BreezeDesk::AppConfig::ProductName).toUtf8();
        std::fprintf(stderr, "%s CLI logging initialization failed: %s\n", productName.constData(),
                     safeError.constData());
    }
    const BreezeDesk::TemporaryCleanupReport cleanup = BreezeDesk::TemporaryFileJanitor::clean();
    if (!cleanup.succeeded()) {
        qCWarning(BreezeDesk::logCli, "Temporary cleanup reported %d failure(s): %s", cleanup.failures,
                  qUtf8Printable(cleanup.error));
    }

    QString storageError;
    if (!BreezeDesk::StoragePaths::ensureLayout(&storageError)) {
        BreezeDesk::writeError(storageError);
        return static_cast<int>(BreezeDesk::CliExitCode::DatabaseFailure);
    }
    BreezeDesk::DatabaseManager database({BreezeDesk::StoragePaths::databaseFile()});
    const BreezeDesk::CliExitCode databaseCode = BreezeDesk::initializeDatabase(&database);
    if (databaseCode != BreezeDesk::CliExitCode::Success) {
        return static_cast<int>(databaseCode);
    }
    BreezeDesk::SqliteRecordingRepository recordings(database);
    BreezeDesk::SqliteJobRepository jobs(database);
    BreezeDesk::SqliteTranscriptRepository transcripts(database);
    BreezeDesk::SqliteGlossaryRepository glossaries(database);
    BreezeDesk::ModelManager models;

    BreezeDesk::CliExitCode result = BreezeDesk::CliExitCode::InvalidArguments;
    if (command == QStringLiteral("import")) {
        QStringList paths = arguments;
        paths.removeAll(QStringLiteral("--json"));
        result = paths.isEmpty() ? BreezeDesk::CliExitCode::InvalidArguments
                                 : BreezeDesk::importFiles(paths, &recordings, json);
    } else if (command == QStringLiteral("library") && arguments.value(0) == QStringLiteral("list")) {
        result = BreezeDesk::listLibrary(&recordings, json);
    } else if (command == QStringLiteral("library") && arguments.value(0) == QStringLiteral("search") &&
               arguments.size() >= 2) {
        result = BreezeDesk::searchLibrary(&database, arguments.at(1), json);
    } else if (command == QStringLiteral("jobs") && arguments.value(0) == QStringLiteral("list")) {
        result = BreezeDesk::listJobs(&jobs, json);
    } else if (command == QStringLiteral("jobs") && arguments.value(0) == QStringLiteral("cancel") &&
               arguments.size() >= 2) {
        result = BreezeDesk::cancelJob(&jobs, arguments.at(1), json);
    } else if (command == QStringLiteral("models") && arguments.value(0) == QStringLiteral("list")) {
        result = BreezeDesk::listModels(&models, json);
    } else if (command == QStringLiteral("models") && arguments.value(0) == QStringLiteral("download") &&
               arguments.size() >= 2) {
        result = BreezeDesk::downloadModel(&models, arguments.at(1), json);
    } else if (command == QStringLiteral("models") && arguments.value(0) == QStringLiteral("verify") &&
               arguments.size() >= 2) {
        result = BreezeDesk::verifyModel(&models, arguments.at(1), json);
    } else if (command == QStringLiteral("export") && !arguments.isEmpty()) {
        result = BreezeDesk::exportRecording(&recordings, &transcripts, arguments.first(), arguments, json);
    } else if (command == QStringLiteral("transcribe") && !arguments.isEmpty()) {
        result = BreezeDesk::transcribeFile(arguments.first(), arguments, &models, &recordings, &jobs,
                                            &transcripts, &glossaries, json);
    } else {
        BreezeDesk::writeError(QStringLiteral("Invalid command. Run %1 --help.")
                                   .arg(QString::fromLatin1(BreezeDesk::AppConfig::CliExecutableName)));
    }
    return static_cast<int>(result);
}
