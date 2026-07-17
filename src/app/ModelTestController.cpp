#include "breezedesk/app/ModelTestController.h"

#include "breezedesk/ipc/IAsrWorkerClient.h"
#include "breezedesk/models/ModelManager.h"

#include <QCborMap>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <utility>

namespace BreezeDesk {
namespace {

constexpr int TestSampleRate = 16'000;
constexpr int TestDurationMs = 3'000;
constexpr int FixtureSampleCount = TestSampleRate * TestDurationMs / 1'000;
constexpr qsizetype FixtureSampleBytes = static_cast<qsizetype>(sizeof(qint16));
constexpr double Pi = 3.14159265358979323846;

QString normalizedBackend(const QString& backend) {
    const QString value = backend.trimmed();
    return value.isEmpty() ? QStringLiteral("Auto") : value;
}

QString workerErrorMessage(const Ipc::Envelope& envelope) {
    const QString message = envelope.payload.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? ModelTestController::tr("The ASR worker rejected the model test.") : message;
}

} // namespace

ModelTestController::ModelTestController(ModelTestDependencies dependencies, QObject* parent)
    : QObject(parent), m_dependencies(std::move(dependencies)), m_models(m_dependencies.models),
      m_workerClient(m_dependencies.workerClient) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &ModelTestController::handleTimeout);
    if (m_workerClient != nullptr) {
        connect(m_workerClient, &Ipc::IAsrWorkerClient::ready, this, [this] {
            if (m_phase == Phase::WaitingForWorker) {
                beginLoadingModel();
            }
        });
        connect(m_workerClient, &Ipc::IAsrWorkerClient::envelopeReceived, this,
                &ModelTestController::handleEnvelope);
        connect(m_workerClient, &Ipc::IAsrWorkerClient::protocolError, this,
                &ModelTestController::handleProtocolError);
        connect(m_workerClient, &Ipc::IAsrWorkerClient::disconnected, this, [this] {
            if (isRunning()) {
                const bool wasLoaded = m_modelLoaded;
                m_modelLoaded = false;
                if (wasLoaded) {
                    emit modelUnloaded(m_modelId);
                }
                if (!m_pendingFailure.isEmpty()) {
                    finish(false);
                } else {
                    fail(tr("The ASR worker stopped during the model test."));
                }
            }
        });
    }
}

ModelTestController::~ModelTestController() {
    if (isRunning() && m_workerClient != nullptr && m_workerClient->isReady() &&
        m_phase == Phase::Transcribing) {
        m_workerClient->sendRequest(Ipc::MessageType::CancelJob, m_jobId, {});
    }
    cleanup();
}

bool ModelTestController::isRunning() const noexcept {
    return m_phase != Phase::Idle;
}

QString ModelTestController::activeModelId() const {
    return m_modelId;
}

void ModelTestController::setBackendPreference(const QString& backend, bool flashAttention) {
    if (isRunning()) {
        return;
    }
    m_backend = normalizedBackend(backend);
    m_flashAttention = flashAttention;
}

