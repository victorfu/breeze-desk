#include "breezedesk/core/TemporaryFileJanitor.h"

#include "breezedesk/core/StoragePaths.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace BreezeDesk {
namespace {

struct DirectoryCandidate {
    QString path;
    QDateTime lastModified;
};

[[nodiscard]] QString normalizedPath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

[[nodiscard]] bool isPathWithin(const QString& path, const QString& parent) {
    if (path == parent) {
        return true;
    }
    const QString prefix = parent.endsWith(QDir::separator()) ? parent : parent + QDir::separator();
    return path.startsWith(prefix, Qt::CaseSensitive);
}

[[nodiscard]] bool isProtected(const QString& path, const QStringList& protectedPaths) {
    return std::any_of(protectedPaths.cbegin(), protectedPaths.cend(), [&path](const QString& protectedPath) {
        return isPathWithin(path, protectedPath) || isPathWithin(protectedPath, path);
    });
}

[[nodiscard]] bool isUnsafeRoot(const QString& directory) {
    const QFileInfo info(directory);
    return info.isRoot() || directory == normalizedPath(QDir::homePath()) ||
           directory == normalizedPath(StoragePaths::root());
}

} // namespace

TemporaryCleanupReport TemporaryFileJanitor::clean(TemporaryCleanupPolicy policy) {
    TemporaryCleanupReport report;
    if (policy.directory.trimmed().isEmpty()) {
        policy.directory = StoragePaths::temporary();
    }
    if (policy.maximumAgeMs < 0) {
        report.error = QStringLiteral("The temporary-file maximum age cannot be negative.");
        return report;
    }

    const QString directory = normalizedPath(policy.directory);
    const QString applicationTemporaryDirectory = normalizedPath(StoragePaths::temporary());
    if (isUnsafeRoot(directory)) {
        report.error = QStringLiteral("The requested cleanup location is too broad.");
        return report;
    }
    if (!policy.allowOutsideApplicationTemporaryDirectory &&
        !isPathWithin(directory, applicationTemporaryDirectory)) {
        report.error = QStringLiteral("Cleanup is restricted to the application temporary directory.");
        return report;
    }

    QStringList protectedPaths;
    protectedPaths.reserve(policy.protectedPaths.size());
    for (const QString& protectedPath : std::as_const(policy.protectedPaths)) {
        const QString normalized = normalizedPath(protectedPath);
        if (!isPathWithin(normalized, directory)) {
            report.error = QStringLiteral("A protected path is outside the cleanup directory.");
            return report;
        }
        protectedPaths.append(normalized);
    }

    const QFileInfo rootInfo(directory);
    if (!rootInfo.exists()) {
        return report;
    }
    if (!rootInfo.isDir() || rootInfo.isSymLink()) {
        report.error = QStringLiteral("The temporary cleanup location is not a regular directory.");
        return report;
    }

    const QDateTime threshold = QDateTime::currentDateTimeUtc().addMSecs(-policy.maximumAgeMs);
    QList<DirectoryCandidate> directories;
    QDirIterator iterator(directory,
                          QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const QString path = normalizedPath(info.absoluteFilePath());
        ++report.entriesScanned;
        if (isProtected(path, protectedPaths)) {
            continue;
        }
        if (info.isDir() && !info.isSymLink()) {
            directories.append({path, info.lastModified().toUTC()});
            continue;
        }
        if (info.lastModified().toUTC() > threshold) {
            continue;
        }
        const qint64 size = info.isFile() ? info.size() : 0;
        if (QFile::remove(path)) {
            ++report.filesRemoved;
            report.bytesReleased += size;
        } else {
            ++report.failures;
        }
    }

    std::sort(directories.begin(), directories.end(),
              [](const DirectoryCandidate& left, const DirectoryCandidate& right) {
                  return left.path.size() > right.path.size();
              });
    for (const DirectoryCandidate& candidate : std::as_const(directories)) {
        if (isProtected(candidate.path, protectedPaths)) {
            continue;
        }
        const QFileInfo info(candidate.path);
        if (info.exists() && candidate.lastModified <= threshold && QDir(candidate.path).isEmpty()) {
            if (QDir().rmdir(candidate.path)) {
                ++report.directoriesRemoved;
            } else {
                ++report.failures;
            }
        }
    }
    return report;
}

} // namespace BreezeDesk
