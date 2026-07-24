#include <breezedesk/app_config.h>
#include <breezedesk/asr/LongFormChunkPlanner.h>
#include <breezedesk/asr/ModelFileIntegrity.h>
#include <breezedesk/asr/PromptTokenBudget.h>
#include <breezedesk/asr/StreamingVadSegmenter.h>
#include <breezedesk/asr/WhisperBackendInfo.h>
#include <breezedesk/asr/WhisperTranscriptionEngine.h>
#include <breezedesk/core/ApplicationLogger.h>
#include <breezedesk/core/StoragePaths.h>
#include <breezedesk/core/TemporaryFileJanitor.h>
#include <breezedesk/ipc/WorkerServer.h>

#include <QtCore/QCborArray>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLoggingCategory>
#include <QtCore/QThread>
#include <QtCore/QtEndian>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <utility>

Q_LOGGING_CATEGORY(workerLog, "breezedesk.asr.worker")

namespace BreezeDesk::Worker {
namespace {

using namespace BreezeDesk::Asr;
using namespace BreezeDesk::Ipc;

Backend backendFromString(const QString& name) {
    if (name.compare(QStringLiteral("cpu"), Qt::CaseInsensitive) == 0) {
        return Backend::Cpu;
    }
    if (name.compare(QStringLiteral("metal"), Qt::CaseInsensitive) == 0) {
        return Backend::Metal;
    }
    if (name.compare(QStringLiteral("vulkan"), Qt::CaseInsensitive) == 0) {
        return Backend::Vulkan;
    }
    return Backend::Auto;
}

TranscriptionPreset presetFromString(const QString& name) {
    if (name.compare(QStringLiteral("fast"), Qt::CaseInsensitive) == 0) {
        return TranscriptionPreset::Fast;
    }
    if (name.compare(QStringLiteral("accurate"), Qt::CaseInsensitive) == 0) {
        return TranscriptionPreset::Accurate;
    }
    return TranscriptionPreset::Balanced;
}

QCborMap segmentPayload(const TranscriptSegment& segment) {
    QCborMap payload;
    payload.insert(QStringLiteral("startMs"), segment.startMs);
    payload.insert(QStringLiteral("endMs"), segment.endMs);
    payload.insert(QStringLiteral("originalText"), segment.originalText);
    payload.insert(QStringLiteral("averageTokenProbability"),
                   static_cast<double>(segment.averageTokenProbability));
    payload.insert(QStringLiteral("minimumTokenProbability"),
                   static_cast<double>(segment.minimumTokenProbability));
    payload.insert(QStringLiteral("noSpeechProbability"), static_cast<double>(segment.noSpeechProbability));
    payload.insert(QStringLiteral("lowConfidence"), segment.lowConfidence);
    return payload;
}

Envelope errorEnvelope(const QString& requestId, const QString& jobId, const AsrError& error) {
    Envelope envelope;
    envelope.type = MessageType::Error;
    envelope.requestId = requestId;
    envelope.jobId = jobId;
    envelope.payload.insert(QStringLiteral("code"), static_cast<qint64>(error.code));
    envelope.payload.insert(QStringLiteral("message"), error.message);
    envelope.payload.insert(QStringLiteral("technicalDetails"), error.technicalDetails);
    return envelope;
}

struct PcmLayout {
    qint64 dataOffset = 0;
    qint64 dataSize = 0;
};

AsrError inspectPcmLayout(QFile& file, PcmLayout* layout) {
    if (layout == nullptr || file.size() <= 0) {
        return {AsrErrorCode::InvalidAudio, QStringLiteral("Normalized audio is empty"), {}};
    }
    const QByteArray header = file.peek(12);
    if (header.size() < 12 || header.first(4) != QByteArrayLiteral("RIFF") ||
        header.sliced(8, 4) != QByteArrayLiteral("WAVE")) {
        if (QFileInfo(file.fileName()).suffix().compare(QStringLiteral("pcm"), Qt::CaseInsensitive) != 0) {
            return {AsrErrorCode::InvalidAudio,
                    QStringLiteral("Normalized audio is not a RIFF/WAVE or raw PCM file"), file.fileName()};
        }
        if ((file.size() % 2) != 0) {
            return {AsrErrorCode::InvalidAudio, QStringLiteral("Raw PCM data has an incomplete sample"),
                    file.fileName()};
        }
        *layout = {0, file.size()};
        return {};
    }

    if (!file.seek(12)) {
        return {AsrErrorCode::IoError, QStringLiteral("Unable to inspect normalized WAV audio"),
                file.errorString()};
    }
    bool formatFound = false;
    bool dataFound = false;
    quint16 audioFormat = 0;
    quint16 channels = 0;
    quint32 waveSampleRate = 0;
    quint16 bitsPerSample = 0;
    while (file.pos() + 8 <= file.size()) {
        const QByteArray chunkHeader = file.read(8);
        if (chunkHeader.size() != 8) {
            break;
        }
        const QByteArray chunkId = chunkHeader.first(4);
        const quint32 chunkSize =
            qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(chunkHeader.constData() + 4));
        const qint64 chunkDataOffset = file.pos();
        const qint64 chunkEnd = chunkDataOffset + static_cast<qint64>(chunkSize);
        if (chunkEnd < chunkDataOffset || chunkEnd > file.size()) {
            return {AsrErrorCode::InvalidAudio, QStringLiteral("WAV chunk extends past the end of the file"),
                    QString::fromLatin1(chunkId)};
        }
        if (chunkId == QByteArrayLiteral("fmt ")) {
            if (chunkSize < 16) {
                return {AsrErrorCode::InvalidAudio, QStringLiteral("WAV format chunk is too short"), {}};
            }
            const QByteArray format = file.read(16);
            const auto* bytes = reinterpret_cast<const uchar*>(format.constData());
            audioFormat = qFromLittleEndian<quint16>(bytes);
            channels = qFromLittleEndian<quint16>(bytes + 2);
            waveSampleRate = qFromLittleEndian<quint32>(bytes + 4);
            bitsPerSample = qFromLittleEndian<quint16>(bytes + 14);
            formatFound = true;
        } else if (chunkId == QByteArrayLiteral("data")) {
            layout->dataOffset = chunkDataOffset;
            layout->dataSize = static_cast<qint64>(chunkSize);
            dataFound = true;
        }
        const qint64 paddedEnd = chunkEnd + static_cast<qint64>(chunkSize & 1U);
        if (paddedEnd > file.size() || !file.seek(paddedEnd)) {
            return {AsrErrorCode::InvalidAudio, QStringLiteral("Unable to advance through WAV chunks"),
                    file.errorString()};
        }
        if (formatFound && dataFound) {
            break;
        }
    }
    if (!formatFound || !dataFound || audioFormat != 1 || channels != 1 || waveSampleRate != 16'000U ||
        bitsPerSample != 16 || (layout->dataSize % 2) != 0) {
        return {AsrErrorCode::InvalidAudio, QStringLiteral("WAV must contain 16 kHz mono signed PCM16 audio"),
                QStringLiteral("format=%1 channels=%2 rate=%3 bits=%4")
                    .arg(audioFormat)
                    .arg(channels)
                    .arg(waveSampleRate)
                    .arg(bitsPerSample)};
    }
    return {};
}

AsrError readPcm16Chunk(const QString& path, qint64 startMs, qint64 endMs, std::atomic_bool& cancelled,
                        QVector<float>* samples) {
    constexpr qint64 sampleRate = 16'000;
    constexpr qint64 bytesPerSample = 2;
    constexpr qint64 blockBytes = 1024 * 1024;
    if (samples == nullptr || startMs < 0 || endMs <= startMs ||
        endMs > std::numeric_limits<qint64>::max() / sampleRate) {
        return {AsrErrorCode::InvalidRequest, QStringLiteral("Invalid PCM time range"), {}};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {AsrErrorCode::IoError, QStringLiteral("Unable to open normalized audio"), file.errorString()};
    }
    PcmLayout layout;
    const AsrError layoutError = inspectPcmLayout(file, &layout);
    if (layoutError.isError()) {
        return layoutError;
    }
    const qint64 firstSample = startMs * sampleRate / 1000;
    qint64 lastSample = endMs * sampleRate / 1000;
    const qint64 totalSamples = layout.dataSize / bytesPerSample;
    // A millisecond endpoint is coarser than the PCM sample clock. A duration
    // rounded to the nearest millisecond can therefore address up to one
    // millisecond beyond the final sample. Clamp only that rounding-sized
    // overshoot; larger ranges remain protocol errors.
    if (lastSample > totalSamples && lastSample - totalSamples <= sampleRate / 1000) {
        lastSample = totalSamples;
    }
    const qint64 relativeByteOffset = firstSample * bytesPerSample;
    const qint64 byteCount = (lastSample - firstSample) * bytesPerSample;
    if (relativeByteOffset < 0 || byteCount <= 0 || relativeByteOffset > layout.dataSize - byteCount ||
        byteCount / bytesPerSample > std::numeric_limits<int>::max()) {
        return {AsrErrorCode::InvalidAudio,
                QStringLiteral("Requested PCM range is outside the normalized audio"),
                QStringLiteral("offset=%1 bytes=%2 fileSize=%3")
                    .arg(relativeByteOffset)
                    .arg(byteCount)
                    .arg(layout.dataSize)};
    }
    const qint64 byteOffset = layout.dataOffset + relativeByteOffset;
    if (!file.seek(byteOffset)) {
        return {AsrErrorCode::IoError, QStringLiteral("Unable to seek normalized audio"), file.errorString()};
    }

    samples->clear();
    samples->reserve(static_cast<int>(byteCount / bytesPerSample));
    qint64 remaining = byteCount;
    while (remaining > 0) {
        if (cancelled.load(std::memory_order_relaxed)) {
            return {AsrErrorCode::Cancelled, QStringLiteral("Transcription was cancelled"), {}};
        }
        const QByteArray bytes = file.read(std::min(blockBytes, remaining));
        if (bytes.isEmpty() || (bytes.size() % bytesPerSample) != 0) {
            return {AsrErrorCode::IoError, QStringLiteral("Unable to read normalized audio"),
                    file.errorString()};
        }
        const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
        const qsizetype count = bytes.size() / bytesPerSample;
        for (qsizetype index = 0; index < count; ++index) {
            const qint16 value = qFromLittleEndian<qint16>(data + index * bytesPerSample);
            samples->append(static_cast<float>(value) / 32768.0F);
        }
        remaining -= bytes.size();
    }
    return {};
}

struct VadAnalysis {
    AsrError error;
    QList<SpeechRegion> regions;
    qint64 durationMs = 0;
};

VadAnalysis analyzePcmSpeech(const QString& path, WhisperVadContext& vad, const VadOptions& options,
                             std::atomic_bool& cancelled, const std::function<void(int)>& progress) {
    constexpr qsizetype vadFrameSamples = 512;
    constexpr qsizetype framesPerBlock = 128;
    constexpr qsizetype samplesPerBlock = vadFrameSamples * framesPerBlock;
    constexpr qint64 bytesPerSample = 2;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {{AsrErrorCode::IoError, QStringLiteral("Unable to open normalized audio for speech analysis"),
                 file.errorString()},
                {},
                0};
    }
    PcmLayout layout;
    const AsrError layoutError = inspectPcmLayout(file, &layout);
    if (layoutError.isError()) {
        return {layoutError, {}, 0};
    }
    if (!file.seek(layout.dataOffset)) {
        return {
            {AsrErrorCode::IoError, QStringLiteral("Unable to seek normalized audio"), file.errorString()},
            {},
            0};
    }

