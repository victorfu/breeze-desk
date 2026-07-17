#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/database/Recording.h"
#include "breezedesk/jobs/TranscriptionJob.h"
#include "breezedesk/transcript/TranscriptSegment.h"

namespace BreezeDesk {

class IJobRepository;
class IRecordingRepository;
class ITranscriptRepository;

struct DurableTranscriptionDescriptor {
    Recording recording;
    TranscriptionJob job;
    QList<JobChunk> chunks;
};

struct DurableTranscriptionIdentity {
    QString recordingId;
    QString jobId;
    QList<JobChunk> chunks;
    bool resumed = false;
};

class CliTranscriptionPersistence final {
  public:
    CliTranscriptionPersistence(IRecordingRepository& recordings, IJobRepository& jobs,
                                ITranscriptRepository& transcripts);

    [[nodiscard]] Result<DurableTranscriptionIdentity> beginNew(DurableTranscriptionDescriptor descriptor);
    [[nodiscard]] Result<DurableTranscriptionIdentity> resume(const QString& jobId, const QString& sourcePath,
                                                              const QString& normalizedPcmPath);

    [[nodiscard]] Result<void> beginNormalization();
    [[nodiscard]] Result<void> updateNormalizationProgress(double fraction);
    [[nodiscard]] Result<void> beginModelLoad();
    [[nodiscard]] Result<void> beginSpeechAnalysis();
    [[nodiscard]] Result<void> updateSpeechAnalysisProgress(double fraction);
    [[nodiscard]] Result<void> replaceChunkPlan(QList<JobChunk> chunks);
    [[nodiscard]] Result<void> beginTranscription();
    [[nodiscard]] Result<void> updateTranscriptionProgress(double fraction);
    [[nodiscard]] Result<JobChunk> beginChunk(int ordinal);
    [[nodiscard]] Result<void> saveChunkSegments(int ordinal, QList<TranscriptSegment> segments,
                                                 bool provisional);
    [[nodiscard]] Result<void> completeChunk(int ordinal, QList<TranscriptSegment> segments);
    [[nodiscard]] Result<void> interrupt(const QString& reason,
                                         const QString& errorCode = QStringLiteral("WorkerCrashed"));
    [[nodiscard]] Result<void> fail(const QString& errorCode, const QString& message);
    [[nodiscard]] Result<void> complete();
    [[nodiscard]] Result<QList<TranscriptSegment>> persistedSegments() const;

    [[nodiscard]] bool isActive() const noexcept { return m_active; }
    [[nodiscard]] const DurableTranscriptionIdentity& identity() const { return m_identity; }

  private:
    [[nodiscard]] Result<TranscriptionJob> currentJob() const;
    [[nodiscard]] Result<JobChunk> chunk(int ordinal) const;
    [[nodiscard]] Result<void> transitionTo(JobState state, const QString& errorCode = {},
                                            const QString& message = {});
    [[nodiscard]] Result<void> updateProgressMonotonically(JobStage stage, double progress,
                                                           int lastCompletedChunk = -1);
    [[nodiscard]] Result<void> updateRecordingNormalizedPath(const QString& path);
    [[nodiscard]] Result<void> requireActive() const;
    void refreshChunk(const JobChunk& chunk);

    IRecordingRepository& m_recordings;
    IJobRepository& m_jobs;
    ITranscriptRepository& m_transcripts;
    DurableTranscriptionIdentity m_identity;
    bool m_active = false;
};

} // namespace BreezeDesk
