#include <breezedesk/asr/WhisperSegmentCollector.h>

#include <breezedesk/asr/SegmentMetrics.h>

#include <algorithm>

#ifdef BREEZEDESK_HAS_WHISPER
#include <whisper.h>
#endif

namespace BreezeDesk::Asr {

WhisperSegmentCollector::WhisperSegmentCollector(qint64 globalOffsetMs, float lowConfidenceThreshold,
                                                 TranscriptionCallbacks callbacks)
    : m_globalOffsetMs(globalOffsetMs), m_lowConfidenceThreshold(lowConfidenceThreshold),
      m_callbacks(std::move(callbacks)) {}

void WhisperSegmentCollector::collectAvailable(whisper_context* context) {
#ifdef BREEZEDESK_HAS_WHISPER
    if (context == nullptr) {
        return;
    }
    const int count = whisper_full_n_segments(context);
    for (int segmentIndex = m_collectedCount; segmentIndex < count; ++segmentIndex) {
        TranscriptSegment segment;
        segment.startMs = SegmentMetrics::timestampTicksToMilliseconds(
            whisper_full_get_segment_t0(context, segmentIndex), m_globalOffsetMs);
        segment.endMs = SegmentMetrics::timestampTicksToMilliseconds(
            whisper_full_get_segment_t1(context, segmentIndex), m_globalOffsetMs);
        segment.originalText = QString::fromUtf8(whisper_full_get_segment_text(context, segmentIndex));
        segment.noSpeechProbability = whisper_full_get_segment_no_speech_prob(context, segmentIndex);

        const int tokenCount = whisper_full_n_tokens(context, segmentIndex);
        QVector<float> probabilities;
        probabilities.reserve(tokenCount);
        for (int tokenIndex = 0; tokenIndex < tokenCount; ++tokenIndex) {
            probabilities.append(whisper_full_get_token_p(context, segmentIndex, tokenIndex));
        }
        const auto confidence = SegmentMetrics::confidence(probabilities, m_lowConfidenceThreshold);
        segment.averageTokenProbability = confidence.averageProbability;
        segment.minimumTokenProbability = confidence.minimumProbability;
        segment.lowConfidence = confidence.lowConfidence;
        m_segments.append(segment);
        if (m_callbacks.segment) {
            try {
                m_callbacks.segment(segment);
            } catch (...) {
                // Never let an application callback unwind through whisper.cpp's C ABI.
            }
        }
    }
    m_collectedCount = count;
#else
    Q_UNUSED(context)
    Q_UNUSED(m_globalOffsetMs)
    Q_UNUSED(m_lowConfidenceThreshold)
    Q_UNUSED(m_collectedCount)
#endif
}

const QList<TranscriptSegment>& WhisperSegmentCollector::segments() const noexcept {
    return m_segments;
}

void WhisperSegmentCollector::callback(whisper_context* context, whisper_state* state, int newSegmentCount,
                                       void* userData) {
    Q_UNUSED(state)
    Q_UNUSED(newSegmentCount)
    auto* collector = static_cast<WhisperSegmentCollector*>(userData);
    if (collector != nullptr) {
        collector->collectAvailable(context);
    }
}

} // namespace BreezeDesk::Asr
