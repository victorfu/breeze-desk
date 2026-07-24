#include <breezedesk/asr/WhisperBackendInfo.h>

#ifdef BREEZEDESK_HAS_WHISPER
#include <whisper.h>
#endif

namespace BreezeDesk::Asr {

bool WhisperBackendInfo::runtimeAvailable() noexcept {
#ifdef BREEZEDESK_HAS_WHISPER
    return true;
#else
    return false;
#endif
}

QString WhisperBackendInfo::version() {
#ifdef BREEZEDESK_HAS_WHISPER
    return QString::fromUtf8(whisper_version());
#else
    return QStringLiteral("unavailable");
#endif
}

QString WhisperBackendInfo::systemInfo() {
#ifdef BREEZEDESK_HAS_WHISPER
    return QString::fromUtf8(whisper_print_system_info());
#else
    return QStringLiteral("whisper.cpp is disabled in this build");
#endif
}

Backend WhisperBackendInfo::compiledBackend() noexcept {
#if defined(BREEZEDESK_WHISPER_BACKEND_METAL)
    return Backend::Metal;
#elif defined(BREEZEDESK_WHISPER_BACKEND_VULKAN)
    return Backend::Vulkan;
#else
    return Backend::Cpu;
#endif
}

} // namespace BreezeDesk::Asr
