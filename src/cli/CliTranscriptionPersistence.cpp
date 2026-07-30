#include "breezedesk/cli/CliTranscriptionPersistence.h"

#include "breezedesk/database/IRecordingRepository.h"
#include "breezedesk/jobs/IJobRepository.h"
#include "breezedesk/jobs/JobQueue.h"
#include "breezedesk/jobs/JobRecoveryService.h"
#include "breezedesk/jobs/JobStateMachine.h"
#include "breezedesk/transcript/ITranscriptRepository.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace BreezeDesk {
namespace {

QString normalizedPath(const QString& path) {
    const QFileInfo information(path);
    const QString canonical = information.canonicalFilePath();
    return canonical.isEmpty() ? information.absoluteFilePath() : canonical;
}

QString segmentHash(const QList<TranscriptSegment>& segments) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    constexpr QByteArrayView separator("\0", 1);
    for (const TranscriptSegment& segment : segments) {
        hash.addData(QByteArray::number(segment.startMs));
        hash.addData(separator);
        hash.addData(QByteArray::number(segment.endMs));
        hash.addData(separator);
        hash.addData(segment.originalText.toUtf8());
        hash.addData(separator);
        hash.addData(segment.editedText.toUtf8());
        hash.addData(separator);
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool normalizeSha256(const QString& sourceHash, QString* normalized) {
    if (normalized == nullptr) {
        return false;
    }
    *normalized = sourceHash.trimmed().toLower();
    return normalized->size() == 64 &&
           std::all_of(normalized->cbegin(), normalized->cend(), [](const QChar character) {
               return (character >= QLatin1Char('0') && character <= QLatin1Char('9')) ||
                      (character >= QLatin1Char('a') && character <= QLatin1Char('f'));
           });
}

UserFacingError jobCancelledError() {
    return UserFacingError::validation(ErrorCode::JobCancelled,
                                       QStringLiteral("The transcription was cancelled."));
}

} // namespace

CliTranscriptionPersistence::CliTranscriptionPersistence(IRecordingRepository& recordings,
                                                         IJobRepository& jobs,
                                                         ITranscriptRepository& transcripts,
                                                         QString ownerToken,
                                                         std::function<bool()> cancellationRequested,
                                                         std::function<void(const QString&)> waitNotification)
    : m_recordings(recordings), m_jobs(jobs), m_transcripts(transcripts),
      m_ownerToken(ownerToken.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                        : std::move(ownerToken)),
      m_cancellationRequested(std::move(cancellationRequested)),
      m_waitNotification(std::move(waitNotification)) {}

Result<DurableTranscriptionIdentity>
CliTranscriptionPersistence::beginNew(DurableTranscriptionDescriptor descriptor) {
    if (m_active)
        return Result<DurableTranscriptionIdentity>::failure(UserFacingError::validation(
            ErrorCode::InvalidStateTransition,
            QStringLiteral("A durable transcription session is already active.")));
    if (descriptor.recording.title.trimmed().isEmpty() ||
        descriptor.recording.sourcePath.trimmed().isEmpty() || descriptor.chunks.isEmpty()) {
        return Result<DurableTranscriptionIdentity>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument,
            QStringLiteral("A source recording and at least one transcription chunk are required.")));
    }
    const auto recovered = JobRecoveryService(m_jobs).recoverAfterAbnormalShutdown();
    if (!recovered)
        return Result<DurableTranscriptionIdentity>::failure(recovered.error());
    descriptor.recording.sourcePath = QFileInfo(descriptor.recording.sourcePath).absoluteFilePath();
    auto existingResult = m_recordings.findBySourcePath(descriptor.recording.sourcePath);
    if (!existingResult)
        return Result<DurableTranscriptionIdentity>::failure(existingResult.error());

    Recording recording = descriptor.recording;
    if (existingResult.value()) {
        recording = *existingResult.value();
    } else {
        if (recording.id.isEmpty())
            recording.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto createResult = m_recordings.create(recording);
        if (!createResult)
            return Result<DurableTranscriptionIdentity>::failure(createResult.error());
    }

    TranscriptionJob job = descriptor.job;
    if (job.id.isEmpty())
        job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.recordingId = recording.id;
    job.queueHidden = true;
    auto jobIdResult = JobQueue(m_jobs).enqueue(job);
    if (!jobIdResult)
        return Result<DurableTranscriptionIdentity>::failure(jobIdResult.error());

    for (int index = 0; index < descriptor.chunks.size(); ++index) {
        JobChunk& planned = descriptor.chunks[index];
        planned.id.clear();
        planned.jobId = job.id;
        planned.ordinal = index;
        planned.state = ChunkState::Pending;
        planned.attempts = 0;
    }
    auto chunkResult = m_jobs.replaceChunks(job.id, descriptor.chunks);
    if (!chunkResult) {
        const auto cleanup = JobQueue(m_jobs).cancel(job.id);
        Q_UNUSED(cleanup)
        return Result<DurableTranscriptionIdentity>::failure(chunkResult.error());
    }
    auto savedChunks = m_jobs.chunks(job.id);
    if (!savedChunks) {
        const auto cleanup = JobQueue(m_jobs).cancel(job.id);
        Q_UNUSED(cleanup)
        return Result<DurableTranscriptionIdentity>::failure(savedChunks.error());
    }

    m_identity = {recording.id, job.id, savedChunks.value(), false};
    m_sourceBindingStage = JobStage::Preparing;
    m_active = true;
    auto claimResult = waitForExecutionClaim(job.id);
    if (!claimResult) {
        m_active = false;
        return Result<DurableTranscriptionIdentity>::failure(claimResult.error());
    }
    auto progressResult =
        m_jobs.updateProgress(job.id, JobStage::Preparing,
                              MonotonicJobProgress::map(JobStage::Preparing, 1.0), -1, m_ownerToken);
    if (!progressResult) {
        const auto checkpoint = transitionTo(JobState::Interrupted, QStringLiteral("DatabaseQueryFailed"),
                                             progressResult.error().diagnosticString());
        if (!checkpoint)
            return Result<DurableTranscriptionIdentity>::failure(checkpoint.error());
        m_active = false;
        return Result<DurableTranscriptionIdentity>::failure(progressResult.error());
    }
    return Result<DurableTranscriptionIdentity>::success(m_identity);
}

