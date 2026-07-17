#include <breezedesk/asr/WhisperContext.h>

#include <breezedesk/asr/ModelFileIntegrity.h>

#include <QtCore/QFileInfo>

#include <algorithm>
#include <limits>

#ifdef BREEZEDESK_HAS_WHISPER
#include <whisper.h>
#endif

namespace BreezeDesk::Asr {

void WhisperContextDeleter::operator()(whisper_context* context) const noexcept {
#ifdef BREEZEDESK_HAS_WHISPER
    if (context != nullptr) {
        whisper_free(context);
    }
#else
    Q_UNUSED(context)
#endif
}

void WhisperVadContextDeleter::operator()(whisper_vad_context* context) const noexcept {
#ifdef BREEZEDESK_HAS_WHISPER
    if (context != nullptr) {
        whisper_vad_free(context);
    }
#else
    Q_UNUSED(context)
#endif
}

WhisperContext::~WhisperContext() = default;

void WhisperContext::reset(whisper_context* context) noexcept {
    m_context.reset(context);
}

whisper_context* WhisperContext::get() const noexcept {
    return m_context.get();
}

WhisperContext::operator bool() const noexcept {
    return m_context != nullptr;
}

WhisperVadContext::~WhisperVadContext() = default;

AsrError WhisperVadContext::load(const QString& modelPath, const QByteArray& expectedSha256, int threadCount,
                                 bool useGpu, int gpuDevice) {
    reset();
    const AsrError integrity = ModelFileIntegrity::verifySha256(modelPath, expectedSha256);
    if (integrity.isError()) {
        return integrity;
    }
#ifdef BREEZEDESK_HAS_WHISPER
    auto parameters = whisper_vad_default_context_params();
    parameters.n_threads = qMax(1, threadCount);
    parameters.use_gpu = useGpu;
    parameters.gpu_device = qMax(0, gpuDevice);
    const QByteArray path = modelPath.toUtf8();
    m_context.reset(whisper_vad_init_from_file_with_params(path.constData(), parameters));
    if (!m_context) {
        return {AsrErrorCode::VadModelLoadFailed, QStringLiteral("Unable to load the VAD model"), modelPath};
    }
    return {};
#else
    Q_UNUSED(modelPath)
    Q_UNUSED(expectedSha256)
    Q_UNUSED(threadCount)
    Q_UNUSED(useGpu)
    Q_UNUSED(gpuDevice)
    return {AsrErrorCode::RuntimeUnavailable, QStringLiteral("This build does not include whisper.cpp"), {}};
#endif
}

void WhisperVadContext::reset() noexcept {
    m_context.reset();
}

bool WhisperVadContext::isLoaded() const noexcept {
    return m_context != nullptr;
}

AsrError WhisperVadContext::analyzeBlock(const QVector<float>& samples, bool preserveState,
                                         QVector<float>* probabilities) {
    if (probabilities == nullptr || samples.isEmpty() || samples.size() > std::numeric_limits<int>::max()) {
        return {AsrErrorCode::InvalidAudio,
                QStringLiteral("VAD requires non-empty samples and an output buffer"),
                {}};
    }
#ifdef BREEZEDESK_HAS_WHISPER
    if (!m_context) {
        return {AsrErrorCode::VadModelLoadFailed, QStringLiteral("VAD model is not loaded"), {}};
    }
    const int sampleCount = static_cast<int>(samples.size());
    const bool detected =
        preserveState ? whisper_vad_detect_speech_no_reset(m_context.get(), samples.constData(), sampleCount)
                      : whisper_vad_detect_speech(m_context.get(), samples.constData(), sampleCount);
    if (!detected) {
        return {AsrErrorCode::InferenceFailed, QStringLiteral("VAD analysis failed"),
                QStringLiteral("The input must be 16 kHz and aligned to 512-sample frames")};
    }
    const int count = whisper_vad_n_probs(m_context.get());
    const float* source = whisper_vad_probs(m_context.get());
    if (count < 0 || (count > 0 && source == nullptr)) {
        return {AsrErrorCode::InferenceFailed, QStringLiteral("VAD did not return probability data"), {}};
    }
    probabilities->resize(count);
    if (count > 0) {
        std::copy_n(source, count, probabilities->begin());
    }
    return {};
#else
    Q_UNUSED(preserveState)
    probabilities->clear();
    return {AsrErrorCode::RuntimeUnavailable, QStringLiteral("This build does not include whisper.cpp"), {}};
#endif
}

AsrError WhisperVadContext::speechSegments(const QVector<float>& samples, const VadOptions& options,
                                           QList<SpeechRegion>* regions) {
    if (regions == nullptr || samples.isEmpty() || samples.size() > std::numeric_limits<int>::max()) {
        return {
            AsrErrorCode::InvalidAudio, QStringLiteral("VAD segmentation requires non-empty samples"), {}};
    }
    regions->clear();
#ifdef BREEZEDESK_HAS_WHISPER
    if (!m_context) {
        return {AsrErrorCode::VadModelLoadFailed, QStringLiteral("VAD model is not loaded"), {}};
    }
    auto parameters = whisper_vad_default_params();
    parameters.threshold = options.threshold;
    parameters.min_speech_duration_ms = options.minimumSpeechMs;
    parameters.min_silence_duration_ms = options.minimumSilenceMs;
    parameters.max_speech_duration_s = options.maximumSpeechSeconds;
    parameters.speech_pad_ms = options.speechPaddingMs;
    parameters.samples_overlap = options.samplesOverlapSeconds;
    std::unique_ptr<whisper_vad_segments, decltype(&whisper_vad_free_segments)> segments(
        whisper_vad_segments_from_samples(m_context.get(), parameters, samples.constData(),
                                          static_cast<int>(samples.size())),
        &whisper_vad_free_segments);
    if (!segments) {
        return {AsrErrorCode::InferenceFailed, QStringLiteral("VAD segmentation failed"), {}};
    }
    const int count = whisper_vad_segments_n_segments(segments.get());
    regions->reserve(count);
    for (int index = 0; index < count; ++index) {
        // v1.9.1 exposes these values in centiseconds despite the float return type.
        const auto startMs = qRound64(whisper_vad_segments_get_segment_t0(segments.get(), index) * 10.0F);
        const auto endMs = qRound64(whisper_vad_segments_get_segment_t1(segments.get(), index) * 10.0F);
        regions->append({startMs, endMs});
    }
    return {};
#else
    Q_UNUSED(options)
    return {AsrErrorCode::RuntimeUnavailable, QStringLiteral("This build does not include whisper.cpp"), {}};
#endif
}

void WhisperVadContext::resetStreamingState() {
#ifdef BREEZEDESK_HAS_WHISPER
    if (m_context) {
        whisper_vad_reset_state(m_context.get());
    }
#endif
}

} // namespace BreezeDesk::Asr
