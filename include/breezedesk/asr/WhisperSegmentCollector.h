#pragma once

#include <breezedesk/asr/AsrTypes.h>

#include <QtCore/QList>

struct whisper_context;
struct whisper_state;

namespace BreezeDesk::Asr {

class WhisperSegmentCollector final {
  public:
    WhisperSegmentCollector(qint64 globalOffsetMs, float lowConfidenceThreshold,
                            TranscriptionCallbacks callbacks = {});

    void collectAvailable(whisper_context* context);
    [[nodiscard]] const QList<TranscriptSegment>& segments() const noexcept;

    static void callback(whisper_context* context, whisper_state* state, int newSegmentCount, void* userData);

  private:
    qint64 m_globalOffsetMs;
    float m_lowConfidenceThreshold;
    TranscriptionCallbacks m_callbacks;
    QList<TranscriptSegment> m_segments;
    int m_collectedCount = 0;
};

} // namespace BreezeDesk::Asr
