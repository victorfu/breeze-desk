#include "breezedesk/core/ApplicationLogger.h"

#include "breezedesk/core/StoragePaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace BreezeDesk {
namespace {

constexpr qint64 MinimumLogFileBytes = 1024;
constexpr int MinimumMessageCharacters = 128;
constexpr int MaximumRetainedFiles = 20;

QMutex loggerMutex;
ApplicationLogger* activeLogger = nullptr;

[[nodiscard]] LogSeverity severityFor(const QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return LogSeverity::Debug;
    case QtInfoMsg:
        return LogSeverity::Info;
    case QtWarningMsg:
        return LogSeverity::Warning;
    case QtCriticalMsg:
    case QtFatalMsg:
        return LogSeverity::Critical;
    }
    return LogSeverity::Critical;
}

[[nodiscard]] int severityRank(const LogSeverity severity) {
    switch (severity) {
    case LogSeverity::Debug:
        return 0;
    case LogSeverity::Info:
        return 1;
    case LogSeverity::Warning:
        return 2;
    case LogSeverity::Critical:
        return 3;
    }
    return 3;
}

[[nodiscard]] QString severityName(const QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("debug");
    case QtInfoMsg:
        return QStringLiteral("info");
    case QtWarningMsg:
        return QStringLiteral("warning");
    case QtCriticalMsg:
        return QStringLiteral("critical");
    case QtFatalMsg:
        return QStringLiteral("fatal");
    }
    return QStringLiteral("critical");
}

[[nodiscard]] UserFacingError loggingError(const QString& message, const QString& details = {}) {
    return {ErrorDomain::Storage,
            ErrorCode::Unknown,
            QStringLiteral("Logging unavailable"),
            message,
            QStringLiteral("Verify that the application data directory is writable."),
            details,
            false,
            {}};
}

[[nodiscard]] QString safeProcessName(QString processName) {
    processName = QFileInfo(processName.trimmed()).fileName();
    static const QRegularExpression unsafeCharacters(QStringLiteral("[^A-Za-z0-9._-]"));
    processName.replace(unsafeCharacters, QStringLiteral("_"));
    while (processName.startsWith(QLatin1Char('.'))) {
        processName.remove(0, 1);
    }
    if (processName.isEmpty()) {
        return QStringLiteral("breezedesk");
    }
    return processName.left(64);
}