    StreamingVadConfiguration segmenterConfiguration;
    segmenterConfiguration.threshold = options.threshold;
    segmenterConfiguration.minimumSpeechMs = options.minimumSpeechMs;
    segmenterConfiguration.minimumSilenceMs = options.minimumSilenceMs;
    segmenterConfiguration.speechPaddingMs = options.speechPaddingMs;
    StreamingVadSegmenter segmenter(segmenterConfiguration);
    vad.resetStreamingState();
    qint64 processedBytes = 0;
    while (processedBytes < layout.dataSize) {
        if (cancelled.load(std::memory_order_relaxed)) {
            return {{AsrErrorCode::Cancelled, QStringLiteral("Speech analysis was cancelled"), {}}, {}, 0};
        }
        const qint64 requestedBytes =
            std::min(static_cast<qint64>(samplesPerBlock) * bytesPerSample, layout.dataSize - processedBytes);
        const QByteArray bytes = file.read(requestedBytes);
        if (bytes.size() != requestedBytes || (bytes.size() % bytesPerSample) != 0) {
            return {{AsrErrorCode::IoError,
                     QStringLiteral("Unable to read normalized audio for speech analysis"),
                     file.errorString()},
                    {},
                    0};
        }
        QVector<float> samples;
        samples.reserve(bytes.size() / bytesPerSample + vadFrameSamples);
        const auto* source = reinterpret_cast<const uchar*>(bytes.constData());
        const qsizetype sampleCount = bytes.size() / bytesPerSample;
        for (qsizetype index = 0; index < sampleCount; ++index) {
            const qint16 value = qFromLittleEndian<qint16>(source + index * bytesPerSample);
            samples.append(static_cast<float>(value) / 32768.0F);
        }
        const qsizetype remainder = samples.size() % vadFrameSamples;
        if (remainder != 0) {
            samples.resize(samples.size() + vadFrameSamples - remainder);
        }
        QVector<float> probabilities;
        const AsrError analysisError = vad.analyzeBlock(samples, true, &probabilities);
        if (analysisError.isError()) {
            return {analysisError, {}, 0};
        }
        // whisper.cpp reuses its probability buffer for every no-reset call;
        // append the copied QVector before processing the next bounded block.
        segmenter.appendProbabilities(probabilities);
        processedBytes += bytes.size();
        if (progress) {
            progress(
                qRound(100.0 * static_cast<double>(processedBytes) / static_cast<double>(layout.dataSize)));
        }
    }
    const qint64 totalSamples = layout.dataSize / bytesPerSample;
    // Use the greatest fully readable millisecond. readPcm16Chunk also accepts
    // a sub-millisecond rounded endpoint from older persisted chunk plans.
    const qint64 durationMs = totalSamples * 1000 / 16'000;
    return {{}, segmenter.finish(durationMs), durationMs};
}

