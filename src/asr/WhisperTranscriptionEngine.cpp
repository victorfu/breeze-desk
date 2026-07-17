#include <breezedesk/asr/WhisperTranscriptionEngine.h>

namespace BreezeDesk::Asr {

WhisperTranscriptionEngine::WhisperTranscriptionEngine() = default;
WhisperTranscriptionEngine::~WhisperTranscriptionEngine() = default;

ModelLoadResult WhisperTranscriptionEngine::loadModel(const ModelLoadOptions& options) {
    m_cancelRequested.store(false, std::memory_order_relaxed);
    return m_session.load(options);
}

void WhisperTranscriptionEngine::unloadModel() {
    m_cancelRequested.store(true, std::memory_order_relaxed);
    m_session.unload();
}

bool WhisperTranscriptionEngine::isModelLoaded() const noexcept {
    return m_session.isLoaded();
}

TranscriptionResult WhisperTranscriptionEngine::transcribe(const QVector<float>& samples,
                                                           qint64 globalOffsetMs,
                                                           const TranscriptionOptions& options,
                                                           const TranscriptionCallbacks& callbacks) {
    m_cancelRequested.store(false, std::memory_order_relaxed);
    return m_session.transcribe(samples, globalOffsetMs, options, callbacks, m_cancelRequested);
}

void WhisperTranscriptionEngine::requestCancellation() noexcept {
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

int WhisperTranscriptionEngine::tokenCount(const QString& text) const {
    return m_session.tokenCount(text);
}

int WhisperTranscriptionEngine::maximumPromptTokens() const {
    return m_session.maximumPromptTokens();
}

} // namespace BreezeDesk::Asr