Result<DurableTranscriptionIdentity> CliTranscriptionPersistence::resume(const QString& jobId,
                                                                         const QString& sourcePath) {
    if (m_active)
        return Result<DurableTranscriptionIdentity>::failure(UserFacingError::validation(
            ErrorCode::InvalidStateTransition,
            QStringLiteral("A durable transcription session is already active.")));
    const auto recovered = JobRecoveryService(m_jobs).recoverAfterAbnormalShutdown();
    if (!recovered)
        return Result<DurableTranscriptionIdentity>::failure(recovered.error());
    auto jobResult = m_jobs.findById(jobId);
    if (!jobResult)
        return Result<DurableTranscriptionIdentity>::failure(jobResult.error());
    if (!jobResult.value())
        return Result<DurableTranscriptionIdentity>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The interrupted transcription job does not exist.")));
    if (jobResult.value()->state == JobState::Cancelling || jobResult.value()->state == JobState::Cancelled) {
        return Result<DurableTranscriptionIdentity>::failure(jobCancelledError());
    }
    if (jobResult.value()->state != JobState::Interrupted && jobResult.value()->state != JobState::Failed)
        return Result<DurableTranscriptionIdentity>::failure(UserFacingError::validation(
            ErrorCode::InvalidStateTransition,
            QStringLiteral("Only interrupted or failed transcription jobs can be resumed.")));
    auto recordingResult = m_recordings.findById(jobResult.value()->recordingId);
    if (!recordingResult)
        return Result<DurableTranscriptionIdentity>::failure(recordingResult.error());
    if (!recordingResult.value())
        return Result<DurableTranscriptionIdentity>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The source recording for this job no longer exists.")));
    if (normalizedPath(recordingResult.value()->sourcePath) != normalizedPath(sourcePath))
        return Result<DurableTranscriptionIdentity>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument,
            QStringLiteral("The resume source does not match the interrupted job.")));

    auto savedChunks = m_jobs.chunks(jobId);
    if (!savedChunks)
        return Result<DurableTranscriptionIdentity>::failure(savedChunks.error());
    if (savedChunks.value().isEmpty())
        return Result<DurableTranscriptionIdentity>::failure(
            UserFacingError::validation(ErrorCode::InvalidStateTransition,
                                        QStringLiteral("The interrupted job has no durable chunk plan.")));

    m_identity = {jobResult.value()->recordingId, jobId, savedChunks.value(), true};
    m_sourceBindingStage = jobResult.value()->stage;
    m_active = true;
    const bool retryingFailedJob = jobResult.value()->state == JobState::Failed;
    auto resumeResult =
        retryingFailedJob ? m_jobs.transition(jobId, JobState::Queued) : JobQueue(m_jobs).resume(jobId);
    if (!resumeResult) {
        m_active = false;
        return Result<DurableTranscriptionIdentity>::failure(resumeResult.error());
    }
    auto claimResult = waitForExecutionClaim(jobId);
    if (!claimResult) {
        m_active = false;
        return Result<DurableTranscriptionIdentity>::failure(claimResult.error());
    }
    for (JobChunk& saved : m_identity.chunks) {
        if (saved.state == ChunkState::Completed)
            continue;
        saved.state = ChunkState::Pending;
        saved.startedAt = {};
        saved.completedAt = {};
        saved.error.clear();
        auto updateResult = m_jobs.updateChunk(saved, m_ownerToken);
        if (!updateResult) {
            const auto checkpoint = transitionTo(JobState::Interrupted, QStringLiteral("DatabaseQueryFailed"),
                                                 updateResult.error().diagnosticString());
            if (!checkpoint)
                return Result<DurableTranscriptionIdentity>::failure(checkpoint.error());
            m_active = false;
            return Result<DurableTranscriptionIdentity>::failure(updateResult.error());
        }
    }
    return Result<DurableTranscriptionIdentity>::success(m_identity);
}

