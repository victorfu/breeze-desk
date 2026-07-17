#pragma once

#include <breezedesk/asr/ITranscriptionEngine.h>
#include <breezedesk/asr/WhisperModelSession.h>

#include <atomic>

namespace BreezeDesk::Asr {

class WhisperTranscriptionEngine final : public ITranscriptionEngine {
  public:
    WhisperTranscriptionEngine();
    ~WhisperTranscriptionEngine() override;

    [[nodiscard]] ModelLoadResult loadModel(const ModelLoadOptions& options) override;
    void unloadModel() override;
    [[nodiscard]] bool isModelLoaded() const noexcept override;
    [[nodiscard]] TranscriptionResult transcribe(const QVector<float>& samples, qint64 globalOffsetMs,
                                                 const TranscriptionOptions& options,
                                                 const TranscriptionCallbacks& callbacks) override;
    void requestCancellation() noexcept override;

    [[nodiscard]] int tokenCount(const QString& text) const;
    [[nodiscard]] int maximumPromptTokens() const;

  private:
    WhisperModelSession m_session;
    std::atomic_bool m_cancelRequested = false;
};

} // namespace BreezeDesk::Asr