TranscriptionOptions transcriptionOptions(const QCborMap& payload) {
    TranscriptionOptions options;
    options.language = payload.value(QStringLiteral("language")).toString(QStringLiteral("zh"));
    options.preset =
        presetFromString(payload.value(QStringLiteral("preset")).toString(QStringLiteral("balanced")));
    options.initialPrompt = payload.value(QStringLiteral("initialPrompt")).toString();
    options.noContext = payload.value(QStringLiteral("noContext")).toBool(false);
    options.carryInitialPrompt = payload.value(QStringLiteral("carryInitialPrompt")).toBool(true);
    options.tokenTimestamps = payload.value(QStringLiteral("tokenTimestamps")).toBool(true);
    options.threadCount = static_cast<int>(payload.value(QStringLiteral("threadCount")).toInteger(0));
    options.suppressBlank = payload.value(QStringLiteral("suppressBlank")).toBool(true);
    options.suppressNonSpeechTokens = payload.value(QStringLiteral("suppressNonSpeechTokens")).toBool(true);
    options.noSpeechThreshold =
        static_cast<float>(payload.value(QStringLiteral("noSpeechThreshold")).toDouble(0.6));
    options.lowConfidenceThreshold =
        static_cast<float>(payload.value(QStringLiteral("lowConfidenceThreshold")).toDouble(0.45));
    options.vad.enabled = payload.value(QStringLiteral("vadEnabled")).toBool(true);
    options.vad.modelPath = payload.value(QStringLiteral("vadModelPath")).toString();
    options.vad.expectedSha256 = payload.value(QStringLiteral("vadModelSha256")).toString().toLatin1();
    options.vad.threshold = static_cast<float>(payload.value(QStringLiteral("vadThreshold")).toDouble(0.5));
    return options;
}

