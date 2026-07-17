#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace BreezeDesk {

enum class ErrorDomain {
    None,
    Application,
    Database,
    Audio,
    Asr,
    Worker,
    Model,
    Network,
    Storage,
    Export,
    Permission,
    Validation,
    Settings,
};

enum class ErrorCode {
    None,
    Unknown,
    InvalidArgument,
    InvalidStateTransition,
    NotFound,
    AlreadyExists,
    OperationCancelled,
    ModelNotInstalled,
    ModelChecksumMismatch,
    ModelLoadFailed,
    UnsupportedModel,
    BackendUnavailable,
    GpuInitializationFailed,
    OutOfMemory,
    AudioDecodeFailed,
    UnsupportedMedia,
    SourceFileMissing,
    InsufficientDiskSpace,
    DatabaseOpenFailed,
    DatabaseQueryFailed,
    DatabaseMigrationFailed,
    DatabaseCorrupt,
    WorkerCrashed,
    WorkerProtocolMismatch,
    WorkerTimeout,
    JobCancelled,
    PermissionDenied,
    ExportFailed,
    NetworkUnavailable,
    DownloadFailed,
    ChecksumMismatch,
    SerializationFailed,
    SettingsReadFailed,
    SettingsWriteFailed,
};

struct UserFacingError {
    ErrorDomain domain = ErrorDomain::Application;
    ErrorCode code = ErrorCode::Unknown;
    QString title;
    QString message;
    QString suggestedAction;
    QString technicalDetails;
    bool retryable = false;
    QMap<QString, QString> context;

    [[nodiscard]] bool isError() const noexcept { return code != ErrorCode::None; }
    [[nodiscard]] QString diagnosticString() const;

    static UserFacingError validation(ErrorCode code, const QString& message,
                                      const QString& technicalDetails = {});
    static UserFacingError database(ErrorCode code, const QString& message,
                                    const QString& technicalDetails = {}, bool retryable = false);
};

[[nodiscard]] QString errorDomainName(ErrorDomain domain);
[[nodiscard]] QString errorCodeName(ErrorCode code);

} // namespace BreezeDesk
