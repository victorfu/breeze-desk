#include "breezedesk/audio/AudioCacheManager.h"

#include "breezedesk/core/StoragePaths.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace BreezeDesk {
namespace {

constexpr auto NormalizationProfileTag = ".timeline-v2";

QString pathComparisonKey(const QString& path) {
    if (path.isEmpty()) {
        return {};
    }
    QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

Qt::CaseSensitivity fileNameCaseSensitivity() {
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

bool belongsToGeneration(const QFileInfo& file, const QString& extension,
                         const QString& generationId) {
    if (generationId.isEmpty()) {
        return false;
    }
    const QString suffix =
        QStringLiteral(".%1.%2").arg(generationId, extension);
    return file.fileName().endsWith(suffix, fileNameCaseSensitivity());
}

} // namespace

QString AudioCacheManager::cacheRoot() {
    const QString root = StoragePaths::cache();
    QDir().mkpath(root);
    return root;
}

QString AudioCacheManager::normalizedAudioPath(const QString& recordingId,
                                               const QString& generationId) {
    const QString directory = QDir(cacheRoot()).filePath(QStringLiteral("audio"));
    QDir().mkpath(directory);
    const QString generationSuffix =
        generationId.isEmpty() ? QString{} : QStringLiteral(".") + generationId;
    return QDir(directory).filePath(recordingId + QString::fromLatin1(NormalizationProfileTag) +
                                    generationSuffix + QStringLiteral(".wav"));
}

bool AudioCacheManager::isReusableNormalizedAudioPath(const QString& path) {
    if (path.trimmed().isEmpty()) {
        return false;
    }
    const QString profileToken =
        QString::fromLatin1(NormalizationProfileTag) + QLatin1Char('.');
    return QFileInfo(path).fileName().contains(profileToken, fileNameCaseSensitivity());
}

QString AudioCacheManager::waveformPath(const QString& recordingId, const QString& generationId) {
    const QString directory = QDir(cacheRoot()).filePath(QStringLiteral("waveforms"));
    QDir().mkpath(directory);
    const QString generationSuffix =
        generationId.isEmpty() ? QString{} : QStringLiteral(".") + generationId;
    return QDir(directory).filePath(recordingId + generationSuffix + QStringLiteral(".bwpk"));
}

qint64 AudioCacheManager::cacheSizeBytes() {
    qint64 total = 0;
    QDirIterator iterator(cacheRoot(), QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        total += iterator.fileInfo().size();
    }
    return total;
}

bool AudioCacheManager::clear(QString* error) {
    QDir directory(cacheRoot());
    if (!directory.removeRecursively()) {
        if (error != nullptr) {
            *error = QStringLiteral("The audio cache could not be removed.");
        }
        return false;
    }
    return QDir().mkpath(cacheRoot());
}

void AudioCacheManager::removeExpiredTemporaryFiles(int maximumAgeHours) {
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-maximumAgeHours * 3600);
    QDirIterator iterator(cacheRoot(), {QStringLiteral("*.tmp.*"), QStringLiteral("*.part")}, QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (iterator.fileInfo().lastModified().toUTC() < cutoff) {
            QFile::remove(path);
        }
    }
}

int AudioCacheManager::removeExpiredOrphanedGenerationFiles(
    const QSet<QString>& referencedPaths, const QString& protectedGenerationId,
    int maximumAgeHours) {
    QSet<QString> referencedKeys;
    for (const QString& path : referencedPaths) {
        const QString key = pathComparisonKey(path);
        if (!key.isEmpty()) {
            referencedKeys.insert(key);
        }
    }

    const QDateTime cutoff =
        QDateTime::currentDateTimeUtc().addSecs(-static_cast<qint64>(qMax(0, maximumAgeHours)) * 3600);
    int removed = 0;
    const auto removeOrphans = [&](const QString& directoryName, const QString& extension) {
        const QString directory = QDir(cacheRoot()).filePath(directoryName);
        QDirIterator iterator(directory, {QStringLiteral("*.%1").arg(extension)},
                              QDir::Files | QDir::NoSymLinks);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            const QFileInfo file = iterator.fileInfo();
            if (referencedKeys.contains(pathComparisonKey(path)) ||
                belongsToGeneration(file, extension, protectedGenerationId)) {
                continue;
            }
            const QDateTime lastModified = file.lastModified();
            if (!lastModified.isValid() || lastModified.toUTC() >= cutoff) {
                continue;
            }
            if (QFile::remove(path)) {
                ++removed;
            }
        }
    };

    removeOrphans(QStringLiteral("audio"), QStringLiteral("wav"));
    removeOrphans(QStringLiteral("waveforms"), QStringLiteral("bwpk"));
    return removed;
}

} // namespace BreezeDesk
