#include "breezedesk/audio/FFmpegLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace BreezeDesk {
namespace {
QString locateTool(const QString& applicationDirectory, const QString& baseName) {
    const QStringList candidateNames{baseName, baseName + QStringLiteral(".exe")};
    for (const QString& candidateName : candidateNames) {
        const QString bundledPath = QDir(applicationDirectory).filePath(candidateName);
        if (QFileInfo(bundledPath).isFile())
            return bundledPath;
    }
    for (const QString& candidateName : candidateNames) {
        const QString path = QStandardPaths::findExecutable(candidateName);
        if (!path.isEmpty())
            return path;
    }
    return {};
}
} // namespace

FFmpegLocator::Tools FFmpegLocator::locate() {
    Tools result;
    const QString appDirectory = QCoreApplication::applicationDirPath();
    result.ffmpegPath = locateTool(appDirectory, QStringLiteral("ffmpeg"));
    result.ffprobePath = locateTool(appDirectory, QStringLiteral("ffprobe"));
    if (!result.isValid()) {
        result.error = QStringLiteral(
            "ffmpeg and ffprobe were not found. Install the packaged media tools or set PATH for a "
            "development build.");
    }
    return result;
}

QString FFmpegLocator::version(const QString& executable, QString* error) {
    QProcess process;
    process.start(executable, {QStringLiteral("-version")}, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000) || !process.waitForFinished(5000)) {
        process.kill();
        if (error != nullptr) {
            *error = process.errorString();
        }
        return {};
    }
    const QString firstLine =
        QString::fromUtf8(process.readAllStandardOutput()).section(QLatin1Char('\n'), 0, 0).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error != nullptr) {
            *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        }
        return {};
    }
    return firstLine;
}

} // namespace BreezeDesk
