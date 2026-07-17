#pragma once

#include <breezedesk/asr/AsrTypes.h>

#include <QtCore/QByteArray>
#include <QtCore/QString>

namespace BreezeDesk::Asr {

class ModelFileIntegrity final {
  public:
    [[nodiscard]] static bool isValidSha256(const QByteArray& sha256) noexcept;
    [[nodiscard]] static AsrError verifySha256(const QString& path, const QByteArray& expectedSha256,
                                               QByteArray* actualSha256 = nullptr);
};

} // namespace BreezeDesk::Asr
