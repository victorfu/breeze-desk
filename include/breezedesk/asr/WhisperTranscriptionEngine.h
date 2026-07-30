#pragma once

#include <breezedesk/asr/ITranscriptionEngine.h>
#include <breezedesk/asr/WhisperModelSession.h>

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
                                                 const TranscriptionCallbacks& callbacks,
                                                 const CancellationFlag& cancellation) override;

    [[nodiscard]] int tokenCount(const QString& text) const;
    [[nodiscard]] int maximumPromptTokens() const;

  private:
    WhisperModelSession m_session;
};

} // namespace BreezeDesk::Asr
