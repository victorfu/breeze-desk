#include <breezedesk/asr/WhisperParameterMapper.h>

#include <algorithm>

namespace BreezeDesk::Asr {

WhisperMappedParameters WhisperParameterMapper::map(const TranscriptionOptions& options,
                                                    int idealThreadCount) {
    const auto preset = PresetRegistry::configuration(options.preset);
    WhisperMappedParameters mapped;
    mapped.strategy = preset.strategy;
    mapped.language = options.language.trimmed();
    mapped.detectLanguage = mapped.language.isEmpty() ||
                            mapped.language.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0;
    if (mapped.detectLanguage) {
        mapped.language = QStringLiteral("auto");
    }
    mapped.noContext = options.noContext || preset.noContext;
    mapped.tokenTimestamps = options.tokenTimestamps;
    mapped.carryInitialPrompt = options.carryInitialPrompt;
    mapped.threadCount = options.threadCount > 0 ? options.threadCount : std::max(1, idealThreadCount);
    mapped.suppressBlank = options.suppressBlank;
    mapped.suppressNonSpeechTokens = options.suppressNonSpeechTokens;
    mapped.noSpeechThreshold = std::clamp(options.noSpeechThreshold, 0.0F, 1.0F);
    mapped.temperature = options.temperature >= 0.0F ? options.temperature : preset.temperature;
    mapped.temperatureIncrement =
        options.temperatureIncrement >= 0.0F ? options.temperatureIncrement : preset.temperatureIncrement;
    mapped.bestOf = preset.bestOf;
    mapped.beamSize = preset.beamSize;
    mapped.vadEnabled = options.vad.enabled;
    return mapped;
}

} // namespace BreezeDesk::Asr
