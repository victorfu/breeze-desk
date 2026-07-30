#include "breezedesk/audio/MediaMetadata.h"

#include <QJsonArray>
#include <QLocale>

namespace BreezeDesk {

namespace {
qint64 parseMilliseconds(const QJsonValue& value) {
    bool ok = false;
    const double seconds = QLocale::c().toDouble(value.toString(), &ok);
    return ok ? qRound64(seconds * 1000.0) : 0;
}

int parseInt(const QJsonValue& value) {
    bool ok = false;
    const int result = value.toString().toInt(&ok);
    return ok ? result : value.toInt();
}
} // namespace

MediaMetadata MediaMetadata::fromFfprobeJson(const QJsonObject& root, QString* error) {
    MediaMetadata metadata;
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    int audioStreamIndex = 0;
    bool selectedAudioIsDefault = false;
    for (const QJsonValue& entry : streams) {
        const QJsonObject stream = entry.toObject();
        const QString type = stream.value(QStringLiteral("codec_type")).toString();
        if (type == QStringLiteral("audio")) {
            const bool isDefault =
                parseInt(stream.value(QStringLiteral("disposition"))
                             .toObject()
                             .value(QStringLiteral("default"))) != 0;
            const bool select = !metadata.hasAudio || (!selectedAudioIsDefault && isDefault);
            metadata.hasAudio = true;
            if (select) {
                metadata.audioStreamIndex = audioStreamIndex;
                metadata.codecName = stream.value(QStringLiteral("codec_name")).toString();
                metadata.sampleRate = parseInt(stream.value(QStringLiteral("sample_rate")));
                metadata.channelCount = parseInt(stream.value(QStringLiteral("channels")));
                metadata.durationMs = parseMilliseconds(stream.value(QStringLiteral("duration")));
                selectedAudioIsDefault = isDefault;
            }
            ++audioStreamIndex;
        } else if (type == QStringLiteral("video")) {
            metadata.hasVideo = true;
        }
    }

    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    metadata.formatName = format.value(QStringLiteral("format_name")).toString();
    metadata.bitRate = format.value(QStringLiteral("bit_rate")).toString().toLongLong();
    if (metadata.durationMs <= 0) {
        metadata.durationMs = parseMilliseconds(format.value(QStringLiteral("duration")));
    }
    if (!metadata.hasAudio && error != nullptr) {
        *error = QStringLiteral("The selected media does not contain an audio stream.");
    }
    return metadata;
}

} // namespace BreezeDesk
