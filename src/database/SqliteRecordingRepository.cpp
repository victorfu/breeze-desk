#include "breezedesk/database/SqliteRecordingRepository.h"

#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/core/TimeUtils.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/DatabaseSearchService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <utility>
#include <string>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#elif defined(Q_OS_UNIX)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace BreezeDesk {
namespace {

#if defined(Q_OS_WIN)
constexpr Qt::CaseSensitivity PathCaseSensitivity = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity PathCaseSensitivity = Qt::CaseSensitive;
#endif

constexpr auto ManagedMediaArtifact = "managed_media";
constexpr auto NormalizedPcmArtifact = "normalized_pcm";
constexpr auto WaveformArtifact = "waveform";
// Linux accepts up to 40 followed symbolic links and Windows permits up to 63 reparse-point
// traversals. Keep comparison resolution above both platform ceilings while retaining a guard
// against corrupt Qt shortcut chains.
constexpr int MaxSymlinkResolutionDepth = 64;

struct PendingArtifactDeletion {
    QString id;
    QString recordingId;
    QString artifactKind;
    QString absolutePath;
    int attemptCount{0};
};

UserFacingError queryError(const QString& message, const QSqlQuery& query) {
    return UserFacingError::database(ErrorCode::DatabaseQueryFailed, message, query.lastError().text(), true);
}

Recording readRecording(QSqlQuery& query) {
    Recording value;
    value.id = query.value(QStringLiteral("id")).toString();
    value.title = query.value(QStringLiteral("title")).toString();
    value.sourcePath = query.value(QStringLiteral("source_path")).toString();
    value.managedMediaPath = query.value(QStringLiteral("managed_media_path")).toString();
    value.normalizedPcmPath = query.value(QStringLiteral("normalized_pcm_path")).toString();
    value.sourceHash = query.value(QStringLiteral("source_hash")).toString();
    value.mediaType = query.value(QStringLiteral("media_type")).toString();
    value.durationMs = query.value(QStringLiteral("duration_ms")).toLongLong();
    value.sampleRate = query.value(QStringLiteral("sample_rate")).toInt();
    value.channelCount = query.value(QStringLiteral("channel_count")).toInt();
    value.waveformPath = query.value(QStringLiteral("waveform_path")).toString();
    value.createdAt = TimeUtils::fromStorageString(query.value(QStringLiteral("created_at")).toString());
    value.updatedAt = TimeUtils::fromStorageString(query.value(QStringLiteral("updated_at")).toString());
    value.deletedAt = TimeUtils::fromStorageString(query.value(QStringLiteral("deleted_at")).toString());
    value.notes = query.value(QStringLiteral("notes")).toString();
    value.reviewState = query.value(QStringLiteral("review_state")).toString();
    value.activeJobId = query.value(QStringLiteral("active_job_id")).toString();
    value.latestJobState = query.value(QStringLiteral("latest_job_state")).toString();
    value.latestJobModelId = query.value(QStringLiteral("latest_job_model_id")).toString();
    value.latestJobProgress = query.value(QStringLiteral("latest_job_progress")).toDouble();
    return value;
}

QString recordingSelection(const QString& alias) {
    return QStringLiteral("%1.*,"
                          "COALESCE((SELECT j.state FROM transcription_jobs j WHERE j.recording_id=%1.id "
                          "ORDER BY j.created_at DESC,j.id DESC LIMIT 1),'') AS latest_job_state,"
                          "COALESCE((SELECT j.model_id FROM transcription_jobs j WHERE j.recording_id=%1.id "
                          "ORDER BY j.created_at DESC,j.id DESC LIMIT 1),'') AS latest_job_model_id,"
                          "COALESCE((SELECT j.progress FROM transcription_jobs j WHERE j.recording_id=%1.id "
                          "ORDER BY j.created_at DESC,j.id DESC LIMIT 1),0) AS latest_job_progress")
        .arg(alias);
}

Result<QStringList> loadTags(QSqlDatabase& database, const QString& recordingId) {
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT t.name FROM tags t JOIN recording_tags rt ON rt.tag_id=t.id "
                                 "WHERE rt.recording_id=? ORDER BY t.name COLLATE NOCASE"));
    query.addBindValue(recordingId);
    if (!query.exec())
        return Result<QStringList>::failure(queryError(QStringLiteral("Tags could not be loaded."), query));
    QStringList tags;
    while (query.next())
        tags.append(query.value(0).toString());
    return Result<QStringList>::success(tags);
}

Result<QHash<QString, QStringList>> loadTags(QSqlDatabase& database, const QStringList& recordingIds) {
    QHash<QString, QStringList> tags;
    if (recordingIds.isEmpty()) {
        return Result<QHash<QString, QStringList>>::success(tags);
    }
    constexpr int TagBatchSize = 500;
    const int idCount = static_cast<int>(recordingIds.size());
    for (int offset = 0; offset < idCount; offset += TagBatchSize) {
        const int batchSize = qMin(TagBatchSize, idCount - offset);
        QStringList placeholders;
        placeholders.fill(QStringLiteral("?"), batchSize);
        QSqlQuery query(database);
        query.prepare(QStringLiteral("SELECT rt.recording_id,t.name FROM recording_tags rt "
                                     "JOIN tags t ON t.id=rt.tag_id WHERE rt.recording_id IN (%1) "
                                     "ORDER BY rt.recording_id,t.name COLLATE NOCASE")
                          .arg(placeholders.join(QLatin1Char(','))));
        for (int index = 0; index < batchSize; ++index) {
            query.addBindValue(recordingIds.at(offset + index));
        }
        if (!query.exec()) {
            return Result<QHash<QString, QStringList>>::failure(
                queryError(QStringLiteral("Tags could not be loaded."), query));
        }
        while (query.next()) {
            tags[query.value(0).toString()].append(query.value(1).toString());
        }
    }
    return Result<QHash<QString, QStringList>>::success(tags);
}

QString safeSortColumn(const QString& requested) {
    static const QSet<QString> columns = {QStringLiteral("title"), QStringLiteral("created_at"),
                                          QStringLiteral("updated_at"), QStringLiteral("duration_ms"),
                                          QStringLiteral("review_state")};
    return columns.contains(requested) ? requested : QStringLiteral("updated_at");
}

QString nonNull(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value;
}

QString normalizedAbsolutePath(const QString& path) {
    const QFileInfo info(path);
    if (path.trimmed().isEmpty() || !info.isAbsolute()) {
        return {};
    }
    return QDir::fromNativeSeparators(QDir::cleanPath(info.absoluteFilePath()));
}

bool hasUnsafeWindowsPathSyntax(const QString& path) {
#if defined(Q_OS_WIN)
    const QString normalized = QDir::fromNativeSeparators(path.trimmed());
    if (normalized.startsWith(QLatin1String("//?/"), Qt::CaseInsensitive) ||
        normalized.startsWith(QLatin1String("//./"), Qt::CaseInsensitive)) {
        return true;
    }
    const qsizetype firstColon = normalized.indexOf(QLatin1Char(':'));
    if (normalized.startsWith(QLatin1String("//"))) {
        return firstColon >= 0;
    }
    return firstColon != 1 || normalized.indexOf(QLatin1Char(':'), 2) >= 0;
#else
    Q_UNUSED(path);
    return false;
#endif
}

