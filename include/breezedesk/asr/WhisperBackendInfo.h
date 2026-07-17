#pragma once

#include <breezedesk/asr/AsrTypes.h>

#include <QtCore/QString>

namespace BreezeDesk::Asr {

class WhisperBackendInfo final {
  public:
    [[nodiscard]] static bool runtimeAvailable() noexcept;
    [[nodiscard]] static QString version();
    [[nodiscard]] static QString systemInfo();
    [[nodiscard]] static Backend compiledBackend() noexcept;
};

} // namespace BreezeDesk::Asr
