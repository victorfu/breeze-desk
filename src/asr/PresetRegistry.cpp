#include <breezedesk/asr/PresetRegistry.h>

namespace BreezeDesk::Asr {

PresetConfiguration PresetRegistry::configuration(TranscriptionPreset preset) {
    switch (preset) {
    case TranscriptionPreset::Fast:
        return {SamplingStrategy::Greedy, 1, 0, 0.0F, 0.0F, true};
    case TranscriptionPreset::Balanced:
        return {SamplingStrategy::Greedy, 2, 0, 0.0F, 0.2F, false};
    case TranscriptionPreset::Accurate:
        return {SamplingStrategy::BeamSearch, 0, 5, 0.0F, 0.2F, false};
    }
    return {};
}

} // namespace BreezeDesk::Asr