bool pathEquals(const QString& left, const QString& right) {
    return left.compare(right, PathCaseSensitivity) == 0;
}

QString resolvedComparisonPath(const QString& path, const int symlinkDepth = 0) {
    const QString normalizedPath = normalizedAbsolutePath(path);
    if (normalizedPath.isEmpty() || symlinkDepth > MaxSymlinkResolutionDepth) {
        return {};
    }

    const QFileInfo original(normalizedPath);
    if (original.isSymLink()) {
        const QString target = normalizedAbsolutePath(original.symLinkTarget());
        if (target.isEmpty() || pathEquals(target, normalizedPath)) {
            return {};
        }
        return resolvedComparisonPath(target, symlinkDepth + 1);
    }

    QString current = normalizedPath;
    QStringList unresolvedComponents;
    for (int depth = 0; depth < 256; ++depth) {
        const QString canonical =
            QDir::fromNativeSeparators(QFileInfo(current).canonicalFilePath());
        if (!canonical.isEmpty()) {
            QString resolved = canonical;
            for (const QString& component : std::as_const(unresolvedComponents)) {
                resolved = QDir(resolved).filePath(component);
            }
            return QDir::fromNativeSeparators(QDir::cleanPath(resolved));
        }

        const QFileInfo currentInfo(current);
        if (currentInfo.isSymLink()) {
            const QString target = resolvedComparisonPath(currentInfo.symLinkTarget(), symlinkDepth + 1);
            if (target.isEmpty()) {
                return {};
            }
            QString resolved = target;
            for (const QString& component : std::as_const(unresolvedComponents)) {
                resolved = QDir(resolved).filePath(component);
            }
            return QDir::fromNativeSeparators(QDir::cleanPath(resolved));
        }
        const QString component = currentInfo.fileName();
        const QString parent =
            QDir::fromNativeSeparators(QDir::cleanPath(currentInfo.absolutePath()));
        if (component.isEmpty() || parent.isEmpty() || pathEquals(parent, current)) {
            break;
        }
        unresolvedComponents.prepend(component);
        current = parent;
    }
    return {};
}

bool isStrictChildPath(const QString& path, const QString& directory) {
    const QString normalizedPath = normalizedAbsolutePath(path);
    const QString normalizedDirectory = normalizedAbsolutePath(directory);
    if (normalizedPath.isEmpty() || normalizedDirectory.isEmpty() ||
        pathEquals(normalizedPath, normalizedDirectory)) {
        return false;
    }
    const QString prefix = normalizedDirectory.endsWith(QLatin1Char('/'))
                               ? normalizedDirectory
                               : normalizedDirectory + QLatin1Char('/');
    return normalizedPath.startsWith(prefix, PathCaseSensitivity);
}

bool pathsEquivalent(const QString& left, const QString& right) {
    const QString normalizedLeft = normalizedAbsolutePath(left);
    const QString normalizedRight = normalizedAbsolutePath(right);
    if (normalizedLeft.isEmpty() || normalizedRight.isEmpty()) {
        return false;
    }
    if (pathEquals(normalizedLeft, normalizedRight)) {
        return true;
    }
    const QString resolvedLeft = resolvedComparisonPath(normalizedLeft);
    const QString resolvedRight = resolvedComparisonPath(normalizedRight);
    return !resolvedLeft.isEmpty() && !resolvedRight.isEmpty() &&
           pathEquals(resolvedLeft, resolvedRight);
}

bool pathsAreProvablyDistinct(const QString& left, const QString& right) {
    const QString normalizedLeft = normalizedAbsolutePath(left);
    if (normalizedLeft.isEmpty()) {
        return false;
    }
    if (right.trimmed().isEmpty()) {
        return true;
    }
    const QString normalizedRight = normalizedAbsolutePath(right);
    if (normalizedRight.isEmpty() || pathEquals(normalizedLeft, normalizedRight)) {
        return false;
    }
    const QString resolvedLeft = resolvedComparisonPath(normalizedLeft);
    const QString resolvedRight = resolvedComparisonPath(normalizedRight);
    return !resolvedLeft.isEmpty() && !resolvedRight.isEmpty() &&
           !pathEquals(resolvedLeft, resolvedRight);
}

QString artifactRoot(const QString& artifactKind) {
    if (artifactKind == QLatin1String(ManagedMediaArtifact)) {
        return StoragePaths::recordings();
    }
    if (artifactKind == QLatin1String(NormalizedPcmArtifact) ||
        artifactKind == QLatin1String(WaveformArtifact)) {
        return StoragePaths::cache();
    }
    return {};
}

bool isSafeArtifactPath(const QString& path, const QString& artifactKind) {
    if (hasUnsafeWindowsPathSyntax(path)) {
        return false;
    }
    const QString root = artifactRoot(artifactKind);
    const QString absolutePath = normalizedAbsolutePath(path);
    if (root.isEmpty() || !isStrictChildPath(absolutePath, root)) {
        return false;
    }

    const QFileInfo fileInfo(absolutePath);
    const QString canonicalRoot =
        QDir::fromNativeSeparators(QFileInfo(root).canonicalFilePath());
    const QString canonicalParent =
        QDir::fromNativeSeparators(QFileInfo(fileInfo.absolutePath()).canonicalFilePath());
    if (canonicalRoot.isEmpty() || canonicalParent.isEmpty() ||
        (!pathEquals(canonicalParent, canonicalRoot) &&
         !isStrictChildPath(canonicalParent, canonicalRoot))) {
        return false;
    }
#if defined(Q_OS_UNIX)
    const QString relativeParent =
        QDir::fromNativeSeparators(QDir(root).relativeFilePath(fileInfo.absolutePath()));
    if (relativeParent == QLatin1String("..") ||
        relativeParent.startsWith(QLatin1String("../"))) {
        return false;
    }
    QString currentParent = root;
    const QStringList parentComponents = relativeParent.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& component : parentComponents) {
        if (component == QLatin1String(".")) {
            continue;
        }
        currentParent = QDir(currentParent).filePath(component);
        if (QFileInfo(currentParent).isSymLink()) {
            return false;
        }
    }
#endif
    if (fileInfo.isSymLink()) {
        // Only file links are valid artifacts. Directory links are never media/cache files and
        // would require directory-removal semantics rather than file deletion.
        return !fileInfo.isDir();
    }
    if (fileInfo.isDir()) {
        return false;
    }
    if (!fileInfo.exists()) {
        return true;
    }

    const QString canonicalPath = QDir::fromNativeSeparators(fileInfo.canonicalFilePath());
    return !canonicalPath.isEmpty() && isStrictChildPath(canonicalPath, canonicalRoot);
}

#if defined(Q_OS_WIN)
QString withoutExtendedWindowsPrefix(const QString& path) {
    if (path.startsWith(QStringLiteral("\\\\?\\UNC\\"), Qt::CaseInsensitive)) {
        return QStringLiteral("\\\\") + path.mid(8);
    }
    if (path.startsWith(QStringLiteral("\\\\?\\"), Qt::CaseInsensitive)) {
        return path.mid(4);
    }
    return path;
}

