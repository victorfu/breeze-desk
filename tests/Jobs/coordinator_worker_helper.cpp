#include "breezedesk/asr/AsrTypes.h"
#include "breezedesk/ipc/Protocol.h"
#include "breezedesk/ipc/WorkerServer.h"

#include <QCborArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
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
    Ipc::WorkerServer server;
    if (!server.listen(serverName, sessionToken)) {
        return 2;
    }
    QHash<QString, QString> deferredAnalysisRequests;

    QObject::connect(
        &server, &Ipc::WorkerServer::envelopeReceived, &application,
        [&server, &deferredAnalysisRequests](const quint64 clientId, const Ipc::Envelope& request) {
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
                server.send(clientId, loaded);
                return;
            }
            if (request.type == Ipc::MessageType::StartTranscription) {
                const qint64 startMs = request.payload.value(QStringLiteral("startMs")).toInteger(-1);
                const qint64 endMs = request.payload.value(QStringLiteral("endMs")).toInteger(-1);
                const bool valid =
                    request.jobId == QStringLiteral("job-coordinator") &&
                    QFileInfo(request.payload.value(QStringLiteral("pcmPath")).toString()).isFile() &&
                    startMs >= 0 && endMs > startMs &&
                    request.payload.value(QStringLiteral("vadEnabled")).toBool() &&
                    QFileInfo(request.payload.value(QStringLiteral("vadModelPath")).toString()).isFile() &&
                    request.payload.value(QStringLiteral("vadModelSha256")).toString().size() == 64;
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

                Ipc::Envelope completed;
                completed.type = Ipc::MessageType::ChunkCompleted;
                completed.requestId = request.requestId;
                completed.jobId = request.jobId;
                completed.payload.insert(QStringLiteral("segmentCount"), 1);
                completed.payload.insert(QStringLiteral("timingsMs"),
                                         QCborMap{{QStringLiteral("encode"), startMs == 0 ? 12.5 : 13.5}});
                server.send(clientId, completed);
                if (request.payload.value(QStringLiteral("finalChunk")).toBool()) {
                    completed.type = Ipc::MessageType::TranscriptionCompleted;
                    server.send(clientId, completed);
                }
                return;
            }
            if (request.type == Ipc::MessageType::CancelJob) {
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