Result<void> CliTranscriptionPersistence::bindSourceMedia(const QString& sourceHash) {
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    QString normalizedSourceHash;
    if (!normalizeSha256(sourceHash, &normalizedSourceHash))
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A valid source-media SHA-256 is required.")));
    auto jobResult = currentJob();
    if (!jobResult)
        return Result<void>::failure(jobResult.error());
    const QString boundSourceHash =
        jobResult.value().parameters.value(QStringLiteral("sourceHash")).toString();
    if (!boundSourceHash.isEmpty()) {
        if (boundSourceHash.compare(normalizedSourceHash, Qt::CaseInsensitive) != 0)
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidStateTransition,
                QStringLiteral("The source media changed after this transcription started. Start a new "
                               "transcription so existing chunks are not mixed with different audio.")));
        return renewExecutionLease();
    }

    const bool chunkHasStarted =
        std::any_of(m_identity.chunks.cbegin(), m_identity.chunks.cend(), [](const JobChunk& chunk) {
            return chunk.state != ChunkState::Pending || chunk.attempts > 0;
        });
    if (m_identity.resumed && (m_sourceBindingStage != JobStage::Preparing || chunkHasStarted))
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidStateTransition,
            QStringLiteral("The interrupted job has no verified source-media identity. Start a new "
                           "transcription so existing chunks are not mixed with different audio.")));

    QJsonObject parameters = jobResult.value().parameters;
    parameters.insert(QStringLiteral("sourceHash"), normalizedSourceHash);
    return m_jobs.updateParameters(m_identity.jobId, parameters, m_ownerToken);
}

Result<void> CliTranscriptionPersistence::beginNormalization() {
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    auto transitionResult = transitionTo(JobState::Normalizing);
    if (!transitionResult)
        return transitionResult;
    return updateNormalizationProgress(0.0);
}

Result<void> CliTranscriptionPersistence::updateNormalizationProgress(const double fraction) {
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    return updateProgressMonotonically(JobStage::NormalizingAudio,
                                       MonotonicJobProgress::map(JobStage::NormalizingAudio, fraction));
}

