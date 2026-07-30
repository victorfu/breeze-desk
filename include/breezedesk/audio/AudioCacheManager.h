#pragma once

#include <QSet>
#include <QString>

namespace BreezeDesk {

class AudioCacheManager final {
  public:
    [[nodiscard]] static QString cacheRoot();
    [[nodiscard]] static QString normalizedAudioPath(const QString& recordingId,
                                                     const QString& generationId);
    [[nodiscard]] static QString waveformPath(const QString& recordingId,
                                              const QString& generationId);
    [[nodiscard]] static qint64 cacheSizeBytes();
    [[nodiscard]] static bool clear(QString* error = nullptr);
    static void removeExpiredTemporaryFiles(int maximumAgeHours = 24);
    [[nodiscard]] static int
    removeExpiredOrphanedGenerationFiles(const QSet<QString>& referencedPaths,
                                         const QString& protectedGenerationId = {},
                                         int maximumAgeHours = 24);
};

} // namespace BreezeDesk