QList<PromptPart> promptParts(const QCborMap& payload) {
    constexpr qsizetype MaximumPromptParts = 4'096;
    constexpr qsizetype MaximumPartCharacters = 4'096;
    QList<PromptPart> result;
    const QCborArray encoded = payload.value(QStringLiteral("promptParts")).toArray();
    result.reserve(std::min(encoded.size(), MaximumPromptParts));
    for (qsizetype index = 0; index < encoded.size() && index < MaximumPromptParts; ++index) {
        const QCborMap item = encoded.at(index).toMap();
        PromptPart part;
        part.text = item.value(QStringLiteral("text")).toString().left(MaximumPartCharacters).trimmed();
        part.priority = static_cast<int>(std::clamp(item.value(QStringLiteral("priority")).toInteger(),
                                                    qint64{-1'000'000}, qint64{1'000'000}));
        const QString kind = item.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("meetingContext")) {
            part.kind = PromptPartKind::MeetingContext;
        } else if (kind == QLatin1String("previousTranscript")) {
            part.kind = PromptPartKind::PreviousTranscript;
        } else {
            part.kind = PromptPartKind::Glossary;
        }
        if (!part.text.isEmpty()) {
            result.append(std::move(part));
        }
    }
    return result;
}

VadOptions vadOptions(const QCborMap& payload) {
    VadOptions options;
    options.enabled = true;
    options.modelPath = payload.value(QStringLiteral("vadModelPath")).toString();
    options.expectedSha256 = payload.value(QStringLiteral("vadModelSha256")).toString().toLatin1();
    options.threshold = std::clamp(
        static_cast<float>(payload.value(QStringLiteral("vadThreshold")).toDouble(0.5)), 0.01F, 0.99F);
    options.minimumSpeechMs =
        std::max(0, static_cast<int>(payload.value(QStringLiteral("vadMinimumSpeechMs")).toInteger(250)));
    options.minimumSilenceMs =
        std::max(0, static_cast<int>(payload.value(QStringLiteral("vadMinimumSilenceMs")).toInteger(100)));
    options.maximumSpeechSeconds = std::max(
        1.0F, static_cast<float>(payload.value(QStringLiteral("vadMaximumSpeechSeconds")).toDouble(900.0)));
    options.speechPaddingMs =
        std::max(0, static_cast<int>(payload.value(QStringLiteral("vadSpeechPaddingMs")).toInteger(30)));
    return options;
}

} // namespace

class InferenceRunner final : public QObject {
    Q_OBJECT

  public:
    using QObject::QObject;

    void cancel() {
        m_cancelled.store(true, std::memory_order_relaxed);
        m_engine.requestCancellation();
    }

