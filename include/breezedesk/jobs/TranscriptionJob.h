#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace BreezeDesk {

enum class JobState {
    Queued,
    Preparing,
    Normalizing,
    WaitingForModel,
    LoadingModel,
    AnalyzingSpeech,
    Transcribing,
    Finalizing,
    Completed,
    Cancelling,
    Cancelled,
    Failed,
    Interrupted,
};

enum class JobStage {
    Preparing,
    InspectingMedia,
    NormalizingAudio,
    AnalyzingSpeech,
    LoadingModel,
    Transcribing,
    Finalizing,
    Completed,
};

enum class ChunkState { Pending, Running, Completed, Cancelled, Failed, Interrupted };

[[nodiscard]] QString jobStateName(JobState state);
[[nodiscard]] JobState jobStateFromName(const QString& name, JobState fallback = JobState::Queued);
[[nodiscard]] QString jobStageName(JobStage stage);
[[nodiscard]] JobStage jobStageFromName(const QString& name, JobStage fallback = JobStage::Preparing);
[[nodiscard]] QString chunkStateName(ChunkState state);
[[nodiscard]] ChunkState chunkStateFromName(const QString& name, ChunkState fallback = ChunkState::Pending);

struct TranscriptionJob {
    QString id;
    QString recordingId;
    JobState state = JobState::Queued;
    JobStage stage = JobStage::Preparing;
    double progress = 0.0;
    QString modelId;
    QString modelChecksum;
    QString engineVersion;
    QString workerVersion;
    QString backend;
    QString language = QStringLiteral("zh");
    QString preset = QStringLiteral("balanced");
    QString glossaryProfileId;
    QString meetingContext;
    bool vadEnabled = true;
    QString errorCode;
    QString errorMessage;
    QJsonObject diagnostics;
    QJsonObject parameters;
    int queuePosition = 0;
    int revisionNumber = 1;
    int retryCount = 0;
    QDateTime createdAt;
    QDateTime startedAt;
    QDateTime completedAt;
    QDateTime interruptedAt;
    int lastCompletedChunk = -1;
};

struct JobChunk {
    QString id;
    QString jobId;
    int ordinal = 0;
    qint64 startMs = 0;
    qint64 endMs = 0;
    qint64 overlapBeforeMs = 0;
    qint64 overlapAfterMs = 0;
    ChunkState state = ChunkState::Pending;
    int attempts = 0;
    QDateTime startedAt;
    QDateTime completedAt;
    QString error;
    QString resultHash;
    QJsonObject diagnostics;
};

} // namespace BreezeDesk
