#pragma once

#include "breezedesk/core/Result.h"

#include <QString>
#include <QtLogging>

#include <memory>

namespace BreezeDesk {

enum class LogSeverity {
    Debug,
    Info,
    Warning,
    Critical,
};

struct LoggingConfiguration {
    QString processName;
    QString logDirectory;
    qint64 maximumFileBytes = 5 * 1024 * 1024;
    int retainedFileCount = 5;
    int maximumMessageCharacters = 16 * 1024;
    bool redactFilePaths = true;
    bool mirrorToStandardError = true;
#if defined(QT_NO_DEBUG)
    LogSeverity minimumSeverity = LogSeverity::Info;
#else
    LogSeverity minimumSeverity = LogSeverity::Debug;
#endif
};

class LogSanitizer final {
  public:
    [[nodiscard]] static QString sanitize(QString message, bool redactFilePaths = true);

  private:
    LogSanitizer() = delete;
};

class ApplicationLogger final {
  public:
    explicit ApplicationLogger(LoggingConfiguration configuration);
    ~ApplicationLogger();

    ApplicationLogger(const ApplicationLogger&) = delete;
    ApplicationLogger& operator=(const ApplicationLogger&) = delete;
    ApplicationLogger(ApplicationLogger&&) = delete;
    ApplicationLogger& operator=(ApplicationLogger&&) = delete;

    [[nodiscard]] Result<void> install();
    void uninstall();
    void flush();

    [[nodiscard]] bool isInstalled() const noexcept;
    [[nodiscard]] QString activeLogPath() const;

  private:
    class Private;

    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);
    [[nodiscard]] QByteArray appendMessage(QtMsgType type, const QMessageLogContext& context,
                                           const QString& message);

    std::unique_ptr<Private> d;
};

} // namespace BreezeDesk