  public slots:
    void loadModel(quint64 clientId, QString requestId, ModelLoadOptions options) {
        const ModelLoadResult result = m_engine.loadModel(options);
        if (result.error.isError()) {
            emit response(clientId, errorEnvelope(requestId, {}, result.error));
        } else {
            Envelope envelope;
            envelope.type = MessageType::ModelLoaded;
            envelope.requestId = requestId;
            envelope.payload.insert(QStringLiteral("selectedBackend"), backendName(result.requestedBackend));
            envelope.payload.insert(QStringLiteral("actualBackend"), backendName(result.actualBackend));
            envelope.payload.insert(QStringLiteral("flashAttention"), result.flashAttentionEnabled);
            envelope.payload.insert(QStringLiteral("usedFallback"), result.usedFallback);
            envelope.payload.insert(QStringLiteral("runtimeVersion"), result.runtimeVersion);
            envelope.payload.insert(QStringLiteral("systemInfo"), result.systemInfo);
            envelope.payload.insert(QStringLiteral("loadTimeMs"), result.loadTimeMs);
            emit response(clientId, envelope);
        }
        emit operationFinished();
    }

    void unloadModel(quint64 clientId, QString requestId) {
        m_engine.unloadModel();
        Envelope envelope;
        envelope.type = MessageType::UnloadModel;
        envelope.requestId = requestId;
        envelope.payload.insert(QStringLiteral("unloaded"), true);
        emit response(clientId, envelope);
        emit operationFinished();
    }

    void analyzeSpeech(quint64 clientId, QString requestId, QString jobId, QString pcmPath,
                       VadOptions options) {
        m_cancelled.store(false, std::memory_order_relaxed);
        AsrError loadError;
        if (!m_vad.isLoaded() || m_vadModelPath != options.modelPath ||
            m_vadModelSha256 != options.expectedSha256.toLower()) {
            // Silero is below 1 MiB and runs quickly on CPU. Keeping its weights
            // on CPU also avoids cross-backend tensor placement failures seen
            // when a Metal-built whisper.cpp loads VAD weights on the GPU but
            // schedules the streaming graph on BLAS/CPU.
            loadError = m_vad.load(options.modelPath, options.expectedSha256,
                                   std::max(1, QThread::idealThreadCount() - 1), false, 0);
            if (!loadError.isError()) {
                m_vadModelPath = options.modelPath;
                m_vadModelSha256 = options.expectedSha256.toLower();
            }
        } else {
            loadError = ModelFileIntegrity::verifySha256(options.modelPath, options.expectedSha256);
        }
        if (loadError.isError()) {
            emit response(clientId, errorEnvelope(requestId, jobId, loadError));
            emit operationFinished();
            return;
        }

        int lastProgress = -1;
        const auto progress = [this, clientId, requestId, jobId, &lastProgress](int value) {
            value = std::clamp(value, lastProgress, 100);
            if (value == lastProgress) {
                return;
            }
            lastProgress = value;
            Envelope envelope;
            envelope.type = MessageType::Progress;
            envelope.requestId = requestId;
            envelope.jobId = jobId;
            envelope.payload.insert(QStringLiteral("stage"), QStringLiteral("AnalyzingSpeech"));
            envelope.payload.insert(QStringLiteral("progress"), value);
            emit response(clientId, envelope);
        };
        const VadAnalysis analysis = analyzePcmSpeech(pcmPath, m_vad, options, m_cancelled, progress);
        if (analysis.error.code == AsrErrorCode::Cancelled) {
            emit response(clientId, cancellationEnvelope(requestId, jobId));
        } else if (analysis.error.isError()) {
            emit response(clientId, errorEnvelope(requestId, jobId, analysis.error));
        } else {
            Envelope completed;
            completed.type = MessageType::SpeechAnalysisCompleted;
            completed.requestId = requestId;
            completed.jobId = jobId;
            completed.payload.insert(QStringLiteral("durationMs"), analysis.durationMs);
            QCborArray regions;
            for (const auto& region : analysis.regions) {
                QCborMap value;
                value.insert(QStringLiteral("startMs"), region.startMs);
                value.insert(QStringLiteral("endMs"), region.endMs);
                regions.append(value);
            }
            completed.payload.insert(QStringLiteral("speechRegions"), regions);
            QCborArray chunks;
            const auto chunkPlan = LongFormChunkPlanner().plan(analysis.durationMs, analysis.regions);
            for (const auto& chunk : chunkPlan) {
                QCborMap value;
                value.insert(QStringLiteral("ordinal"), chunk.ordinal);
                value.insert(QStringLiteral("startMs"), chunk.startMs);
                value.insert(QStringLiteral("endMs"), chunk.endMs);
                value.insert(QStringLiteral("overlapBeforeMs"), chunk.overlapBeforeMs);
                value.insert(QStringLiteral("overlapAfterMs"), chunk.overlapAfterMs);
                chunks.append(value);
            }
            completed.payload.insert(QStringLiteral("chunks"), chunks);
            emit response(clientId, completed);
        }
        emit operationFinished();
    }

