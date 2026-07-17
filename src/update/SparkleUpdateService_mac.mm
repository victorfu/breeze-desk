#include "UpdateServiceFactory.h"

#include "breezedesk/app_config.h"
#include "breezedesk/update_config.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <objc/runtime.h>

namespace BreezeDesk {
namespace {

template <typename Return, typename... Arguments>
Return sendMessage(id receiver, SEL selector, Arguments... arguments) {
    using Function = Return (*)(id, SEL, Arguments...);
    return reinterpret_cast<Function>(objc_msgSend)(receiver, selector, arguments...);
}

QString bundledFrameworkPath() {
    const QString overridePath = qEnvironmentVariable("BREEZEDESK_SPARKLE_FRAMEWORK_PATH");
    if (!overridePath.trimmed().isEmpty()) {
        return QFileInfo(overridePath).absoluteFilePath();
    }

    NSString* privateFrameworksPath = [[NSBundle mainBundle] privateFrameworksPath];
    if (privateFrameworksPath == nil) {
        return {};
    }
    return QDir(QString::fromNSString(privateFrameworksPath)).filePath(QStringLiteral("Sparkle.framework"));
}

QString descriptionForError(NSError* error) {
    if (error == nil) {
        return QStringLiteral("Unknown framework loading error");
    }
    return QString::fromNSString(error.localizedDescription);
}

} // namespace

class SparkleUpdateService final : public IUpdateService {
  public:
    explicit SparkleUpdateService(QObject* parent) : IUpdateService(parent) { initialize(); }

    ~SparkleUpdateService() override {
        if (m_controller != nil) {
            sendMessage<void>(m_controller, sel_registerName("release"));
            m_controller = nil;
        }
        if (m_framework != nil) {
            [m_framework unload];
            [m_framework release];
            m_framework = nil;
        }
    }

    [[nodiscard]] bool isAvailable() const override { return m_controller != nil; }

    void checkForUpdates(bool userInitiated) override {
        if (QThread::currentThread() != thread()) {
            QMetaObject::invokeMethod(
                this, [this, userInitiated] { checkForUpdates(userInitiated); }, Qt::QueuedConnection);
            return;
        }
        if (m_controller == nil) {
            emit updateError(m_error.isEmpty() ? QStringLiteral("Sparkle.framework is unavailable.")
                                               : m_error);
            return;
        }

        if (userInitiated) {
            sendMessage<void>(m_controller, sel_registerName("checkForUpdates:"), nil);
            return;
        }

        id updater = sendMessage<id>(m_controller, sel_registerName("updater"));
        if (updater == nil || !sendMessage<BOOL>(updater, sel_registerName("respondsToSelector:"),
                                                 sel_registerName("checkForUpdatesInBackground"))) {
            emit updateError(
                QStringLiteral("The installed Sparkle framework is incompatible with this %1 build.")
                    .arg(QString::fromLatin1(AppConfig::ProductName)));
            return;
        }
        sendMessage<void>(updater, sel_registerName("checkForUpdatesInBackground"));
    }

  private:
    void initialize() {
        const QString appcastUrl = QString::fromUtf8(BREEZEDESK_APPCAST_URL).trimmed();
        const QString publicKey = QString::fromUtf8(BREEZEDESK_EDDSA_PUBLIC_KEY).trimmed();
        if (!appcastUrl.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive) || publicKey.isEmpty()) {
            m_error = QStringLiteral("Sparkle updates require an HTTPS appcast and an EdDSA public "
                                     "key configured at build time.");
            return;
        }

        const QString frameworkPath = bundledFrameworkPath();
        if (!QFileInfo(frameworkPath).isDir()) {
            m_error = QStringLiteral("Sparkle.framework is not installed in this application bundle.");
            return;
        }

        m_framework = [[NSBundle alloc] initWithPath:frameworkPath.toNSString()];
        NSError* loadError = nil;
        if (m_framework == nil || ![m_framework loadAndReturnError:&loadError]) {
            m_error = QStringLiteral("Sparkle.framework could not be loaded: %1")
                          .arg(descriptionForError(loadError));
            if (m_framework != nil) {
                [m_framework release];
                m_framework = nil;
            }
            return;
        }

        Class controllerClass = NSClassFromString(@"SPUStandardUpdaterController");
        const SEL initializer =
            sel_registerName("initWithStartingUpdater:updaterDelegate:userDriverDelegate:");
        if (controllerClass == Nil ||
            !sendMessage<BOOL>(controllerClass, sel_registerName("instancesRespondToSelector:"),
                               initializer)) {
            m_error = QStringLiteral("The installed Sparkle framework does not expose the Sparkle "
                                     "2 standard updater API.");
            return;
        }

        id allocated = sendMessage<id>(controllerClass, sel_registerName("alloc"));
        m_controller = sendMessage<id>(allocated, initializer, YES, nil, nil);
        if (m_controller == nil) {
            m_error = QStringLiteral("Sparkle could not create its standard updater controller.");
            return;
        }

        const SEL manualCheck = sel_registerName("checkForUpdates:");
        const SEL updaterSelector = sel_registerName("updater");
        if (!sendMessage<BOOL>(m_controller, sel_registerName("respondsToSelector:"), manualCheck) ||
            !sendMessage<BOOL>(m_controller, sel_registerName("respondsToSelector:"), updaterSelector)) {
            sendMessage<void>(m_controller, sel_registerName("release"));
            m_controller = nil;
            m_error = QStringLiteral("The installed Sparkle framework is incompatible with this %1 build.")
                          .arg(QString::fromLatin1(AppConfig::ProductName));
        }
    }

    NSBundle* m_framework = nil;
    id m_controller = nil;
    QString m_error;
};

std::unique_ptr<IUpdateService> createNativeUpdateService(QObject* parent) {
    return std::make_unique<SparkleUpdateService>(parent);
}

} // namespace BreezeDesk
