#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/transcript/TranscriptSegment.h"

#include <optional>

namespace BreezeDesk {

class ITranscriptRepository {
  public:
    virtual ~ITranscriptRepository() = default;

    [[nodiscard]] virtual Result<QList<TranscriptSegment>>
    segmentsForJob(const QString& jobId, bool includeProvisional = true) const = 0;
    [[nodiscard]] virtual Result<std::optional<TranscriptSegment>>
    segment(const QString& segmentId) const = 0;
    [[nodiscard]] virtual Result<void> replaceRevision(const QString& recordingId, const QString& jobId,
                                                       QList<TranscriptSegment> segments) = 0;
    [[nodiscard]] virtual Result<void> saveEditedRevision(const QString& recordingId, const QString& jobId,
                                                          QList<TranscriptSegment> segments) = 0;
    [[nodiscard]] virtual Result<void> replaceChunk(const QString& recordingId, const QString& jobId,
                                                    const QString& chunkId, QList<TranscriptSegment> segments,
                                                    bool provisional, int attempt) = 0;
    [[nodiscard]] virtual Result<void> saveEditedSegment(const TranscriptSegment& segment) = 0;
    [[nodiscard]] virtual Result<void> deleteSegment(const QString& segmentId) = 0;
};

} // namespace BreezeDesk
