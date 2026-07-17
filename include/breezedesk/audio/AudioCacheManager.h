#pragma once

#include <QString>

namespace BreezeDesk {

class AudioCacheManager final {
  public:
    [[nodiscard]] static QString cacheRoot();
    [[nodiscard]] static QString normalizedAudioPath(const QString& recordingId);
    [[nodiscard]] static QString waveformPath(const QString& recordingId);
    [[nodiscard]] static qint64 cacheSizeBytes();
    [[nodiscard]] static bool clear(QString* error = nullptr);
    static void removeExpiredTemporaryFiles(int maximumAgeHours = 24);
};

} // namespace BreezeDesk
