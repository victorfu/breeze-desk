#pragma once

#include "breezedesk/ipc/Protocol.h"
#include "breezedesk/jobs/TranscriptionJob.h"
#include "breezedesk/transcript/TranscriptSegment.h"

#include <QObject>
#include <QPointer>

#include <atomic>
#include <memory>

namespace BreezeDesk {

class FFmpegNormalizationService;
class IJobRepository;
class IGlossaryRepository;
class IRecordingRepository;
class ITranscriptRepository;
class ModelManager;
class NormalizationOperation;
class TranscriptionSettingsManager;
class WorkerProcessManager;

class TranscriptionCoordinator final : public QObject {
    Q_OBJECT

  public:
    TranscriptionCoordinator(IRecordingRepository& recordings, IJobRepository& jobs,
                             ITranscriptRepository& transcripts, ModelManager& models,
                             WorkerProcessManager& worker, TranscriptionSettingsManager* settings = nullptr,
                             QObject* parent = nullptr);
    ~TranscriptionCoordinator() override;

    void initialize();
    void shutdown();
    void setGlossaryRepository(IGlossaryRepository* repository);
    [[nodiscard]] bool isTranscriptionActive() const noexcept;
    void setExternalWorkerReserved(bool reserved);
    void invalidateLoadedModel();

  public slots:
    void enqueue(const QString& jobId, const QString& recordingId);
    void cancel(const QString& jobId);
    void retry(const QString& jobId);
    void resume(const QString& jobId);
    void reorder(const QString& jobId, int destination);
    void clearCompleted();
    void setPauseAfterCurrent(bool enabled);

  signals:
    void jobChanged(const QString& jobId, const QString& recordingId, const QString& title,
                    const QString& state, const QString& stage, double progress, const QString& error);
    void transcriptChanged(const QString& recordingId, const QString& jobId, bool editingLocked);
    void libraryChanged();
    void errorOccurred(const QString& message);

  private:
    enum class RequestKind { None, AnalyzeSpeech, LoadModel, TranscribeChunk };

    void scheduleNext();
    void beginJob(const TranscriptionJob& job);
    void inspectMedia();
    void beginNormalization();
    void beginWaveformGeneration();
    void prepareChunks();
    bool saveChunkPlan(QList<JobChunk> chunks, QString* error = nullptr);
    void beginWaitingForModel();
    void ensureWorkerReady(const QString& jobId, int attempt = 0);
    void analyzeSpeech();
    void loadModel();
    void startNextChunk();
    void handleWorkerEnvelope(const Ipc::Envelope& envelope);
    void persistPartialSegments();
    bool finalizeCurrentChunk(QString* error);
    void completeActiveJob();
    void failActiveJob(const QString& code, const QString& message);
    void finishCancellation();
    void interruptActiveJob(const QString& reason);
    void clearActive();
    void clearLoadedAsrModel();
    void clearLoadedVadModel();
    bool persistLoadedRuntimeInfo(QString* error = nullptr);
    void publish(const QString& jobId);
    void publish(const TranscriptionJob& job);
    void advanceProgress(JobStage stage, double fraction, int lastCompletedChunk = -1);
    [[nodiscard]] QString recordingTitle(const QString& recordingId) const;
    [[nodiscard]] bool activeJobMatches(const QString& jobId) const;

    IRecordingRepository& m_recordings;
    IJobRepository& m_jobs;
    ITranscriptRepository& m_transcripts;
    ModelManager& m_models;
    WorkerProcessManager& m_worker;
    TranscriptionSettingsManager* m_settings{nullptr};
    IGlossaryRepository* m_glossaryRepository{nullptr};
    std::unique_ptr<FFmpegNormalizationService> m_normalizer;
    QPointer<NormalizationOperation> m_normalization;
    std::shared_ptr<std::atomic_bool> m_waveformCancellation;
    TranscriptionJob m_activeJob;
    QList<JobChunk> m_chunks;
    QList<TranscriptSegment> m_currentSegments;
    int m_currentChunkIndex{-1};
    int m_nextOrdinal{0};
    int m_lastNormalizationPercent{-1};
    QString m_activeSourcePath;
    QString m_activeNormalizedPath;
    QString m_loadedModelId;
    QString m_loadedModelPath;
    QByteArray m_loadedModelSha256;
    QString m_loadedBackend;
    QString m_loadedActualBackend;
    QString m_loadedRuntimeVersion;
    QJsonObject m_loadedRuntimeDiagnostics;
    QString m_loadedVadModelId;
    QString m_loadedVadPath;
    QString m_requestId;
    RequestKind m_requestKind{RequestKind::None};
    bool m_loadedFlashAttention{false};
    bool m_initialized{false};
    bool m_shuttingDown{false};
    bool m_pauseAfterCurrent{false};
    bool m_externalWorkerReserved{false};
    bool m_activeRevisionPublished{false};
};

} // namespace BreezeDesk
