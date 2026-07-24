#include "breezedesk/platform/PlatformCapabilities.h"

#include "breezedesk/update_config.h"

namespace BreezeDesk {

PlatformCapabilities PlatformCapabilities::current() {
    PlatformCapabilities result;
#if defined(Q_OS_MACOS)
    result.supportsMetal = true;
    result.supportsAutoLaunch = true;
    result.supportsTray = true;
    result.supportsNativeUpdate = BREEZEDESK_ENABLE_UPDATES != 0;
    result.supportsFileAssociations = true;
    result.supportsMicrophonePermission = true;
#elif defined(Q_OS_WIN)
    result.supportsVulkan = true;
    result.supportsAutoLaunch = true;
    result.supportsTray = true;
    result.supportsNativeUpdate = BREEZEDESK_ENABLE_UPDATES != 0;
    result.supportsFileAssociations = true;
    result.supportsMicrophonePermission = true;
#endif
    return result;
}

} // namespace BreezeDesk
