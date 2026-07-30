#pragma once

#include "breezedesk/audio/IAudioMetadataReader.h"

#include <atomic>

namespace BreezeDesk {

class FFprobeService final : public IAudioMetadataReader {
  public:
    explicit FFprobeService(QString ffprobePath);
    [[nodiscard]] MediaMetadata inspect(const QString& path, QString* error = nullptr) const override;
    [[nodiscard]] MediaMetadata inspect(const QString& path, const std::atomic_bool* cancellation,
                                        QString* error = nullptr) const;

  private:
    QString m_ffprobePath;
};

} // namespace BreezeDesk
