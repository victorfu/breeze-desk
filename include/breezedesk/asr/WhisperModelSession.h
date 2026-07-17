#pragma once

#include <breezedesk/asr/AsrTypes.h>
#include <breezedesk/asr/WhisperContext.h>

#include <QtCore/QMutex>
#include <QtCore/QVector>

#include <atomic>

namespace BreezeDesk::Asr {

class WhisperModelSession final {
  public:
    WhisperModelSession();
    ~WhisperModelSession();
    WhisperModelSession(const WhisperModelSession&) = delete;
    WhisperModelSession& operator=(const WhisperModelSession&) = delete;

    [[nodiscard]] ModelLoadResult load(const ModelLoadOptions& options);
    void unload();
    [[nodiscard]] bool isLoaded() const noexcept;
    [[nodiscard]] int tokenCount(const QString& text) const;
    [[nodiscard]] int maximumPromptTokens() const;
    [[nodiscard]] TranscriptionResult transcribe(const QVector<float>& samples, qint64 globalOffsetMs,
                                                 const TranscriptionOptions& options,
                                                 const TranscriptionCallbacks& callbacks,
                                                 std::atomic_bool& cancelRequested);

  private:
    mutable QMutex m_mutex;
    WhisperContext m_context;
    Backend m_backend = Backend::Cpu;
    bool m_flashAttention = false;
};

} // namespace BreezeDesk::Asr
