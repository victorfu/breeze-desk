#include "breezedesk/audio/MediaMetadata.h"

#include <QJsonArray>
#include <QLocale>

#include <limits>

namespace BreezeDesk {

namespace {
bool parseMilliseconds(const QJsonValue& value, qint64* milliseconds) {
    if (milliseconds == nullptr) {
        return false;
    }
    bool ok = false;
    const double seconds = QLocale::c().toDouble(value.toString(), &ok);
    if (!ok || !qIsFinite(seconds) ||
        seconds > static_cast<double>(std::numeric_limits<qint64>::max()) / 1000.0 ||
        seconds < static_cast<double>(std::numeric_limits<qint64>::min()) / 1000.0) {
        return false;
    }
    *milliseconds = qRound64(seconds * 1000.0);
    return true;
}

int parseInt(const QJsonValue& value) {
    bool ok = false;
    const int result = value.toString().toInt(&ok);
    return ok ? result : value.toInt();
}

bool checkedSubtract(const qint64 left, const qint64 right, qint64* result) {
    if (result == nullptr ||
        (right > 0 && left < std::numeric_limits<qint64>::min() + right) ||
        (right < 0 && left > std::numeric_limits<qint64>::max() + right)) {
        return false;
    }
    *result = left - right;
    return true;
}

bool checkedAdd(const qint64 left, const qint64 right, qint64* result) {
    if (result == nullptr ||
        (right > 0 && left > std::numeric_limits<qint64>::max() - right) ||
        (right < 0 && left < std::numeric_limits<qint64>::min() - right)) {
        return false;
    }
    *result = left + right;
    return true;
}
} // namespace

MediaMetadata MediaMetadata::fromFfprobeJson(const QJsonObject& root, QString* error) {
    MediaMetadata metadata;
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    int audioStreamIndex = 0;
    bool selectedAudioIsDefault = false;
    qint64 selectedAudioDurationMs = 0;
    qint64 selectedAudioStartMs = 0;
    bool selectedAudioStartKnown = false;
    qint64 earliestStreamStartMs = 0;
    bool earliestStreamStartKnown = false;
    for (const QJsonValue& entry : streams) {
        const QJsonObject stream = entry.toObject();
        const QString type = stream.value(QStringLiteral("codec_type")).toString();
        qint64 streamStartMs = 0;
        const bool streamStartKnown =
            parseMilliseconds(stream.value(QStringLiteral("start_time")), &streamStartMs);
        if ((type == QStringLiteral("audio") || type == QStringLiteral("video")) &&
            streamStartKnown &&
            (!earliestStreamStartKnown || streamStartMs < earliestStreamStartMs)) {
            earliestStreamStartMs = streamStartMs;
            earliestStreamStartKnown = true;
        }
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
                selectedAudioDurationMs = 0;
                (void)parseMilliseconds(stream.value(QStringLiteral("duration")),
                                        &selectedAudioDurationMs);
                selectedAudioStartMs = streamStartMs;
                selectedAudioStartKnown = streamStartKnown;
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
    qint64 formatStartMs = 0;
    bool formatStartKnown =
        parseMilliseconds(format.value(QStringLiteral("start_time")), &formatStartMs);
    if (!formatStartKnown && earliestStreamStartKnown) {
        formatStartMs = earliestStreamStartMs;
        formatStartKnown = true;
    }
    bool audioTimelineEndKnown = false;
    if (selectedAudioDurationMs > 0) {
        qint64 relativeAudioStartMs = 0;
        const bool relativeAudioStartKnown =
            !selectedAudioStartKnown || !formatStartKnown ||
            checkedSubtract(selectedAudioStartMs, formatStartMs, &relativeAudioStartMs);
        qint64 audioTimelineEndMs = 0;
        if (relativeAudioStartKnown &&
            checkedAdd(relativeAudioStartMs, selectedAudioDurationMs, &audioTimelineEndMs)) {
            metadata.durationMs = qMax<qint64>(0, audioTimelineEndMs);
            audioTimelineEndKnown = true;
        }
    }
    if (!audioTimelineEndKnown) {
        (void)parseMilliseconds(format.value(QStringLiteral("duration")), &metadata.durationMs);
    }
    if (!metadata.hasAudio && error != nullptr) {
        *error = QStringLiteral("The selected media does not contain an audio stream.");
    }
    return metadata;
}

} // namespace BreezeDesk
