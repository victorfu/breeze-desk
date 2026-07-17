#include <breezedesk/asr/WhisperModelSession.h>

#include <breezedesk/asr/ModelFileIntegrity.h>

#include <breezedesk/asr/PresetRegistry.h>
#include <breezedesk/asr/WhisperBackendInfo.h>
#include <breezedesk/asr/WhisperLogBridge.h>
#include <breezedesk/asr/WhisperParameterMapper.h>
#include <breezedesk/asr/WhisperSegmentCollector.h>

#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

#ifdef BREEZEDESK_HAS_WHISPER
#include <whisper.h>
#endif

namespace BreezeDesk::Asr {
namespace {

#ifdef BREEZEDESK_HAS_WHISPER
struct CallbackState {
    std::atomic_bool* cancelRequested = nullptr;
    std::function<void(int)> progress;
    int lastProgress = -1;
};

void progressCallback(whisper_context*, whisper_state*, int progress, void* userData) {
    auto* state = static_cast<CallbackState*>(userData);
    if (state == nullptr || !state->progress) {
        return;
    }
    const int monotonicProgress = std::clamp(progress, state->lastProgress, 99);
    if (monotonicProgress == state->lastProgress) {
        return;
    }
    state->lastProgress = monotonicProgress;
    try {
        state->progress(monotonicProgress);
    } catch (...) {
        // Never let application callbacks unwind through the C ABI.
    }
}

bool abortCallback(void* userData) {
    const auto* state = static_cast<const CallbackState*>(userData);
    return state != nullptr && state->cancelRequested != nullptr &&
           state->cancelRequested->load(std::memory_order_relaxed);
}

struct LoadAttempt {
    bool useGpu = false;
    bool flashAttention = false;
    Backend backend = Backend::Cpu;
};
#endif

} // namespace

WhisperModelSession::WhisperModelSession() {
    WhisperLogBridge::install();
}

WhisperModelSession::~WhisperModelSession() {
    unload();
}

ModelLoadResult WhisperModelSession::load(const ModelLoadOptions& options) {
    ModelLoadResult result;
    result.requestedBackend = options.backend;
    QElapsedTimer timer;
    timer.start();

    QMutexLocker locker(&m_mutex);
    m_context.reset();
    result.error = ModelFileIntegrity::verifySha256(options.modelPath, options.expectedSha256);
    if (result.error.isError()) {
        result.loadTimeMs = timer.elapsed();
        return result;
    }
    result.runtimeVersion = WhisperBackendInfo::version();
    result.systemInfo = WhisperBackendInfo::systemInfo();
#ifdef BREEZEDESK_HAS_WHISPER
    const Backend compiledBackend = WhisperBackendInfo::compiledBackend();
    const bool asksForGpu = options.backend != Backend::Cpu;
    const bool requestedBackendAvailable = options.backend == Backend::Auto ||
                                           options.backend == Backend::Cpu ||
                                           options.backend == compiledBackend;
    std::vector<LoadAttempt> attempts;
    if (asksForGpu && requestedBackendAvailable && compiledBackend != Backend::Cpu) {
        attempts.push_back({true, options.flashAttention, compiledBackend});
        if (options.flashAttention) {
            attempts.push_back({true, false, compiledBackend});
        }
    }
    attempts.push_back({false, options.flashAttention, Backend::Cpu});
    if (options.flashAttention) {
        attempts.push_back({false, false, Backend::Cpu});
    }

    const QByteArray modelPath = options.modelPath.toUtf8();
    int attemptIndex = 0;
    for (const auto& attempt : attempts) {
        ++attemptIndex;
        auto parameters = whisper_context_default_params();
        parameters.use_gpu = attempt.useGpu;
        parameters.flash_attn = attempt.flashAttention;
        parameters.gpu_device = qMax(0, options.gpuDevice);
        WhisperContext candidate;
        candidate.reset(whisper_init_from_file_with_params(modelPath.constData(), parameters));
        if (!candidate) {
            continue;
        }
        m_context = std::move(candidate);
        m_backend = attempt.backend;
        m_flashAttention = attempt.flashAttention;
        result.actualBackend = m_backend;
        result.flashAttentionEnabled = m_flashAttention;
        result.usedFallback = attemptIndex > 1 ||
                              (options.backend != Backend::Auto && options.backend != m_backend) ||
                              (options.flashAttention && !m_flashAttention);
        result.loadTimeMs = timer.elapsed();
        return result;
    }

    result.error = {AsrErrorCode::ModelLoadFailed, QStringLiteral("Unable to load the ASR model"),
                    QStringLiteral("GPU, flash-attention, and CPU fallback attempts failed")};
#else
    Q_UNUSED(options)
    result.error = {
        AsrErrorCode::RuntimeUnavailable, QStringLiteral("This build does not include whisper.cpp"), {}};
#endif
    result.loadTimeMs = timer.elapsed();
    return result;
}

void WhisperModelSession::unload() {
    QMutexLocker locker(&m_mutex);
    m_context.reset();
    m_backend = Backend::Cpu;
    m_flashAttention = false;
}

bool WhisperModelSession::isLoaded() const noexcept {
    QMutexLocker locker(&m_mutex);
    return static_cast<bool>(m_context);
}

int WhisperModelSession::tokenCount(const QString& text) const {
    QMutexLocker locker(&m_mutex);
#ifdef BREEZEDESK_HAS_WHISPER
    if (!m_context || text.isEmpty()) {
        return 0;
    }
    const QByteArray utf8 = text.toUtf8();
    const int result = whisper_tokenize(m_context.get(), utf8.constData(), nullptr, 0);
    return result < 0 ? -result : result;
#else
    Q_UNUSED(text)
    return -1;
#endif
}

int WhisperModelSession::maximumPromptTokens() const {
    QMutexLocker locker(&m_mutex);
#ifdef BREEZEDESK_HAS_WHISPER
    return m_context ? whisper_n_text_ctx(m_context.get()) / 2 : 0;
#else
    return 0;
#endif
}

TranscriptionResult WhisperModelSession::transcribe(const QVector<float>& samples, qint64 globalOffsetMs,
                                                    const TranscriptionOptions& options,
                                                    const TranscriptionCallbacks& callbacks,
                                                    std::atomic_bool& cancelRequested) {
    TranscriptionResult result;
    QMutexLocker locker(&m_mutex);
    if (samples.isEmpty() || samples.size() > std::numeric_limits<int>::max()) {
        result.error = {AsrErrorCode::InvalidAudio,
                        QStringLiteral("Transcription requires non-empty 16 kHz mono samples"),
                        {}};
        return result;
    }
#ifdef BREEZEDESK_HAS_WHISPER
    if (!m_context) {
        result.error = {AsrErrorCode::ModelLoadFailed, QStringLiteral("No ASR model is loaded"), {}};
        return result;
    }
    if (cancelRequested.load(std::memory_order_relaxed)) {
        result.error = {AsrErrorCode::Cancelled, QStringLiteral("Transcription was cancelled"), {}};
        return result;
    }

    if (options.vad.enabled) {
        const AsrError integrity =
            ModelFileIntegrity::verifySha256(options.vad.modelPath, options.vad.expectedSha256);
        if (integrity.isError()) {
            result.error = integrity;
            return result;
        }
    }

    const auto mapped = WhisperParameterMapper::map(options, std::max(1, QThread::idealThreadCount() - 1));
    const auto strategy = mapped.strategy == SamplingStrategy::BeamSearch ? WHISPER_SAMPLING_BEAM_SEARCH
                                                                          : WHISPER_SAMPLING_GREEDY;
    auto parameters = whisper_full_default_params(strategy);
    const QByteArray language = mapped.language.toUtf8();
    const QByteArray initialPrompt = options.initialPrompt.toUtf8();
    const QByteArray vadPath = options.vad.modelPath.toUtf8();

    parameters.n_threads = mapped.threadCount;
    parameters.translate = false;
    parameters.no_context = mapped.noContext;
    parameters.no_timestamps = false;
    parameters.token_timestamps = mapped.tokenTimestamps;
    parameters.print_special = false;
    parameters.print_progress = false;
    parameters.print_realtime = false;
    parameters.print_timestamps = false;
    parameters.initial_prompt = initialPrompt.isEmpty() ? nullptr : initialPrompt.constData();
    parameters.carry_initial_prompt = mapped.carryInitialPrompt;
    parameters.language = language.constData();
    // With language="auto", false means detect then continue transcription.
    parameters.detect_language = false;
    parameters.suppress_blank = mapped.suppressBlank;
    parameters.suppress_nst = mapped.suppressNonSpeechTokens;
    parameters.no_speech_thold = mapped.noSpeechThreshold;
    parameters.temperature = mapped.temperature;
    parameters.temperature_inc = mapped.temperatureIncrement;
    parameters.greedy.best_of = mapped.bestOf;
    parameters.beam_search.beam_size = mapped.beamSize;
    parameters.vad = mapped.vadEnabled && !vadPath.isEmpty();
    parameters.vad_model_path = parameters.vad ? vadPath.constData() : nullptr;
    parameters.vad_params = whisper_vad_default_params();
    parameters.vad_params.threshold = options.vad.threshold;
    parameters.vad_params.min_speech_duration_ms = options.vad.minimumSpeechMs;
    parameters.vad_params.min_silence_duration_ms = options.vad.minimumSilenceMs;
    parameters.vad_params.max_speech_duration_s = options.vad.maximumSpeechSeconds;
    parameters.vad_params.speech_pad_ms = options.vad.speechPaddingMs;
    parameters.vad_params.samples_overlap = options.vad.samplesOverlapSeconds;

    CallbackState callbackState{&cancelRequested, callbacks.progress, -1};
    WhisperSegmentCollector collector(globalOffsetMs, options.lowConfidenceThreshold, callbacks);
    parameters.progress_callback = &progressCallback;
    parameters.progress_callback_user_data = &callbackState;
    parameters.new_segment_callback = &WhisperSegmentCollector::callback;
    parameters.new_segment_callback_user_data = &collector;
    parameters.abort_callback = &abortCallback;
    parameters.abort_callback_user_data = &callbackState;

    if (callbacks.progress) {
        callbacks.progress(0);
        callbackState.lastProgress = 0;
    }
    const int status =
        whisper_full(m_context.get(), parameters, samples.constData(), static_cast<int>(samples.size()));
    collector.collectAvailable(m_context.get());
    result.segments = collector.segments();

    std::unique_ptr<whisper_timings> timings(whisper_get_timings(m_context.get()));
    if (timings) {
        result.timingsMs.insert(QStringLiteral("sample"), timings->sample_ms);
        result.timingsMs.insert(QStringLiteral("encode"), timings->encode_ms);
        result.timingsMs.insert(QStringLiteral("decode"), timings->decode_ms);
        result.timingsMs.insert(QStringLiteral("batch"), timings->batchd_ms);
        result.timingsMs.insert(QStringLiteral("prompt"), timings->prompt_ms);
    }

    if (cancelRequested.load(std::memory_order_relaxed)) {
        result.error = {AsrErrorCode::Cancelled, QStringLiteral("Transcription was cancelled"), {}};
    } else if (status != 0) {
        result.error = {AsrErrorCode::InferenceFailed, QStringLiteral("whisper.cpp transcription failed"),
                        QStringLiteral("whisper_full returned %1").arg(status)};
    } else if (callbacks.progress) {
        callbacks.progress(100);
    }
#else
    Q_UNUSED(globalOffsetMs)
    Q_UNUSED(options)
    Q_UNUSED(callbacks)
    Q_UNUSED(cancelRequested)
    result.error = {
        AsrErrorCode::RuntimeUnavailable, QStringLiteral("This build does not include whisper.cpp"), {}};
#endif
    return result;
}

} // namespace BreezeDesk::Asr
