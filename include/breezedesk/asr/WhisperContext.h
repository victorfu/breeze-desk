#pragma once

#include <breezedesk/asr/AsrTypes.h>
#include <breezedesk/asr/LongFormChunkPlanner.h>

#include <QtCore/QVector>

#include <memory>

struct whisper_context;
struct whisper_vad_context;

namespace BreezeDesk::Asr {

struct WhisperContextDeleter {
    void operator()(whisper_context* context) const noexcept;
};

struct WhisperVadContextDeleter {
    void operator()(whisper_vad_context* context) const noexcept;
};

class WhisperContext final {
  public:
    WhisperContext() = default;
    ~WhisperContext();
    WhisperContext(WhisperContext&&) noexcept = default;
    WhisperContext& operator=(WhisperContext&&) noexcept = default;
    WhisperContext(const WhisperContext&) = delete;
    WhisperContext& operator=(const WhisperContext&) = delete;

    void reset(whisper_context* context = nullptr) noexcept;
    [[nodiscard]] whisper_context* get() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;

  private:
    std::unique_ptr<whisper_context, WhisperContextDeleter> m_context;
};

class WhisperVadContext final {
  public:
    WhisperVadContext() = default;
    ~WhisperVadContext();
    WhisperVadContext(WhisperVadContext&&) noexcept = default;
    WhisperVadContext& operator=(WhisperVadContext&&) noexcept = default;
    WhisperVadContext(const WhisperVadContext&) = delete;
    WhisperVadContext& operator=(const WhisperVadContext&) = delete;

    [[nodiscard]] AsrError load(const QString& modelPath, const QByteArray& expectedSha256, int threadCount,
                                bool useGpu, int gpuDevice);
    void reset() noexcept;
    [[nodiscard]] bool isLoaded() const noexcept;
    [[nodiscard]] AsrError analyzeBlock(const QVector<float>& samples, bool preserveState,
                                        QVector<float>* probabilities);
    [[nodiscard]] AsrError speechSegments(const QVector<float>& samples, const VadOptions& options,
                                          QList<SpeechRegion>* regions);
    void resetStreamingState();

  private:
    std::unique_ptr<whisper_vad_context, WhisperVadContextDeleter> m_context;
};

} // namespace BreezeDesk::Asr
