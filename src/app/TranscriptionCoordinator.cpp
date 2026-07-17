#include "breezedesk/app/TranscriptionCoordinator.h"

#include "breezedesk/app/WorkerProcessManager.h"
#include "breezedesk/app_config.h"
#include "breezedesk/asr/AsrTypes.h"
#include "breezedesk/asr/LongFormChunkPlanner.h"
#include "breezedesk/asr/OverlapDeduplicator.h"
#include "breezedesk/audio/AudioCacheManager.h"
#include "breezedesk/audio/FFmpegLocator.h"
#include "breezedesk/audio/FFmpegNormalizationService.h"
#include "breezedesk/audio/FFprobeService.h"
#include "breezedesk/audio/WaveformGenerator.h"
#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/database/IRecordingRepository.h"
#include "breezedesk/glossary/GlossaryPostProcessor.h"
#include "breezedesk/glossary/IGlossaryRepository.h"
#include "breezedesk/ipc/AsrWorkerClient.h"
#include "breezedesk/jobs/IJobRepository.h"
#include "breezedesk/jobs/JobStateMachine.h"
#include "breezedesk/models/ModelManager.h"
#include "breezedesk/settings/SettingsManagers.h"
#include "breezedesk/transcript/ITranscriptRepository.h"
#include "breezedesk/version.h"

#include <QCborArray>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent>

#include <algorithm>
#include <utility>

