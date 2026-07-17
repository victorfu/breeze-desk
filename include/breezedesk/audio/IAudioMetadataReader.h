#pragma once

#include "breezedesk/audio/MediaMetadata.h"

#include <QString>

namespace BreezeDesk {

class IAudioMetadataReader {
  public:
    virtual ~IAudioMetadataReader() = default;
    [[nodiscard]] virtual MediaMetadata inspect(const QString& path, QString* error = nullptr) const = 0;
};

} // namespace BreezeDesk