Result<void> CliTranscriptionPersistence::updateNormalizedAudio(const QString& path, const qint64 durationMs,
                                                                const QString& sourceHash,
                                                                const bool regenerated) {
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    QString normalizedSourceHash;
    if (path.trimmed().isEmpty() || durationMs <= 0 || !normalizeSha256(sourceHash, &normalizedSourceHash))
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument,
            QStringLiteral("Normalized audio requires a path, positive duration, and source SHA-256.")));
    auto recording = m_recordings.findById(m_identity.recordingId);
    if (!recording)
        return Result<void>::failure(recording.error());
    if (!recording.value())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The durable source recording no longer exists.")));
    Recording changed = *recording.value();
    if (regenerated || changed.sourceHash.compare(normalizedSourceHash, Qt::CaseInsensitive) != 0) {
        changed.waveformPath.clear();
    }
    changed.normalizedPcmPath = normalizedPath(path);
    changed.durationMs = durationMs;
    changed.sourceHash = normalizedSourceHash;
    return m_recordings.update(changed, m_identity.jobId, m_ownerToken);
}

Result<void> CliTranscriptionPersistence::beginModelLoad() {
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    auto transitionResult = transitionTo(JobState::LoadingModel);
    if (!transitionResult)
        return transitionResult;
    return updateProgressMonotonically(JobStage::LoadingModel,
                                       MonotonicJobProgress::map(JobStage::LoadingModel, 0.0));
}

Result<void> CliTranscriptionPersistence::beginSpeechAnalysis() {
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    auto transitionResult = transitionTo(JobState::AnalyzingSpeech);
    if (!transitionResult)
        return transitionResult;
    return updateSpeechAnalysisProgress(0.0);
}

Result<void> CliTranscriptionPersistence::updateSpeechAnalysisProgress(const double fraction) {
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    return updateProgressMonotonically(JobStage::AnalyzingSpeech,
                                       MonotonicJobProgress::map(JobStage::AnalyzingSpeech, fraction));
}

Result<void> CliTranscriptionPersistence::replaceChunkPlan(QList<JobChunk> chunks) {
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    if (chunks.isEmpty())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument,
            QStringLiteral("Speech analysis must produce at least one transcription chunk.")));
    const bool hasStartedChunk =
        std::any_of(m_identity.chunks.cbegin(), m_identity.chunks.cend(), [](const JobChunk& saved) {
            return saved.state != ChunkState::Pending || saved.attempts > 0;
        });
    if (hasStartedChunk)
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidStateTransition,
            QStringLiteral("A chunk plan cannot change after transcription has started.")));
    for (int index = 0; index < chunks.size(); ++index) {
        JobChunk& chunk = chunks[index];
        chunk.id.clear();
        chunk.jobId = m_identity.jobId;
        chunk.ordinal = index;
        chunk.state = ChunkState::Pending;
        chunk.attempts = 0;
        chunk.startedAt = {};
        chunk.completedAt = {};
        chunk.error.clear();
        chunk.resultHash.clear();
    }
    auto replaceResult = m_jobs.replaceChunks(m_identity.jobId, chunks, m_ownerToken);
    if (!replaceResult)
        return replaceResult;
    auto savedResult = m_jobs.chunks(m_identity.jobId);
    if (!savedResult)
        return Result<void>::failure(savedResult.error());
    m_identity.chunks = savedResult.value();
    return Result<void>::success();
}

Result<void> CliTranscriptionPersistence::beginTranscription() {
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    auto transitionResult = transitionTo(JobState::Transcribing);
    if (!transitionResult)
        return transitionResult;
    return updateTranscriptionProgress(0.0);
}

Result<void> CliTranscriptionPersistence::updateTranscriptionProgress(const double fraction) {
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    return updateProgressMonotonically(JobStage::Transcribing,
                                       MonotonicJobProgress::map(JobStage::Transcribing, fraction));
}

Result<JobChunk> CliTranscriptionPersistence::beginChunk(const int ordinal) {
    auto activeResult = requireActive();
    if (!activeResult)
        return Result<JobChunk>::failure(activeResult.error());
    auto chunkResult = chunk(ordinal);
    if (!chunkResult)
        return chunkResult;
    JobChunk value = chunkResult.value();
    if (value.state == ChunkState::Completed)
        return Result<JobChunk>::failure(UserFacingError::validation(
            ErrorCode::InvalidStateTransition,
            QStringLiteral("A completed transcription chunk cannot be run again.")));
    value.state = ChunkState::Running;
    value.attempts += 1;
    value.startedAt = QDateTime::currentDateTimeUtc();
    value.completedAt = {};
    value.error.clear();
    auto updateResult = m_jobs.updateChunk(value, m_ownerToken);
    if (!updateResult)
        return Result<JobChunk>::failure(updateResult.error());
    refreshChunk(value);
    return Result<JobChunk>::success(value);
}

