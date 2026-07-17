#include <breezedesk/asr/LongFormChunkPlanner.h>

#include <QtCore/QList>

#include <algorithm>
#include <limits>

namespace BreezeDesk::Asr {

LongFormChunkPlanner::LongFormChunkPlanner(ChunkPlannerConfiguration configuration)
    : m_configuration(configuration) {}

QList<TranscriptionChunk> LongFormChunkPlanner::plan(qint64 durationMs,
                                                     const QList<SpeechRegion>& speechRegions) const {
    QList<TranscriptionChunk> chunks;
    if (durationMs <= 0) {
        return chunks;
    }
    if (durationMs <= m_configuration.shortAudioThresholdMs) {
        chunks.append({0, 0, durationMs, 0, 0});
        return chunks;
    }

    QList<SpeechRegion> regions;
    regions.reserve(speechRegions.size());
    for (const auto& region : speechRegions) {
        const qint64 start = std::clamp(region.startMs, qint64{0}, durationMs);
        const qint64 end = std::clamp(region.endMs, qint64{0}, durationMs);
        if (end > start) {
            regions.append({start, end});
        }
    }
    std::sort(regions.begin(), regions.end(),
              [](const auto& left, const auto& right) { return left.startMs < right.startMs; });

    QList<qint64> silenceBoundaries;
    if (regions.isEmpty()) {
        for (qint64 boundary = m_configuration.targetChunkMs; boundary < durationMs;
             boundary += m_configuration.targetChunkMs) {
            silenceBoundaries.append(boundary);
        }
    }
    qint64 previousEnd = 0;
    for (const auto& region : regions) {
        if (region.startMs > previousEnd) {
            silenceBoundaries.append(previousEnd + (region.startMs - previousEnd) / 2);
        }
        previousEnd = std::max(previousEnd, region.endMs);
    }
    if (previousEnd < durationMs) {
        silenceBoundaries.append(previousEnd + (durationMs - previousEnd) / 2);
    }

    qint64 cursor = 0;
    int ordinal = 0;
    while (cursor < durationMs) {
        const qint64 remaining = durationMs - cursor;
        if (remaining <= m_configuration.maximumPreferredChunkMs) {
            chunks.append(
                {ordinal, cursor, durationMs, chunks.isEmpty() ? 0 : chunks.constLast().overlapAfterMs, 0});
            break;
        }

        const qint64 preferredMin = cursor + m_configuration.minimumPreferredChunkMs;
        const qint64 preferredMax = std::min(durationMs, cursor + m_configuration.maximumPreferredChunkMs);
        const qint64 target = std::min(durationMs, cursor + m_configuration.targetChunkMs);
        qint64 boundary = -1;
        qint64 bestDistance = std::numeric_limits<qint64>::max();
        for (const qint64 candidate : silenceBoundaries) {
            if (candidate < preferredMin || candidate > preferredMax) {
                continue;
            }
            const qint64 distance = qAbs(candidate - target);
            if (distance < bestDistance) {
                boundary = candidate;
                bestDistance = distance;
            }
        }

        bool hardCut = false;
        if (boundary <= cursor) {
            const qint64 hardMaximum = std::min(durationMs, cursor + m_configuration.hardMaximumChunkMs);
            for (const qint64 candidate : silenceBoundaries) {
                if (candidate < preferredMin || candidate > hardMaximum) {
                    continue;
                }
                const qint64 distance = qAbs(candidate - target);
                if (distance < bestDistance) {
                    boundary = candidate;
                    bestDistance = distance;
                }
            }
            if (boundary <= cursor) {
                boundary = hardMaximum;
                hardCut = boundary < durationMs;
            }
        }

        const qint64 overlap = hardCut ? m_configuration.hardCutOverlapMs : 0;
        chunks.append(
            {ordinal++, cursor, boundary, chunks.isEmpty() ? 0 : chunks.constLast().overlapAfterMs, overlap});
        const qint64 nextCursor = boundary - overlap;
        if (nextCursor <= cursor) {
            break;
        }
        cursor = nextCursor;
    }
    return chunks;
}

} // namespace BreezeDesk::Asr
