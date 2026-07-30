#pragma once

#include <QCborMap>
#include <QtGlobal>

namespace BreezeDesk::Asr {

inline constexpr qint64 WorkerSegmentTimestampToleranceMs = 1'000;

enum class WorkerSegmentValidationError {
    None,
    MalformedTimestamps,
    OutsideActiveRange,
    EmptyRange,
};

struct WorkerSegmentValidationResult {
    qint64 startMs = 0;
    qint64 endMs = 0;
    WorkerSegmentValidationError error = WorkerSegmentValidationError::None;

    [[nodiscard]] bool isValid() const noexcept {
        return error == WorkerSegmentValidationError::None;
    }
    [[nodiscard]] explicit operator bool() const noexcept { return isValid(); }
};

[[nodiscard]] WorkerSegmentValidationResult
validateWorkerSegmentRange(const QCborMap& payload, qint64 activeChunkStartMs,
                           qint64 activeChunkEndMs, qint64 recordingDurationMs);

[[nodiscard]] WorkerSegmentValidationResult
reconcileWorkerSegmentRange(WorkerSegmentValidationResult validated, qint64 previousEndMs);

} // namespace BreezeDesk::Asr
