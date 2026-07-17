#include "breezedesk/platform/IPlatformService.h"

#include "breezedesk/app_config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>

namespace BreezeDesk {

class MacPlatformService final : public IPlatformService {
  public:
    PlatformCapabilities capabilities() const override { return PlatformCapabilities::current(); }

    bool revealInFileManager(const QString& path, QString* error) const override {
        const QString absolutePath = QFileInfo(path).absoluteFilePath();
        const bool started =
            QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), absolutePath});
        if (!started && error != nullptr) {
            *error = QStringLiteral("Finder could not reveal the selected file.");
        }
        return started;
    }

    bool setLaunchAtLogin(bool enabled, QString* error) override {
        if (@available(macOS 13.0, *)) {
            SMAppService* service = [SMAppService mainAppService];
            NSError* nativeError = nil;
            const BOOL success = enabled ? [service registerAndReturnError:&nativeError]
                                         : [service unregisterAndReturnError:&nativeError];
            if (!success && error != nullptr) {
                *error = QString::fromNSString(nativeError.localizedDescription);
            }
            return success;
        }
        if (error != nullptr) {
            *error = QStringLiteral("Launch at login requires macOS 13 or later.");
        }
        return false;
    }

    bool launchAtLogin(QString* error) const override {
        Q_UNUSED(error)
        if (@available(macOS 13.0, *)) {
            const SMAppServiceStatus status = [SMAppService mainAppService].status;
            return status == SMAppServiceStatusEnabled;
        }
        return false;
    }

    bool requestMicrophonePermission(QString* error) override {
        const AVAuthorizationStatus status =
            [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
        if (status == AVAuthorizationStatusAuthorized) {
            return true;
        }
        if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) {
            if (error != nullptr) {
                *error = QStringLiteral("Microphone access is disabled. Enable %1 in System Settings "
                                        "> Privacy & Security > Microphone.")
                             .arg(QString::fromLatin1(AppConfig::ProductName));
            }
            return false;
        }
        __block BOOL granted = NO;
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL allowed) {
                                   granted = allowed;
                                   dispatch_semaphore_signal(semaphore);
                                 }];
        dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 30LL * NSEC_PER_SEC));
        if (!granted && error != nullptr) {
            *error = QStringLiteral("Microphone permission was not granted.");
        }
        return granted;
    }

    QString installSource() const override {
        const QString path = QCoreApplication::applicationDirPath();
        if (path.contains(QStringLiteral("/Applications/"))) {
            return QStringLiteral("direct");
        }
        if (path.contains(QStringLiteral("/Cellar/")) || path.contains(QStringLiteral("/homebrew/"))) {
            return QStringLiteral("homebrew");
        }
        return QStringLiteral("development");
    }

    QString gpuDescription() const override {
        QProcess process;
        process.start(
            QStringLiteral("/usr/sbin/system_profiler"),
            {QStringLiteral("SPDisplaysDataType"), QStringLiteral("-detailLevel"), QStringLiteral("mini")},
            QIODevice::ReadOnly);
        if (!process.waitForFinished(5000)) {
            process.kill();
            return QStringLiteral("Apple GPU");
        }
        const QString output = QString::fromUtf8(process.readAllStandardOutput());
        for (const QString& line : output.split(QLatin1Char('\n'))) {
            if (line.contains(QStringLiteral("Chipset Model:"))) {
                return line.section(QLatin1Char(':'), 1).trimmed();
            }
        }
        return QStringLiteral("Apple GPU");
    }

    void activateApplication() override { [NSApp activateIgnoringOtherApps:YES]; }
};

IPlatformService* createPlatformService() {
    return new MacPlatformService;
}

} // namespace BreezeDesk
