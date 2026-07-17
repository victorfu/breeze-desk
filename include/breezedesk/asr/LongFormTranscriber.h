#pragma once

#include <breezedesk/asr/AsrTypes.h>

#include <QtCore/QList>

namespace BreezeDesk::Asr {

class LongFormTranscriber final {
  public:
    [[nodiscard]] static QList<TranscriptSegment>
    mergeCompletedChunks(const QList<QList<TranscriptSegment>>& chunkSegments,
                         const QList<bool>& chunksHaveTimestampOverlap, QStringList* diagnostics = nullptr);
};

} // namespace BreezeDesk::Asr
