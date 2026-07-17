#include <breezedesk/asr/WhisperLogBridge.h>

#include <QtCore/QLoggingCategory>
#include <QtCore/QString>

#ifdef BREEZEDESK_HAS_WHISPER
#include <whisper.h>
#endif

Q_LOGGING_CATEGORY(whisperLog, "breezedesk.asr.whisper")

namespace BreezeDesk::Asr {
namespace {

#ifdef BREEZEDESK_HAS_WHISPER
void logCallback(ggml_log_level level, const char* text, void*) {
    const QString message = QString::fromUtf8(text).trimmed();
    if (message.isEmpty()) {
        return;
    }
    switch (level) {
    case GGML_LOG_LEVEL_ERROR:
        qCCritical(whisperLog, "%s", qUtf8Printable(message));
        break;
    case GGML_LOG_LEVEL_WARN:
        qCWarning(whisperLog, "%s", qUtf8Printable(message));
        break;
    case GGML_LOG_LEVEL_INFO:
        qCInfo(whisperLog, "%s", qUtf8Printable(message));
        break;
    default:
        qCDebug(whisperLog, "%s", qUtf8Printable(message));
        break;
    }
}
#endif

} // namespace

void WhisperLogBridge::install() {
#ifdef BREEZEDESK_HAS_WHISPER
    whisper_log_set(&logCallback, nullptr);
#endif
}

} // namespace BreezeDesk::Asr