bool removeArtifactByHandle(const QString& path, const QString& artifactKind, QString* error) {
    const QString canonicalRoot = QDir::fromNativeSeparators(
        QFileInfo(artifactRoot(artifactKind)).canonicalFilePath());
    if (canonicalRoot.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("The managed file root could not be resolved.");
        }
        return false;
    }
    const QString nativePath = QDir::toNativeSeparators(normalizedAbsolutePath(path));
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(nativePath.utf16()),
                                DELETE | FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        if (error != nullptr) {
            *error = QStringLiteral("Windows could not open the managed file for deletion (error %1).")
                         .arg(static_cast<qulonglong>(code));
        }
        return false;
    }

    const auto fail = [&](const QString& message) {
        if (error != nullptr) {
            *error = message;
        }
        CloseHandle(handle);
        return false;
    };

    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle, &information)) {
        return fail(QStringLiteral("Windows could not inspect the managed file handle (error %1).")
                        .arg(static_cast<qulonglong>(GetLastError())));
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return fail(QStringLiteral("A managed file path unexpectedly resolved to a directory."));
    }

    constexpr DWORD FinalPathFlags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, FinalPathFlags);
    if (required == 0) {
        return fail(QStringLiteral("Windows could not resolve the managed file handle (error %1).")
                        .arg(static_cast<qulonglong>(GetLastError())));
    }
    std::wstring finalPathBuffer(static_cast<size_t>(required) + 1, L'\0');
    const DWORD written = GetFinalPathNameByHandleW(
        handle, finalPathBuffer.data(), static_cast<DWORD>(finalPathBuffer.size()), FinalPathFlags);
    if (written == 0 || static_cast<size_t>(written) >= finalPathBuffer.size()) {
        return fail(QStringLiteral("Windows could not resolve the managed file handle (error %1).")
                        .arg(static_cast<qulonglong>(GetLastError())));
    }
    const QString finalPath = QDir::fromNativeSeparators(withoutExtendedWindowsPrefix(
        QString::fromWCharArray(finalPathBuffer.data(), static_cast<qsizetype>(written))));
    if (!isStrictChildPath(finalPath, canonicalRoot)) {
        return fail(QStringLiteral("The managed file handle resolved outside application storage."));
    }

    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    if (!SetFileInformationByHandle(handle, FileDispositionInfo, &disposition,
                                    sizeof(disposition))) {
        return fail(QStringLiteral("Windows could not mark the managed file for deletion (error %1).")
                        .arg(static_cast<qulonglong>(GetLastError())));
    }
    CloseHandle(handle);
    return true;
}
#endif

#if defined(Q_OS_UNIX)
int openPinnedAbsoluteDirectory(const QString& directoryPath, int* errorCode) {
    int descriptor = ::open("/", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (descriptor < 0) {
        if (errorCode != nullptr) {
            *errorCode = errno;
        }
        return -1;
    }

    const QStringList components =
        QDir::fromNativeSeparators(directoryPath).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& componentText : components) {
        if (componentText == QLatin1String(".") || componentText == QLatin1String("..")) {
            ::close(descriptor);
            if (errorCode != nullptr) {
                *errorCode = EINVAL;
            }
            return -1;
        }
        const QByteArray component = QFile::encodeName(componentText);
        const int child = ::openat(descriptor, component.constData(),
                                   O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
        if (child < 0) {
            const int code = errno;
            ::close(descriptor);
            if (errorCode != nullptr) {
                *errorCode = code;
            }
            return -1;
        }
        ::close(descriptor);
        descriptor = child;
    }
    return descriptor;
}

bool removeArtifactAtPinnedRoot(const QString& path, const QString& artifactKind, QString* error) {
    const QString root = artifactRoot(artifactKind);
    const QString canonicalRoot = QDir::fromNativeSeparators(QFileInfo(root).canonicalFilePath());
    const QString absolutePath = normalizedAbsolutePath(path);
    const QString relativePath =
        QDir::fromNativeSeparators(QDir(root).relativeFilePath(absolutePath));
    const QStringList components = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (root.isEmpty() || canonicalRoot.isEmpty() || absolutePath.isEmpty() || components.isEmpty() ||
        relativePath == QLatin1String("..") || relativePath.startsWith(QLatin1String("../")) ||
        components.contains(QStringLiteral(".."))) {
        if (error != nullptr) {
            *error = QStringLiteral("The managed file path could not be resolved relative to its root.");
        }
        return false;
    }

    const auto setErrnoError = [&](const QString& action, const int code) {
        if (error != nullptr) {
            *error = QStringLiteral("%1: %2").arg(action, QString::fromLocal8Bit(std::strerror(code)));
        }
    };
    int rootOpenError = 0;
    int parentDescriptor = openPinnedAbsoluteDirectory(canonicalRoot, &rootOpenError);
    if (parentDescriptor < 0) {
        if (rootOpenError == ENOENT) {
            return true;
        }
        setErrnoError(QStringLiteral("The managed file root could not be opened"), rootOpenError);
        return false;
    }

    for (qsizetype index = 0; index + 1 < components.size(); ++index) {
        const QByteArray component = QFile::encodeName(components.at(index));
        const int childDescriptor =
            ::openat(parentDescriptor, component.constData(),
                     O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
        if (childDescriptor < 0) {
            const int code = errno;
            ::close(parentDescriptor);
            if (code == ENOENT) {
                return true;
            }
            setErrnoError(QStringLiteral("A managed file parent could not be opened safely"), code);
            return false;
        }
        ::close(parentDescriptor);
        parentDescriptor = childDescriptor;
    }

    const QByteArray leaf = QFile::encodeName(components.constLast());
    struct stat entryInformation {};
    if (::fstatat(parentDescriptor, leaf.constData(), &entryInformation,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        const int code = errno;
        ::close(parentDescriptor);
        if (code == ENOENT) {
            return true;
        }
        setErrnoError(QStringLiteral("The managed file entry could not be inspected"), code);
        return false;
    }
    if (S_ISDIR(entryInformation.st_mode)) {
        ::close(parentDescriptor);
        if (error != nullptr) {
            *error = QStringLiteral("A managed file path unexpectedly resolved to a directory.");
        }
        return false;
    }
    if (::unlinkat(parentDescriptor, leaf.constData(), 0) != 0) {
        const int code = errno;
        ::close(parentDescriptor);
        if (code == ENOENT) {
            return true;
        }
        setErrnoError(QStringLiteral("The managed file could not be unlinked safely"), code);
        return false;
    }
    ::close(parentDescriptor);
    return true;
}
#endif

bool removeArtifactFile(const QString& path, const QString& artifactKind, QString* error) {
#if defined(Q_OS_WIN)
    return removeArtifactByHandle(path, artifactKind, error);
#elif defined(Q_OS_UNIX)
    return removeArtifactAtPinnedRoot(path, artifactKind, error);
#else
    QFile file(path);
    const bool removed = file.remove();
    if (!removed && error != nullptr) {
        *error = file.errorString();
    }
    return removed;
#endif
}

bool resolvedPathIsWithin(const QString& path, const QString& root) {
    const QString resolvedPath = resolvedComparisonPath(path);
    const QString canonicalRoot =
        QDir::fromNativeSeparators(QFileInfo(root).canonicalFilePath());
    return !resolvedPath.isEmpty() && !canonicalRoot.isEmpty() &&
           isStrictChildPath(resolvedPath, canonicalRoot);
}

bool isWithinManagedDeletionRoot(const QString& path) {
    return isStrictChildPath(path, StoragePaths::recordings()) ||
           isStrictChildPath(path, StoragePaths::cache()) ||
           resolvedPathIsWithin(path, StoragePaths::recordings()) ||
           resolvedPathIsWithin(path, StoragePaths::cache());
}

bool isSafeManagedReference(const QString& path) {
    if (isStrictChildPath(path, StoragePaths::recordings()) ||
        isStrictChildPath(path, StoragePaths::cache())) {
        return isSafeArtifactPath(path, QString::fromLatin1(ManagedMediaArtifact)) ||
               isSafeArtifactPath(path, QString::fromLatin1(NormalizedPcmArtifact));
    }
    return resolvedPathIsWithin(path, StoragePaths::recordings()) ||
           resolvedPathIsWithin(path, StoragePaths::cache());
}

Result<void> ensureNewManagedReferencesExist(
    QSqlDatabase& database, const QString& existingRecordingId,
    const QList<QPair<int, QString>>& requestedReferences) {
    QStringList existingReferences(4);
    if (!existingRecordingId.isEmpty()) {
        QSqlQuery existing(database);
        existing.prepare(QStringLiteral(
            "SELECT source_path,managed_media_path,normalized_pcm_path,waveform_path "
            "FROM recordings WHERE id=?"));
        existing.addBindValue(existingRecordingId);
        if (!existing.exec()) {
            return Result<void>::failure(queryError(
                QStringLiteral("The recording file references could not be checked."), existing));
        }
        if (existing.next()) {
            for (int column = 0; column < existingReferences.size(); ++column) {
                existingReferences[column] = existing.value(column).toString();
            }
        }
    }

    for (const auto& [column, requestedPath] : requestedReferences) {
        if (requestedPath.trimmed().isEmpty() || column < 0 || column >= existingReferences.size() ||
            pathsEquivalent(requestedPath, existingReferences.at(column)) ||
            !isWithinManagedDeletionRoot(requestedPath)) {
            continue;
        }
        if (!isSafeManagedReference(requestedPath) || !QFileInfo(requestedPath).isFile()) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::NotFound,
                QStringLiteral("An application-managed recording file no longer exists. Try again.")));
        }
    }
    return Result<void>::success();
}

