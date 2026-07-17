#pragma once

#include <breezedesk/asr/LongFormChunkPlanner.h>

#include <QtCore/QList>
#include <QtCore/QVector>

namespace BreezeDesk::Asr {

struct StreamingVadConfiguration {
    float threshold = 0.5F;
    qint64 probabilityFrameMs = 32;
    qint64 minimumSpeechMs = 250;
    qint64 minimumSilenceMs = 100;
    qint64 speechPaddingMs = 30;
};

class StreamingVadSegmenter final {
  public:
    explicit StreamingVadSegmenter(StreamingVadConfiguration configuration = {});

    void appendProbabilities(const QVector<float>& probabilities);
    [[nodiscard]] QList<SpeechRegion> finish(qint64 audioDurationMs);
    void reset();

  private:
    void closeSpeech(qint64 endMs);

    StreamingVadConfiguration m_configuration;
    QList<SpeechRegion> m_regions;
    qint64 m_frameIndex = 0;
    qint64 m_speechStartMs = -1;
    qint64 m_silenceStartMs = -1;
};

} // namespace BreezeDesk::Asr
