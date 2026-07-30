#include "breezedesk/asr/WorkerSegmentValidator.h"

#include <algorithm>

namespace BreezeDesk::Asr {

WorkerSegmentValidationResult validateWorkerSegmentRange(const QCborMap& payload,
                                                          const qint64 activeChunkStartMs,
                                                          const qint64 activeChunkEndMs,
                                                          const qint64 recordingDurationMs) {
    const QCborValue rawStart = payload.value(QStringLiteral("startMs"));
    const QCborValue rawEnd = payload.value(QStringLiteral("endMs"));
    if (!rawStart.isInteger() || !rawEnd.isInteger()) {
        WorkerSegmentValidationResult result;
        result.error = WorkerSegmentValidationError::MalformedTimestamps;
        return result;
    }

    const qint64 allowedStartMs = std::max<qint64>(0, activeChunkStartMs);
    const qint64 allowedEndMs = std::min(activeChunkEndMs, recordingDurationMs);
    const qint64 rawStartMs = rawStart.toInteger();
    const qint64 rawEndMs = rawEnd.toInteger();
    if (allowedEndMs <= allowedStartMs ||
        rawStartMs < allowedStartMs - WorkerSegmentTimestampToleranceMs ||
        (rawEndMs > allowedEndMs &&
         rawEndMs - allowedEndMs > WorkerSegmentTimestampToleranceMs) ||
        rawEndMs <= rawStartMs) {
        WorkerSegmentValidationResult result;
        result.error = WorkerSegmentValidationError::OutsideActiveRange;
        return result;
    }

    WorkerSegmentValidationResult result;
    result.startMs = std::max(allowedStartMs, rawStartMs);
    result.endMs = std::min(allowedEndMs, rawEndMs);
    if (result.endMs <= result.startMs) {
        result.error = WorkerSegmentValidationError::EmptyRange;
    }
    return result;
}

WorkerSegmentValidationResult reconcileWorkerSegmentRange(WorkerSegmentValidationResult validated,
                                                           const qint64 previousEndMs) {
    if (!validated) {
        return validated;
    }
    validated.startMs = std::max(validated.startMs, previousEndMs);
    if (validated.endMs <= validated.startMs) {
        validated.error = WorkerSegmentValidationError::EmptyRange;
    }
    return validated;
}

} // namespace BreezeDesk::Asr
