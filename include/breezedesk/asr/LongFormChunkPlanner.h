#pragma once

#include <QtCore/QList>

namespace BreezeDesk::Asr {

struct SpeechRegion {
    qint64 startMs = 0;
    qint64 endMs = 0;
};

struct TranscriptionChunk {
    int ordinal = 0;
    qint64 startMs = 0;
    qint64 endMs = 0;
    qint64 overlapBeforeMs = 0;
    qint64 overlapAfterMs = 0;
};

struct ChunkPlannerConfiguration {
    qint64 shortAudioThresholdMs = 12 * 60 * 1000;
    qint64 targetChunkMs = 10 * 60 * 1000;
    qint64 minimumPreferredChunkMs = 8 * 60 * 1000;
    qint64 maximumPreferredChunkMs = 12 * 60 * 1000;
    qint64 hardMaximumChunkMs = 15 * 60 * 1000;
    qint64 hardCutOverlapMs = 900;
};

class LongFormChunkPlanner final {
  public:
    explicit LongFormChunkPlanner(ChunkPlannerConfiguration configuration = {});

    [[nodiscard]] QList<TranscriptionChunk> plan(qint64 durationMs,
                                                 const QList<SpeechRegion>& speechRegions) const;

  private:
    ChunkPlannerConfiguration m_configuration;
};

} // namespace BreezeDesk::Asr
