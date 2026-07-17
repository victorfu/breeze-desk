#include "breezedesk/audio/AudioCacheManager.h"

#include "breezedesk/core/StoragePaths.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>

namespace BreezeDesk {

QString AudioCacheManager::cacheRoot() {
    const QString root = StoragePaths::cache();
    QDir().mkpath(root);
    return root;
}

QString AudioCacheManager::normalizedAudioPath(const QString& recordingId) {
    const QString directory = QDir(cacheRoot()).filePath(QStringLiteral("audio"));
    QDir().mkpath(directory);
    return QDir(directory).filePath(recordingId + QStringLiteral(".wav"));
}

QString AudioCacheManager::waveformPath(const QString& recordingId) {
    const QString directory = QDir(cacheRoot()).filePath(QStringLiteral("waveforms"));
    QDir().mkpath(directory);
    return QDir(directory).filePath(recordingId + QStringLiteral(".bwpk"));
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

} // namespace BreezeDesk
