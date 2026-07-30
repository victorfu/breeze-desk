#include "breezedesk/asr/AsrTypes.h"
#include "breezedesk/ipc/Protocol.h"
#include "breezedesk/ipc/WorkerServer.h"

#include <QCborArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QThread>
#include <QTimer>

using namespace BreezeDesk;

namespace {

QString optionValue(const QStringList& arguments, const QString& name) {
    const qsizetype index = arguments.indexOf(name);
    return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : QString{};
}

Ipc::Envelope errorEnvelope(const Ipc::Envelope& request, const QString& message) {
    Ipc::Envelope response;
    response.type = Ipc::MessageType::Error;
    response.requestId = request.requestId;
    response.jobId = request.jobId;
    response.payload.insert(QStringLiteral("code"), static_cast<qint64>(Asr::AsrErrorCode::InvalidRequest));
    response.payload.insert(QStringLiteral("message"), message);
    return response;
}

bool validAnalysisRequest(const Ipc::Envelope& request) {
    const QCborMap& payload = request.payload;
    return QFileInfo(payload.value(QStringLiteral("pcmPath")).toString()).isFile() &&
           QFileInfo(payload.value(QStringLiteral("vadModelPath")).toString()).isFile() &&
           payload.value(QStringLiteral("vadModelSha256")).toString().size() == 64 &&
           payload.value(QStringLiteral("vadThreshold")).toDouble() == 0.5 &&
           payload.value(QStringLiteral("vadMinimumSpeechMs")).toInteger() == 250 &&
           payload.value(QStringLiteral("vadMinimumSilenceMs")).toInteger() == 100 &&
           payload.value(QStringLiteral("vadMaximumSpeechSeconds")).toDouble() == 900.0 &&
           payload.value(QStringLiteral("vadSpeechPaddingMs")).toInteger() == 30;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    const QString serverName = optionValue(arguments, QStringLiteral("--server"));
    const QByteArray sessionToken = QByteArray::fromBase64(
        optionValue(arguments, QStringLiteral("--session-token")).toLatin1(), QByteArray::Base64UrlEncoding);
    const QString restartStatePath = qEnvironmentVariable("BREEZEDESK_TEST_COORDINATOR_RESTART_STATE");
    if (!restartStatePath.isEmpty()) {
        int previousLaunches = 0;
        QFile state(restartStatePath);
        if (state.open(QIODevice::ReadOnly)) {
            previousLaunches = state.readAll().trimmed().toInt();
            state.close();
        }
        if (state.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            state.write(QByteArray::number(previousLaunches + 1));
            state.close();
        }
        if (previousLaunches > 0) {
            QThread::msleep(2'000);
        }
    }
    int staleGraceLaunch = 0;
    const QString staleGraceStatePath =
        qEnvironmentVariable("BREEZEDESK_TEST_COORDINATOR_STALE_GRACE_STATE");
    if (!staleGraceStatePath.isEmpty()) {
        QFile state(staleGraceStatePath);
        if (state.open(QIODevice::ReadOnly)) {
            staleGraceLaunch = state.readAll().trimmed().toInt();
            state.close();
        }
        ++staleGraceLaunch;
        if (state.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            state.write(QByteArray::number(staleGraceLaunch));
            state.close();
        }
    }
    int forcedRecoveryLaunch = 0;
    const QString forcedRecoveryStatePath =
        qEnvironmentVariable("BREEZEDESK_TEST_COORDINATOR_FORCED_RECOVERY_STATE");
    if (!forcedRecoveryStatePath.isEmpty()) {
        QFile state(forcedRecoveryStatePath);
        if (state.open(QIODevice::ReadOnly)) {
            forcedRecoveryLaunch = state.readAll().trimmed().toInt();
            state.close();
        }
        ++forcedRecoveryLaunch;
        if (state.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            state.write(QByteArray::number(forcedRecoveryLaunch));
            state.close();
        }
        if (forcedRecoveryLaunch >= 2 && forcedRecoveryLaunch <= 4) {
            return 23;
        }
    }
    int connectRetryLaunch = 0;
    const QString connectRetryStatePath =
        qEnvironmentVariable("BREEZEDESK_TEST_COORDINATOR_CONNECT_RETRY_STATE");
    if (!connectRetryStatePath.isEmpty()) {
        QFile state(connectRetryStatePath);
        if (state.open(QIODevice::ReadOnly)) {
            connectRetryLaunch = state.readAll().trimmed().toInt();
            state.close();
        }
        ++connectRetryLaunch;
        if (state.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            state.write(QByteArray::number(connectRetryLaunch));
            state.close();
        }
        if (connectRetryLaunch == 1) {
            QThread::msleep(10'000);
        } else if (connectRetryLaunch == 2) {
            // Longer than the first generation's remaining retry budget in the
            // regression test, but shorter than a fresh generation's budget.
            QThread::msleep(3'500);
        }
    }
    Ipc::WorkerServer server;
    if (!server.listen(serverName, sessionToken)) {
        return 2;
    }
    QHash<QString, QString> deferredAnalysisRequests;
    Ipc::Envelope deferredModelLoaded;
    quint64 deferredModelClientId = 0;
    const QString deferredModelStatePath =
        qEnvironmentVariable("BREEZEDESK_TEST_COORDINATOR_DEFER_MODEL_LOAD_STATE");

    QObject::connect(
        &server, &Ipc::WorkerServer::envelopeReceived, &application,
        [&server, &deferredAnalysisRequests, &deferredModelLoaded, &deferredModelClientId,
         deferredModelStatePath, staleGraceLaunch,
         forcedRecoveryLaunch](const quint64 clientId, const Ipc::Envelope& request) {
            if (request.type == Ipc::MessageType::GetCapabilities) {
                Ipc::Envelope capabilities;
                capabilities.type = Ipc::MessageType::Capabilities;
                capabilities.requestId = request.requestId;
                capabilities.payload.insert(
                    QStringLiteral("runtimeAvailable"),
                    qEnvironmentVariable("BREEZEDESK_TEST_COORDINATOR_RUNTIME_AVAILABLE", "1") !=
                        QLatin1String("0"));
                capabilities.payload.insert(QStringLiteral("compiledBackend"), QStringLiteral("cpu"));
                capabilities.payload.insert(QStringLiteral("whisperVersion"),
                                            QStringLiteral("fake-whisper-1.2.3"));
                server.send(clientId, capabilities);
                return;
            }
            if (request.type == Ipc::MessageType::AnalyzeSpeech) {
                if (!validAnalysisRequest(request)) {
                    server.send(clientId, errorEnvelope(request, QStringLiteral("Invalid analysis payload")));
                    return;
                }
                Ipc::Envelope progress;
                progress.type = Ipc::MessageType::Progress;
                progress.requestId = request.requestId;
                progress.jobId = request.jobId;
                progress.payload.insert(QStringLiteral("stage"), QStringLiteral("AnalyzingSpeech"));
                progress.payload.insert(QStringLiteral("progress"), 50);
                server.send(clientId, progress);
                if (request.jobId == QStringLiteral("job-cancel")) {
                    deferredAnalysisRequests.insert(request.jobId, request.requestId);
                    return;
                }

                Ipc::Envelope completed;
                completed.type = Ipc::MessageType::SpeechAnalysisCompleted;
                completed.requestId = request.requestId;
                completed.jobId = request.jobId;
                completed.payload.insert(QStringLiteral("durationMs"), 1'300'000);
                QCborArray chunks;
                chunks.append(QCborMap{{QStringLiteral("ordinal"), 0},
                                       {QStringLiteral("startMs"), 0},
                                       {QStringLiteral("endMs"), 650'000},
                                       {QStringLiteral("overlapBeforeMs"), 0},
                                       {QStringLiteral("overlapAfterMs"), 900}});
                chunks.append(QCborMap{{QStringLiteral("ordinal"), 1},
                                       {QStringLiteral("startMs"), 649'100},
                                       {QStringLiteral("endMs"), 1'300'000},
                                       {QStringLiteral("overlapBeforeMs"), 900},
                                       {QStringLiteral("overlapAfterMs"), 0}});
                completed.payload.insert(QStringLiteral("chunks"), chunks);
                server.send(clientId, completed);
                return;
            }
            if (request.type == Ipc::MessageType::LoadModel) {
                const bool valid =
                    QFileInfo(request.payload.value(QStringLiteral("modelPath")).toString()).isFile() &&
                    request.payload.value(QStringLiteral("modelSha256")).toString().size() == 64 &&
                    request.payload.value(QStringLiteral("backend")).toString() == QStringLiteral("auto") &&
                    request.payload.value(QStringLiteral("flashAttention")).toBool();
                if (!valid) {
                    server.send(clientId, errorEnvelope(request, QStringLiteral("Invalid model payload")));
                    return;
                }
                Ipc::Envelope loaded;
                loaded.type = Ipc::MessageType::ModelLoaded;
                loaded.requestId = request.requestId;
                loaded.payload.insert(QStringLiteral("selectedBackend"), QStringLiteral("Auto"));
                loaded.payload.insert(QStringLiteral("actualBackend"), QStringLiteral("CPU"));
                loaded.payload.insert(QStringLiteral("flashAttention"), true);
                loaded.payload.insert(QStringLiteral("usedFallback"), true);
                loaded.payload.insert(QStringLiteral("runtimeVersion"), QStringLiteral("fake-whisper-1.2.3"));
                loaded.payload.insert(QStringLiteral("systemInfo"), QStringLiteral("fake-worker-system"));
                loaded.payload.insert(QStringLiteral("loadTimeMs"), 42);
                if (!deferredModelStatePath.isEmpty()) {
                    QFile state(deferredModelStatePath);
                    if (state.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        state.write(request.requestId.toUtf8());
                    }
                    deferredModelLoaded = loaded;
                    deferredModelClientId = clientId;
                    return;
                }
                server.send(clientId, loaded);
                return;
            }
            if (request.type == Ipc::MessageType::StartTranscription) {
                const QString transcriptionSentinel =
                    qEnvironmentVariable("BREEZEDESK_TEST_COORDINATOR_TRANSCRIPTION_SENTINEL");
                if (!transcriptionSentinel.isEmpty()) {
                    QFile sentinel(transcriptionSentinel);
                    if (sentinel.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        sentinel.write(request.jobId.toUtf8());
                    }
                }
                const qint64 startMs = request.payload.value(QStringLiteral("startMs")).toInteger(-1);
                const qint64 endMs = request.payload.value(QStringLiteral("endMs")).toInteger(-1);
                const bool leaseHandoffRequest =
                    request.jobId == QStringLiteral("job-lease-handoff");
                const bool postHandoffRequest =
                    request.jobId == QStringLiteral("job-after-lease-handoff");
                const bool staleGraceHandoffRequest =
                    request.jobId == QStringLiteral("job-stale-grace-handoff");
                const bool staleGraceNextRequest =
                    request.jobId == QStringLiteral("job-stale-grace-next");
                const bool checkpointFailureRequest =
                    request.jobId == QStringLiteral("job-progress-checkpoint-failure");
                const bool postCheckpointFailureRequest =
                    request.jobId == QStringLiteral("job-after-progress-checkpoint-failure");
                const bool completedCheckpointFailureRequest =
                    request.jobId == QStringLiteral("job-completed-chunk-checkpoint-failure");
                const bool workerCrashRequest =
                    request.jobId == QStringLiteral("job-worker-crash");
                const bool postWorkerCrashRequest =
                    request.jobId == QStringLiteral("job-after-worker-crash");
                const bool validVadPayload =
                    (leaseHandoffRequest || postHandoffRequest || staleGraceHandoffRequest ||
                     staleGraceNextRequest || checkpointFailureRequest ||
                     postCheckpointFailureRequest || completedCheckpointFailureRequest ||
                     workerCrashRequest || postWorkerCrashRequest)
                        ? !request.payload.value(QStringLiteral("vadEnabled")).toBool()
                        : request.payload.value(QStringLiteral("vadEnabled")).toBool() &&
                              QFileInfo(request.payload.value(QStringLiteral("vadModelPath")).toString())
                                  .isFile() &&
                              request.payload.value(QStringLiteral("vadModelSha256")).toString().size() == 64;
                const bool valid =
                    (request.jobId == QStringLiteral("job-coordinator") || leaseHandoffRequest ||
                     postHandoffRequest || staleGraceHandoffRequest || staleGraceNextRequest ||
                     checkpointFailureRequest || postCheckpointFailureRequest || workerCrashRequest ||
                     postWorkerCrashRequest || completedCheckpointFailureRequest) &&
                    QFileInfo(request.payload.value(QStringLiteral("pcmPath")).toString()).isFile() &&
                    startMs >= 0 && endMs > startMs && validVadPayload &&
                    (!staleGraceNextRequest || staleGraceLaunch >= 2);
                if (!valid) {
                    server.send(clientId,
                                errorEnvelope(request, QStringLiteral("Invalid transcription payload")));
                    return;
                }

                Ipc::Envelope progress;
                progress.type = Ipc::MessageType::Progress;
                progress.requestId = request.requestId;
                progress.jobId = request.jobId;
                progress.payload.insert(QStringLiteral("progress"), 50);
                server.send(clientId, progress);

                if (checkpointFailureRequest) {
                    return;
                }
                if (workerCrashRequest) {
                    QTimer::singleShot(50, QCoreApplication::instance(), &QCoreApplication::quit);
                    return;
                }

                Ipc::Envelope segment;
                segment.type = Ipc::MessageType::PartialSegment;
                segment.requestId = request.requestId;
                segment.jobId = request.jobId;
                segment.payload.insert(QStringLiteral("startMs"), startMs + 100);
                segment.payload.insert(QStringLiteral("endMs"), startMs + 1'000);
                segment.payload.insert(QStringLiteral("originalText"), startMs == 0
                                                                           ? QStringLiteral("first chunk")
                                                                           : QStringLiteral("second chunk"));
                segment.payload.insert(QStringLiteral("averageTokenProbability"), 0.9);
                segment.payload.insert(QStringLiteral("minimumTokenProbability"), 0.8);
                segment.payload.insert(QStringLiteral("noSpeechProbability"), 0.1);
                segment.payload.insert(QStringLiteral("lowConfidence"), false);
                server.send(clientId, segment);

                if (leaseHandoffRequest || staleGraceHandoffRequest) {
                    return;
                }

                const auto sendCompletion = [&server, clientId, request, startMs] {
                    Ipc::Envelope completed;
                    completed.type = Ipc::MessageType::ChunkCompleted;
                    completed.requestId = request.requestId;
                    completed.jobId = request.jobId;
                    completed.payload.insert(QStringLiteral("segmentCount"), 1);
                    completed.payload.insert(
                        QStringLiteral("timingsMs"),
                        QCborMap{{QStringLiteral("encode"), startMs == 0 ? 12.5 : 13.5}});
                    server.send(clientId, completed);
                    if (request.payload.value(QStringLiteral("finalChunk")).toBool()) {
                        completed.type = Ipc::MessageType::TranscriptionCompleted;
                        server.send(clientId, completed);
                    }
                };
                if (staleGraceNextRequest) {
                    // Keep the replacement request running beyond the first process's
                    // five-second forced-cancellation deadline.
                    QTimer::singleShot(7'000, &server, sendCompletion);
                } else {
                    sendCompletion();
                }
                return;
            }
            if (request.type == Ipc::MessageType::CancelJob) {
                if (request.jobId.isEmpty() && !deferredModelLoaded.requestId.isEmpty()) {
                    const Ipc::Envelope loaded = deferredModelLoaded;
                    const quint64 modelClientId = deferredModelClientId;
                    deferredModelLoaded = {};
                    deferredModelClientId = 0;
                    QTimer::singleShot(750, &server, [&server, modelClientId, loaded] {
                        server.send(modelClientId, loaded);
                    });
                    return;
                }
                if (request.jobId == QStringLiteral("job-lease-handoff")) {
                    return;
                }
                if (request.jobId == QStringLiteral("job-progress-checkpoint-failure")) {
                    return;
                }
                if (request.jobId == QStringLiteral("job-stale-grace-handoff") &&
                    staleGraceLaunch == 1) {
                    QTimer::singleShot(100, QCoreApplication::instance(), &QCoreApplication::quit);
                    return;
                }
                if (request.jobId == QStringLiteral("job-restart-exhaustion") &&
                    forcedRecoveryLaunch == 1) {
                    QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
                    return;
                }
                Ipc::Envelope cancelled;
                cancelled.type = Ipc::MessageType::JobCancelled;
                cancelled.requestId = deferredAnalysisRequests.take(request.jobId);
                if (cancelled.requestId.isEmpty()) {
                    cancelled.requestId = request.requestId;
                }
                cancelled.jobId = request.jobId;
                server.send(clientId, cancelled);
                return;
            }
            if (request.type == Ipc::MessageType::Shutdown) {
                QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
                return;
            }
            server.send(clientId, errorEnvelope(request, QStringLiteral("Unsupported test request")));
        });
    return application.exec();
}
