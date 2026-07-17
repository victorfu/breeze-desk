#pragma once

#include <breezedesk/asr/AsrTypes.h>

namespace BreezeDesk::Asr {

enum class SamplingStrategy {
    Greedy,
    BeamSearch,
};

struct PresetConfiguration {
    SamplingStrategy strategy = SamplingStrategy::Greedy;
    int bestOf = 1;
    int beamSize = 0;
    float temperature = 0.0F;
    float temperatureIncrement = 0.0F;
    bool noContext = false;
};

class PresetRegistry final {
  public:
    [[nodiscard]] static PresetConfiguration configuration(TranscriptionPreset preset);
};

} // namespace BreezeDesk::Asr
