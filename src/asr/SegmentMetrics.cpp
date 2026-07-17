#include <breezedesk/asr/SegmentMetrics.h>

#include <algorithm>
#include <limits>

namespace BreezeDesk::Asr {

qint64 SegmentMetrics::timestampTicksToMilliseconds(qint64 ticks, qint64 globalOffsetMs) noexcept {
    constexpr qint64 millisecondsPerTick = 10;
    if (ticks > std::numeric_limits<qint64>::max() / millisecondsPerTick) {
        return std::numeric_limits<qint64>::max();
    }
    if (ticks < std::numeric_limits<qint64>::min() / millisecondsPerTick) {
        return std::numeric_limits<qint64>::min();
    }
    const qint64 scaled = ticks * millisecondsPerTick;
    if (globalOffsetMs > 0 && scaled > std::numeric_limits<qint64>::max() - globalOffsetMs) {
        return std::numeric_limits<qint64>::max();
    }
    if (globalOffsetMs < 0 && scaled < std::numeric_limits<qint64>::min() - globalOffsetMs) {
        return std::numeric_limits<qint64>::min();
    }
    return globalOffsetMs + scaled;
}

ConfidenceMetrics SegmentMetrics::confidence(const QVector<float>& tokenProbabilities,
                                             float lowConfidenceThreshold) {
    ConfidenceMetrics metrics;
    if (tokenProbabilities.isEmpty()) {
        return metrics;
    }
    float sum = 0.0F;
    float minimum = std::numeric_limits<float>::max();
    for (const float probability : tokenProbabilities) {
        const float bounded = std::clamp(probability, 0.0F, 1.0F);
        sum += bounded;
        minimum = std::min(minimum, bounded);
    }
    metrics.averageProbability = sum / static_cast<float>(tokenProbabilities.size());
    metrics.minimumProbability = minimum;
    metrics.lowConfidence = metrics.averageProbability < lowConfidenceThreshold ||
                            metrics.minimumProbability < lowConfidenceThreshold;
    return metrics;
}

} // namespace BreezeDesk::Asr
