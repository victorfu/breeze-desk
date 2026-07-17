#pragma once

#include <breezedesk/asr/AsrTypes.h>
#include <breezedesk/asr/PresetRegistry.h>

namespace BreezeDesk::Asr {

struct WhisperMappedParameters {
    SamplingStrategy strategy = SamplingStrategy::Greedy;
    QString language;
    bool detectLanguage = false;
    bool translate = false;
    bool noContext = false;
    bool noTimestamps = false;
    bool tokenTimestamps = true;
    bool carryInitialPrompt = true;
    int threadCount = 1;
    bool suppressBlank = true;
    bool suppressNonSpeechTokens = true;
    float noSpeechThreshold = 0.6F;
    float temperature = 0.0F;
    float temperatureIncrement = 0.2F;
    int bestOf = 2;
    int beamSize = 0;
    bool vadEnabled = true;
};

class WhisperParameterMapper final {
  public:
    [[nodiscard]] static WhisperMappedParameters map(const TranscriptionOptions& options,
                                                     int idealThreadCount);
};

} // namespace BreezeDesk::Asr