Result<void> enqueueArtifactDeletions(QSqlDatabase& database, const Recording& recording) {
    struct Candidate {
        QString kind;
        QString path;
    };
    QList<Candidate> candidates;
    candidates.append({QString::fromLatin1(ManagedMediaArtifact), recording.managedMediaPath});
    if (pathsAreProvablyDistinct(recording.normalizedPcmPath, recording.sourcePath)) {
        candidates.append({QString::fromLatin1(NormalizedPcmArtifact), recording.normalizedPcmPath});
    }
    if (pathsAreProvablyDistinct(recording.waveformPath, recording.sourcePath)) {
        candidates.append({QString::fromLatin1(WaveformArtifact), recording.waveformPath});
    }

    QSet<QString> queuedPaths;
    const QString createdAt = TimeUtils::nowStorageString();
    for (const Candidate& candidate : std::as_const(candidates)) {
        const QString absolutePath = normalizedAbsolutePath(candidate.path);
        QString deduplicationKey = absolutePath;
#if defined(Q_OS_WIN)
        deduplicationKey = deduplicationKey.toCaseFolded();
#endif
        if (absolutePath.isEmpty() || queuedPaths.contains(deduplicationKey) ||
            !isSafeArtifactPath(absolutePath, candidate.kind) ||
            (!QFileInfo(absolutePath).exists() && !QFileInfo(absolutePath).isSymLink())) {
            continue;
        }
        queuedPaths.insert(deduplicationKey);
        QSqlQuery insert(database);
        insert.prepare(QStringLiteral(
            "INSERT INTO pending_recording_artifact_deletions("
            "id,recording_id,artifact_kind,absolute_path,created_at,next_attempt_at) "
            "VALUES(?,?,?,?,?,?)"));
        insert.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
        insert.addBindValue(recording.id);
        insert.addBindValue(candidate.kind);
        insert.addBindValue(absolutePath);
        insert.addBindValue(createdAt);
        insert.addBindValue(createdAt);
        if (!insert.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("Managed file cleanup could not be scheduled."), insert));
        }
    }
    return Result<void>::success();
}

Result<bool> pathIsReferenced(QSqlDatabase& database, const QString& path) {
    QSqlQuery references(database);
    if (!references.exec(QStringLiteral(
            "SELECT source_path,managed_media_path,normalized_pcm_path,waveform_path FROM recordings"))) {
        return Result<bool>::failure(
            queryError(QStringLiteral("Managed file references could not be checked."), references));
    }
    while (references.next()) {
        for (int column = 0; column < 4; ++column) {
            if (pathsEquivalent(path, references.value(column).toString())) {
                return Result<bool>::success(true);
            }
        }
    }
    return Result<bool>::success(false);
}

Result<void> ensureNoActiveExecutionLease(QSqlDatabase& database, const QString& recordingId) {
    QSqlQuery lease(database);
    lease.prepare(QStringLiteral(
        "SELECT l.expires_at FROM asr_execution_lease l "
        "JOIN transcription_jobs j ON j.id=l.job_id "
        "WHERE l.resource='asr' AND j.recording_id=? LIMIT 1"));
    lease.addBindValue(recordingId);
    if (!lease.exec()) {
        return Result<void>::failure(
            queryError(QStringLiteral("The recording execution lease could not be checked."), lease));
    }
    if (lease.next()) {
        const QDateTime expiresAt = TimeUtils::fromStorageString(lease.value(0).toString());
        if (!expiresAt.isValid() || expiresAt > QDateTime::currentDateTimeUtc()) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidStateTransition,
                QStringLiteral("The recording cannot be replaced while it is being transcribed.")));
        }
    }
    return Result<void>::success();
}

Result<void> ensureJobBelongsToRecording(QSqlDatabase& database, const QString& jobId,
                                         const QString& recordingId) {
    QSqlQuery job(database);
    job.prepare(QStringLiteral("SELECT 1 FROM transcription_jobs WHERE id=? AND recording_id=?"));
    job.addBindValue(jobId);
    job.addBindValue(recordingId);
    if (!job.exec()) {
        return Result<void>::failure(
            queryError(QStringLiteral("The transcription job could not be validated."), job));
    }
    if (!job.next()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument,
            QStringLiteral("The transcription job does not belong to this recording.")));
    }
    return Result<void>::success();
}

QStringList normalizedTags(const QStringList& tags) {
    QStringList unique;
    QSet<QString> seen;
    for (const QString& tag : tags) {
        const QString clean = tag.trimmed();
        const QString key = clean.toCaseFolded();
        if (!clean.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            unique.append(clean);
        }
    }
    return unique;
}

