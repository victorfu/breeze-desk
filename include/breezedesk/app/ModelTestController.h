#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>

namespace BreezeDesk {

class ModelManager;

namespace Ipc {
class IAsrWorkerClient;
struct Envelope;
struct ProtocolError;
} // namespace Ipc

struct ModelTestTimeouts {
    int workerStartupMs{10'000};
    int modelLoadMs{120'000};
    int transcriptionMs{180'000};
    int modelUnloadMs{10'000};
};

struct ModelTestDependencies {
    ModelManager* models{nullptr};
    Ipc::IAsrWorkerClient* workerClient{nullptr};
    std::function<bool()> ensureWorkerStarted;
    std::function<bool()> workerReserved;
    std::function<void(bool)> setExternalWorkerReserved;
    std::function<void()> invalidateWorkerModelCache;
    std::function<void()> abortWorker;
    QString temporaryDirectory;
    ModelTestTimeouts timeouts;
};

class ModelTestController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)

  public:
    explicit ModelTestController(ModelTestDependencies dependencies, QObject* parent = nullptr);
    ~ModelTestController() override;

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] QString activeModelId() const;
    void setBackendPreference(const QString& backend, bool flashAttention);

  public slots:
    void testModel(const QString& modelId);
    void cancel();

  signals:
    void runningChanged();
    void testStarted(const QString& modelId);
    void progressChanged(const QString& modelId, double progress);
    void modelLoaded(const QString& modelId, const QString& selectedBackend, const QString& actualBackend,
                     const QString& runtimeVersion, qint64 loadTimeMs);
    void modelUnloaded(const QString& modelId);
    void testSucceeded(const QString& modelId, const QString& selectedBackend, const QString& actualBackend,
                       const QString& runtimeVersion, qint64 loadTimeMs);
    void testFailed(const QString& modelId, const QString& message, const QString& technicalDetails);
    void testCancelled(const QString& modelId);

  private:
    enum class Phase { Idle, WaitingForWorker, LoadingModel, Transcribing, UnloadingModel };

    [[nodiscard]] bool createFixture(QString* error);
    void beginLoadingModel();
    void beginTranscription();
    void beginUnload();
    void handleEnvelope(const Ipc::Envelope& envelope);
    void handleProtocolError(const Ipc::ProtocolError& error);
    void handleTimeout();
    void abortLoadedWorker(const QString& message, const QString& technicalDetails = {});
    void fail(const QString& message, const QString& technicalDetails = {});
    void finish(bool success);
    void cleanup();
    void armTimeout(int milliseconds);

    ModelTestDependencies m_dependencies;
    QPointer<ModelManager> m_models;
    QPointer<Ipc::IAsrWorkerClient> m_workerClient;
    QTimer m_timeout;
    Phase m_phase{Phase::Idle};
    QString m_modelId;
    QString m_modelPath;
    QByteArray m_modelSha256;
    QString m_fixturePath;
    QString m_requestId;
    QString m_jobId;
    QString m_backend{QStringLiteral("Auto")};
    QString m_selectedBackend;
    QString m_actualBackend;
    QString m_runtimeVersion;
    QString m_pendingFailure;
    QString m_pendingTechnicalDetails;
    qint64 m_loadTimeMs{0};
    bool m_flashAttention{true};
    bool m_cancelRequested{false};
    bool m_modelLoaded{false};
    bool m_inferenceSucceeded{false};
    bool m_reservationHeld{false};
    int m_unloadAttempts{0};
};

} // namespace BreezeDesk
