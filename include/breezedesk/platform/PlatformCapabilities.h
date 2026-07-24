#pragma once

#include <QObject>

namespace BreezeDesk {

struct PlatformCapabilities {
    Q_GADGET
    Q_PROPERTY(bool supportsMetal MEMBER supportsMetal)
    Q_PROPERTY(bool supportsVulkan MEMBER supportsVulkan)
    Q_PROPERTY(bool supportsAutoLaunch MEMBER supportsAutoLaunch)
    Q_PROPERTY(bool supportsTray MEMBER supportsTray)
    Q_PROPERTY(bool supportsNativeUpdate MEMBER supportsNativeUpdate)
    Q_PROPERTY(bool supportsFileAssociations MEMBER supportsFileAssociations)
    Q_PROPERTY(bool supportsMicrophonePermission MEMBER supportsMicrophonePermission)

  public:
    bool supportsMetal = false;
    bool supportsVulkan = false;
    bool supportsAutoLaunch = false;
    bool supportsTray = false;
    bool supportsNativeUpdate = false;
    bool supportsFileAssociations = false;
    bool supportsMicrophonePermission = false;

    [[nodiscard]] static PlatformCapabilities current();
};

} // namespace BreezeDesk