    void transcribe(quint64 clientId, QString requestId, QString jobId, QString pcmPath, qint64 startMs,
                    qint64 endMs, TranscriptionOptions options, QList<PromptPart> structuredPrompt,
                    bool finalChunk) {
        m_cancelled.store(false, std::memory_order_relaxed);
        QVector<float> samples;
        const AsrError readError = readPcm16Chunk(pcmPath, startMs, endMs, m_cancelled, &samples);
        if (readError.isError()) {
            emit response(clientId, readError.code == AsrErrorCode::Cancelled
                                        ? cancellationEnvelope(requestId, jobId)
                                        : errorEnvelope(requestId, jobId, readError));
            finishJob();
            return;
        }

        int promptTokenCount = 0;
        int omittedPromptParts = 0;
        if (!structuredPrompt.isEmpty()) {
            if (!options.initialPrompt.trimmed().isEmpty()) {
                structuredPrompt.prepend({options.initialPrompt, PromptPartKind::MeetingContext, 1'000'000});
            }
            const PromptBudgetResult budget =
                PromptTokenBudget::compose(std::move(structuredPrompt), m_engine.maximumPromptTokens(),
                                           [this](const QString& text) { return m_engine.tokenCount(text); });
            options.initialPrompt = budget.prompt;
            promptTokenCount = budget.tokenCount;
            omittedPromptParts = static_cast<int>(budget.omitted.size());
        }

        int lastProgress = -1;
        TranscriptionCallbacks callbacks;
        callbacks.progress = [this, clientId, requestId, jobId, &lastProgress](int progress) {
            if (m_cancelled.load(std::memory_order_relaxed)) {
                m_engine.requestCancellation();
            }
            progress = std::clamp(progress, lastProgress, 100);
            if (progress == lastProgress) {
                return;
            }
            lastProgress = progress;
            Envelope envelope;
            envelope.type = MessageType::Progress;
            envelope.requestId = requestId;
            envelope.jobId = jobId;
            envelope.payload.insert(QStringLiteral("progress"), progress);
            emit response(clientId, envelope);
        };
        callbacks.segment = [this, clientId, requestId, jobId](const TranscriptSegment& segment) {
            Envelope envelope;
            envelope.type = MessageType::PartialSegment;
            envelope.requestId = requestId;
            envelope.jobId = jobId;
            envelope.payload = segmentPayload(segment);
            emit response(clientId, envelope);
        };

        const auto result = m_engine.transcribe(samples, startMs, options, callbacks);
        if (result.error.code == AsrErrorCode::Cancelled) {
            emit response(clientId, cancellationEnvelope(requestId, jobId));
        } else if (result.error.isError()) {
            emit response(clientId, errorEnvelope(requestId, jobId, result.error));
        } else {
            Envelope completed;
            completed.type = MessageType::ChunkCompleted;
            completed.requestId = requestId;
            completed.jobId = jobId;
            completed.payload.insert(QStringLiteral("segmentCount"),
                                     static_cast<qint64>(result.segments.size()));
            completed.payload.insert(QStringLiteral("promptTokenCount"), promptTokenCount);
            completed.payload.insert(QStringLiteral("omittedPromptParts"), omittedPromptParts);
            QCborMap timings;
            for (auto iterator = result.timingsMs.cbegin(); iterator != result.timingsMs.cend(); ++iterator) {
                timings.insert(iterator.key(), iterator.value());
            }
            completed.payload.insert(QStringLiteral("timingsMs"), timings);
            emit response(clientId, completed);
            if (finalChunk) {
                Envelope finished = completed;
                finished.type = MessageType::TranscriptionCompleted;
                emit response(clientId, finished);
            }
        }
        finishJob();
    }

  signals:
    void response(quint64 clientId, const BreezeDesk::Ipc::Envelope& envelope);
    void operationFinished();

  private:
    static Envelope cancellationEnvelope(const QString& requestId, const QString& jobId) {
        Envelope envelope;
        envelope.type = MessageType::JobCancelled;
        envelope.requestId = requestId;
        envelope.jobId = jobId;
        envelope.payload.insert(QStringLiteral("partialResultsPreserved"), true);
        return envelope;
    }

    void finishJob() { emit operationFinished(); }

    WhisperTranscriptionEngine m_engine;
    WhisperVadContext m_vad;
    std::atomic_bool m_cancelled = false;
    QString m_vadModelPath;
    QByteArray m_vadModelSha256;
};

class WorkerController final : public QObject {
    Q_OBJECT

  public:
    WorkerController(WorkerServer* server, InferenceRunner* runner, QString workerVersion,
                     QObject* parent = nullptr)
        : QObject(parent), m_server(server), m_runner(runner), m_workerVersion(std::move(workerVersion)) {
        connect(server, &WorkerServer::envelopeReceived, this, &WorkerController::handleEnvelope);
        connect(
            runner, &InferenceRunner::response, server,
            [server](quint64 clientId, Envelope envelope) {
                if (!server->send(clientId, std::move(envelope))) {
                    qCWarning(workerLog, "Unable to send an inference response to client %llu",
                              static_cast<unsigned long long>(clientId));
                }
            },
            Qt::QueuedConnection);
        connect(
            runner, &InferenceRunner::operationFinished, this,
            [this] {
                m_busy = false;
                m_activeJobId.clear();
                if (m_shutdownRequested) {
                    QCoreApplication::quit();
                }
            },
            Qt::QueuedConnection);
    }

