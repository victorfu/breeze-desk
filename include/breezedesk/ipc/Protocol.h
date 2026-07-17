#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QCborMap>
#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtCore/QStringView>

#include <optional>

namespace BreezeDesk::Ipc {

inline constexpr qint64 kProtocolVersion = 2;
inline constexpr qsizetype kFramePrefixSize = 4;
inline constexpr quint32 kMaximumMessageSize = 16U * 1024U * 1024U;
inline constexpr int kHeartbeatIntervalMs = 2'000;
inline constexpr int kHeartbeatTimeoutMs = 10'000;

enum class MessageType {
    Hello,
    HelloAck,
    GetCapabilities,
    Capabilities,
    LoadModel,
    ModelLoaded,
    UnloadModel,
    AnalyzeSpeech,
    SpeechAnalysisCompleted,
    StartTranscription,
    Progress,
    PartialSegment,
    ChunkCompleted,
    TranscriptionCompleted,
    CancelJob,
    JobCancelled,
    Error,
    Ping,
    Pong,
    Shutdown,
    ActivateApplication,
    ActivationAccepted,
    ApplicationCommand,
    ApplicationCommandResult,
};

[[nodiscard]] QString messageTypeName(MessageType type);
[[nodiscard]] std::optional<MessageType> messageTypeFromName(QStringView name);

enum class ProtocolErrorCode {
    None,
    FrameTooLarge,
    InvalidFrameLength,
    InvalidCbor,
    InvalidEnvelope,
    UnsupportedMessage,
    ProtocolMismatch,
    AuthenticationFailed,
    HandshakeRequired,
    HeartbeatTimeout,
    SocketError,
};

struct ProtocolError {
    ProtocolErrorCode code = ProtocolErrorCode::None;
    QString detail;

    [[nodiscard]] bool isError() const noexcept { return code != ProtocolErrorCode::None; }
};

struct Envelope {
    qint64 protocolVersion = kProtocolVersion;
    MessageType type = MessageType::Error;
    QString requestId;
    QString jobId;
    QString workerVersion;
    QByteArray sessionToken;
    QCborMap payload;

    [[nodiscard]] QCborMap toCbor() const;
    [[nodiscard]] static std::optional<Envelope> fromCbor(const QCborMap& map,
                                                          ProtocolError* error = nullptr);
};

} // namespace BreezeDesk::Ipc

Q_DECLARE_METATYPE(BreezeDesk::Ipc::Envelope)
Q_DECLARE_METATYPE(BreezeDesk::Ipc::ProtocolError)
