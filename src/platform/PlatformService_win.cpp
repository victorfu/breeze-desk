#include "breezedesk/app_config.h"
#include "breezedesk/platform/IPlatformService.h"
#include "breezedesk/platform/InstallSourceClassifier.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>

#include <windows.h>

namespace BreezeDesk {

class WindowsPlatformService final : public IPlatformService {
  public:
    PlatformCapabilities capabilities() const override { return PlatformCapabilities::current(); }

    bool revealInFileManager(const QString& path, QString* error) const override {
        const QString nativePath = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
        const bool started =
            QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), nativePath});
        if (!started && error != nullptr) {
            *error = QStringLiteral("Explorer could not reveal the selected file.");
        }
        return started;
    }

    bool setLaunchAtLogin(bool enabled, QString* error) override {
        QSettings registry(
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
            QSettings::NativeFormat);
        const QString key = QString::fromLatin1(AppConfig::ProductName);
        if (enabled) {
            registry.setValue(key, QStringLiteral("\"") +
                                       QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) +
                                       QStringLiteral("\" --background"));
        } else {
            registry.remove(key);
        }
        registry.sync();
        if (registry.status() != QSettings::NoError && error != nullptr) {
            *error = QStringLiteral("The Windows startup entry could not be updated.");
        }
        return registry.status() == QSettings::NoError;
    }

    bool launchAtLogin(QString* error) const override {
        Q_UNUSED(error)
        QSettings registry(
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
            QSettings::NativeFormat);
        return registry.contains(QString::fromLatin1(AppConfig::ProductName));
    }

    bool requestMicrophonePermission(QString* error) override {
        Q_UNUSED(error)
        return true; // Qt Multimedia reports the actionable device-level permission error.
    }

    QString installSource() const override {
        QSettings installation(QStringLiteral("HKEY_CURRENT_USER\\Software\\%1")
                                   .arg(QString::fromLatin1(AppConfig::WindowsProductId)),
                               QSettings::NativeFormat);
        return classifyWindowsInstallSource(QCoreApplication::applicationDirPath(),
                                            installation.value(QStringLiteral("InstallLocation")).toString());
    }

    QString gpuDescription() const override {
        QProcess process;
        process.start(
            QStringLiteral("powershell.exe"),
            {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
             QStringLiteral(
                 "(Get-CimInstance Win32_VideoController | Select-Object -First 1 -ExpandProperty Name)")},
            QIODevice::ReadOnly);
        if (!process.waitForFinished(5000)) {
            process.kill();
            return QStringLiteral("Unknown GPU");
        }
        const QString name = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        return name.isEmpty() ? QStringLiteral("Unknown GPU") : name;
    }

    void activateApplication() override {
        const HWND window = GetActiveWindow();
        if (window != nullptr) {
            ShowWindow(window, SW_RESTORE);
            SetForegroundWindow(window);
        }
    }
};

IPlatformService* createPlatformService() {
    return new WindowsPlatformService;
}

} // namespace BreezeDesk