  private slots:
    void handleEnvelope(quint64 clientId, const Envelope& envelope) {
        qCDebug(workerLog, "Received %s from client %llu", qUtf8Printable(messageTypeName(envelope.type)),
                static_cast<unsigned long long>(clientId));
        switch (envelope.type) {
        case MessageType::GetCapabilities:
            sendCapabilities(clientId, envelope.requestId);
            break;
        case MessageType::LoadModel:
            if (!claimOperation(clientId, envelope)) {
                break;
            }
            queueLoad(clientId, envelope);
            break;
        case MessageType::UnloadModel:
            if (!claimOperation(clientId, envelope)) {
                break;
            }
            QMetaObject::invokeMethod(
                m_runner,
                [runner = m_runner, clientId, requestId = envelope.requestId] {
                    runner->unloadModel(clientId, requestId);
                },
                Qt::QueuedConnection);
            break;
        case MessageType::AnalyzeSpeech:
            if (!claimOperation(clientId, envelope)) {
                break;
            }
            queueSpeechAnalysis(clientId, envelope);
            break;
        case MessageType::StartTranscription:
            if (!claimOperation(clientId, envelope)) {
                break;
            }
            queueTranscription(clientId, envelope);
            break;
        case MessageType::CancelJob:
            if (envelope.jobId.isEmpty() || envelope.jobId == m_activeJobId) {
                m_runner->cancel();
            }
            break;
        case MessageType::Shutdown:
            m_shutdownRequested = true;
            m_runner->cancel();
            if (!m_busy) {
                QCoreApplication::quit();
            }
            break;
        default:
            m_server->send(clientId, errorEnvelope(envelope.requestId, envelope.jobId,
                                                   {AsrErrorCode::InvalidRequest,
                                                    QStringLiteral("Message is not valid for the worker"),
                                                    messageTypeName(envelope.type)}));
            break;
        }
    }

  private:
    bool claimOperation(quint64 clientId, const Envelope& envelope) {
        if (m_busy) {
            m_server->send(clientId, errorEnvelope(envelope.requestId, envelope.jobId,
                                                   {AsrErrorCode::Busy, QStringLiteral("ASR worker is busy"),
                                                    m_activeJobId}));
            return false;
        }
        m_busy = true;
        m_activeJobId = envelope.jobId;
        return true;
    }

    void sendCapabilities(quint64 clientId, const QString& requestId) {
        Envelope response;
        response.type = MessageType::Capabilities;
        response.requestId = requestId;
        response.workerVersion = m_workerVersion;
        response.payload.insert(QStringLiteral("runtimeAvailable"), WhisperBackendInfo::runtimeAvailable());
        response.payload.insert(QStringLiteral("streamingVad"), true);
        response.payload.insert(QStringLiteral("compiledBackend"),
                                backendName(WhisperBackendInfo::compiledBackend()));
        response.payload.insert(QStringLiteral("whisperVersion"), WhisperBackendInfo::version());
        // Full system info initializes hardware backends on some builds. It is
        // reported by ModelLoaded from the inference thread instead of blocking IPC.
        response.payload.insert(QStringLiteral("systemInfoDeferred"), true);
        response.payload.insert(QStringLiteral("protocolVersion"), kProtocolVersion);
        if (!m_server->send(clientId, response)) {
            qCWarning(workerLog, "Unable to send capabilities to client %llu",
                      static_cast<unsigned long long>(clientId));
        }
    }

    void queueLoad(quint64 clientId, const Envelope& envelope) {
        ModelLoadOptions options;
        options.modelPath = envelope.payload.value(QStringLiteral("modelPath")).toString();
        options.expectedSha256 = envelope.payload.value(QStringLiteral("modelSha256")).toString().toLatin1();
        options.backend = backendFromString(
            envelope.payload.value(QStringLiteral("backend")).toString(QStringLiteral("auto")));
        options.gpuDevice =
            static_cast<int>(envelope.payload.value(QStringLiteral("gpuDevice")).toInteger(0));
        options.flashAttention = envelope.payload.value(QStringLiteral("flashAttention")).toBool(true);
        QMetaObject::invokeMethod(
            m_runner,
            [runner = m_runner, clientId, requestId = envelope.requestId, options = std::move(options)] {
                runner->loadModel(clientId, requestId, options);
            },
            Qt::QueuedConnection);
    }