void ModelTestController::testModel(const QString& modelId) {
    if (isRunning()) {
        emit testFailed(modelId, tr("Wait for the current model test to finish."), {});
        return;
    }
    if (m_models == nullptr || m_workerClient == nullptr) {
        emit testFailed(modelId, tr("The model test service is unavailable."), {});
        return;
    }
    const QString path = m_models->modelPath(modelId);
    if (modelId.isEmpty() || path.isEmpty() || !QFileInfo(path).isFile()) {
        emit testFailed(modelId, tr("Install this model before testing it."), {});
        return;
    }
    const QByteArray expectedSha256 = m_models->expectedSha256(modelId);
    if (expectedSha256.size() != 64) {
        emit testFailed(modelId, tr("This model does not have a trusted SHA-256."), {});
        return;
    }
    if (m_dependencies.workerReserved && m_dependencies.workerReserved()) {
        emit testFailed(modelId, tr("Wait for the active transcription job to finish."), {});
        return;
    }

    m_modelId = modelId;
    m_modelPath = path;
    m_modelSha256 = expectedSha256;
    m_jobId = QStringLiteral("model-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_cancelRequested = false;
    m_modelLoaded = false;
    m_inferenceSucceeded = false;
    m_pendingFailure.clear();
    m_pendingTechnicalDetails.clear();
    m_selectedBackend.clear();
    m_actualBackend.clear();
    m_runtimeVersion.clear();
    m_loadTimeMs = 0;
    m_unloadAttempts = 0;
    if (m_dependencies.setExternalWorkerReserved) {
        m_dependencies.setExternalWorkerReserved(true);
        m_reservationHeld = true;
    }
    if (m_dependencies.invalidateWorkerModelCache) {
        m_dependencies.invalidateWorkerModelCache();
    }
    m_phase = Phase::WaitingForWorker;
    emit runningChanged();
    QString fixtureError;
    if (!createFixture(&fixtureError)) {
        fail(tr("The local model-test audio could not be created."), fixtureError);
        return;
    }

    m_models->setModelInUse(m_modelId, true);
    emit testStarted(m_modelId);
    emit progressChanged(m_modelId, 0.0);

    if (m_workerClient->isReady()) {
        beginLoadingModel();
        return;
    }
    if (!m_dependencies.ensureWorkerStarted || !m_dependencies.ensureWorkerStarted()) {
        fail(tr("The native ASR worker could not be started."));
        return;
    }
    armTimeout(m_dependencies.timeouts.workerStartupMs);
}

void ModelTestController::cancel() {
    if (!isRunning() || m_cancelRequested) {
        return;
    }
    m_cancelRequested = true;
    if (m_phase == Phase::WaitingForWorker) {
        finish(false);
        return;
    }
    if (m_phase == Phase::Transcribing && m_workerClient != nullptr && m_workerClient->isReady()) {
        m_workerClient->sendRequest(Ipc::MessageType::CancelJob, m_jobId, {});
        armTimeout(m_dependencies.timeouts.modelUnloadMs);
    }
    // whisper.cpp model loading is intentionally not terminated. Once loading
    // finishes, handleEnvelope() unloads the context without starting inference.
}

bool ModelTestController::createFixture(QString* error) {
    const QString directory = m_dependencies.temporaryDirectory.trimmed();
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        if (error != nullptr) {
            *error = tr("The temporary directory is unavailable.");
        }
        return false;
    }
    m_fixturePath = QDir(directory).filePath(
        QStringLiteral("model-test-%1.pcm").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QByteArray pcm(FixtureSampleCount * static_cast<int>(sizeof(qint16)), Qt::Uninitialized);
    for (int index = 0; index < FixtureSampleCount; ++index) {
        const double time = static_cast<double>(index) / static_cast<double>(TestSampleRate);
        const double attack = std::min(1.0, time * 8.0);
        const double release = std::min(1.0, (TestDurationMs / 1'000.0 - time) * 8.0);
        const double envelope = std::max(0.0, std::min(attack, release));
        const double sample =
            envelope * (0.11 * std::sin(2.0 * Pi * 173.0 * time) + 0.07 * std::sin(2.0 * Pi * 263.0 * time) +
                        0.03 * std::sin(2.0 * Pi * 431.0 * time));
        const auto value = static_cast<qint16>(std::clamp(sample, -1.0, 1.0) * 32767.0);
        qToLittleEndian<qint16>(
            value, reinterpret_cast<uchar*>(pcm.data() + static_cast<qsizetype>(index) * FixtureSampleBytes));
    }
    QSaveFile output(m_fixturePath);
    if (!output.open(QIODevice::WriteOnly) || output.write(pcm) != pcm.size() || !output.commit()) {
        if (error != nullptr) {
            *error = output.errorString();
        }
        output.cancelWriting();
        QFile::remove(m_fixturePath);
        m_fixturePath.clear();
        return false;
    }
    return true;
}

void ModelTestController::beginLoadingModel() {
    if (m_phase != Phase::WaitingForWorker || m_workerClient == nullptr || !m_workerClient->isReady()) {
        return;
    }
    m_phase = Phase::LoadingModel;
    m_requestId =
        m_workerClient->sendRequest(Ipc::MessageType::LoadModel, {},
                                    {{QStringLiteral("modelPath"), m_modelPath},
                                     {QStringLiteral("modelSha256"), QString::fromLatin1(m_modelSha256)},
                                     {QStringLiteral("backend"), m_backend},
                                     {QStringLiteral("flashAttention"), m_flashAttention}});
    if (m_requestId.isEmpty()) {
        fail(tr("The model-load request could not be sent to the ASR worker."));
        return;
    }
    emit progressChanged(m_modelId, 0.05);
    armTimeout(m_dependencies.timeouts.modelLoadMs);
}

void ModelTestController::beginTranscription() {
    if (m_workerClient == nullptr || !m_workerClient->isReady()) {
        fail(tr("The ASR worker disconnected before the audio test."));
        return;
    }
    m_phase = Phase::Transcribing;
    m_requestId = m_workerClient->sendRequest(Ipc::MessageType::StartTranscription, m_jobId,
                                              {{QStringLiteral("pcmPath"), m_fixturePath},
                                               {QStringLiteral("startMs"), 0},
                                               {QStringLiteral("endMs"), TestDurationMs},
                                               {QStringLiteral("finalChunk"), true},
                                               {QStringLiteral("language"), QStringLiteral("zh")},
                                               {QStringLiteral("preset"), QStringLiteral("fast")},
                                               {QStringLiteral("tokenTimestamps"), false},
                                               {QStringLiteral("vadEnabled"), false}});
    if (m_requestId.isEmpty()) {
        fail(tr("The audio-test request could not be sent to the ASR worker."));
        return;
    }
    emit progressChanged(m_modelId, 0.2);
    armTimeout(m_dependencies.timeouts.transcriptionMs);
}

void ModelTestController::beginUnload() {
    if (!m_modelLoaded || m_workerClient == nullptr || !m_workerClient->isReady()) {
        finish(m_inferenceSucceeded && m_pendingFailure.isEmpty() && !m_cancelRequested);
        return;
    }
    m_phase = Phase::UnloadingModel;
    ++m_unloadAttempts;
    m_requestId = m_workerClient->sendRequest(Ipc::MessageType::UnloadModel, {}, {});
    if (m_requestId.isEmpty()) {
        abortLoadedWorker(tr("The tested model could not be unloaded safely."));
        return;
    }
    armTimeout(m_dependencies.timeouts.modelUnloadMs);
}

void ModelTestController::handleEnvelope(const Ipc::Envelope& envelope) {
    if (!isRunning() || envelope.requestId != m_requestId) {
        return;
    }
    if (envelope.type == Ipc::MessageType::Error) {
        const QString message = workerErrorMessage(envelope);
        const QString details = envelope.payload.value(QStringLiteral("technicalDetails")).toString();
        if (m_phase == Phase::UnloadingModel) {
            if (m_unloadAttempts < 3) {
                QTimer::singleShot(100, this, &ModelTestController::beginUnload);
            } else {
                abortLoadedWorker(tr("The model test finished, but the worker could not unload the model."),
                                  message +
                                      (details.isEmpty() ? QString() : QStringLiteral(" — ") + details));
            }
            return;
        }
        m_pendingFailure = message;
        m_pendingTechnicalDetails = details;
        if (m_modelLoaded) {
            QTimer::singleShot(0, this, &ModelTestController::beginUnload);
        } else {
            fail(message, details);
        }
        return;
    }

    if (m_phase == Phase::LoadingModel && envelope.type == Ipc::MessageType::ModelLoaded) {
        m_modelLoaded = true;
        m_selectedBackend = envelope.payload.value(QStringLiteral("selectedBackend")).toString(m_backend);
        m_actualBackend = envelope.payload.value(QStringLiteral("actualBackend")).toString(m_selectedBackend);
        m_runtimeVersion = envelope.payload.value(QStringLiteral("runtimeVersion")).toString();
        m_loadTimeMs = envelope.payload.value(QStringLiteral("loadTimeMs")).toInteger();
        emit modelLoaded(m_modelId, m_selectedBackend, m_actualBackend, m_runtimeVersion, m_loadTimeMs);
        if (m_cancelRequested) {
            QTimer::singleShot(0, this, &ModelTestController::beginUnload);
        } else {
            QTimer::singleShot(0, this, &ModelTestController::beginTranscription);
        }
        return;
    }

    if (m_phase == Phase::Transcribing) {
        if (envelope.jobId != m_jobId) {
            return;
        }
        if (envelope.type == Ipc::MessageType::Progress) {
            const qint64 workerProgress = std::clamp(
                envelope.payload.value(QStringLiteral("progress")).toInteger(), qint64{0}, qint64{100});
            emit progressChanged(m_modelId, 0.2 + static_cast<double>(workerProgress) * 0.0075);
            return;
        }
        if (envelope.type == Ipc::MessageType::JobCancelled) {
            m_cancelRequested = true;
            QTimer::singleShot(0, this, &ModelTestController::beginUnload);
            return;
        }
        if (envelope.type == Ipc::MessageType::TranscriptionCompleted) {
            m_inferenceSucceeded = true;
            emit progressChanged(m_modelId, 0.95);
            QTimer::singleShot(0, this, &ModelTestController::beginUnload);
            return;
        }
    }

    if (m_phase == Phase::UnloadingModel && envelope.type == Ipc::MessageType::UnloadModel) {
        m_modelLoaded = false;
        emit modelUnloaded(m_modelId);
        emit progressChanged(m_modelId, 1.0);
        finish(m_inferenceSucceeded && m_pendingFailure.isEmpty() && !m_cancelRequested);
    }
}

void ModelTestController::handleProtocolError(const Ipc::ProtocolError& error) {
    if (isRunning()) {
        const bool wasLoaded = m_modelLoaded;
        m_modelLoaded = false;
        if (wasLoaded) {
            emit modelUnloaded(m_modelId);
        }
        fail(tr("The model test lost its authenticated worker connection."), error.detail);
    }
}

void ModelTestController::handleTimeout() {
    if (!isRunning()) {
        return;
    }
    if (m_phase == Phase::Transcribing && m_workerClient != nullptr && m_workerClient->isReady()) {
        m_workerClient->sendRequest(Ipc::MessageType::CancelJob, m_jobId, {});
    }
    abortLoadedWorker(tr("The model test timed out."));
}

void ModelTestController::abortLoadedWorker(const QString& message, const QString& technicalDetails) {
    if (!isRunning()) {
        return;
    }
    m_pendingFailure = message;
    m_pendingTechnicalDetails = technicalDetails;
    if (m_dependencies.abortWorker) {
        m_dependencies.abortWorker();
    }
    if (isRunning()) {
        const bool wasLoaded = m_modelLoaded;
        m_modelLoaded = false;
        if (wasLoaded) {
            emit modelUnloaded(m_modelId);
        }
        finish(false);
    }
}

void ModelTestController::fail(const QString& message, const QString& technicalDetails) {
    if (!isRunning()) {
        return;
    }
    m_pendingFailure = message;
    m_pendingTechnicalDetails = technicalDetails;
    finish(false);
}

void ModelTestController::finish(bool success) {
    if (!isRunning()) {
        return;
    }
    const QString modelId = m_modelId;
    const QString selectedBackend = m_selectedBackend;
    const QString actualBackend = m_actualBackend;
    const QString runtimeVersion = m_runtimeVersion;
    const qint64 loadTimeMs = m_loadTimeMs;
    const bool cancelled = m_cancelRequested;
    const QString failureMessage = m_pendingFailure;
    const QString failureDetails = m_pendingTechnicalDetails;
    cleanup();
    if (success) {
        emit testSucceeded(modelId, selectedBackend, actualBackend, runtimeVersion, loadTimeMs);
    } else if (cancelled && failureMessage.isEmpty()) {
        emit testCancelled(modelId);
    } else if (!failureMessage.isEmpty()) {
        emit testFailed(modelId, failureMessage, failureDetails);
    }
}

void ModelTestController::cleanup() {
    const bool wasRunning = isRunning();
    m_timeout.stop();
    if (m_models != nullptr && !m_modelId.isEmpty() && !m_modelLoaded) {
        m_models->setModelInUse(m_modelId, false);
    }
    if (!m_fixturePath.isEmpty()) {
        QFile::remove(m_fixturePath);
    }
    if (m_reservationHeld && m_dependencies.setExternalWorkerReserved) {
        m_dependencies.setExternalWorkerReserved(false);
    }
    m_reservationHeld = false;
    m_fixturePath.clear();
    m_requestId.clear();
    m_jobId.clear();
    m_modelPath.clear();
    m_modelSha256.clear();
    m_modelId.clear();
    m_pendingFailure.clear();
    m_pendingTechnicalDetails.clear();
    m_phase = Phase::Idle;
    if (wasRunning) {
        emit runningChanged();
    }
}

void ModelTestController::armTimeout(int milliseconds) {
    m_timeout.start(std::max(1, milliseconds));
}

} // namespace BreezeDesk
