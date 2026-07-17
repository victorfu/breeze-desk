#include <breezedesk/ipc/Protocol.h>

#include <QtCore/QCborValue>

#include <array>

namespace BreezeDesk::Ipc {
namespace {

using MessageEntry = std::pair<MessageType, QStringView>;

constexpr std::array<MessageEntry, 24> kMessageEntries{{
    {MessageType::Hello, u"Hello"},
    {MessageType::HelloAck, u"HelloAck"},
    {MessageType::GetCapabilities, u"GetCapabilities"},
    {MessageType::Capabilities, u"Capabilities"},
    {MessageType::LoadModel, u"LoadModel"},
    {MessageType::ModelLoaded, u"ModelLoaded"},
    {MessageType::UnloadModel, u"UnloadModel"},
    {MessageType::AnalyzeSpeech, u"AnalyzeSpeech"},
    {MessageType::SpeechAnalysisCompleted, u"SpeechAnalysisCompleted"},
    {MessageType::StartTranscription, u"StartTranscription"},
    {MessageType::Progress, u"Progress"},
    {MessageType::PartialSegment, u"PartialSegment"},
    {MessageType::ChunkCompleted, u"ChunkCompleted"},
    {MessageType::TranscriptionCompleted, u"TranscriptionCompleted"},
    {MessageType::CancelJob, u"CancelJob"},
    {MessageType::JobCancelled, u"JobCancelled"},
    {MessageType::Error, u"Error"},
    {MessageType::Ping, u"Ping"},
    {MessageType::Pong, u"Pong"},
    {MessageType::Shutdown, u"Shutdown"},
    {MessageType::ActivateApplication, u"ActivateApplication"},
    {MessageType::ActivationAccepted, u"ActivationAccepted"},
    {MessageType::ApplicationCommand, u"ApplicationCommand"},
    {MessageType::ApplicationCommandResult, u"ApplicationCommandResult"},
}};

void setError(ProtocolError* error, ProtocolErrorCode code, const QString& detail) {
    if (error != nullptr) {
        *error = {code, detail};
    }
}

} // namespace

QString messageTypeName(MessageType type) {
    for (const auto& [candidate, name] : kMessageEntries) {
        if (candidate == type) {
            return name.toString();
        }
    }
    return QStringLiteral("Error");
}

std::optional<MessageType> messageTypeFromName(QStringView name) {
    for (const auto& [type, candidate] : kMessageEntries) {
        if (candidate == name) {
            return type;
        }
    }
    return std::nullopt;
}

QCborMap Envelope::toCbor() const {
    QCborMap map;
    map.insert(QStringLiteral("protocolVersion"), protocolVersion);
    map.insert(QStringLiteral("type"), messageTypeName(type));
    map.insert(QStringLiteral("requestId"), requestId);
    map.insert(QStringLiteral("jobId"), jobId);
    map.insert(QStringLiteral("workerVersion"), workerVersion);
    map.insert(QStringLiteral("sessionToken"), sessionToken);
    map.insert(QStringLiteral("payload"), payload);
    return map;
}

std::optional<Envelope> Envelope::fromCbor(const QCborMap& map, ProtocolError* error) {
    const auto protocolValue = map.value(QStringLiteral("protocolVersion"));
    const auto typeValue = map.value(QStringLiteral("type"));
    const auto requestValue = map.value(QStringLiteral("requestId"));
    const auto jobValue = map.value(QStringLiteral("jobId"));
    const auto workerValue = map.value(QStringLiteral("workerVersion"));
    const auto tokenValue = map.value(QStringLiteral("sessionToken"));
    const auto payloadValue = map.value(QStringLiteral("payload"));

    if (!protocolValue.isInteger() || !typeValue.isString() || !requestValue.isString() ||
        !jobValue.isString() || !workerValue.isString() || !tokenValue.isByteArray() ||
        !payloadValue.isMap()) {
        setError(error, ProtocolErrorCode::InvalidEnvelope,
                 QStringLiteral("Envelope contains missing or incorrectly typed fields"));
        return std::nullopt;
    }

    const auto type = messageTypeFromName(typeValue.toString());
    if (!type.has_value()) {
        setError(error, ProtocolErrorCode::UnsupportedMessage,
                 QStringLiteral("Unknown message type: %1").arg(typeValue.toString()));
        return std::nullopt;
    }

    Envelope envelope;
    envelope.protocolVersion = protocolValue.toInteger();
    envelope.type = *type;
    envelope.requestId = requestValue.toString();
    envelope.jobId = jobValue.toString();
    envelope.workerVersion = workerValue.toString();
    envelope.sessionToken = tokenValue.toByteArray();
    envelope.payload = payloadValue.toMap();
    if (error != nullptr) {
        *error = {};
    }
    return envelope;
}

} // namespace BreezeDesk::Ipc
