#include <breezedesk/asr/LongFormTranscriber.h>

#include <breezedesk/asr/OverlapDeduplicator.h>

namespace BreezeDesk::Asr {

QList<TranscriptSegment>
LongFormTranscriber::mergeCompletedChunks(const QList<QList<TranscriptSegment>>& chunkSegments,
                                          const QList<bool>& chunksHaveTimestampOverlap,
                                          QStringList* diagnostics) {
    QList<TranscriptSegment> merged;
    for (qsizetype chunkIndex = 0; chunkIndex < chunkSegments.size(); ++chunkIndex) {
        QList<TranscriptSegment> segments = chunkSegments.at(chunkIndex);
        const bool hasOverlap = chunkIndex > 0 && chunkIndex - 1 < chunksHaveTimestampOverlap.size() &&
                                chunksHaveTimestampOverlap.at(chunkIndex - 1);
        if (hasOverlap && !merged.isEmpty() && !segments.isEmpty()) {
            auto deduplicated = OverlapDeduplicator::deduplicate(merged.constLast().originalText,
                                                                 segments.first().originalText, true);
            if (!deduplicated.diagnostic.isEmpty() && diagnostics != nullptr) {
                diagnostics->append(deduplicated.diagnostic);
            }
            if (deduplicated.matchedUnits > 0) {
                segments.first().originalText = deduplicated.text;
                if (segments.first().originalText.isEmpty()) {
                    segments.removeFirst();
                }
            }
        }
        merged.append(segments);
    }
    return merged;
}

} // namespace BreezeDesk::Asr
