#pragma once

#include "breezedesk/audio/IAudioMetadataReader.h"

namespace BreezeDesk {

class FFprobeService final : public IAudioMetadataReader {
  public:
    explicit FFprobeService(QString ffprobePath);
    [[nodiscard]] MediaMetadata inspect(const QString& path, QString* error = nullptr) const override;

  private:
    QString m_ffprobePath;
};

} // namespace BreezeDesk
