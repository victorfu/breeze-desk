#include <breezedesk/asr/StreamingVadSegmenter.h>

#include <algorithm>

namespace BreezeDesk::Asr {

StreamingVadSegmenter::StreamingVadSegmenter(StreamingVadConfiguration configuration)
    : m_configuration(configuration) {}

void StreamingVadSegmenter::appendProbabilities(const QVector<float>& probabilities) {
    for (const float probability : probabilities) {
        const qint64 frameStart = m_frameIndex * m_configuration.probabilityFrameMs;
        if (probability >= m_configuration.threshold) {
            if (m_speechStartMs < 0) {
                m_speechStartMs = frameStart;
            }
            m_silenceStartMs = -1;
        } else if (m_speechStartMs >= 0) {
            if (m_silenceStartMs < 0) {
                m_silenceStartMs = frameStart;
            }
            if (frameStart - m_silenceStartMs >= m_configuration.minimumSilenceMs) {
                closeSpeech(m_silenceStartMs);
            }
        }
        ++m_frameIndex;
    }
}

QList<SpeechRegion> StreamingVadSegmenter::finish(qint64 audioDurationMs) {
    if (m_speechStartMs >= 0) {
        closeSpeech(std::max(qint64{0}, audioDurationMs));
    }
    if (!m_regions.isEmpty()) {
        m_regions.last().endMs = std::min(m_regions.last().endMs, std::max(qint64{0}, audioDurationMs));
    }
    return m_regions;
}

void StreamingVadSegmenter::reset() {
    m_regions.clear();
    m_frameIndex = 0;
    m_speechStartMs = -1;
    m_silenceStartMs = -1;
}

void StreamingVadSegmenter::closeSpeech(qint64 endMs) {
    if (m_speechStartMs >= 0 && endMs - m_speechStartMs >= m_configuration.minimumSpeechMs) {
        const qint64 paddedStart = std::max(qint64{0}, m_speechStartMs - m_configuration.speechPaddingMs);
        const qint64 paddedEnd = endMs + m_configuration.speechPaddingMs;
        if (!m_regions.isEmpty() && paddedStart <= m_regions.last().endMs) {
            m_regions.last().endMs = std::max(m_regions.last().endMs, paddedEnd);
        } else {
            m_regions.append({paddedStart, paddedEnd});
        }
    }
    m_speechStartMs = -1;
    m_silenceStartMs = -1;
}

} // namespace BreezeDesk::Asr
