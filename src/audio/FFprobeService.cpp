#include "breezedesk/audio/FFprobeService.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>

namespace BreezeDesk {

FFprobeService::FFprobeService(QString ffprobePath) : m_ffprobePath(std::move(ffprobePath)) {}

MediaMetadata FFprobeService::inspect(const QString& path, QString* error) const {
    return inspect(path, nullptr, error);
}

MediaMetadata FFprobeService::inspect(const QString& path, const std::atomic_bool* cancellation,
                                      QString* error) const {
    if (!QFileInfo(path).isFile()) {
        if (error != nullptr) {
            *error = QStringLiteral("Source media does not exist: %1").arg(path);
        }
        return {};
    }
    QProcess process;
    const QStringList arguments{
        QStringLiteral("-v"),        QStringLiteral("error"),        QStringLiteral("-protocol_whitelist"),
        QStringLiteral("file,pipe"), QStringLiteral("-show_format"), QStringLiteral("-show_streams"),
        QStringLiteral("-of"),       QStringLiteral("json"),         path};
    process.start(m_ffprobePath, arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000)) {
        process.kill();
        if (error != nullptr) {
            *error = QStringLiteral("ffprobe failed: %1").arg(process.errorString());
        }
        return {};
    }
    QElapsedTimer timeout;
    timeout.start();
    while (process.state() != QProcess::NotRunning && timeout.elapsed() < 30'000) {
        if (cancellation != nullptr && cancellation->load(std::memory_order_relaxed)) {
            process.kill();
            process.waitForFinished(1'000);
            if (error != nullptr) {
                *error = QStringLiteral("Media inspection was cancelled.");
            }
            return {};
        }
        process.waitForFinished(100);
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1'000);
        if (error != nullptr) {
            *error = QStringLiteral("ffprobe timed out: %1").arg(process.errorString());
        }
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error != nullptr) {
            *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        }
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("ffprobe returned malformed JSON: %1").arg(parseError.errorString());
        }
        return {};
    }
    MediaMetadata metadata = MediaMetadata::fromFfprobeJson(document.object(), error);
    metadata.sourcePath = QFileInfo(path).absoluteFilePath();
    return metadata;
}

} // namespace BreezeDesk