Result<void> replaceTags(QSqlDatabase& database, const QString& recordingId,
                         const QStringList& tags) {
    QSqlQuery remove(database);
    remove.prepare(QStringLiteral("DELETE FROM recording_tags WHERE recording_id=?"));
    remove.addBindValue(recordingId);
    if (!remove.exec()) {
        return Result<void>::failure(
            queryError(QStringLiteral("Existing tags could not be replaced."), remove));
    }
    for (const QString& tag : tags) {
        QSqlQuery insertTag(database);
        const QString tagId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        insertTag.prepare(QStringLiteral("INSERT OR IGNORE INTO tags(id,name,created_at) VALUES(?,?,?)"));
        insertTag.addBindValue(tagId);
        insertTag.addBindValue(tag);
        insertTag.addBindValue(TimeUtils::nowStorageString());
        if (!insertTag.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("A tag could not be created."), insertTag));
        }
        QSqlQuery attach(database);
        attach.prepare(QStringLiteral("INSERT INTO recording_tags(recording_id,tag_id) SELECT ?,id FROM "
                                      "tags WHERE name=? COLLATE NOCASE"));
        attach.addBindValue(recordingId);
        attach.addBindValue(tag);
        if (!attach.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("A tag could not be attached."), attach));
        }
    }
    return Result<void>::success();
}

Result<void> updateMetadataColumn(DatabaseManager& databaseManager, const QString& recordingId,
                                  const QString& column, const QVariant& value,
                                  const bool rebuildSearch) {
    if (recordingId.trimmed().isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A recording ID is required.")));
    }
    return databaseManager.immediateTransaction([&](QSqlDatabase& database) {
        QSqlQuery query(database);
        query.prepare(QStringLiteral("UPDATE recordings SET %1=?,updated_at=? WHERE id=?").arg(column));
        query.addBindValue(value);
        query.addBindValue(TimeUtils::nowStorageString());
        query.addBindValue(recordingId);
        if (!query.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The recording metadata could not be updated."), query));
        }
        if (query.numRowsAffected() == 0) {
            return Result<void>::failure(UserFacingError::database(
                ErrorCode::NotFound, QStringLiteral("The recording no longer exists."), recordingId));
        }
        return rebuildSearch
                   ? DatabaseSearchService(databaseManager).rebuildRecording(database, recordingId)
                   : Result<void>::success();
    });
}

} // namespace

SqliteRecordingRepository::SqliteRecordingRepository(DatabaseManager& databaseManager,
                                                       ArtifactFileRemover artifactFileRemover,
                                                       ManagedReferenceWriteHook managedReferenceWriteHook)
    : m_databaseManager(databaseManager),
      m_artifactFileRemover(std::move(artifactFileRemover)),
      m_managedReferenceWriteHook(std::move(managedReferenceWriteHook)) {}

Result<void> SqliteRecordingRepository::create(Recording recording) {
    if (recording.title.trimmed().isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A recording title is required.")));
    }
    if (recording.id.isEmpty())
        recording.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!recording.createdAt.isValid())
        recording.createdAt = now;
    recording.updatedAt = now;
    const QStringList tags = normalizedTags(recording.tags);
    return m_databaseManager.immediateTransaction([&](QSqlDatabase& database) {
        if (m_managedReferenceWriteHook) {
            m_managedReferenceWriteHook();
        }
        const auto references = ensureNewManagedReferencesExist(
            database, {}, {{0, recording.sourcePath},
                           {1, recording.managedMediaPath},
                           {2, recording.normalizedPcmPath},
                           {3, recording.waveformPath}});
        if (!references) {
            return references;
        }
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "INSERT INTO recordings(id,title,source_path,managed_media_path,normalized_pcm_path,"
            "source_hash,media_type,duration_ms,sample_rate,channel_count,waveform_path,created_at,"
            "updated_at,deleted_at,notes,review_state,active_job_id) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        query.addBindValue(recording.id);
        query.addBindValue(recording.title.trimmed());
        query.addBindValue(nonNull(recording.sourcePath));
        query.addBindValue(nonNull(recording.managedMediaPath));
        query.addBindValue(nonNull(recording.normalizedPcmPath));
        query.addBindValue(nonNull(recording.sourceHash));
        query.addBindValue(nonNull(recording.mediaType));
        query.addBindValue(recording.durationMs);
        query.addBindValue(recording.sampleRate);
        query.addBindValue(recording.channelCount);
        query.addBindValue(nonNull(recording.waveformPath));
        query.addBindValue(TimeUtils::toStorageString(recording.createdAt));
        query.addBindValue(TimeUtils::toStorageString(recording.updatedAt));
        query.addBindValue(QVariant());
        query.addBindValue(nonNull(recording.notes));
        query.addBindValue(nonNull(recording.reviewState));
        query.addBindValue(recording.activeJobId.isEmpty() ? QVariant() : QVariant(recording.activeJobId));
        if (!query.exec())
            return Result<void>::failure(
                queryError(QStringLiteral("The recording could not be created."), query));
        const auto tagsResult = replaceTags(database, recording.id, tags);
        if (!tagsResult) {
            return tagsResult;
        }
        return DatabaseSearchService(m_databaseManager).rebuildRecording(database, recording.id);
    });
}