    void queueTranscription(quint64 clientId, const Envelope& envelope) {
        const QString pcmPath = envelope.payload.value(QStringLiteral("pcmPath")).toString();
        const qint64 startMs = envelope.payload.value(QStringLiteral("startMs")).toInteger();
        const qint64 endMs = envelope.payload.value(QStringLiteral("endMs")).toInteger();
        const bool finalChunk = envelope.payload.value(QStringLiteral("finalChunk")).toBool(false);
        const auto options = transcriptionOptions(envelope.payload);
        const auto structuredPrompt = promptParts(envelope.payload);
        QMetaObject::invokeMethod(
            m_runner,
            [runner = m_runner, clientId, requestId = envelope.requestId, jobId = envelope.jobId, pcmPath,
             startMs, endMs, options, structuredPrompt, finalChunk] {
                runner->transcribe(clientId, requestId, jobId, pcmPath, startMs, endMs, options,
                                   structuredPrompt, finalChunk);
            },
            Qt::QueuedConnection);
    }

    void queueSpeechAnalysis(quint64 clientId, const Envelope& envelope) {
        const QString pcmPath = envelope.payload.value(QStringLiteral("pcmPath")).toString();
        const auto options = vadOptions(envelope.payload);
        QMetaObject::invokeMethod(
            m_runner,
            [runner = m_runner, clientId, requestId = envelope.requestId, jobId = envelope.jobId, pcmPath,
             options] { runner->analyzeSpeech(clientId, requestId, jobId, pcmPath, options); },
            Qt::QueuedConnection);
    }

    WorkerServer* m_server;
    InferenceRunner* m_runner;
    QString m_workerVersion;
    QString m_activeJobId;
    bool m_busy = false;
    bool m_shutdownRequested = false;
};

} // namespace BreezeDesk::Worker

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const QString productName = QString::fromLatin1(BreezeDesk::AppConfig::ProductName);
    const QString workerExecutableName = QString::fromLatin1(BreezeDesk::AppConfig::WorkerExecutableName);
    QCoreApplication::setApplicationName(workerExecutableName);

    BreezeDesk::LoggingConfiguration loggingConfiguration;
    loggingConfiguration.processName = workerExecutableName;
    loggingConfiguration.logDirectory = BreezeDesk::StoragePaths::logs();
    BreezeDesk::ApplicationLogger logger(loggingConfiguration);
    const auto loggerResult = logger.install();
    if (!loggerResult) {
        const QByteArray safeError =
            BreezeDesk::LogSanitizer::sanitize(loggerResult.error().diagnosticString()).toUtf8();
        std::fprintf(stderr, "%s worker logging initialization failed: %s\n", qUtf8Printable(productName),
                     safeError.constData());
    }
    const BreezeDesk::TemporaryCleanupReport cleanup = BreezeDesk::TemporaryFileJanitor::clean();
    if (!cleanup.succeeded()) {
        qCWarning(workerLog, "Temporary cleanup reported %d failure(s): %s", cleanup.failures,
                  qUtf8Printable(cleanup.error));
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("%1 native ASR worker").arg(productName));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("server"), QStringLiteral("Local server name"), QStringLiteral("name")});
    parser.addOption({QStringLiteral("session-token"), QStringLiteral("Base64url session token"),
                      QStringLiteral("token")});
    parser.addOption({QStringLiteral("worker-version"), QStringLiteral("Worker version"),
                      QStringLiteral("version"), QStringLiteral("development")});
    parser.process(application);

    const QString serverName = parser.value(QStringLiteral("server"));
    const QByteArray sessionToken = QByteArray::fromBase64(
        parser.value(QStringLiteral("session-token")).toLatin1(), QByteArray::Base64UrlEncoding);
    if (serverName.isEmpty() || sessionToken.size() < 16) {
        qCCritical(workerLog, "%s", "A server name and at least 128 bits of session token are required");
        return 2;
    }

    qRegisterMetaType<BreezeDesk::Asr::ModelLoadOptions>();
    qRegisterMetaType<BreezeDesk::Asr::TranscriptionOptions>();
    qRegisterMetaType<BreezeDesk::Ipc::Envelope>();

    BreezeDesk::Ipc::WorkerServer server;
    if (!server.listen(serverName, sessionToken)) {
        qCCritical(workerLog, "Unable to listen on local endpoint: %s", qUtf8Printable(server.errorString()));
        return 3;
    }

    auto* inferenceThread = new QThread;
    auto* runner = new BreezeDesk::Worker::InferenceRunner;
    runner->moveToThread(inferenceThread);
    QObject::connect(inferenceThread, &QThread::finished, runner, &QObject::deleteLater);
    inferenceThread->start();
    BreezeDesk::Worker::WorkerController controller(&server, runner,
                                                    parser.value(QStringLiteral("worker-version")));

    const int exitCode = application.exec();
    runner->cancel();
    inferenceThread->quit();
    if (!inferenceThread->wait(5'000)) {
        qCCritical(workerLog, "%s", "Inference did not abort within the worker shutdown grace period");
        std::_Exit(12);
    }
    delete inferenceThread;
    return exitCode;
}

#include "main.moc"
