#pragma once

#include "breezedesk/platform/PlatformCapabilities.h"

#include <QString>

namespace BreezeDesk {

class IPlatformService {
  public:
    virtual ~IPlatformService() = default;
    [[nodiscard]] virtual PlatformCapabilities capabilities() const = 0;
    [[nodiscard]] virtual bool revealInFileManager(const QString& path, QString* error = nullptr) const = 0;
    [[nodiscard]] virtual bool setLaunchAtLogin(bool enabled, QString* error = nullptr) = 0;
    [[nodiscard]] virtual bool launchAtLogin(QString* error = nullptr) const = 0;
    [[nodiscard]] virtual bool requestMicrophonePermission(QString* error = nullptr) = 0;
    [[nodiscard]] virtual QString installSource() const = 0;
    [[nodiscard]] virtual QString gpuDescription() const = 0;
    virtual void activateApplication() = 0;
};

[[nodiscard]] IPlatformService* createPlatformService();

} // namespace BreezeDesk
