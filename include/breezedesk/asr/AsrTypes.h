#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QMetaType>
#include <QtCore/QString>

#include <functional>

namespace BreezeDesk::Asr {

enum class Backend {
    Auto,
    Cpu,
    Metal,
    Vulkan,
};

enum class TranscriptionPreset {
    Fast,
    Balanced,
    Accurate,
};

enum class AsrErrorCode {
    None,
    RuntimeUnavailable,
    ModelFileMissing,
    ModelChecksumMismatch,
    ModelLoadFailed,
    VadModelLoadFailed,
    InvalidAudio,
    Busy,
    Cancelled,
    InferenceFailed,
    InvalidRequest,
    IoError,
};

struct AsrError {
    AsrErrorCode code = AsrErrorCode::None;
    QString message;
    QString technicalDetails;

    [[nodiscard]] bool isError() const noexcept { return code != AsrErrorCode::None; }
};

struct ModelLoadOptions {
    QString modelPath;
    QByteArray expectedSha256;
    Backend backend = Backend::Auto;
    int gpuDevice = 0;
    bool flashAttention = true;
};

struct ModelLoadResult {
    AsrError error;
    Backend requestedBackend = Backend::Auto;
    Backend actualBackend = Backend::Cpu;
    bool flashAttentionEnabled = false;
    bool usedFallback = false;
    QString runtimeVersion;
    QString systemInfo;
    qint64 loadTimeMs = 0;
};

struct VadOptions {
    bool enabled = true;
    QString modelPath;
    QByteArray expectedSha256;
    float threshold = 0.5F;
    int minimumSpeechMs = 250;
    int minimumSilenceMs = 100;
    float maximumSpeechSeconds = 900.0F;
    int speechPaddingMs = 30;
    float samplesOverlapSeconds = 0.1F;
};

struct TranscriptionOptions {
    QString language = QStringLiteral("zh");
    TranscriptionPreset preset = TranscriptionPreset::Balanced;
    QString initialPrompt;
    bool noContext = false;
    bool carryInitialPrompt = true;
    bool tokenTimestamps = true;
    int threadCount = 0;
    bool suppressBlank = true;
    bool suppressNonSpeechTokens = true;
    float noSpeechThreshold = 0.6F;
    float temperature = 0.0F;
    float temperatureIncrement = -1.0F;
    float lowConfidenceThreshold = 0.45F;
    VadOptions vad;
};

struct TranscriptSegment {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString originalText;
    float averageTokenProbability = 0.0F;
    float minimumTokenProbability = 0.0F;
    float noSpeechProbability = 0.0F;
    bool lowConfidence = false;
};

struct TranscriptionResult {
    AsrError error;
    QList<TranscriptSegment> segments;
    QMap<QString, double> timingsMs;
};

struct TranscriptionCallbacks {
    std::function<void(int)> progress;
    std::function<void(const TranscriptSegment&)> segment;
};

[[nodiscard]] QString backendName(Backend backend);

} // namespace BreezeDesk::Asr

Q_DECLARE_METATYPE(BreezeDesk::Asr::TranscriptSegment)
Q_DECLARE_METATYPE(BreezeDesk::Asr::AsrError)
Q_DECLARE_METATYPE(BreezeDesk::Asr::ModelLoadOptions)
Q_DECLARE_METATYPE(BreezeDesk::Asr::TranscriptionOptions)
