#pragma once

namespace BreezeDesk {

enum class CliExitCode {
    Success = 0,
    InvalidArguments = 2,
    SourceMissing = 3,
    ModelUnavailable = 4,
    MediaFailure = 5,
    WorkerFailure = 6,
    TranscriptionFailure = 7,
    DatabaseFailure = 8,
    ExportFailure = 9,
    NetworkFailure = 10,
    Cancelled = 11,
    InternalFailure = 12,
};

} // namespace BreezeDesk
