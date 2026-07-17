#include "breezedesk/platform/IPlatformService.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

namespace BreezeDesk {

class GenericPlatformService final : public IPlatformService {
  public:
    PlatformCapabilities capabilities() const override { return PlatformCapabilities::current(); }
    bool revealInFileManager(const QString& path, QString* error) const override {
        const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        if (!opened && error != nullptr) {
            *error = QStringLiteral("The file manager could not be opened.");
        }
        return opened;
    }
    bool setLaunchAtLogin(bool, QString* error) override {
        if (error != nullptr) {
            *error = QStringLiteral("Launch at login is not supported on this platform.");
        }
        return false;
    }
    bool launchAtLogin(QString*) const override { return false; }
    bool requestMicrophonePermission(QString*) override { return true; }
    QString installSource() const override { return QStringLiteral("development"); }
    QString gpuDescription() const override { return QStringLiteral("Unknown GPU"); }
    void activateApplication() override {}
};

IPlatformService* createPlatformService() {
    return new GenericPlatformService;
}

} // namespace BreezeDesk
