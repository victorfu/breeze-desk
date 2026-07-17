#include "breezedesk/update/UpdateCoordinator.h"

#include "UpdateServiceFactory.h"
#include "breezedesk/platform/IPlatformService.h"
#include "breezedesk/update/NullUpdateService.h"
#include "breezedesk/update_config.h"

namespace BreezeDesk {

UpdateCoordinator::UpdateCoordinator(IPlatformService* platform, QObject* parent) : QObject(parent) {
    const bool directInstall = platform != nullptr && platform->installSource() == QStringLiteral("direct");
#if BREEZEDESK_ENABLE_UPDATES
    if (directInstall && QString::fromUtf8(BREEZEDESK_APPCAST_URL).startsWith(QStringLiteral("https://"))) {
        m_service = createNativeUpdateService(this);
    }
#else
    Q_UNUSED(directInstall)
#endif
    if (!m_service) {
        m_service = std::make_unique<NullUpdateService>(this);
    }
    connect(m_service.get(), &IUpdateService::updateAvailable, this, &UpdateCoordinator::updateAvailable);
    connect(m_service.get(), &IUpdateService::noUpdateAvailable, this, &UpdateCoordinator::noUpdateAvailable);
    connect(m_service.get(), &IUpdateService::updateError, this, &UpdateCoordinator::error);
}

bool UpdateCoordinator::isAvailable() const {
    return m_service->isAvailable();
}
void UpdateCoordinator::checkForUpdates(bool userInitiated) {
    m_service->checkForUpdates(userInitiated);
}

} // namespace BreezeDesk