Result<void> CliTranscriptionPersistence::saveChunkSegments(const int ordinal,
                                                            QList<TranscriptSegment> segments,
                                                            const bool provisional) {
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    auto chunkResult = chunk(ordinal);
    if (!chunkResult)
        return Result<void>::failure(chunkResult.error());
    const auto recordingResult = m_recordings.findById(m_identity.recordingId);
    if (!recordingResult)
        return Result<void>::failure(recordingResult.error());
    if (!recordingResult.value().has_value())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The transcription recording no longer exists.")));
    const JobChunk& activeChunk = chunkResult.value();
    const qint64 allowedStartMs = std::max<qint64>(0, activeChunk.startMs);
    const qint64 allowedEndMs = recordingResult.value()->durationMs > 0
                                    ? std::min(activeChunk.endMs, recordingResult.value()->durationMs)
                                    : activeChunk.endMs;
    for (const TranscriptSegment& segment : std::as_const(segments)) {
        if (allowedEndMs <= allowedStartMs || segment.startMs < allowedStartMs ||
            segment.endMs > allowedEndMs || segment.endMs <= segment.startMs) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::WorkerProtocolMismatch,
                QStringLiteral("The ASR worker returned a segment outside the active chunk.")));
        }
    }
    for (TranscriptSegment& segment : segments) {
        segment.recordingId = m_identity.recordingId;
        segment.jobId = m_identity.jobId;
        segment.chunkId = activeChunk.id;
        segment.provisional = provisional;
        segment.attempt = activeChunk.attempts;
    }
    return m_transcripts.replaceChunk(m_identity.recordingId, m_identity.jobId, activeChunk.id,
                                      std::move(segments), provisional, activeChunk.attempts,
                                      m_ownerToken);
}

Result<void> CliTranscriptionPersistence::completeChunk(const int ordinal,
                                                        QList<TranscriptSegment> segments) {
    auto chunkResult = chunk(ordinal);
    if (!chunkResult)
        return Result<void>::failure(chunkResult.error());
    auto segmentResult = saveChunkSegments(ordinal, segments, false);
    if (!segmentResult)
        return segmentResult;
    JobChunk value = chunkResult.value();
    value.state = ChunkState::Completed;
    value.completedAt = QDateTime::currentDateTimeUtc();
    value.error.clear();
    value.resultHash = segmentHash(segments);
    auto updateResult = m_jobs.updateChunk(value, m_ownerToken);
    if (!updateResult)
        return updateResult;
    refreshChunk(value);

    int completedCount = 0;
    int lastContiguous = -1;
    QList<JobChunk> ordered = m_identity.chunks;
    std::sort(ordered.begin(), ordered.end(),
              [](const JobChunk& left, const JobChunk& right) { return left.ordinal < right.ordinal; });
    for (const JobChunk& saved : ordered) {
        if (saved.state == ChunkState::Completed) {
            ++completedCount;
            if (saved.ordinal == lastContiguous + 1)
                lastContiguous = saved.ordinal;
        }
    }
    const double fraction =
        static_cast<double>(completedCount) / static_cast<double>(qMax(1, ordered.size()));
    return m_jobs.updateProgress(m_identity.jobId, JobStage::Transcribing,
                                 MonotonicJobProgress::map(JobStage::Transcribing, fraction), lastContiguous,
                                 m_ownerToken);
}

Result<bool> CliTranscriptionPersistence::cancellationRequested() const {
    if (m_identity.jobId.isEmpty()) {
        return Result<bool>::failure(UserFacingError::validation(
            ErrorCode::InvalidStateTransition,
            QStringLiteral("The durable transcription session has not been initialized.")));
    }
    const auto current = currentJob();
    if (!current) {
        return Result<bool>::failure(current.error());
    }
    return Result<bool>::success(current.value().state == JobState::Cancelling ||
                                 current.value().state == JobState::Cancelled);
}