Result<void> SqliteRecordingRepository::update(const Recording& recording, const QString& jobId,
                                               const QString& ownerToken) {
    if (recording.id.trimmed().isEmpty() ||
        (ownerToken.isEmpty() && recording.title.trimmed().isEmpty())) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("Recording ID and title are required.")));
    }
    if (jobId.isEmpty() != ownerToken.isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument,
            QStringLiteral("A transcription job and lease owner must be supplied together.")));
    }
    const bool replaceRecordingTags = !recording.tags.isEmpty();
    const QStringList tags = normalizedTags(recording.tags);
    const auto updateAllFields = [&](QSqlDatabase& database) {
        const auto leaseCheck = ensureNoActiveExecutionLease(database, recording.id);
        if (!leaseCheck) {
            return leaseCheck;
        }
        if (m_managedReferenceWriteHook) {
            m_managedReferenceWriteHook();
        }
        const auto references = ensureNewManagedReferencesExist(
            database, recording.id, {{0, recording.sourcePath},
                                     {1, recording.managedMediaPath},
                                     {2, recording.normalizedPcmPath},
                                     {3, recording.waveformPath}});
        if (!references) {
            return references;
        }
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "UPDATE recordings SET title=?,source_path=?,managed_media_path=?,normalized_pcm_path=?,"
            "source_hash=?,media_type=?,duration_ms=?,sample_rate=?,channel_count=?,waveform_path=?,"
            "updated_at=?,notes=?,review_state=? WHERE id=?"));
        query.addBindValue(recording.title.trimmed());
        query.addBindValue(nonNull(recording.sourcePath));
        query.addBindValue(nonNull(recording.managedMediaPath));
        query.addBindValue(nonNull(recording.normalizedPcmPath));
        query.addBindValue(nonNull(recording.sourceHash));
        query.addBindValue(nonNull(recording.mediaType));
        query.addBindValue(qMax<qint64>(0, recording.durationMs));
        query.addBindValue(qMax(0, recording.sampleRate));
        query.addBindValue(qMax(0, recording.channelCount));
        query.addBindValue(nonNull(recording.waveformPath));
        query.addBindValue(TimeUtils::nowStorageString());
        query.addBindValue(nonNull(recording.notes));
        query.addBindValue(nonNull(recording.reviewState));
        query.addBindValue(recording.id);
        if (!query.exec())
            return Result<void>::failure(
                queryError(QStringLiteral("The recording could not be updated."), query));
        if (query.numRowsAffected() == 0) {
            return Result<void>::failure(UserFacingError::database(
                ErrorCode::NotFound, QStringLiteral("The recording no longer exists."), recording.id));
        }
        if (replaceRecordingTags) {
            const auto tagsResult = replaceTags(database, recording.id, tags);
            if (!tagsResult) {
                return tagsResult;
            }
        }
        return DatabaseSearchService(m_databaseManager).rebuildRecording(database, recording.id);
    };
    const auto updateDerivedFields = [&](QSqlDatabase& database) {
        const auto jobCheck = ensureJobBelongsToRecording(database, jobId, recording.id);
        if (!jobCheck) {
            return jobCheck;
        }
        if (m_managedReferenceWriteHook) {
            m_managedReferenceWriteHook();
        }
        const auto references = ensureNewManagedReferencesExist(
            database, recording.id,
            {{2, recording.normalizedPcmPath}, {3, recording.waveformPath}});
        if (!references) {
            return references;
        }
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "UPDATE recordings SET normalized_pcm_path=?,source_hash=?,media_type=?,duration_ms=?,"
            "sample_rate=?,channel_count=?,waveform_path=?,updated_at=? WHERE id=?"));
        query.addBindValue(nonNull(recording.normalizedPcmPath));
        query.addBindValue(nonNull(recording.sourceHash));
        query.addBindValue(nonNull(recording.mediaType));
        query.addBindValue(qMax<qint64>(0, recording.durationMs));
        query.addBindValue(qMax(0, recording.sampleRate));
        query.addBindValue(qMax(0, recording.channelCount));
        query.addBindValue(nonNull(recording.waveformPath));
        query.addBindValue(TimeUtils::nowStorageString());
        query.addBindValue(recording.id);
        if (!query.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The recording artifacts could not be updated."), query));
        }
        if (query.numRowsAffected() == 0) {
            return Result<void>::failure(UserFacingError::database(
                ErrorCode::NotFound, QStringLiteral("The recording no longer exists."), recording.id));
        }
        return Result<void>::success();
    };
    return ownerToken.isEmpty()
               ? m_databaseManager.immediateTransaction(updateAllFields)
               : m_databaseManager.executionLeaseTransaction(jobId, ownerToken,
                                                               updateDerivedFields);
}

Result<void> SqliteRecordingRepository::updateTitle(const QString& recordingId, const QString& title) {
    const QString trimmedTitle = title.trimmed();
    if (trimmedTitle.isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A recording title is required.")));
    }
    return updateMetadataColumn(m_databaseManager, recordingId, QStringLiteral("title"),
                                trimmedTitle, true);
}

Result<void> SqliteRecordingRepository::updateNotes(const QString& recordingId, const QString& notes) {
    return updateMetadataColumn(m_databaseManager, recordingId, QStringLiteral("notes"),
                                nonNull(notes), true);
}

Result<void> SqliteRecordingRepository::updateReviewState(const QString& recordingId,
                                                          const QString& reviewState) {
    if (reviewState.trimmed().isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A review state is required.")));
    }
    return updateMetadataColumn(m_databaseManager, recordingId, QStringLiteral("review_state"),
                                reviewState, false);
}

Result<void> SqliteRecordingRepository::relinkSource(const QString& recordingId,
                                                     const QString& sourcePath,
                                                     const bool clearDerivedArtifacts) {
    if (recordingId.trimmed().isEmpty() || sourcePath.isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A recording and source path are required.")));
    }
    return m_databaseManager.immediateTransaction([&](QSqlDatabase& database) {
        const auto leaseCheck = ensureNoActiveExecutionLease(database, recordingId);
        if (!leaseCheck) {
            return leaseCheck;
        }
        if (m_managedReferenceWriteHook) {
            m_managedReferenceWriteHook();
        }
        const auto references =
            ensureNewManagedReferencesExist(database, recordingId, {{0, sourcePath}});
        if (!references) {
            return references;
        }
        QSqlQuery query(database);
        if (clearDerivedArtifacts) {
            query.prepare(QStringLiteral(
                "UPDATE recordings SET source_path=?,normalized_pcm_path='',source_hash='',media_type='',"
                "duration_ms=0,sample_rate=0,channel_count=0,waveform_path='',updated_at=? WHERE id=?"));
        } else {
            query.prepare(QStringLiteral("UPDATE recordings SET source_path=?,updated_at=? WHERE id=?"));
        }
        query.addBindValue(sourcePath);
        query.addBindValue(TimeUtils::nowStorageString());
        query.addBindValue(recordingId);
        if (!query.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The recording source could not be relinked."), query));
        }
        if (query.numRowsAffected() == 0) {
            return Result<void>::failure(UserFacingError::database(
                ErrorCode::NotFound, QStringLiteral("The recording no longer exists."), recordingId));
        }
        return Result<void>::success();
    });
}

Result<std::optional<Recording>> SqliteRecordingRepository::findById(const QString& id) const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<std::optional<Recording>>::failure(connectionResult.error());
    QSqlDatabase database = connectionResult.value();
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT %1 FROM recordings r WHERE r.id=?")
                      .arg(recordingSelection(QStringLiteral("r"))));
    query.addBindValue(id);
    if (!query.exec())
        return Result<std::optional<Recording>>::failure(
            queryError(QStringLiteral("The recording could not be loaded."), query));
    if (!query.next())
        return Result<std::optional<Recording>>::success(std::nullopt);
    Recording recording = readRecording(query);
    auto tags = loadTags(database, id);
    if (!tags)
        return Result<std::optional<Recording>>::failure(tags.error());
    recording.tags = tags.value();
    return Result<std::optional<Recording>>::success(recording);
}

Result<std::optional<Recording>>
SqliteRecordingRepository::findBySourcePath(const QString& sourcePath) const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<std::optional<Recording>>::failure(connectionResult.error());
    QSqlDatabase database = connectionResult.value();
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT %1 FROM recordings r WHERE r.source_path=? AND r.deleted_at IS "
                                 "NULL ORDER BY r.updated_at DESC LIMIT 1")
                      .arg(recordingSelection(QStringLiteral("r"))));
    query.addBindValue(nonNull(sourcePath));
    if (!query.exec())
        return Result<std::optional<Recording>>::failure(
            queryError(QStringLiteral("The recording source could not be looked up."), query));
    if (!query.next())
        return Result<std::optional<Recording>>::success(std::nullopt);
    Recording recording = readRecording(query);
    auto tags = loadTags(database, recording.id);
    if (!tags)
        return Result<std::optional<Recording>>::failure(tags.error());
    recording.tags = tags.value();
    return Result<std::optional<Recording>>::success(recording);
}

