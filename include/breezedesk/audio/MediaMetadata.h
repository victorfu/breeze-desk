#pragma once

#include <QJsonObject>
#include <QString>

namespace BreezeDesk {

struct MediaMetadata {
    QString sourcePath;
    QString formatName;
    QString codecName;
    qint64 durationMs = 0;
    int sampleRate = 0;
    int channelCount = 0;
    qint64 bitRate = 0;
    bool hasAudio = false;
    bool hasVideo = false;

    [[nodiscard]] static MediaMetadata fromFfprobeJson(const QJsonObject& root, QString* error = nullptr);
};

} // namespace BreezeDesk