Result<void> CliTranscriptionPersistence::cancel(const QString& reason) {
    if (!m_active) {
        const auto current = currentJob();
        if (current && current.value().state == JobState::Cancelled) {
            return Result<void>::success();
        }
        return Result<void>::failure(
            UserFacingError::validation(ErrorCode::InvalidStateTransition,
                                        QStringLiteral("No durable transcription session is active.")));
    }

    auto current = currentJob();
    if (!current) {
        return Result<void>::failure(current.error());
    }
    if (current.value().state == JobState::Cancelled) {
        m_active = false;
        return Result<void>::success();
    }
    if (current.value().state != JobState::Cancelling) {
        const auto requested = JobQueue(m_jobs).cancel(m_identity.jobId);
        if (!requested) {
            return requested;
        }
        current = currentJob();
        if (!current) {
            return Result<void>::failure(current.error());
        }
        if (current.value().state == JobState::Cancelled) {
            m_active = false;
            return Result<void>::success();
        }
    }

    const auto chunksResult = m_jobs.chunks(m_identity.jobId);
    if (!chunksResult) {
        return Result<void>::failure(chunksResult.error());
    }
    for (const JobChunk& saved : chunksResult.value()) {
        if (saved.state != ChunkState::Running && saved.state != ChunkState::Interrupted &&
            saved.state != ChunkState::Failed) {
            continue;
        }
        JobChunk cancelledChunk = saved;
        cancelledChunk.state = ChunkState::Cancelled;
        cancelledChunk.error = reason;
        const auto updated = m_jobs.updateChunk(cancelledChunk, m_ownerToken);
        if (!updated) {
            return updated;
        }
        refreshChunk(cancelledChunk);
    }
    const auto cancelled = transitionTo(JobState::Cancelled, QStringLiteral("JobCancelled"), reason);
    if (cancelled) {
        m_active = false;
    }
    return cancelled;
}

Result<void> CliTranscriptionPersistence::interrupt(const QString& reason, const QString& errorCode) {
    const auto cancellation = finishCancellationIfRequested(QStringLiteral("Cancelled by the user."));
    if (!cancellation) {
        return Result<void>::failure(cancellation.error());
    }
    if (cancellation.value()) {
        return Result<void>::failure(jobCancelledError());
    }
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    for (JobChunk& saved : m_identity.chunks) {
        if (saved.state != ChunkState::Running)
            continue;
        saved.state = ChunkState::Interrupted;
        saved.error = reason;
        auto updateResult = m_jobs.updateChunk(saved, m_ownerToken);
        if (!updateResult)
            return updateResult;
    }
    auto result = transitionTo(JobState::Interrupted, errorCode, reason);
    if (!result) {
        const auto racedCancellation =
            finishCancellationIfRequested(QStringLiteral("Cancelled by the user."));
        if (!racedCancellation) {
            return Result<void>::failure(racedCancellation.error());
        }
        if (racedCancellation.value()) {
            return Result<void>::failure(jobCancelledError());
        }
    }
    if (result) {
        m_active = false;
    }
    return result;
}

Result<void> CliTranscriptionPersistence::fail(const QString& errorCode, const QString& message) {
    const auto cancellation = finishCancellationIfRequested(QStringLiteral("Cancelled by the user."));
    if (!cancellation) {
        return Result<void>::failure(cancellation.error());
    }
    if (cancellation.value()) {
        return Result<void>::failure(jobCancelledError());
    }
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    for (JobChunk& saved : m_identity.chunks) {
        if (saved.state != ChunkState::Running)
            continue;
        saved.state = ChunkState::Failed;
        saved.error = message;
        auto updateResult = m_jobs.updateChunk(saved, m_ownerToken);
        if (!updateResult)
            return updateResult;
    }
    auto result = transitionTo(JobState::Failed, errorCode, message);
    if (!result) {
        const auto racedCancellation =
            finishCancellationIfRequested(QStringLiteral("Cancelled by the user."));
        if (!racedCancellation) {
            return Result<void>::failure(racedCancellation.error());
        }
        if (racedCancellation.value()) {
            return Result<void>::failure(jobCancelledError());
        }
    }
    if (result) {
        m_active = false;
    }
    return result;
}