Result<RecordingPage> SqliteRecordingRepository::list(const RecordingQuery& request) const {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<RecordingPage>::failure(connectionResult.error());
    QSqlDatabase database = connectionResult.value();
    constexpr int MaximumPageSize = 1'000;
    const int limit = qBound(1, request.limit, MaximumPageSize);
    const int offset = qMax(0, request.offset);
    QStringList where;
    QVariantList binds;
    if (request.deletedOnly)
        where.append(QStringLiteral("r.deleted_at IS NOT NULL"));
    else if (!request.includeDeleted)
        where.append(QStringLiteral("r.deleted_at IS NULL"));
    if (!request.status.isEmpty()) {
        where.append(QStringLiteral("r.review_state=?"));
        binds.append(request.status);
    }
    if (!request.tag.isEmpty()) {
        where.append(QStringLiteral("EXISTS(SELECT 1 FROM recording_tags rt JOIN tags t ON t.id=rt.tag_id "
                                    "WHERE rt.recording_id=r.id AND t.name=? COLLATE NOCASE)"));
        binds.append(request.tag);
    }
    if (!request.searchText.trimmed().isEmpty()) {
        where.append(
            QStringLiteral("(r.title LIKE ? ESCAPE '\\' OR r.notes LIKE ? ESCAPE '\\' OR "
                           "EXISTS(SELECT 1 FROM recording_tags srt JOIN tags st ON st.id=srt.tag_id "
                           "WHERE srt.recording_id=r.id AND st.name LIKE ? ESCAPE '\\') OR "
                           "EXISTS(SELECT 1 FROM transcript_segments s WHERE s.recording_id=r.id "
                           "AND (s.edited_text LIKE ? ESCAPE '\\' OR s.original_text LIKE ? ESCAPE '\\')))"));
        QString pattern = request.searchText;
        pattern.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        pattern.replace(QLatin1Char('%'), QStringLiteral("\\%"));
        pattern.replace(QLatin1Char('_'), QStringLiteral("\\_"));
        pattern = QLatin1Char('%') + pattern + QLatin1Char('%');
        for (int i = 0; i < 5; ++i)
            binds.append(pattern);
    }
    const QString clause =
        where.isEmpty() ? QString() : QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    QSqlQuery count(database);
    count.prepare(QStringLiteral("SELECT COUNT(*) FROM recordings r") + clause);
    for (const QVariant& bind : binds)
        count.addBindValue(bind);
    if (!count.exec() || !count.next())
        return Result<RecordingPage>::failure(
            queryError(QStringLiteral("The recording count could not be loaded."), count));
    RecordingPage page;
    page.totalCount = count.value(0).toInt();
    page.offset = offset;
    page.limit = limit;
    const QString direction =
        request.sortOrder == Qt::AscendingOrder ? QStringLiteral("ASC") : QStringLiteral("DESC");
    QSqlQuery rows(database);
    rows.prepare(QStringLiteral("SELECT %1 FROM recordings r").arg(recordingSelection(QStringLiteral("r"))) +
                 clause + QStringLiteral(" ORDER BY r.") + safeSortColumn(request.sortColumn) +
                 QLatin1Char(' ') + direction + QStringLiteral(", r.id ASC LIMIT ? OFFSET ?"));
    for (const QVariant& bind : binds)
        rows.addBindValue(bind);
    rows.addBindValue(limit);
    rows.addBindValue(offset);
    if (!rows.exec())
        return Result<RecordingPage>::failure(
            queryError(QStringLiteral("The recordings could not be loaded."), rows));
    QStringList recordingIds;
    while (rows.next()) {
        page.items.append(readRecording(rows));
        recordingIds.append(page.items.constLast().id);
    }
    const auto tags = loadTags(database, recordingIds);
    if (!tags) {
        return Result<RecordingPage>::failure(tags.error());
    }
    for (Recording& recording : page.items) {
        recording.tags = tags.value().value(recording.id);
    }
    return Result<RecordingPage>::success(page);
}

Result<void> SqliteRecordingRepository::setTags(const QString& recordingId, const QStringList& tags) {
    const QStringList unique = normalizedTags(tags);
    return m_databaseManager.transaction([&](QSqlDatabase& database) {
        const auto tagsResult = replaceTags(database, recordingId, unique);
        if (!tagsResult) {
            return tagsResult;
        }
        return DatabaseSearchService(m_databaseManager).rebuildRecording(database, recordingId);
    });
}

Result<void> SqliteRecordingRepository::moveToTrash(const QString& id) {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(
        QStringLiteral("UPDATE recordings SET deleted_at=?,updated_at=? WHERE id=? AND deleted_at IS NULL"));
    const QString now = TimeUtils::nowStorageString();
    query.addBindValue(now);
    query.addBindValue(now);
    query.addBindValue(id);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The recording could not be moved to Trash."), query));
    return Result<void>::success();
}

Result<void> SqliteRecordingRepository::restore(const QString& id) {
    auto connectionResult = m_databaseManager.connection();
    if (!connectionResult)
        return Result<void>::failure(connectionResult.error());
    QSqlQuery query(connectionResult.value());
    query.prepare(QStringLiteral(
        "UPDATE recordings SET deleted_at=NULL,updated_at=? WHERE id=? AND deleted_at IS NOT NULL"));
    query.addBindValue(TimeUtils::nowStorageString());
    query.addBindValue(id);
    if (!query.exec())
        return Result<void>::failure(
            queryError(QStringLiteral("The recording could not be restored."), query));
    return Result<void>::success();
}

Result<void> SqliteRecordingRepository::permanentlyDelete(const QString& id) {
    return m_databaseManager.immediateTransaction([&](QSqlDatabase& database) {
        const auto leaseCheck = ensureNoActiveExecutionLease(database, id);
        if (!leaseCheck) {
            return leaseCheck;
        }
        Recording recording;
        QSqlQuery existing(database);
        existing.prepare(QStringLiteral(
            "SELECT source_path,managed_media_path,normalized_pcm_path,waveform_path,deleted_at "
            "FROM recordings WHERE id=?"));
        existing.addBindValue(id);
        if (!existing.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The recording could not be checked before deletion."), existing));
        }
        if (!existing.next() || existing.value(4).isNull()) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidStateTransition,
                QStringLiteral("Only recordings in Trash can be permanently deleted.")));
        }
        recording.id = id;
        recording.sourcePath = existing.value(0).toString();
        recording.managedMediaPath = existing.value(1).toString();
        recording.normalizedPcmPath = existing.value(2).toString();
        recording.waveformPath = existing.value(3).toString();
        const auto enqueueResult = enqueueArtifactDeletions(database, recording);
        if (!enqueueResult) {
            return enqueueResult;
        }
        QSqlQuery query(database);
        query.prepare(QStringLiteral("DELETE FROM recordings WHERE id=? AND deleted_at IS NOT NULL"));
        query.addBindValue(id);
        if (!query.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The recording could not be permanently deleted."), query));
        }
        if (query.numRowsAffected() == 0) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidStateTransition,
                QStringLiteral("Only recordings in Trash can be permanently deleted.")));
        }
        return DatabaseSearchService(m_databaseManager).removeRecording(database, id);
    });
}

