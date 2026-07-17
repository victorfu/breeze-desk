#include "breezedesk/core/Error.h"

#include <QStringBuilder>

namespace BreezeDesk {

QString errorDomainName(const ErrorDomain domain) {
    switch (domain) {
    case ErrorDomain::None:
        return QStringLiteral("None");
    case ErrorDomain::Application:
        return QStringLiteral("Application");
    case ErrorDomain::Database:
        return QStringLiteral("Database");
    case ErrorDomain::Audio:
        return QStringLiteral("Audio");
    case ErrorDomain::Asr:
        return QStringLiteral("ASR");
    case ErrorDomain::Worker:
        return QStringLiteral("Worker");
    case ErrorDomain::Model:
        return QStringLiteral("Model");
    case ErrorDomain::Network:
        return QStringLiteral("Network");
    case ErrorDomain::Storage:
        return QStringLiteral("Storage");
    case ErrorDomain::Export:
        return QStringLiteral("Export");
    case ErrorDomain::Permission:
        return QStringLiteral("Permission");
    case ErrorDomain::Validation:
        return QStringLiteral("Validation");
    case ErrorDomain::Settings:
        return QStringLiteral("Settings");
    }
    return QStringLiteral("Unknown");
}

QString errorCodeName(const ErrorCode code) {
    switch (code) {
    case ErrorCode::None:
        return QStringLiteral("None");
    case ErrorCode::Unknown:
        return QStringLiteral("Unknown");
    case ErrorCode::InvalidArgument:
        return QStringLiteral("InvalidArgument");
    case ErrorCode::InvalidStateTransition:
        return QStringLiteral("InvalidStateTransition");
    case ErrorCode::NotFound:
        return QStringLiteral("NotFound");
    case ErrorCode::AlreadyExists:
        return QStringLiteral("AlreadyExists");
    case ErrorCode::OperationCancelled:
        return QStringLiteral("OperationCancelled");
    case ErrorCode::ModelNotInstalled:
        return QStringLiteral("ModelNotInstalled");
    case ErrorCode::ModelChecksumMismatch:
        return QStringLiteral("ModelChecksumMismatch");
    case ErrorCode::ModelLoadFailed:
        return QStringLiteral("ModelLoadFailed");
    case ErrorCode::UnsupportedModel:
        return QStringLiteral("UnsupportedModel");
    case ErrorCode::BackendUnavailable:
        return QStringLiteral("BackendUnavailable");
    case ErrorCode::GpuInitializationFailed:
        return QStringLiteral("GpuInitializationFailed");
    case ErrorCode::OutOfMemory:
        return QStringLiteral("OutOfMemory");
    case ErrorCode::AudioDecodeFailed:
        return QStringLiteral("AudioDecodeFailed");
    case ErrorCode::UnsupportedMedia:
        return QStringLiteral("UnsupportedMedia");
    case ErrorCode::SourceFileMissing:
        return QStringLiteral("SourceFileMissing");
    case ErrorCode::InsufficientDiskSpace:
        return QStringLiteral("InsufficientDiskSpace");
    case ErrorCode::DatabaseOpenFailed:
        return QStringLiteral("DatabaseOpenFailed");
    case ErrorCode::DatabaseQueryFailed:
        return QStringLiteral("DatabaseQueryFailed");
    case ErrorCode::DatabaseMigrationFailed:
        return QStringLiteral("DatabaseMigrationFailed");
    case ErrorCode::DatabaseCorrupt:
        return QStringLiteral("DatabaseCorrupt");
    case ErrorCode::WorkerCrashed:
        return QStringLiteral("WorkerCrashed");
    case ErrorCode::WorkerProtocolMismatch:
        return QStringLiteral("WorkerProtocolMismatch");
    case ErrorCode::WorkerTimeout:
        return QStringLiteral("WorkerTimeout");
    case ErrorCode::JobCancelled:
        return QStringLiteral("JobCancelled");
    case ErrorCode::PermissionDenied:
        return QStringLiteral("PermissionDenied");
    case ErrorCode::ExportFailed:
        return QStringLiteral("ExportFailed");
    case ErrorCode::NetworkUnavailable:
        return QStringLiteral("NetworkUnavailable");
    case ErrorCode::DownloadFailed:
        return QStringLiteral("DownloadFailed");
    case ErrorCode::ChecksumMismatch:
        return QStringLiteral("ChecksumMismatch");
    case ErrorCode::SerializationFailed:
        return QStringLiteral("SerializationFailed");
    case ErrorCode::SettingsReadFailed:
        return QStringLiteral("SettingsReadFailed");
    case ErrorCode::SettingsWriteFailed:
        return QStringLiteral("SettingsWriteFailed");
    }
    return QStringLiteral("Unknown");
}

QString UserFacingError::diagnosticString() const {
    QString result = errorDomainName(domain) % QLatin1Char('/') % errorCodeName(code);
    if (!message.isEmpty()) {
        result += QStringLiteral(": ") % message;
    }
    if (!technicalDetails.isEmpty()) {
        result += QStringLiteral(" [") % technicalDetails % QLatin1Char(']');
    }
    return result;
}

UserFacingError UserFacingError::validation(const ErrorCode code, const QString& message,
                                            const QString& technicalDetails) {
    return {ErrorDomain::Validation,
            code,
            QStringLiteral("Invalid data"),
            message,
            QStringLiteral("Review the highlighted values and try again."),
            technicalDetails,
            false,
            {}};
}

UserFacingError UserFacingError::database(const ErrorCode code, const QString& message,
                                          const QString& technicalDetails, const bool retryable) {
    return {ErrorDomain::Database,
            code,
            QStringLiteral("Database error"),
            message,
            retryable ? QStringLiteral("Try the operation again.")
                      : QStringLiteral("Open Diagnostics for technical details."),
            technicalDetails,
            retryable,
            {}};
}

} // namespace BreezeDesk