Result<void> CliTranscriptionPersistence::complete() {
    const auto cancellation = finishCancellationIfRequested(QStringLiteral("Cancelled by the user."));
    if (!cancellation) {
        return Result<void>::failure(cancellation.error());
    }
    if (cancellation.value()) {
        return Result<void>::failure(jobCancelledError());
    }
    auto activeResult = requireActive();
    if (!activeResult)
        return activeResult;
    auto chunksResult = m_jobs.chunks(m_identity.jobId);
    if (!chunksResult)
        return Result<void>::failure(chunksResult.error());
    const bool allCompleted =
        std::all_of(chunksResult.value().cbegin(), chunksResult.value().cend(),
                    [](const JobChunk& saved) { return saved.state == ChunkState::Completed; });
    if (!allCompleted)
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidStateTransition,
            QStringLiteral("A transcription cannot finish until every chunk is complete.")));
    auto finalizing = transitionTo(JobState::Finalizing);
    if (!finalizing)
        return finalizing;
    auto progress = m_jobs.updateProgress(m_identity.jobId, JobStage::Finalizing,
                                          MonotonicJobProgress::map(JobStage::Finalizing, 1.0),
                                          chunksResult.value().constLast().ordinal, m_ownerToken);
    if (!progress)
        return progress;
    auto completed = m_jobs.completeAndActivate(m_identity.recordingId, m_identity.jobId, m_ownerToken);
    if (!completed) {
        const auto racedCancellation =
            finishCancellationIfRequested(QStringLiteral("Cancelled by the user."));
        if (!racedCancellation) {
            return Result<void>::failure(racedCancellation.error());
        }
        if (racedCancellation.value()) {
            return Result<void>::failure(jobCancelledError());
        }
        return completed;
    }
    m_active = false;
    return Result<void>::success();
}

Result<void> CliTranscriptionPersistence::renewExecutionLease() {
    if (!m_active) {
        return Result<void>::failure(
            UserFacingError::validation(ErrorCode::InvalidStateTransition,
                                        QStringLiteral("No durable transcription session is active.")));
    }
    const auto renewed = m_jobs.renewLease(m_identity.jobId, m_ownerToken);
    if (!renewed)
        return Result<void>::failure(renewed.error());
    return Result<void>::success();
}

Result<void> CliTranscriptionPersistence::quiesceWorkerBeforeTerminalCheckpoint(
    const std::function<void()>& quiesceWorker) {
    if (!quiesceWorker) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A worker quiescence callback is required.")));
    }

    // WorkerProcessManager::stop() can synchronously wait for graceful shutdown before killing a
    // non-responsive worker. Refresh the lease first so another process cannot reclaim the job while
    // that bounded wait is in progress. Even when renewal fails, stop the local worker before returning:
    // the caller must not leave an unfenced operation running or checkpoint it with a stale token.
    const auto renewed = renewExecutionLease();
    quiesceWorker();
    return renewed;
}

Result<QList<TranscriptSegment>> CliTranscriptionPersistence::persistedSegments() const {
    auto activeResult = requireActive();
    if (!activeResult && m_identity.jobId.isEmpty())
        return Result<QList<TranscriptSegment>>::failure(activeResult.error());
    return m_transcripts.segmentsForJob(m_identity.jobId, true);
}

Result<TranscriptionJob> CliTranscriptionPersistence::currentJob() const {
    auto result = m_jobs.findById(m_identity.jobId);
    if (!result)
        return Result<TranscriptionJob>::failure(result.error());
    if (!result.value())
        return Result<TranscriptionJob>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The durable transcription job no longer exists.")));
    return Result<TranscriptionJob>::success(*result.value());
}

Result<JobChunk> CliTranscriptionPersistence::chunk(const int ordinal) const {
    for (const JobChunk& saved : m_identity.chunks) {
        if (saved.ordinal == ordinal)
            return Result<JobChunk>::success(saved);
    }
    return Result<JobChunk>::failure(UserFacingError::validation(
        ErrorCode::NotFound, QStringLiteral("The requested transcription chunk does not exist.")));
}