[[nodiscard]] QString replaceSensitiveFields(QString message) {
    static const QString keys =
        QStringLiteral("authorization|password|secret|session[_-]?token|auth[_-]?token|transcript|"
                       "original[_-]?text|edited[_-]?text|glossary|initial[_-]?prompt|meeting[_-]?context|"
                       "audio[_-]?(?:samples|content)|pcm[_-]?samples");
    static const QRegularExpression jsonField(
        QStringLiteral("\\\"(%1)\\\"\\s*:\\s*(?:\\\"(?:\\\\.|[^\\\"])*\\\"|[^,}\\r\\n]*)").arg(keys),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression keyValueField(
        QStringLiteral("\\b(%1)\\s*[:=]\\s*(?:\\\"(?:\\\\.|[^\\\"])*\\\"|'[^']*'|[^,;\\r\\n]*)").arg(keys),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression privateMarker(QStringLiteral("\\[(private|sensitive)\\].*?\\[/\\1\\]"),
                                                  QRegularExpression::CaseInsensitiveOption |
                                                      QRegularExpression::DotMatchesEverythingOption);

    message.replace(privateMarker, QStringLiteral("<redacted>"));
    message.replace(jsonField, QStringLiteral("\"\\1\":\"<redacted>\""));
    message.replace(keyValueField, QStringLiteral("\\1=<redacted>"));
    return message;
}

[[nodiscard]] QString replacePaths(QString message) {
    static const QRegularExpression quotedPath(
        QStringLiteral("([\\\"'])(?:file:/+|[A-Za-z]:[\\\\/]|/)[^\\\"'\\r\\n]+\\1"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression fileUrl(QStringLiteral("\\bfile:/+[^\\s,;)}\\]]+"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression windowsPath(
        QStringLiteral("(?:\\b[A-Za-z]:[\\\\/]|\\\\\\\\[^\\s\\\\/]+[\\\\/])[^\\r\\n,;)}\\]]+"));
    static const QRegularExpression unixPath(QStringLiteral("(?<![:/A-Za-z0-9_.-])/(?:[^\\r\\n,;)}\\]]+)"));

    message.replace(quotedPath, QStringLiteral("<path>"));
    message.replace(fileUrl, QStringLiteral("<path>"));
    message.replace(windowsPath, QStringLiteral("<path>"));
    message.replace(unixPath, QStringLiteral("<path>"));

    const QString home = QDir::homePath();
    if (!home.isEmpty()) {
        message.replace(home, QStringLiteral("<path>"), Qt::CaseSensitive);
    }
    const QString dataRoot = StoragePaths::root();
    if (!dataRoot.isEmpty()) {
        message.replace(dataRoot, QStringLiteral("<path>"), Qt::CaseSensitive);
    }
    return message;
}

} // namespace

class ApplicationLogger::Private final {
  public:
    explicit Private(LoggingConfiguration value) : configuration(std::move(value)) {}

    LoggingConfiguration configuration;
    QFile file;
    QString processName;
    QString activePath;
    QtMessageHandler previousHandler = nullptr;
    std::atomic_bool installed{false};
};

QString LogSanitizer::sanitize(QString message, const bool redactFilePaths) {
    message = replaceSensitiveFields(std::move(message));
    if (redactFilePaths) {
        message = replacePaths(std::move(message));
    }
    message.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    message.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return message;
}

ApplicationLogger::ApplicationLogger(LoggingConfiguration configuration)
    : d(std::make_unique<Private>(std::move(configuration))) {}

ApplicationLogger::~ApplicationLogger() {
    uninstall();
}

Result<void> ApplicationLogger::install() {
    QMutexLocker locker(&loggerMutex);
    if (d->installed) {
        return Result<void>::success();
    }
    if (activeLogger != nullptr) {
        return Result<void>::failure(
            loggingError(QStringLiteral("Another ApplicationLogger is already installed.")));
    }
    if (d->configuration.maximumFileBytes < MinimumLogFileBytes || d->configuration.retainedFileCount < 1 ||
        d->configuration.retainedFileCount > MaximumRetainedFiles ||
        d->configuration.maximumMessageCharacters < MinimumMessageCharacters) {
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument,
            QStringLiteral("The logging limits are outside the supported range.")));
    }

    const QString directory = d->configuration.logDirectory.trimmed().isEmpty()
                                  ? StoragePaths::logs()
                                  : QFileInfo(d->configuration.logDirectory).absoluteFilePath();
    if (!QDir().mkpath(directory)) {
        return Result<void>::failure(loggingError(QStringLiteral("The log directory could not be created.")));
    }

    d->processName = safeProcessName(d->configuration.processName);
    d->activePath = QDir(directory).filePath(d->processName + QStringLiteral(".log"));
    const QFileInfo activeInfo(d->activePath);
    if (activeInfo.isSymLink()) {
        return Result<void>::failure(loggingError(QStringLiteral("The active log path is a symbolic link.")));
    }

    const int firstDiscardedArchive = d->configuration.retainedFileCount;
    const QString archivePattern = d->processName + QStringLiteral(".*.log");
    const QRegularExpression archiveExpression(
        QStringLiteral("^%1\\.(\\d+)\\.log$").arg(QRegularExpression::escape(d->processName)));
    const QFileInfoList archives = QDir(directory).entryInfoList({archivePattern}, QDir::Files);
    for (const QFileInfo& archive : archives) {
        const QRegularExpressionMatch match = archiveExpression.match(archive.fileName());
        if (match.hasMatch() && match.captured(1).toInt() >= firstDiscardedArchive) {
            QFile::remove(archive.absoluteFilePath());
        }
    }

    d->file.setFileName(d->activePath);
    // Keep the file in binary mode so the encoded byte count used for rotation
    // is also the byte count written on Windows (where Text expands LF to CRLF).
    if (!d->file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return Result<void>::failure(
            loggingError(QStringLiteral("The active log file could not be opened."), d->file.errorString()));
    }
    d->file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    activeLogger = this;
    d->previousHandler = qInstallMessageHandler(&ApplicationLogger::messageHandler);
    d->installed = true;
    return Result<void>::success();
}

void ApplicationLogger::uninstall() {
    QMutexLocker locker(&loggerMutex);
    if (!d->installed) {
        return;
    }
    if (activeLogger == this) {
        qInstallMessageHandler(d->previousHandler);
        activeLogger = nullptr;
    }
    d->file.flush();
    d->file.close();
    d->installed = false;
}

void ApplicationLogger::flush() {
    QMutexLocker locker(&loggerMutex);
    if (d->installed) {
        d->file.flush();
    }
}

bool ApplicationLogger::isInstalled() const noexcept {
    return d->installed.load();
}

QString ApplicationLogger::activeLogPath() const {
    return d->activePath;
}

void ApplicationLogger::messageHandler(const QtMsgType type, const QMessageLogContext& context,
                                       const QString& message) {
    QByteArray formatted;
    bool mirror = false;
    {
        QMutexLocker locker(&loggerMutex);
        if (activeLogger != nullptr) {
            formatted = activeLogger->appendMessage(type, context, message);
            mirror = activeLogger->d->configuration.mirrorToStandardError;
        }
    }
    if (mirror && !formatted.isEmpty()) {
        static_cast<void>(
            std::fwrite(formatted.constData(), 1, static_cast<std::size_t>(formatted.size()), stderr));
        std::fflush(stderr);
    }
    if (type == QtFatalMsg) {
        std::abort();
    }
}

QByteArray ApplicationLogger::appendMessage(const QtMsgType type, const QMessageLogContext& context,
                                            const QString& message) {
    if (severityRank(severityFor(type)) < severityRank(d->configuration.minimumSeverity)) {
        return {};
    }

    QString safeMessage = message.left(d->configuration.maximumMessageCharacters);
    if (message.size() > safeMessage.size()) {
        safeMessage += QStringLiteral("…<truncated>");
    }
    safeMessage = LogSanitizer::sanitize(std::move(safeMessage), d->configuration.redactFilePaths);
    const QString category = context.category == nullptr || context.category[0] == '\0'
                                 ? QStringLiteral("default")
                                 : QString::fromUtf8(context.category);
    const QString safeCategory = LogSanitizer::sanitize(category, true).left(128);
    const QString prefix =
        QStringLiteral("%1 [%2:%3] [%4] [%5] ")
            .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), d->processName,
                 QString::number(QCoreApplication::applicationPid()), severityName(type), safeCategory);
    const auto encodeLine = [&prefix](const QString& body) {
        return (prefix + body + QLatin1Char('\n')).toUtf8();
    };
    QByteArray encoded = encodeLine(safeMessage);
    if (encoded.size() > d->configuration.maximumFileBytes) {
        const QString marker = QStringLiteral("…<truncated>");
        qsizetype lower = 0;
        qsizetype upper = safeMessage.size();
        qsizetype best = 0;
        while (lower <= upper) {
            const qsizetype midpoint = lower + ((upper - lower) / 2);
            const QByteArray candidate = encodeLine(safeMessage.left(midpoint) + marker);
            if (candidate.size() <= d->configuration.maximumFileBytes) {
                best = midpoint;
                lower = midpoint + 1;
            } else {
                upper = midpoint - 1;
            }
        }
        encoded = encodeLine(safeMessage.left(best) + marker);
    }

    if (d->file.size() > 0 && d->file.size() + encoded.size() > d->configuration.maximumFileBytes) {
        d->file.flush();
        d->file.close();
        const int lastArchive = d->configuration.retainedFileCount - 1;
        if (lastArchive > 0) {
            QFile::remove(QDir(QFileInfo(d->activePath).absolutePath())
                              .filePath(d->processName + QStringLiteral(".%1.log").arg(lastArchive)));
            for (int index = lastArchive - 1; index >= 1; --index) {
                const QString source = QDir(QFileInfo(d->activePath).absolutePath())
                                           .filePath(d->processName + QStringLiteral(".%1.log").arg(index));
                const QString destination =
                    QDir(QFileInfo(d->activePath).absolutePath())
                        .filePath(d->processName + QStringLiteral(".%1.log").arg(index + 1));
                if (QFileInfo::exists(source)) {
                    QFile::rename(source, destination);
                }
            }
            QFile::rename(d->activePath, QDir(QFileInfo(d->activePath).absolutePath())
                                             .filePath(d->processName + QStringLiteral(".1.log")));
        } else {
            QFile::remove(d->activePath);
        }
        d->file.setFileName(d->activePath);
        if (!d->file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            return encoded;
        }
        d->file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    if (d->file.write(encoded) != encoded.size()) {
        return encoded;
    }
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        d->file.flush();
    }
    return encoded;
}

} // namespace BreezeDesk
