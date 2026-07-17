#include "breezedesk/audio/FFmpegNormalizationService.h"

#include "breezedesk/audio/NormalizedAudioValidator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStorageInfo>
#include <QTimer>
#include <QUuid>

namespace BreezeDesk {

namespace {
constexpr qint64 BytesPerSecond = 16000LL * 2LL;
constexpr qint64 DiskSafetyMargin = 256LL * 1024LL * 1024LL;

class FFmpegNormalizationOperation final : public NormalizationOperation {
  public:
    FFmpegNormalizationOperation(QString executable, QString source, QString output, qint64 duration,
                                 QObject* parent)
        : NormalizationOperation(parent), m_outputPath(std::move(output)), m_durationMs(duration) {
        const QFileInfo outputInfo(m_outputPath);
        QDir().mkpath(outputInfo.absolutePath());
        const QStorageInfo storage(outputInfo.absolutePath());
        const qint64 expectedBytes = (qMax<qint64>(duration, 1000) * BytesPerSecond) / 1000;
        if (storage.isValid() && storage.bytesAvailable() < expectedBytes + DiskSafetyMargin) {
            setError(QStringLiteral("Insufficient disk space for normalized audio."));
            QTimer::singleShot(0, this, [this] { emit finished(false, {}); });
            return;
        }

        m_temporaryPath =
            m_outputPath + QStringLiteral(".tmp.") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::SeparateChannels);
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] { consumeProgress(); });
        connect(m_process, &QProcess::readyReadStandardError, this, [this] {
            m_diagnostics += m_process->readAllStandardError();
            constexpr qsizetype MaximumDiagnostics = 32 * 1024;
            if (m_diagnostics.size() > MaximumDiagnostics) {
                m_diagnostics = m_diagnostics.right(MaximumDiagnostics);
            }
        });
        connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this](int exitCode, QProcess::ExitStatus status) { complete(exitCode, status); });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            if (m_process->state() == QProcess::NotRunning && isRunning()) {
                complete(-1, QProcess::CrashExit);
            }
        });
        setRunning(true);
        const QStringList arguments{QStringLiteral("-hide_banner"),
                                    QStringLiteral("-nostdin"),
                                    QStringLiteral("-y"),
                                    QStringLiteral("-i"),
                                    std::move(source),
                                    QStringLiteral("-vn"),
                                    QStringLiteral("-ac"),
                                    QStringLiteral("1"),
                                    QStringLiteral("-ar"),
                                    QStringLiteral("16000"),
                                    QStringLiteral("-c:a"),
                                    QStringLiteral("pcm_s16le"),
                                    QStringLiteral("-f"),
                                    QStringLiteral("wav"),
                                    QStringLiteral("-progress"),
                                    QStringLiteral("pipe:1"),
                                    QStringLiteral("-loglevel"),
                                    QStringLiteral("warning"),
                                    m_temporaryPath};
        m_process->start(std::move(executable), arguments, QIODevice::ReadOnly);
    }

    ~FFmpegNormalizationOperation() override {
        if (!m_temporaryPath.isEmpty()) {
            QFile::remove(m_temporaryPath);
        }
    }

    void cancel() override {
        if (!isRunning() || m_cancelled) {
            return;
        }
        m_cancelled = true;
        m_process->terminate();
        QTimer::singleShot(2000, m_process, [process = m_process] {
            if (process->state() != QProcess::NotRunning) {
                process->kill();
            }
        });
    }

  private:
    void consumeProgress() {
        m_stdoutBuffer += m_process->readAllStandardOutput();
        qsizetype newline = -1;
        while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
            m_stdoutBuffer.remove(0, newline + 1);
            if (line.startsWith("out_time_ms=")) {
                bool ok = false;
                const qint64 microseconds = line.mid(12).toLongLong(&ok);
                if (ok && m_durationMs > 0) {
                    setProgress(static_cast<double>(microseconds / 1000) / static_cast<double>(m_durationMs));
                }
            }
        }
    }

    void complete(int exitCode, QProcess::ExitStatus status) {
        if (!isRunning()) {
            return;
        }
        setRunning(false);
        if (m_cancelled) {
            setError(QStringLiteral("Audio normalization was cancelled."));
            QFile::remove(m_temporaryPath);
            emit finished(false, {});
            return;
        }
        const QFileInfo outputInfo(m_temporaryPath);
        if (status != QProcess::NormalExit || exitCode != 0 || !outputInfo.isFile()) {
            setError(QStringLiteral("ffmpeg could not normalize the media: %1")
                         .arg(QString::fromUtf8(m_diagnostics).trimmed()));
            QFile::remove(m_temporaryPath);
            emit finished(false, {});
            return;
        }
        QString validationError;
        if (!NormalizedAudioValidator::validate(m_temporaryPath, m_durationMs, nullptr, &validationError)) {
            setError(QStringLiteral("ffmpeg produced invalid normalized audio: %1").arg(validationError));
            QFile::remove(m_temporaryPath);
            emit finished(false, {});
            return;
        }
        if (QFile::exists(m_outputPath) && !QFile::remove(m_outputPath)) {
            setError(QStringLiteral("The existing normalized audio could not be replaced."));
            emit finished(false, {});
            return;
        }
        if (!QFile::rename(m_temporaryPath, m_outputPath)) {
            setError(QStringLiteral("The normalized audio could not be committed atomically."));
            emit finished(false, {});
            return;
        }
        m_temporaryPath.clear();
        setProgress(1.0);
        emit finished(true, m_outputPath);
    }

    QProcess* m_process = nullptr;
    QString m_outputPath;
    QString m_temporaryPath;
    qint64 m_durationMs = 0;
    QByteArray m_stdoutBuffer;
    QByteArray m_diagnostics;
    bool m_cancelled = false;
};
} // namespace

FFmpegNormalizationService::FFmpegNormalizationService(QString ffmpegPath, QObject* parent)
    : QObject(parent), m_ffmpegPath(std::move(ffmpegPath)) {}

NormalizationOperation* FFmpegNormalizationService::normalize(const QString& sourcePath,
                                                              const QString& outputPath, qint64 durationMs,
                                                              QObject* parent) {
    return new FFmpegNormalizationOperation(m_ffmpegPath, sourcePath, outputPath, durationMs, parent);
}

} // namespace BreezeDesk
