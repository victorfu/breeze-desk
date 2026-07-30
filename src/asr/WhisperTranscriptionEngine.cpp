#include <breezedesk/asr/WhisperTranscriptionEngine.h>

namespace BreezeDesk::Asr {

WhisperTranscriptionEngine::WhisperTranscriptionEngine() = default;
WhisperTranscriptionEngine::~WhisperTranscriptionEngine() = default;

ModelLoadResult WhisperTranscriptionEngine::loadModel(const ModelLoadOptions& options) {
    return m_session.load(options);
}

void WhisperTranscriptionEngine::unloadModel() {
    m_session.unload();
}

bool WhisperTranscriptionEngine::isModelLoaded() const noexcept {
    return m_session.isLoaded();
}

TranscriptionResult WhisperTranscriptionEngine::transcribe(const QVector<float>& samples,
                                                           qint64 globalOffsetMs,
                                                           const TranscriptionOptions& options,
                                                           const TranscriptionCallbacks& callbacks,
                                                           const CancellationFlag& cancellation) {
    return m_session.transcribe(samples, globalOffsetMs, options, callbacks, cancellation);
}

int WhisperTranscriptionEngine::tokenCount(const QString& text) const {
    return m_session.tokenCount(text);
}

int WhisperTranscriptionEngine::maximumPromptTokens() const {
    return m_session.maximumPromptTokens();
}

} // namespace BreezeDesk::Asr
