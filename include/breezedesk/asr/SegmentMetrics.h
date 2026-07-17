#pragma once

#include <QtCore/QVector>

namespace BreezeDesk::Asr {

struct ConfidenceMetrics {
    float averageProbability = 0.0F;
    float minimumProbability = 0.0F;
    bool lowConfidence = true;
};

class SegmentMetrics final {
  public:
    [[nodiscard]] static qint64 timestampTicksToMilliseconds(qint64 ticks,
                                                             qint64 globalOffsetMs = 0) noexcept;
    [[nodiscard]] static ConfidenceMetrics confidence(const QVector<float>& tokenProbabilities,
                                                      float lowConfidenceThreshold);
};

} // namespace BreezeDesk::Asr
