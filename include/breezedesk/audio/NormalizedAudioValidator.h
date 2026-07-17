#pragma once

#include <QString>

namespace BreezeDesk {

struct NormalizedAudioInfo {
    qint64 dataOffset{0};
    qint64 dataSize{0};
    qint64 durationMs{0};
};

class NormalizedAudioValidator final {
  public:
    [[nodiscard]] static bool validate(const QString& path, qint64 expectedDurationMs,
                                       NormalizedAudioInfo* info = nullptr, QString* error = nullptr);
};

} // namespace BreezeDesk