namespace BreezeDesk {
namespace {

constexpr int WorkerReadyAttempts = 100;
constexpr int WorkerReadyIntervalMs = 100;
constexpr qsizetype MaximumWorkerChunks = 4'096;
constexpr qint64 SegmentTimestampToleranceMs = 1'000;
constexpr qint64 ShortAudioThresholdMs = 12 * 60 * 1'000;
constexpr double DefaultVadThreshold = 0.5;
constexpr qint64 DefaultVadMinimumSpeechMs = 250;
constexpr qint64 DefaultVadMinimumSilenceMs = 100;
constexpr double DefaultVadMaximumSpeechSeconds = 900.0;
constexpr qint64 DefaultVadSpeechPaddingMs = 30;
constexpr auto VadModelId = "silero-vad-v6.2.0";

QList<GlossaryTerm> glossaryTermsFromParameters(const QJsonObject& parameters) {
    QList<GlossaryTerm> terms;
    const QJsonArray encoded = parameters.value(QStringLiteral("glossaryTerms")).toArray();
    terms.reserve(encoded.size());
    for (const QJsonValue& value : encoded) {
        const QJsonObject item = value.toObject();
        GlossaryTerm term;
        term.id = item.value(QStringLiteral("id")).toString();
        term.canonicalText = item.value(QStringLiteral("canonicalText")).toString();
        for (const QJsonValue& alias : item.value(QStringLiteral("aliases")).toArray()) {
            if (!alias.toString().trimmed().isEmpty()) {
                term.aliases.append(alias.toString());
            }
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

struct InspectionResult {
    MediaMetadata metadata;
    QString error;
};

struct WaveformGenerationResult {
    bool success = false;
    QString path;
    QString error;
};

QList<JobChunk> makeJobChunks(const QString& jobId, const QList<Asr::TranscriptionChunk>& planned) {
    QList<JobChunk> chunks;
    chunks.reserve(planned.size());
    for (const Asr::TranscriptionChunk& source : planned) {
        JobChunk chunk;
        chunk.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        chunk.jobId = jobId;
        chunk.ordinal = source.ordinal;
        chunk.startMs = source.startMs;
        chunk.endMs = source.endMs;
        chunk.overlapBeforeMs = source.overlapBeforeMs;
        chunk.overlapAfterMs = source.overlapAfterMs;
        chunks.append(std::move(chunk));
    }
    return chunks;
}

bool readInteger(const QCborMap& map, const QString& key, qint64* value) {
    const QCborValue encoded = map.value(key);
    if (!encoded.isInteger() || value == nullptr) {
        return false;
    }
    *value = encoded.toInteger();
    return true;
}

bool decodeWorkerChunkPlan(const QCborMap& payload, const QString& jobId, QList<JobChunk>* chunks,
                           qint64* durationMs, QString* error) {
    if (chunks == nullptr || durationMs == nullptr) {
        return false;
    }
    chunks->clear();
    const QCborValue durationValue = payload.value(QStringLiteral("durationMs"));
    const QCborValue chunksValue = payload.value(QStringLiteral("chunks"));
    if (!durationValue.isInteger() || !chunksValue.isArray()) {
        if (error != nullptr) {
            *error = QStringLiteral("SpeechAnalysisCompleted is missing durationMs or chunks.");
        }
        return false;
    }
    *durationMs = durationValue.toInteger();
    const QCborArray encodedChunks = chunksValue.toArray();
    if (*durationMs <= 0 || encodedChunks.isEmpty() || encodedChunks.size() > MaximumWorkerChunks) {
        if (error != nullptr) {
            *error =
                QStringLiteral("The worker returned an invalid speech-analysis duration or chunk count.");
        }
        return false;
    }

    qint64 previousEnd = 0;
    qint64 previousOverlapAfter = 0;
    chunks->reserve(encodedChunks.size());
    for (qsizetype index = 0; index < encodedChunks.size(); ++index) {
        const QCborValue encodedChunk = encodedChunks.at(index);
        if (!encodedChunk.isMap()) {
            if (error != nullptr) {
                *error = QStringLiteral("The worker returned a non-map transcription chunk.");
            }
            return false;
        }
        const QCborMap map = encodedChunk.toMap();
        qint64 ordinal = -1;
        qint64 startMs = -1;
        qint64 endMs = -1;
        qint64 overlapBeforeMs = -1;
        qint64 overlapAfterMs = -1;
        if (!readInteger(map, QStringLiteral("ordinal"), &ordinal) ||
            !readInteger(map, QStringLiteral("startMs"), &startMs) ||
            !readInteger(map, QStringLiteral("endMs"), &endMs) ||
            !readInteger(map, QStringLiteral("overlapBeforeMs"), &overlapBeforeMs) ||
            !readInteger(map, QStringLiteral("overlapAfterMs"), &overlapAfterMs) ||
            ordinal != static_cast<qint64>(index) || startMs < 0 || endMs <= startMs || endMs > *durationMs ||
            overlapBeforeMs < 0 || overlapAfterMs < 0 || overlapBeforeMs >= endMs - startMs ||
            overlapAfterMs >= endMs - startMs) {
            if (error != nullptr) {
                *error = QStringLiteral("The worker returned an invalid transcription chunk at index %1.")
                             .arg(index);
            }
            return false;
        }
        if ((index == 0 && (startMs != 0 || overlapBeforeMs != 0)) ||
            (index > 0 &&
             (overlapBeforeMs != previousOverlapAfter || startMs != previousEnd - overlapBeforeMs))) {
            if (error != nullptr) {
                *error = QStringLiteral("The worker returned a discontinuous transcription chunk plan.");
            }
            return false;
        }
        JobChunk chunk;
        chunk.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        chunk.jobId = jobId;
        chunk.ordinal = static_cast<int>(ordinal);
        chunk.startMs = startMs;
        chunk.endMs = endMs;
        chunk.overlapBeforeMs = overlapBeforeMs;
        chunk.overlapAfterMs = overlapAfterMs;
        chunks->append(std::move(chunk));
        previousEnd = endMs;
        previousOverlapAfter = overlapAfterMs;
    }
    if (chunks->constLast().endMs != *durationMs || chunks->constLast().overlapAfterMs != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("The worker chunk plan does not cover the normalized audio exactly.");
        }
        return false;
    }
    return true;
}

QString workerErrorCode(const QCborMap& payload, const bool analyzingSpeech, const bool loadingModel) {
    const auto code = static_cast<Asr::AsrErrorCode>(
        payload.value(QStringLiteral("code"))
            .toInteger(static_cast<qint64>(Asr::AsrErrorCode::InferenceFailed)));
    switch (code) {
    case Asr::AsrErrorCode::ModelFileMissing:
        return QStringLiteral("ModelNotInstalled");
    case Asr::AsrErrorCode::ModelChecksumMismatch:
        return QStringLiteral("ModelChecksumMismatch");
    case Asr::AsrErrorCode::ModelLoadFailed:
    case Asr::AsrErrorCode::VadModelLoadFailed:
        return QStringLiteral("ModelLoadFailed");
    case Asr::AsrErrorCode::InvalidAudio:
        return QStringLiteral("AudioDecodeFailed");
    case Asr::AsrErrorCode::Busy:
        return QStringLiteral("WorkerTimeout");
    case Asr::AsrErrorCode::Cancelled:
        return QStringLiteral("JobCancelled");
    case Asr::AsrErrorCode::RuntimeUnavailable:
        return QStringLiteral("BackendUnavailable");
    case Asr::AsrErrorCode::InvalidRequest:
        return QStringLiteral("WorkerProtocolMismatch");
    case Asr::AsrErrorCode::IoError:
        return QStringLiteral("SourceFileMissing");
    case Asr::AsrErrorCode::InferenceFailed:
        if (analyzingSpeech) {
            return QStringLiteral("AudioDecodeFailed");
        }
        return loadingModel ? QStringLiteral("ModelLoadFailed") : QStringLiteral("TranscriptionFailed");
    case Asr::AsrErrorCode::None:
        break;
    }
    return QStringLiteral("ModelLoadFailed");
}

QString segmentDigest(const QList<TranscriptSegment>& segments) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    static constexpr char Separator = '\0';
    const QByteArrayView separator(&Separator, 1);
    for (const TranscriptSegment& segment : segments) {
        hash.addData(QByteArray::number(segment.startMs));
        hash.addData(separator);
        hash.addData(QByteArray::number(segment.endMs));
        hash.addData(separator);
        hash.addData(segment.originalText.toUtf8());
        hash.addData(separator);
    }
    return QString::fromLatin1(hash.result().toHex());
}

void reconcileOverlap(const TranscriptSegment& previous, qint64 chunkEnd,
                      QList<TranscriptSegment>* incoming) {
    if (incoming == nullptr || incoming->isEmpty()) {
        return;
    }
    auto deduplicated =
        Asr::OverlapDeduplicator::deduplicate(previous.displayText(), incoming->first().originalText, true);
    if (deduplicated.matchedUnits > 0) {
        incoming->first().originalText = deduplicated.text;
        if (incoming->first().originalText.isEmpty() && incoming->first().endMs <= previous.endMs) {
            incoming->removeFirst();
        }
    }

    QStringList preservedAmbiguousText;
    while (!incoming->isEmpty() && incoming->first().endMs <= previous.endMs) {
        if (!incoming->first().originalText.trimmed().isEmpty()) {
            preservedAmbiguousText.append(incoming->first().originalText.trimmed());
        }
        incoming->removeFirst();
    }
    if (!preservedAmbiguousText.isEmpty()) {
        const QString preserved = preservedAmbiguousText.join(QLatin1Char(' '));
        if (incoming->isEmpty()) {
            TranscriptSegment segment;
            segment.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            segment.startMs = previous.endMs;
            segment.endMs = std::max(previous.endMs + 1, chunkEnd);
            segment.originalText = preserved;
            incoming->append(std::move(segment));
        } else {
            incoming->first().originalText =
                (preserved + QLatin1Char(' ') + incoming->first().originalText).simplified();
        }
    }
    if (!incoming->isEmpty() && incoming->first().startMs < previous.endMs) {
        incoming->first().startMs = previous.endMs;
        if (incoming->first().endMs <= incoming->first().startMs) {
            incoming->first().endMs = incoming->first().startMs + 1;
        }
    }
}

} // namespace

TranscriptionCoordinator::TranscriptionCoordinator(IRecordingRepository& recordings, IJobRepository& jobs,
                                                   ITranscriptRepository& transcripts, ModelManager& models,
                                                   WorkerProcessManager& worker,
                                                   TranscriptionSettingsManager* settings, QObject* parent)
    : QObject(parent), m_recordings(recordings), m_jobs(jobs), m_transcripts(transcripts), m_models(models),
      m_worker(worker), m_settings(settings) {
    const auto tools = FFmpegLocator::locate();
    if (tools.isValid()) {
        m_normalizer = std::make_unique<FFmpegNormalizationService>(tools.ffmpegPath, this);
    }
    connect(m_worker.client(), &Ipc::AsrWorkerClient::envelopeReceived, this,
            &TranscriptionCoordinator::handleWorkerEnvelope);
    connect(&m_worker, &WorkerProcessManager::workerInterrupted, this,
            &TranscriptionCoordinator::interruptActiveJob);
}

TranscriptionCoordinator::~TranscriptionCoordinator() {
    clearLoadedAsrModel();
    clearLoadedVadModel();
}

void TranscriptionCoordinator::initialize() {
    if (m_initialized) {
        return;
    }
    m_initialized = true;
    const auto recovered =
        m_jobs.markRunningJobsInterrupted(tr("%1 was not shut down while this job was running.")
                                              .arg(QString::fromLatin1(AppConfig::ProductName)));
    if (!recovered) {
        emit errorOccurred(recovered.error().message);
    }
    const auto jobs = m_jobs.list(true);
    if (!jobs) {
        emit errorOccurred(jobs.error().message);
        return;
    }
    for (const TranscriptionJob& job : jobs.value()) {
        publish(job);
    }
    QTimer::singleShot(0, this, &TranscriptionCoordinator::scheduleNext);
}

void TranscriptionCoordinator::setGlossaryRepository(IGlossaryRepository* repository) {
    m_glossaryRepository = repository;
}

void TranscriptionCoordinator::shutdown() {
    if (m_shuttingDown) {
        return;
    }
    m_shuttingDown = true;
    m_pauseAfterCurrent = true;
    if (m_activeJob.id.isEmpty()) {
        clearLoadedAsrModel();
        clearLoadedVadModel();
        return;
    }
    if (m_normalization != nullptr && m_normalization->isRunning()) {
        m_normalization->cancel();
    } else if (m_waveformCancellation != nullptr) {
        m_waveformCancellation->store(true, std::memory_order_relaxed);
    } else if (m_requestKind == RequestKind::AnalyzeSpeech || m_requestKind == RequestKind::TranscribeChunk) {
        m_worker.client()->sendRequest(Ipc::MessageType::CancelJob, m_activeJob.id, {});
    }
    interruptActiveJob(
        tr("%1 closed while this job was running.").arg(QString::fromLatin1(AppConfig::ProductName)));
}

void TranscriptionCoordinator::enqueue(const QString& jobId, const QString& recordingId) {
    if (jobId.isEmpty() || recordingId.isEmpty()) {
        emit errorOccurred(tr("A recording and job ID are required."));
        return;
    }
    const auto recording = m_recordings.findById(recordingId);
    if (!recording || !recording.value().has_value()) {
        emit errorOccurred(recording ? tr("The recording no longer exists.") : recording.error().message);
        return;
    }
    TranscriptionJob job;
    job.id = jobId;
    job.recordingId = recordingId;
    job.modelId = m_settings == nullptr ? m_models.defaultModelId() : m_settings->defaultModelId();
    job.modelChecksum = QString::fromLatin1(m_models.expectedSha256(job.modelId));
    job.engineVersion = QStringLiteral("whisper.cpp-v1.9.1");
    job.workerVersion = QString::fromLatin1(BREEZEDESK_VERSION_STRING);
    job.backend =
        m_settings == nullptr ? QStringLiteral("auto") : backendPreferenceName(m_settings->backend());
    job.language = m_settings == nullptr ? QStringLiteral("zh") : m_settings->language();
    job.preset = m_settings == nullptr ? QStringLiteral("balanced") : m_settings->preset();
    job.vadEnabled = m_settings == nullptr || m_settings->vadEnabled();
    if (m_settings != nullptr) {
        job.parameters.insert(QStringLiteral("threadCount"), m_settings->threadCount());
        job.parameters.insert(QStringLiteral("flashAttention"), m_settings->flashAttention());
        job.parameters.insert(QStringLiteral("tokenTimestamps"), m_settings->tokenTimestamps());
        job.parameters.insert(QStringLiteral("lowConfidenceThreshold"), m_settings->lowConfidenceThreshold());
        job.parameters.insert(QStringLiteral("initialPromptBehavior"), m_settings->initialPromptBehavior());
        job.glossaryProfileId = m_settings->glossaryProfileId();
    }
    if (m_glossaryRepository != nullptr && !job.glossaryProfileId.isEmpty() &&
        job.parameters.value(QStringLiteral("initialPromptBehavior")).toString() !=
            QLatin1String("disabled")) {
        const auto profile = m_glossaryRepository->profile(job.glossaryProfileId);
        if (!profile) {
            emit errorOccurred(profile.error().message);
            return;
        }
        if (!profile.value().has_value()) {
            emit errorOccurred(tr("The selected glossary profile no longer exists."));
            return;
        }
        job.meetingContext = profile.value()->projectContext;
        const auto glossaryTerms = m_glossaryRepository->terms(job.glossaryProfileId);
        if (!glossaryTerms) {
            emit errorOccurred(glossaryTerms.error().message);
            return;
        }
        QJsonArray snapshot;
        for (const GlossaryTerm& term : glossaryTerms.value()) {
            QJsonArray aliases;
            for (const QString& alias : term.aliases) {
                aliases.append(alias);
            }
            snapshot.append(QJsonObject{{QStringLiteral("id"), term.id},
                                        {QStringLiteral("canonicalText"), term.canonicalText},
                                        {QStringLiteral("aliases"), aliases},
                                        {QStringLiteral("priority"), term.priority},
                                        {QStringLiteral("caseSensitive"), term.caseSensitive},
                                        {QStringLiteral("enabled"), term.enabled}});
        }
        job.parameters.insert(QStringLiteral("glossaryTerms"), snapshot);
    }
    job.createdAt = QDateTime::currentDateTimeUtc();
    const auto created = m_jobs.create(job);
    if (!created) {
        emit errorOccurred(created.error().message);
        return;
    }
    publish(job);
    scheduleNext();
}

void TranscriptionCoordinator::cancel(const QString& jobId) {
    const auto current = m_jobs.findById(jobId);
    if (!current || !current.value().has_value()) {
        emit errorOccurred(current ? tr("The job no longer exists.") : current.error().message);
        return;
    }
    if (!activeJobMatches(jobId)) {
        const auto result = m_jobs.transition(jobId, JobState::Cancelled);
        if (!result) {
            emit errorOccurred(result.error().message);
        }
        publish(jobId);
        return;
    }
    const auto transition = m_jobs.transition(jobId, JobState::Cancelling);
    if (!transition) {
        emit errorOccurred(transition.error().message);
        return;
    }
    m_activeJob.state = JobState::Cancelling;
    publish(jobId);
    if (m_normalization != nullptr && m_normalization->isRunning()) {
        m_normalization->cancel();
    } else if (m_waveformCancellation != nullptr) {
        m_waveformCancellation->store(true, std::memory_order_relaxed);
    } else if (m_requestKind == RequestKind::AnalyzeSpeech || m_requestKind == RequestKind::TranscribeChunk) {
        m_worker.forceCancelAfterGrace(jobId);
    } else if (m_requestKind == RequestKind::LoadModel) {
        // Model loading is not safely interruptible inside whisper.cpp. Finish the load,
        // then honour cancellation without starting inference.
    } else {
        finishCancellation();
    }
}

void TranscriptionCoordinator::retry(const QString& jobId) {
    const auto transition = m_jobs.transition(jobId, JobState::Queued);
    if (!transition) {
        emit errorOccurred(transition.error().message);
        return;
    }
    publish(jobId);
    scheduleNext();
}

void TranscriptionCoordinator::resume(const QString& jobId) {
    retry(jobId);
}

void TranscriptionCoordinator::reorder(const QString& jobId, int destination) {
    const auto jobs = m_jobs.list(false);
    if (!jobs) {
        emit errorOccurred(jobs.error().message);
        return;
    }
    QStringList ordered;
    for (const TranscriptionJob& job : jobs.value()) {
        if (job.state == JobState::Queued) {
            ordered.append(job.id);
        }
    }
    const qsizetype source = ordered.indexOf(jobId);
    if (source < 0 || ordered.isEmpty()) {
        return;
    }
    const int bounded = qBound(0, destination, static_cast<int>(ordered.size()) - 1);
    ordered.move(source, bounded);
    const auto result = m_jobs.reorder(ordered);
    if (!result) {
        emit errorOccurred(result.error().message);
    }
}

void TranscriptionCoordinator::clearCompleted() {
    const auto result = m_jobs.clearCompleted();
    if (!result) {
        emit errorOccurred(result.error().message);
    }
}

void TranscriptionCoordinator::setPauseAfterCurrent(bool enabled) {
    m_pauseAfterCurrent = enabled;
    if (!enabled) {
        scheduleNext();
    }
}

bool TranscriptionCoordinator::isTranscriptionActive() const noexcept {
    return !m_activeJob.id.isEmpty();
}

void TranscriptionCoordinator::setExternalWorkerReserved(bool reserved) {
    if (m_externalWorkerReserved == reserved) {
        return;
    }
    m_externalWorkerReserved = reserved;
    if (!reserved) {
        scheduleNext();
    }
}

void TranscriptionCoordinator::invalidateLoadedModel() {
    if (!isTranscriptionActive()) {
        clearLoadedAsrModel();
    }
}

void TranscriptionCoordinator::scheduleNext() {
    if (!m_initialized || m_shuttingDown || !m_activeJob.id.isEmpty() || m_pauseAfterCurrent ||
        m_externalWorkerReserved) {
        return;
    }
    const auto jobs = m_jobs.list(false);
    if (!jobs) {
        emit errorOccurred(jobs.error().message);
        return;
    }
    const auto iterator =
        std::find_if(jobs.value().cbegin(), jobs.value().cend(),
                     [](const TranscriptionJob& job) { return job.state == JobState::Queued; });
    if (iterator != jobs.value().cend()) {
        beginJob(*iterator);
    }
}

void TranscriptionCoordinator::beginJob(const TranscriptionJob& job) {
    m_activeJob = job;
    const auto transition = m_jobs.transition(job.id, JobState::Preparing);
    if (!transition) {
        emit errorOccurred(transition.error().message);
        clearActive();
        return;
    }
    m_activeJob.state = JobState::Preparing;
    advanceProgress(JobStage::Preparing, 1.0);
    const auto recording = m_recordings.findById(job.recordingId);
    if (!recording || !recording.value().has_value()) {
        failActiveJob(QStringLiteral("SourceFileMissing"),
                      recording ? tr("The recording no longer exists.") : recording.error().message);
        return;
    }
    const Recording& value = recording.value().value();
    m_activeSourcePath =
        QFileInfo::exists(value.managedMediaPath) ? value.managedMediaPath : value.sourcePath;
    m_activeNormalizedPath = value.normalizedPcmPath.isEmpty()
                                 ? AudioCacheManager::normalizedAudioPath(value.id)
                                 : value.normalizedPcmPath;
    if (!QFileInfo(m_activeSourcePath).isFile()) {
        failActiveJob(QStringLiteral("SourceFileMissing"), tr("The source media file is missing."));
        return;
    }
    if (QFileInfo(m_activeNormalizedPath).size() > 44 && value.durationMs > 0) {
        m_activeJob.parameters.insert(QStringLiteral("durationMs"), value.durationMs);
        if (!value.waveformPath.isEmpty() && QFileInfo(value.waveformPath).isFile()) {
            prepareChunks();
        } else {
            beginWaveformGeneration();
        }
        return;
    }
    inspectMedia();
}

void TranscriptionCoordinator::inspectMedia() {
    const auto tools = FFmpegLocator::locate();
    if (!tools.isValid()) {
        failActiveJob(QStringLiteral("AudioDecodeFailed"), tools.error);
        return;
    }
    advanceProgress(JobStage::InspectingMedia, 0.0);
    const QString source = m_activeSourcePath;
    const QString ffprobe = tools.ffprobePath;
    const QString jobId = m_activeJob.id;
    auto* watcher = new QFutureWatcher<InspectionResult>(this);
    connect(watcher, &QFutureWatcher<InspectionResult>::finished, this, [this, watcher, jobId] {
        const InspectionResult result = watcher->result();
        watcher->deleteLater();
        if (!activeJobMatches(jobId) || m_activeJob.state == JobState::Cancelling) {
            return;
        }
        if (!result.metadata.hasAudio || result.metadata.durationMs <= 0) {
            failActiveJob(QStringLiteral("UnsupportedMedia"),
                          result.error.isEmpty() ? tr("The media does not contain supported audio.")
                                                 : result.error);
            return;
        }
        const auto recording = m_recordings.findById(m_activeJob.recordingId);
        if (!recording || !recording.value().has_value()) {
            failActiveJob(QStringLiteral("SourceFileMissing"), tr("The recording no longer exists."));
            return;
        }
        Recording updated = recording.value().value();
        updated.durationMs = result.metadata.durationMs;
        updated.sampleRate = result.metadata.sampleRate;
        updated.channelCount = result.metadata.channelCount;
        updated.mediaType = result.metadata.hasVideo ? QStringLiteral("video") : QStringLiteral("audio");
        const auto saved = m_recordings.update(updated);
        if (!saved) {
            failActiveJob(QStringLiteral("DatabaseQueryFailed"), saved.error().message);
            return;
        }
        m_activeJob.parameters.insert(QStringLiteral("durationMs"), result.metadata.durationMs);
        advanceProgress(JobStage::InspectingMedia, 1.0);
        beginNormalization();
    });
    watcher->setFuture(QtConcurrent::run([source, ffprobe] {
        InspectionResult result;
        result.metadata = FFprobeService(ffprobe).inspect(source, &result.error);
        return result;
    }));
}

void TranscriptionCoordinator::beginNormalization() {
    if (m_normalizer == nullptr) {
        failActiveJob(QStringLiteral("AudioDecodeFailed"), tr("ffmpeg is not available."));
        return;
    }
    const auto transition = m_jobs.transition(m_activeJob.id, JobState::Normalizing);
    if (!transition) {
        failActiveJob(QStringLiteral("InvalidStateTransition"), transition.error().message);
        return;
    }
    m_activeJob.state = JobState::Normalizing;
    m_lastNormalizationPercent = -1;
    const qint64 duration =
        m_activeJob.parameters.value(QStringLiteral("durationMs")).toVariant().toLongLong();
    m_normalization = m_normalizer->normalize(m_activeSourcePath, m_activeNormalizedPath, duration, this);
    connect(m_normalization, &NormalizationOperation::progressChanged, this, [this] {
        if (m_normalization == nullptr) {
            return;
        }
        const int percent = qRound(m_normalization->progress() * 100.0);
        if (percent != m_lastNormalizationPercent) {
            m_lastNormalizationPercent = percent;
            advanceProgress(JobStage::NormalizingAudio, m_normalization->progress());
        }
    });
    connect(m_normalization, &NormalizationOperation::finished, this,
            [this](bool success, const QString& outputPath) {
                if (m_activeJob.id.isEmpty()) {
                    return;
                }
                if (m_activeJob.state == JobState::Cancelling) {
                    finishCancellation();
                    return;
                }
                if (!success) {
                    const QString message = m_normalization == nullptr ? tr("Audio normalization failed.")
                                                                       : m_normalization->error();
                    failActiveJob(QStringLiteral("AudioDecodeFailed"), message);
                    return;
                }
                m_activeNormalizedPath = outputPath;
                beginWaveformGeneration();
            });
}

void TranscriptionCoordinator::beginWaveformGeneration() {
    if (m_activeJob.state != JobState::Normalizing) {
        const auto transition = m_jobs.transition(m_activeJob.id, JobState::Normalizing);
        if (!transition) {
            failActiveJob(QStringLiteral("InvalidStateTransition"), transition.error().message);
            return;
        }
        m_activeJob.state = JobState::Normalizing;
    }
    advanceProgress(JobStage::NormalizingAudio, 0.98);
    const QString jobId = m_activeJob.id;
    const QString recordingId = m_activeJob.recordingId;
    const QString normalizedPath = m_activeNormalizedPath;
    const QString waveformPath = AudioCacheManager::waveformPath(recordingId);
    m_waveformCancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = m_waveformCancellation;
    auto* watcher = new QFutureWatcher<WaveformGenerationResult>(this);
    connect(watcher, &QFutureWatcher<WaveformGenerationResult>::finished, this,
            [this, watcher, jobId, recordingId, normalizedPath, cancellation] {
                const WaveformGenerationResult result = watcher->result();
                watcher->deleteLater();
                if (!activeJobMatches(jobId)) {
                    return;
                }
                m_waveformCancellation.reset();
                if (m_activeJob.state == JobState::Cancelling ||
                    cancellation->load(std::memory_order_relaxed)) {
                    finishCancellation();
                    return;
                }
                if (!result.success) {
                    failActiveJob(QStringLiteral("AudioDecodeFailed"),
                                  result.error.isEmpty() ? tr("The audio waveform could not be generated.")
                                                         : result.error);
                    return;
                }
                const auto recording = m_recordings.findById(recordingId);
                if (!recording || !recording.value().has_value()) {
                    failActiveJob(QStringLiteral("DatabaseQueryFailed"),
                                  tr("The normalized recording could not be saved."));
                    return;
                }
                Recording updated = recording.value().value();
                updated.normalizedPcmPath = normalizedPath;
                updated.waveformPath = result.path;
                const auto saved = m_recordings.update(updated);
                if (!saved) {
                    failActiveJob(QStringLiteral("DatabaseQueryFailed"), saved.error().message);
                    return;
                }
                advanceProgress(JobStage::NormalizingAudio, 1.0);
                prepareChunks();
            });
    watcher->setFuture(QtConcurrent::run([normalizedPath, waveformPath, cancellation] {
        WaveformGenerationResult result;
        result.path = waveformPath;
        result.success =
            WaveformGenerator::generate(normalizedPath, waveformPath, cancellation.get(), &result.error);
        return result;
    }));
}

void TranscriptionCoordinator::prepareChunks() {
    const auto existing = m_jobs.chunks(m_activeJob.id);
    if (!existing) {
        failActiveJob(QStringLiteral("DatabaseQueryFailed"), existing.error().message);
        return;
    }
    m_chunks = existing.value();
    if (!m_chunks.isEmpty()) {
        beginWaitingForModel();
        return;
    }

    const qint64 duration =
        m_activeJob.parameters.value(QStringLiteral("durationMs")).toVariant().toLongLong();
    if (duration <= 0) {
        failActiveJob(QStringLiteral("UnsupportedMedia"), tr("The recording duration is invalid."));
        return;
    }
    if (!m_activeJob.vadEnabled || duration <= ShortAudioThresholdMs) {
        QString error;
        if (!saveChunkPlan(makeJobChunks(m_activeJob.id, Asr::LongFormChunkPlanner().plan(duration, {})),
                           &error)) {
            failActiveJob(QStringLiteral("DatabaseQueryFailed"), error);
            return;
        }
        beginWaitingForModel();
        return;
    }

    const QString vadPath = m_models.modelPath(QString::fromLatin1(VadModelId));
    if (!QFileInfo(vadPath).isFile()) {
        failActiveJob(QStringLiteral("ModelNotInstalled"),
                      tr("Install the Silero VAD model before transcribing long recordings with VAD."));
        return;
    }
    const auto analyzing = m_jobs.transition(m_activeJob.id, JobState::AnalyzingSpeech);
    if (!analyzing) {
        failActiveJob(QStringLiteral("InvalidStateTransition"), analyzing.error().message);
        return;
    }
    m_activeJob.state = JobState::AnalyzingSpeech;
    advanceProgress(JobStage::AnalyzingSpeech, 0.0);
    ensureWorkerReady(m_activeJob.id);
}

bool TranscriptionCoordinator::saveChunkPlan(QList<JobChunk> chunks, QString* error) {
    if (chunks.isEmpty()) {
        if (error != nullptr) {
            *error = tr("Speech analysis did not produce a transcription chunk plan.");
        }
        return false;
    }
    const auto saved = m_jobs.replaceChunks(m_activeJob.id, chunks);
    if (!saved) {
        if (error != nullptr) {
            *error = saved.error().message;
        }
        return false;
    }
    m_chunks = std::move(chunks);
    return true;
}

void TranscriptionCoordinator::beginWaitingForModel() {
    const auto waiting = m_jobs.transition(m_activeJob.id, JobState::WaitingForModel);
    if (!waiting) {
        failActiveJob(QStringLiteral("InvalidStateTransition"), waiting.error().message);
        return;
    }
    m_activeJob.state = JobState::WaitingForModel;
    advanceProgress(JobStage::AnalyzingSpeech, 1.0);
    ensureWorkerReady(m_activeJob.id);
}

void TranscriptionCoordinator::ensureWorkerReady(const QString& jobId, int attempt) {
    if (!activeJobMatches(jobId)) {
        return;
    }
    if (m_activeJob.state == JobState::Cancelling) {
        finishCancellation();
        return;
    }
    if (m_worker.isReady()) {
        if (m_activeJob.state == JobState::AnalyzingSpeech) {
            analyzeSpeech();
        } else {
            loadModel();
        }
        return;
    }
    if (attempt == 0 && !m_worker.start()) {
        failActiveJob(QStringLiteral("WorkerCrashed"), m_worker.lastError());
        return;
    }
    if (attempt >= WorkerReadyAttempts) {
        failActiveJob(QStringLiteral("WorkerTimeout"), tr("The ASR worker did not become ready."));
        return;
    }
    QTimer::singleShot(WorkerReadyIntervalMs, this,
                       [this, jobId, attempt] { ensureWorkerReady(jobId, attempt + 1); });
}

void TranscriptionCoordinator::analyzeSpeech() {
    if (m_activeJob.state != JobState::AnalyzingSpeech) {
        return;
    }
    const QString vadPath = m_models.modelPath(QString::fromLatin1(VadModelId));
    if (!QFileInfo(vadPath).isFile()) {
        failActiveJob(QStringLiteral("ModelNotInstalled"),
                      tr("The Silero VAD model is no longer installed."));
        return;
    }
    const QByteArray vadSha256 = m_models.expectedSha256(QString::fromLatin1(VadModelId));
    if (vadSha256.size() != 64) {
        failActiveJob(QStringLiteral("ModelChecksumMismatch"),
                      tr("The Silero VAD model does not have a trusted checksum."));
        return;
    }
    if (!m_loadedVadModelId.isEmpty() && m_loadedVadPath != vadPath) {
        clearLoadedVadModel();
    }
    m_loadedVadModelId = QString::fromLatin1(VadModelId);
    m_loadedVadPath = vadPath;
    m_models.setModelInUse(m_loadedVadModelId, true);
    m_requestKind = RequestKind::AnalyzeSpeech;
    m_requestId = m_worker.client()->sendRequest(
        Ipc::MessageType::AnalyzeSpeech, m_activeJob.id,
        {{QStringLiteral("pcmPath"), m_activeNormalizedPath},
         {QStringLiteral("vadModelPath"), vadPath},
         {QStringLiteral("vadModelSha256"), QString::fromLatin1(vadSha256)},
         {QStringLiteral("vadThreshold"), DefaultVadThreshold},
         {QStringLiteral("vadMinimumSpeechMs"), DefaultVadMinimumSpeechMs},
         {QStringLiteral("vadMinimumSilenceMs"), DefaultVadMinimumSilenceMs},
         {QStringLiteral("vadMaximumSpeechSeconds"), DefaultVadMaximumSpeechSeconds},
         {QStringLiteral("vadSpeechPaddingMs"), DefaultVadSpeechPaddingMs}});
    if (m_requestId.isEmpty()) {
        failActiveJob(QStringLiteral("WorkerCrashed"),
                      tr("The speech-analysis request could not be sent to the ASR worker."));
    }
}

void TranscriptionCoordinator::loadModel() {
    const QString modelPath = m_models.modelPath(m_activeJob.modelId);
    if (!QFileInfo(modelPath).isFile()) {
        failActiveJob(QStringLiteral("ModelNotInstalled"),
                      tr("Install the selected ASR model before starting transcription."));
        return;
    }
    const QByteArray modelSha256 = m_activeJob.modelChecksum.isEmpty()
                                       ? m_models.expectedSha256(m_activeJob.modelId)
                                       : m_activeJob.modelChecksum.toLatin1();
    if (modelSha256.size() != 64) {
        failActiveJob(QStringLiteral("ModelChecksumMismatch"),
                      tr("The selected ASR model does not have a trusted checksum."));
        return;
    }
    const auto loading = m_jobs.transition(m_activeJob.id, JobState::LoadingModel);
    if (!loading) {
        failActiveJob(QStringLiteral("InvalidStateTransition"), loading.error().message);
        return;
    }
    m_activeJob.state = JobState::LoadingModel;
    advanceProgress(JobStage::LoadingModel, 0.0);
    const bool requestedFlashAttention =
        m_activeJob.parameters.value(QStringLiteral("flashAttention")).toBool(true);
    if (m_loadedModelPath == modelPath && m_loadedModelSha256 == modelSha256.toLower() &&
        m_loadedBackend == m_activeJob.backend && m_loadedFlashAttention == requestedFlashAttention) {
        QString error;
        if (!persistLoadedRuntimeInfo(&error)) {
            failActiveJob(QStringLiteral("DatabaseQueryFailed"), error);
            return;
        }
        const auto transcribing = m_jobs.transition(m_activeJob.id, JobState::Transcribing);
        if (!transcribing) {
            failActiveJob(QStringLiteral("InvalidStateTransition"), transcribing.error().message);
            return;
        }
        m_activeJob.state = JobState::Transcribing;
        startNextChunk();
        return;
    }
    m_requestKind = RequestKind::LoadModel;
    m_requestId =
        m_worker.client()->sendRequest(Ipc::MessageType::LoadModel, {},
                                       {{QStringLiteral("modelPath"), modelPath},
                                        {QStringLiteral("modelSha256"), QString::fromLatin1(modelSha256)},
                                        {QStringLiteral("backend"), m_activeJob.backend},
                                        {QStringLiteral("flashAttention"), requestedFlashAttention}});
    if (m_requestId.isEmpty()) {
        failActiveJob(QStringLiteral("WorkerCrashed"),
                      tr("The model-load request could not be sent to the ASR worker."));
    }
}

void TranscriptionCoordinator::startNextChunk() {
    if (m_activeJob.state == JobState::Cancelling) {
        finishCancellation();
        return;
    }
    int next = m_currentChunkIndex + 1;
    while (next < m_chunks.size() && m_chunks.at(next).state == ChunkState::Completed) {
        ++next;
    }
    if (next >= m_chunks.size()) {
        completeActiveJob();
        return;
    }
    m_currentChunkIndex = next;
    JobChunk& chunk = m_chunks[m_currentChunkIndex];
    chunk.state = ChunkState::Running;
    ++chunk.attempts;
    chunk.startedAt = QDateTime::currentDateTimeUtc();
    chunk.error.clear();
    const auto updated = m_jobs.updateChunk(chunk);
    if (!updated) {
        failActiveJob(QStringLiteral("DatabaseQueryFailed"), updated.error().message);
        return;
    }
    m_currentSegments.clear();
    const auto existingSegments = m_transcripts.segmentsForJob(m_activeJob.id, true);
    m_nextOrdinal = existingSegments ? static_cast<int>(existingSegments.value().size()) : 0;
    const bool finalChunk =
        std::none_of(m_chunks.cbegin() + m_currentChunkIndex + 1, m_chunks.cend(),
                     [](const JobChunk& candidate) { return candidate.state != ChunkState::Completed; });
    const QString vadPath = m_models.modelPath(QString::fromLatin1(VadModelId));
    if (m_activeJob.vadEnabled && !QFileInfo(vadPath).isFile()) {
        failActiveJob(QStringLiteral("ModelNotInstalled"),
                      tr("The Silero VAD model is no longer installed."));
        return;
    }
    const QByteArray vadSha256 =
        m_activeJob.vadEnabled ? m_models.expectedSha256(QString::fromLatin1(VadModelId)) : QByteArray{};
    if (m_activeJob.vadEnabled && vadSha256.size() != 64) {
        failActiveJob(QStringLiteral("ModelChecksumMismatch"),
                      tr("The Silero VAD model does not have a trusted checksum."));
        return;
    }
    QCborMap payload{
        {QStringLiteral("pcmPath"), m_activeNormalizedPath},
        {QStringLiteral("startMs"), chunk.startMs},
        {QStringLiteral("endMs"), chunk.endMs},
        {QStringLiteral("finalChunk"), finalChunk},
        {QStringLiteral("language"), m_activeJob.language},
        {QStringLiteral("preset"), m_activeJob.preset},
        {QStringLiteral("threadCount"), m_activeJob.parameters.value(QStringLiteral("threadCount")).toInt(0)},
        {QStringLiteral("tokenTimestamps"),
         m_activeJob.parameters.value(QStringLiteral("tokenTimestamps")).toBool(true)},
        {QStringLiteral("lowConfidenceThreshold"),
         m_activeJob.parameters.value(QStringLiteral("lowConfidenceThreshold")).toDouble(0.35)},
        {QStringLiteral("vadEnabled"), m_activeJob.vadEnabled && QFileInfo(vadPath).isFile()},
        {QStringLiteral("vadModelPath"), vadPath},
        {QStringLiteral("vadModelSha256"), QString::fromLatin1(vadSha256)}};
    QCborArray promptParts;
    const QList<GlossaryTerm> glossaryTerms = glossaryTermsFromParameters(m_activeJob.parameters);
    for (const GlossaryTerm& term : glossaryTerms) {
        if (!term.enabled) {
            continue;
        }
        QString text = term.canonicalText;
        if (!term.aliases.isEmpty()) {
            text += QStringLiteral(" (aliases: %1)").arg(term.aliases.join(QStringLiteral(", ")));
        }
        promptParts.append(QCborMap{{QStringLiteral("kind"), QStringLiteral("glossary")},
                                    {QStringLiteral("text"), text},
                                    {QStringLiteral("priority"), term.priority}});
    }
    if (!m_activeJob.meetingContext.trimmed().isEmpty()) {
        promptParts.append(QCborMap{{QStringLiteral("kind"), QStringLiteral("meetingContext")},
                                    {QStringLiteral("text"), m_activeJob.meetingContext}});
    }
    if (existingSegments && !existingSegments.value().isEmpty()) {
        QString previousContext;
        for (auto iterator = existingSegments.value().crbegin();
             iterator != existingSegments.value().crend() && previousContext.size() < 1'000; ++iterator) {
            if (!iterator->provisional && !iterator->displayText().trimmed().isEmpty()) {
                previousContext.prepend(iterator->displayText().trimmed() + QLatin1Char(' '));
            }
        }
        previousContext = previousContext.trimmed().right(1'000);
        if (!previousContext.isEmpty()) {
            promptParts.append(QCborMap{{QStringLiteral("kind"), QStringLiteral("previousTranscript")},
                                        {QStringLiteral("text"), previousContext}});
        }
    }
    if (!promptParts.isEmpty()) {
        payload.insert(QStringLiteral("promptParts"), promptParts);
    }
    m_requestKind = RequestKind::TranscribeChunk;
    m_requestId =
        m_worker.client()->sendRequest(Ipc::MessageType::StartTranscription, m_activeJob.id, payload);
    if (m_requestId.isEmpty()) {
        failActiveJob(QStringLiteral("WorkerCrashed"),
                      tr("The transcription request could not be sent to the ASR worker."));
        return;
    }
    const double completed = static_cast<double>(m_currentChunkIndex) /
                             static_cast<double>(std::max(1, static_cast<int>(m_chunks.size())));
    advanceProgress(JobStage::Transcribing, completed, m_activeJob.lastCompletedChunk);
}

void TranscriptionCoordinator::handleWorkerEnvelope(const Ipc::Envelope& envelope) {
    if (m_activeJob.id.isEmpty() || envelope.requestId != m_requestId) {
        return;
    }
    const bool jobScopedRequest =
        m_requestKind == RequestKind::AnalyzeSpeech || m_requestKind == RequestKind::TranscribeChunk;
    if (jobScopedRequest && envelope.jobId != m_activeJob.id) {
        return;
    }
    if (envelope.type == Ipc::MessageType::Error) {
        const auto asrError =
            static_cast<Asr::AsrErrorCode>(envelope.payload.value(QStringLiteral("code")).toInteger());
        if (m_activeJob.state == JobState::Cancelling && asrError == Asr::AsrErrorCode::Cancelled) {
            finishCancellation();
            return;
        }
        if ((m_requestKind == RequestKind::AnalyzeSpeech || m_requestKind == RequestKind::TranscribeChunk) &&
            (asrError == Asr::AsrErrorCode::ModelChecksumMismatch ||
             asrError == Asr::AsrErrorCode::ModelFileMissing)) {
            clearLoadedVadModel();
        }
        if (m_requestKind == RequestKind::LoadModel) {
            // WhisperModelSession releases its previous context before verifying
            // and loading the requested file, so a failed load invalidates the
            // coordinator's resident-model cache as well.
            clearLoadedAsrModel();
        }
        QString message = envelope.payload.value(QStringLiteral("message")).toString();
        if (message.isEmpty()) {
            message = tr("The ASR worker rejected the request.");
        }
        failActiveJob(workerErrorCode(envelope.payload, m_requestKind == RequestKind::AnalyzeSpeech,
                                      m_requestKind == RequestKind::LoadModel),
                      message);
        return;
    }
    if (envelope.type == Ipc::MessageType::JobCancelled) {
        if (m_activeJob.state == JobState::Cancelling) {
            finishCancellation();
        } else {
            failActiveJob(QStringLiteral("JobCancelled"),
                          tr("The ASR worker cancelled the active operation."));
        }
        return;
    }
    if (m_requestKind == RequestKind::AnalyzeSpeech) {
        if (envelope.type == Ipc::MessageType::Progress) {
            const qint64 progress = std::clamp(envelope.payload.value(QStringLiteral("progress")).toInteger(),
                                               qint64{0}, qint64{100});
            advanceProgress(JobStage::AnalyzingSpeech, static_cast<double>(progress) / 100.0);
            return;
        }
        if (envelope.type != Ipc::MessageType::SpeechAnalysisCompleted) {
            return;
        }
        if (m_activeJob.state == JobState::Cancelling) {
            finishCancellation();
            return;
        }
        QList<JobChunk> chunks;
        qint64 durationMs = 0;
        QString error;
        if (!decodeWorkerChunkPlan(envelope.payload, m_activeJob.id, &chunks, &durationMs, &error)) {
            failActiveJob(QStringLiteral("WorkerProtocolMismatch"), error);
            return;
        }
        const auto recording = m_recordings.findById(m_activeJob.recordingId);
        if (!recording || !recording.value().has_value()) {
            failActiveJob(QStringLiteral("DatabaseQueryFailed"),
                          recording ? tr("The recording no longer exists.") : recording.error().message);
            return;
        }
        Recording updatedRecording = recording.value().value();
        updatedRecording.durationMs = durationMs;
        const auto updated = m_recordings.update(updatedRecording);
        if (!updated) {
            failActiveJob(QStringLiteral("DatabaseQueryFailed"), updated.error().message);
            return;
        }
        m_activeJob.parameters.insert(QStringLiteral("durationMs"), durationMs);
        if (!saveChunkPlan(std::move(chunks), &error)) {
            failActiveJob(QStringLiteral("DatabaseQueryFailed"), error);
            return;
        }
        m_requestId.clear();
        m_requestKind = RequestKind::None;
        beginWaitingForModel();
        return;
    }
    if (m_requestKind == RequestKind::LoadModel) {
        if (envelope.type != Ipc::MessageType::ModelLoaded) {
            return;
        }
        const QString requestedBackend = m_activeJob.backend;
        const bool requestedFlashAttention =
            m_activeJob.parameters.value(QStringLiteral("flashAttention")).toBool(true);
        const QString loadedModelId = m_activeJob.modelId;
        const QString loadedModelPath = m_models.modelPath(loadedModelId);
        clearLoadedAsrModel();
        m_loadedModelId = loadedModelId;
        m_loadedModelPath = loadedModelPath;
        m_loadedModelSha256 = m_activeJob.modelChecksum.isEmpty()
                                  ? m_models.expectedSha256(loadedModelId).toLower()
                                  : m_activeJob.modelChecksum.toLatin1().toLower();
        m_loadedBackend = requestedBackend;
        m_loadedFlashAttention = requestedFlashAttention;
        m_models.setModelInUse(m_loadedModelId, true);

        const QString selectedBackend =
            envelope.payload.value(QStringLiteral("selectedBackend")).toString(requestedBackend);
        const QString actualBackend =
            envelope.payload.value(QStringLiteral("actualBackend")).toString(selectedBackend);
        const QString runtimeVersion =
            envelope.payload.value(QStringLiteral("runtimeVersion")).toString(m_activeJob.engineVersion);
        m_loadedActualBackend = actualBackend;
        m_loadedRuntimeVersion = runtimeVersion;
        m_loadedRuntimeDiagnostics = {
            {QStringLiteral("selectedBackend"), selectedBackend},
            {QStringLiteral("actualBackend"), actualBackend},
            {QStringLiteral("usedFallback"), envelope.payload.value(QStringLiteral("usedFallback")).toBool()},
            {QStringLiteral("flashAttention"),
             envelope.payload.value(QStringLiteral("flashAttention")).toBool()},
            {QStringLiteral("modelLoadTimeMs"),
             envelope.payload.value(QStringLiteral("loadTimeMs")).toInteger()},
            {QStringLiteral("systemInfo"), envelope.payload.value(QStringLiteral("systemInfo")).toString()}};
        QString runtimeError;
        if (!persistLoadedRuntimeInfo(&runtimeError)) {
            failActiveJob(QStringLiteral("DatabaseQueryFailed"), runtimeError);
            return;
        }
        m_requestId.clear();
        m_requestKind = RequestKind::None;
        advanceProgress(JobStage::LoadingModel, 1.0);
        if (m_activeJob.state == JobState::Cancelling) {
            finishCancellation();
            return;
        }
        const auto transcribing = m_jobs.transition(m_activeJob.id, JobState::Transcribing);
        if (!transcribing) {
            failActiveJob(QStringLiteral("InvalidStateTransition"), transcribing.error().message);
            return;
        }
        m_activeJob.state = JobState::Transcribing;
        startNextChunk();
        return;
    }
    if (m_requestKind != RequestKind::TranscribeChunk || m_currentChunkIndex < 0 ||
        m_currentChunkIndex >= m_chunks.size()) {
        return;
    }
    if (envelope.type == Ipc::MessageType::Progress) {
        const qint64 progress = std::clamp(envelope.payload.value(QStringLiteral("progress")).toInteger(),
                                           qint64{0}, qint64{100});
        const double chunkFraction = static_cast<double>(progress) / 100.0;
        const double overall = (static_cast<double>(m_currentChunkIndex) + chunkFraction) /
                               static_cast<double>(std::max(1, static_cast<int>(m_chunks.size())));
        advanceProgress(JobStage::Transcribing, overall, m_activeJob.lastCompletedChunk);
    } else if (envelope.type == Ipc::MessageType::PartialSegment) {
        const JobChunk& chunk = m_chunks.at(m_currentChunkIndex);
        const qint64 rawStartMs = envelope.payload.value(QStringLiteral("startMs")).toInteger(-1);
        const qint64 rawEndMs = envelope.payload.value(QStringLiteral("endMs")).toInteger(-1);
        if (rawStartMs < chunk.startMs - SegmentTimestampToleranceMs ||
            rawEndMs > chunk.endMs + SegmentTimestampToleranceMs || rawEndMs <= rawStartMs) {
            m_worker.forceCancelAfterGrace(m_activeJob.id);
            failActiveJob(QStringLiteral("WorkerProtocolMismatch"),
                          tr("The ASR worker returned a segment outside the active chunk."));
            return;
        }
        TranscriptSegment segment;
        segment.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        segment.recordingId = m_activeJob.recordingId;
        segment.jobId = m_activeJob.id;
        segment.chunkId = chunk.id;
        segment.ordinal = m_nextOrdinal++;
        segment.startMs = std::max(chunk.startMs, rawStartMs);
        segment.endMs = std::min(chunk.endMs, rawEndMs);
        if (segment.endMs <= segment.startMs) {
            failActiveJob(QStringLiteral("WorkerProtocolMismatch"),
                          tr("The ASR worker returned an empty segment time range."));
            return;
        }
        segment.originalText = envelope.payload.value(QStringLiteral("originalText")).toString();
        const GlossaryPostProcessResult postProcessed = GlossaryPostProcessor().applyExplicitAliases(
            segment.originalText, glossaryTermsFromParameters(m_activeJob.parameters));
        if (!postProcessed.replacements.isEmpty()) {
            segment.editedText = postProcessed.text;
            segment.replacementAudit = GlossaryPostProcessor::auditToJson(postProcessed.replacements);
        }
        segment.averageProbability =
            envelope.payload.value(QStringLiteral("averageTokenProbability")).toDouble();
        segment.minimumProbability =
            envelope.payload.value(QStringLiteral("minimumTokenProbability")).toDouble();
        segment.noSpeechProbability =
            envelope.payload.value(QStringLiteral("noSpeechProbability")).toDouble();
        segment.lowConfidence = envelope.payload.value(QStringLiteral("lowConfidence")).toBool();
        segment.provisional = true;
        segment.attempt = chunk.attempts;
        m_currentSegments.append(std::move(segment));
        persistPartialSegments();
    } else if (envelope.type == Ipc::MessageType::ChunkCompleted ||
               envelope.type == Ipc::MessageType::TranscriptionCompleted) {
        QJsonObject timings;
        const QCborMap encodedTimings = envelope.payload.value(QStringLiteral("timingsMs")).toMap();
        for (auto iterator = encodedTimings.constBegin(); iterator != encodedTimings.constEnd(); ++iterator) {
            if (iterator.value().isDouble()) {
                timings.insert(iterator.key().toString(), iterator.value().toDouble());
            } else if (iterator.value().isInteger()) {
                timings.insert(iterator.key().toString(), iterator.value().toInteger());
            }
        }
        m_chunks[m_currentChunkIndex].diagnostics.insert(QStringLiteral("timingsMs"), timings);
        m_chunks[m_currentChunkIndex].diagnostics.insert(
            QStringLiteral("promptTokenCount"),
            envelope.payload.value(QStringLiteral("promptTokenCount")).toInteger());
        m_chunks[m_currentChunkIndex].diagnostics.insert(
            QStringLiteral("omittedPromptParts"),
            envelope.payload.value(QStringLiteral("omittedPromptParts")).toInteger());
        QString error;
        if (!finalizeCurrentChunk(&error)) {
            failActiveJob(QStringLiteral("DatabaseQueryFailed"), error);
            return;
        }
        m_requestId.clear();
        m_requestKind = RequestKind::None;
        QTimer::singleShot(0, this, &TranscriptionCoordinator::startNextChunk);
    }
}

void TranscriptionCoordinator::persistPartialSegments() {
    if (m_currentChunkIndex < 0 || m_currentChunkIndex >= m_chunks.size()) {
        return;
    }
    const JobChunk& chunk = m_chunks.at(m_currentChunkIndex);
    const auto result = m_transcripts.replaceChunk(m_activeJob.recordingId, m_activeJob.id, chunk.id,
                                                   m_currentSegments, true, chunk.attempts);
    if (!result) {
        m_worker.forceCancelAfterGrace(m_activeJob.id);
        failActiveJob(QStringLiteral("DatabaseQueryFailed"), result.error().message);
        return;
    }
    if (!m_activeRevisionPublished) {
        const auto active = m_recordings.setActiveTranscriptJob(m_activeJob.recordingId, m_activeJob.id);
        if (!active) {
            m_worker.forceCancelAfterGrace(m_activeJob.id);
            failActiveJob(QStringLiteral("DatabaseQueryFailed"), active.error().message);
            return;
        }
        m_activeRevisionPublished = true;
    }
    emit transcriptChanged(m_activeJob.recordingId, m_activeJob.id, true);
}

bool TranscriptionCoordinator::finalizeCurrentChunk(QString* error) {
    JobChunk& chunk = m_chunks[m_currentChunkIndex];
    const auto allSegments = m_transcripts.segmentsForJob(m_activeJob.id, true);
    if (!allSegments) {
        if (error != nullptr) {
            *error = allSegments.error().message;
        }
        return false;
    }
    QList<TranscriptSegment> previous;
    for (const TranscriptSegment& segment : allSegments.value()) {
        if (segment.chunkId != chunk.id && !segment.provisional) {
            previous.append(segment);
        }
    }
    if (chunk.overlapBeforeMs > 0 && !previous.isEmpty() && !m_currentSegments.isEmpty()) {
        reconcileOverlap(previous.constLast(), chunk.endMs, &m_currentSegments);
    }
    const QList<GlossaryTerm> glossaryTerms = glossaryTermsFromParameters(m_activeJob.parameters);
    for (TranscriptSegment& segment : m_currentSegments) {
        const GlossaryPostProcessResult postProcessed =
            GlossaryPostProcessor().applyExplicitAliases(segment.originalText, glossaryTerms);
        segment.editedText = postProcessed.replacements.isEmpty() ? QString{} : postProcessed.text;
        segment.replacementAudit = GlossaryPostProcessor::auditToJson(postProcessed.replacements);
        segment.provisional = false;
    }
    const auto saved = m_transcripts.replaceChunk(m_activeJob.recordingId, m_activeJob.id, chunk.id,
                                                  m_currentSegments, false, chunk.attempts);
    if (!saved) {
        if (error != nullptr) {
            *error = saved.error().message;
        }
        return false;
    }
    chunk.state = ChunkState::Completed;
    chunk.completedAt = QDateTime::currentDateTimeUtc();
    chunk.resultHash = segmentDigest(m_currentSegments);
    const auto updated = m_jobs.updateChunk(chunk);
    if (!updated) {
        if (error != nullptr) {
            *error = updated.error().message;
        }
        return false;
    }
    m_activeJob.lastCompletedChunk = std::max(m_activeJob.lastCompletedChunk, chunk.ordinal);
    const double completed = static_cast<double>(m_currentChunkIndex + 1) /
                             static_cast<double>(std::max(1, static_cast<int>(m_chunks.size())));
    advanceProgress(JobStage::Transcribing, completed, m_activeJob.lastCompletedChunk);
    emit transcriptChanged(m_activeJob.recordingId, m_activeJob.id, true);
    return true;
}

void TranscriptionCoordinator::completeActiveJob() {
    const QString jobId = m_activeJob.id;
    const QString recordingId = m_activeJob.recordingId;
    const auto finalizing = m_jobs.transition(jobId, JobState::Finalizing);
    if (!finalizing) {
        failActiveJob(QStringLiteral("InvalidStateTransition"), finalizing.error().message);
        return;
    }
    m_activeJob.state = JobState::Finalizing;
    advanceProgress(JobStage::Finalizing, 1.0, m_activeJob.lastCompletedChunk);
    const auto active = m_recordings.setActiveTranscriptJob(recordingId, jobId);
    if (!active) {
        failActiveJob(QStringLiteral("DatabaseQueryFailed"), active.error().message);
        return;
    }
    const auto completed = m_jobs.transition(jobId, JobState::Completed);
    if (!completed) {
        failActiveJob(QStringLiteral("DatabaseQueryFailed"), completed.error().message);
        return;
    }
    publish(jobId);
    emit transcriptChanged(recordingId, jobId, false);
    emit libraryChanged();
    clearActive();
    QTimer::singleShot(0, this, &TranscriptionCoordinator::scheduleNext);
}

void TranscriptionCoordinator::failActiveJob(const QString& code, const QString& message) {
    if (m_activeJob.id.isEmpty()) {
        emit errorOccurred(message);
        return;
    }
    const QString jobId = m_activeJob.id;
    if (m_currentChunkIndex >= 0 && m_currentChunkIndex < m_chunks.size()) {
        JobChunk& chunk = m_chunks[m_currentChunkIndex];
        if (chunk.state == ChunkState::Running) {
            chunk.state = ChunkState::Failed;
            chunk.error = message;
            (void)m_jobs.updateChunk(chunk);
        }
    }
    const auto failed = m_jobs.transition(jobId, JobState::Failed, code, message);
    if (!failed) {
        emit errorOccurred(failed.error().message);
    }
    publish(jobId);
    if (m_activeRevisionPublished) {
        emit transcriptChanged(m_activeJob.recordingId, jobId, false);
    }
    emit errorOccurred(message);
    clearActive();
    QTimer::singleShot(0, this, &TranscriptionCoordinator::scheduleNext);
}

void TranscriptionCoordinator::finishCancellation() {
    if (m_activeJob.id.isEmpty()) {
        return;
    }
    const QString jobId = m_activeJob.id;
    if (m_currentChunkIndex >= 0 && m_currentChunkIndex < m_chunks.size()) {
        JobChunk& chunk = m_chunks[m_currentChunkIndex];
        if (chunk.state == ChunkState::Running) {
            chunk.state = ChunkState::Cancelled;
            chunk.error = tr("Transcription was cancelled.");
            (void)m_jobs.updateChunk(chunk);
        }
    }
    const auto cancelled = m_jobs.transition(jobId, JobState::Cancelled);
    if (!cancelled) {
        emit errorOccurred(cancelled.error().message);
    }
    publish(jobId);
    if (m_activeRevisionPublished) {
        emit transcriptChanged(m_activeJob.recordingId, jobId, false);
    }
    clearActive();
    QTimer::singleShot(0, this, &TranscriptionCoordinator::scheduleNext);
}

void TranscriptionCoordinator::interruptActiveJob(const QString& reason) {
    clearLoadedAsrModel();
    clearLoadedVadModel();
    if (m_activeJob.id.isEmpty()) {
        return;
    }
    const QString jobId = m_activeJob.id;
    if (m_currentChunkIndex >= 0 && m_currentChunkIndex < m_chunks.size()) {
        JobChunk& chunk = m_chunks[m_currentChunkIndex];
        if (chunk.state == ChunkState::Running) {
            chunk.state = ChunkState::Interrupted;
            chunk.error = reason;
            (void)m_jobs.updateChunk(chunk);
        }
    }
    const auto interrupted =
        m_jobs.transition(jobId, JobState::Interrupted, QStringLiteral("WorkerCrashed"), reason);
    if (!interrupted) {
        emit errorOccurred(interrupted.error().message);
    }
    publish(jobId);
    if (m_activeRevisionPublished) {
        emit transcriptChanged(m_activeJob.recordingId, jobId, false);
    }
    clearActive();
}

void TranscriptionCoordinator::clearActive() {
    if (m_waveformCancellation != nullptr) {
        m_waveformCancellation->store(true, std::memory_order_relaxed);
        m_waveformCancellation.reset();
    }
    m_activeJob = {};
    m_chunks.clear();
    m_currentSegments.clear();
    m_currentChunkIndex = -1;
    m_nextOrdinal = 0;
    m_requestId.clear();
    m_requestKind = RequestKind::None;
    m_activeSourcePath.clear();
    m_activeNormalizedPath.clear();
    m_normalization = nullptr;
    m_activeRevisionPublished = false;
}

void TranscriptionCoordinator::clearLoadedAsrModel() {
    if (!m_loadedModelId.isEmpty()) {
        m_models.setModelInUse(m_loadedModelId, false);
    }
    m_loadedModelId.clear();
    m_loadedModelPath.clear();
    m_loadedModelSha256.clear();
    m_loadedBackend.clear();
    m_loadedActualBackend.clear();
    m_loadedRuntimeVersion.clear();
    m_loadedRuntimeDiagnostics = {};
    m_loadedFlashAttention = false;
}

void TranscriptionCoordinator::clearLoadedVadModel() {
    if (!m_loadedVadModelId.isEmpty()) {
        m_models.setModelInUse(m_loadedVadModelId, false);
    }
    m_loadedVadModelId.clear();
    m_loadedVadPath.clear();
}

bool TranscriptionCoordinator::persistLoadedRuntimeInfo(QString* error) {
    if (m_loadedActualBackend.isEmpty() || m_loadedRuntimeVersion.isEmpty()) {
        if (error != nullptr) {
            *error = tr("The loaded ASR runtime did not report its backend and version.");
        }
        return false;
    }
    const auto saved = m_jobs.updateRuntimeInfo(m_activeJob.id, m_loadedActualBackend, m_loadedRuntimeVersion,
                                                m_activeJob.workerVersion, m_loadedRuntimeDiagnostics);
    if (!saved) {
        if (error != nullptr) {
            *error = saved.error().message;
        }
        return false;
    }
    m_activeJob.backend = m_loadedActualBackend;
    m_activeJob.engineVersion = m_loadedRuntimeVersion;
    for (auto iterator = m_loadedRuntimeDiagnostics.constBegin();
         iterator != m_loadedRuntimeDiagnostics.constEnd(); ++iterator) {
        m_activeJob.diagnostics.insert(iterator.key(), iterator.value());
    }
    return true;
}

void TranscriptionCoordinator::publish(const QString& jobId) {
    const auto job = m_jobs.findById(jobId);
    if (!job || !job.value().has_value()) {
        return;
    }
    publish(job.value().value());
}

void TranscriptionCoordinator::publish(const TranscriptionJob& job) {
    emit jobChanged(job.id, job.recordingId, recordingTitle(job.recordingId), jobStateName(job.state),
                    jobStageName(job.stage), job.progress, job.errorMessage);
}

void TranscriptionCoordinator::advanceProgress(JobStage stage, double fraction, int lastCompletedChunk) {
    if (m_activeJob.id.isEmpty()) {
        return;
    }
    const double progress = std::max(m_activeJob.progress, MonotonicJobProgress::map(stage, fraction));
    const auto result = m_jobs.updateProgress(m_activeJob.id, stage, progress, lastCompletedChunk);
    if (!result) {
        emit errorOccurred(result.error().message);
        return;
    }
    m_activeJob.stage = stage;
    m_activeJob.progress = progress;
    m_activeJob.lastCompletedChunk = std::max(m_activeJob.lastCompletedChunk, lastCompletedChunk);
    publish(m_activeJob);
}

QString TranscriptionCoordinator::recordingTitle(const QString& recordingId) const {
    const auto recording = m_recordings.findById(recordingId);
    return recording && recording.value().has_value() ? recording.value()->title : QString{};
}

bool TranscriptionCoordinator::activeJobMatches(const QString& jobId) const {
    return !jobId.isEmpty() && m_activeJob.id == jobId;
}

} // namespace BreezeDesk
