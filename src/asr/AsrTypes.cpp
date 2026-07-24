#include <breezedesk/asr/AsrTypes.h>

namespace BreezeDesk::Asr {

QString backendName(Backend backend) {
    switch (backend) {
    case Backend::Auto:
        return QStringLiteral("Auto");
    case Backend::Cpu:
        return QStringLiteral("CPU");
    case Backend::Metal:
        return QStringLiteral("Metal");
    case Backend::Vulkan:
        return QStringLiteral("Vulkan");
    }
    return QStringLiteral("Unknown");
}

} // namespace BreezeDesk::Asr