Result<void> CliTranscriptionPersistence::transitionTo(const JobState state, const QString& errorCode,
                                                       const QString& message) {
    auto current = currentJob();
    if (!current)
        return Result<void>::failure(current.error());
    if ((current.value().state == JobState::Cancelling || current.value().state == JobState::Cancelled) &&
        state != JobState::Cancelled) {
        return Result<void>::failure(jobCancelledError());
    }
    if (current.value().state == state)
        return renewExecutionLease();
    return m_jobs.transition(m_identity.jobId, state, errorCode, message, m_ownerToken);
}

Result<void> CliTranscriptionPersistence::updateProgressMonotonically(const JobStage stage,
                                                                      const double progress,
                                                                      const int lastCompletedChunk) {
    auto current = currentJob();
    if (!current)
        return Result<void>::failure(current.error());
    if (current.value().state == JobState::Cancelling || current.value().state == JobState::Cancelled) {
        return Result<void>::failure(jobCancelledError());
    }
    if (static_cast<int>(stage) < static_cast<int>(current.value().stage))
        return renewExecutionLease();
    return m_jobs.updateProgress(m_identity.jobId, stage, progress, lastCompletedChunk, m_ownerToken);
}

Result<void> CliTranscriptionPersistence::requireActive() const {
    if (!m_active) {
        return Result<void>::failure(
            UserFacingError::validation(ErrorCode::InvalidStateTransition,
                                        QStringLiteral("No durable transcription session is active.")));
    }
    const auto cancellation = cancellationRequested();
    if (!cancellation) {
        return Result<void>::failure(cancellation.error());
    }
    return cancellation.value() ? Result<void>::failure(jobCancelledError()) : Result<void>::success();
}

Result<void> CliTranscriptionPersistence::waitForExecutionClaim(const QString& jobId) {
    QElapsedTimer notificationTimer;
    notificationTimer.start();
    bool notified = false;
    while (true) {
        if (m_cancellationRequested && m_cancellationRequested()) {
            const auto cancelled = JobQueue(m_jobs).cancel(jobId);
            if (!cancelled)
                return cancelled;
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::OperationCancelled,
                QStringLiteral("Transcription was cancelled while waiting for another job.")));
        }

        const auto current = m_jobs.findById(jobId);
        if (!current) {
            return Result<void>::failure(current.error());
        }
        if (!current.value().has_value()) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::NotFound, QStringLiteral("The queued transcription job no longer exists.")));
        }
        if (current.value()->state == JobState::Cancelled || current.value()->state == JobState::Cancelling) {
            return Result<void>::failure(jobCancelledError());
        }

        const auto claim = m_jobs.claimQueued(jobId, m_ownerToken);
        if (!claim) {
            const auto raced = m_jobs.findById(jobId);
            if (raced && raced.value().has_value() &&
                (raced.value()->state == JobState::Cancelled ||
                 raced.value()->state == JobState::Cancelling)) {
                return Result<void>::failure(jobCancelledError());
            }
            return Result<void>::failure(claim.error());
        }
        if (claim.value().claimed)
            return Result<void>::success();

        if (!notified || notificationTimer.elapsed() >= 30'000) {
            if (m_waitNotification) {
                const QString activeSuffix =
                    claim.value().activeJobId.isEmpty()
                        ? QString{}
                        : QStringLiteral(" (%1)").arg(claim.value().activeJobId.left(8));
                m_waitNotification(
                    QStringLiteral("Waiting for another transcription to finish%1…").arg(activeSuffix));
            }
            notified = true;
            notificationTimer.restart();
        }
        QThread::msleep(250);
    }
}

Result<bool> CliTranscriptionPersistence::finishCancellationIfRequested(const QString& reason) {
    const auto requested = cancellationRequested();
    if (!requested) {
        return Result<bool>::failure(requested.error());
    }
    if (!requested.value()) {
        return Result<bool>::success(false);
    }
    const auto cancelled = cancel(reason.isEmpty() ? QStringLiteral("Cancelled by the user.") : reason);
    if (!cancelled) {
        return Result<bool>::failure(cancelled.error());
    }
    return Result<bool>::success(true);
}

void CliTranscriptionPersistence::refreshChunk(const JobChunk& changed) {
    for (JobChunk& saved : m_identity.chunks) {
        if (saved.ordinal == changed.ordinal) {
            saved = changed;
            return;
        }
    }
}

} // namespace BreezeDesk
