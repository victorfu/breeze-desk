#include "breezedesk/audio/MediaFileSupport.h"

#include <QFileInfo>
#include <QSet>

namespace BreezeDesk {

QStringList MediaFileSupport::supportedExtensions() {
    return {QStringLiteral("wav"),  QStringLiteral("mp3"), QStringLiteral("m4a"),  QStringLiteral("aac"),
            QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("opus"), QStringLiteral("mp4"),
            QStringLiteral("mov"),  QStringLiteral("mkv"), QStringLiteral("webm")};
}

bool MediaFileSupport::isSupportedPath(const QString& path) {
    static const QSet<QString> extensions = [] {
        const QStringList values = MediaFileSupport::supportedExtensions();
        return QSet<QString>(values.cbegin(), values.cend());
    }();
    return extensions.contains(QFileInfo(path).suffix().toLower());
}

} // namespace BreezeDesk