Result<PendingArtifactDeletionReport>
SqliteRecordingRepository::drainPendingArtifactDeletions(const QString& recordingId) {
    PendingArtifactDeletionReport report;
    QStringList pendingIds;
    const QString eligibleAt = TimeUtils::nowStorageString();
    {
        const auto connection = m_databaseManager.connection();
        if (!connection) {
            return Result<PendingArtifactDeletionReport>::failure(connection.error());
        }
        QString statement = QStringLiteral(
            "SELECT id FROM pending_recording_artifact_deletions WHERE next_attempt_at<=?");
        if (!recordingId.trimmed().isEmpty()) {
            statement += QStringLiteral(" AND recording_id=?");
        }
        statement += QStringLiteral(" ORDER BY created_at,id");
        QSqlQuery pending(connection.value());
        pending.prepare(statement);
        pending.addBindValue(eligibleAt);
        if (!recordingId.trimmed().isEmpty()) {
            pending.addBindValue(recordingId);
        }
        if (!pending.exec()) {
            return Result<PendingArtifactDeletionReport>::failure(
                queryError(QStringLiteral("Pending managed file cleanup could not be read."), pending));
        }
        while (pending.next()) {
            pendingIds.append(pending.value(0).toString());
        }
    }

    enum class Outcome { Skipped, Removed, Missing, Referenced, Unsafe, Failed };
    for (const QString& pendingId : std::as_const(pendingIds)) {
        Outcome outcome = Outcome::Skipped;
        const auto processResult = m_databaseManager.immediateTransaction([&](QSqlDatabase& database) {
            QSqlQuery pending(database);
            pending.prepare(QStringLiteral(
                "SELECT recording_id,artifact_kind,absolute_path,attempt_count "
                "FROM pending_recording_artifact_deletions WHERE id=? AND next_attempt_at<=?"));
            pending.addBindValue(pendingId);
            pending.addBindValue(TimeUtils::nowStorageString());
            if (!pending.exec()) {
                return Result<void>::failure(
                    queryError(QStringLiteral("Pending managed file cleanup could not be read."), pending));
            }
            if (!pending.next()) {
                return Result<void>::success();
            }

            PendingArtifactDeletion row;
            row.id = pendingId;
            row.recordingId = pending.value(0).toString();
            row.artifactKind = pending.value(1).toString();
            row.absolutePath = pending.value(2).toString();
            row.attemptCount = pending.value(3).toInt();

            const auto acknowledge = [&]() -> Result<void> {
                QSqlQuery remove(database);
                remove.prepare(
                    QStringLiteral("DELETE FROM pending_recording_artifact_deletions WHERE id=?"));
                remove.addBindValue(row.id);
                if (!remove.exec()) {
                    return Result<void>::failure(queryError(
                        QStringLiteral("Completed managed file cleanup could not be recorded."), remove));
                }
                return Result<void>::success();
            };
            const auto retry = [&](const QString& error) -> Result<void> {
                QSqlQuery update(database);
                update.prepare(QStringLiteral(
                    "UPDATE pending_recording_artifact_deletions SET attempt_count=?,next_attempt_at=?,"
                    "last_error=? WHERE id=?"));
                update.addBindValue(row.attemptCount + 1);
                const int exponent = qMin(row.attemptCount, 10);
                const qint64 delaySeconds = qMin<qint64>(21'600, 30LL * (1LL << exponent));
                update.addBindValue(
                    TimeUtils::toStorageString(QDateTime::currentDateTimeUtc().addSecs(delaySeconds)));
                update.addBindValue(error.left(1'024));
                update.addBindValue(row.id);
                if (!update.exec()) {
                    return Result<void>::failure(
                        queryError(QStringLiteral("Managed file cleanup could not be rescheduled."), update));
                }
                return Result<void>::success();
            };

            if (!isSafeArtifactPath(row.absolutePath, row.artifactKind)) {
                outcome = Outcome::Unsafe;
                return acknowledge();
            }
            const QFileInfo fileInfo(row.absolutePath);
            if (!fileInfo.exists() && !fileInfo.isSymLink()) {
                outcome = Outcome::Missing;
                return acknowledge();
            }
            const auto referenced = pathIsReferenced(database, row.absolutePath);
            if (!referenced) {
                return Result<void>::failure(referenced.error());
            }
            if (referenced.value()) {
                outcome = Outcome::Referenced;
                return retry(QStringLiteral("The managed file is still referenced by another recording."));
            }
            // Revalidate immediately before the single filesystem operation. The surrounding
            // BEGIN IMMEDIATE fences database references; Windows deletion additionally pins and
            // validates the opened filesystem entry before marking that handle for deletion.
            if (!isSafeArtifactPath(row.absolutePath, row.artifactKind)) {
                outcome = Outcome::Unsafe;
                return acknowledge();
            }

            QString removalError;
            bool removed = false;
            if (m_artifactFileRemover) {
                removed = m_artifactFileRemover(row.absolutePath, &removalError);
            } else {
                removed = removeArtifactFile(row.absolutePath, row.artifactKind, &removalError);
            }
            if (!removed) {
                outcome = Outcome::Failed;
                const QString message = removalError.trimmed().isEmpty()
                                            ? QStringLiteral("The managed file could not be removed.")
                                            : removalError;
                return retry(message);
            }
            outcome = Outcome::Removed;
            return acknowledge();
        });
        if (!processResult) {
            return Result<PendingArtifactDeletionReport>::failure(processResult.error());
        }
        if (outcome != Outcome::Skipped) {
            ++report.entriesClaimed;
        }
        switch (outcome) {
        case Outcome::Removed:
            ++report.filesRemoved;
            break;
        case Outcome::Missing:
            ++report.missingFiles;
            break;
        case Outcome::Referenced:
            ++report.referencedFilesDeferred;
            break;
        case Outcome::Unsafe:
            ++report.unsafeEntriesDiscarded;
            break;
        case Outcome::Failed:
            ++report.failures;
            break;
        case Outcome::Skipped:
            break;
        }
    }
    return Result<PendingArtifactDeletionReport>::success(report);
}

Result<void> SqliteRecordingRepository::setActiveTranscriptJob(const QString& recordingId,
                                                               const QString& jobId) {
    if (recordingId.trimmed().isEmpty() || jobId.trimmed().isEmpty()) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("A recording and transcript job are required.")));
    }
    return m_databaseManager.immediateTransaction([&](QSqlDatabase& database) {
        QSqlQuery job(database);
        job.prepare(
            QStringLiteral("SELECT state FROM transcription_jobs WHERE id=? AND recording_id=?"));
        job.addBindValue(jobId);
        job.addBindValue(recordingId);
        if (!job.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The transcript job could not be checked."), job));
        }
        if (!job.next()) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::NotFound, QStringLiteral("The requested transcript job does not exist.")));
        }
        if (job.value(0).toString() != QLatin1String("Completed")) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidStateTransition,
                QStringLiteral("Only a completed transcript can be activated.")));
        }
        QSqlQuery update(database);
        update.prepare(
            QStringLiteral("UPDATE recordings SET active_job_id=?,updated_at=? WHERE id=?"));
        update.addBindValue(jobId);
        update.addBindValue(TimeUtils::nowStorageString());
        update.addBindValue(recordingId);
        if (!update.exec()) {
            return Result<void>::failure(
                queryError(QStringLiteral("The active transcript could not be changed."), update));
        }
        if (update.numRowsAffected() == 0) {
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::NotFound, QStringLiteral("The recording no longer exists.")));
        }
        return DatabaseSearchService(m_databaseManager).rebuildRecording(database, recordingId);
    });
}

} // namespace BreezeDesk
