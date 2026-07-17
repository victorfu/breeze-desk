#pragma once

#include "breezedesk/update/IUpdateService.h"

#include <memory>

namespace BreezeDesk {

class IPlatformService;

class UpdateCoordinator final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ isAvailable NOTIFY availabilityChanged)

  public:
    explicit UpdateCoordinator(IPlatformService* platform, QObject* parent = nullptr);
    [[nodiscard]] bool isAvailable() const;
    Q_INVOKABLE void checkForUpdates(bool userInitiated = true);

  signals:
    void availabilityChanged();
    void updateAvailable(const QString& version, const QString& releaseNotes);
    void noUpdateAvailable();
    void error(const QString& message);

  private:
    std::unique_ptr<IUpdateService> m_service;
};

} // namespace BreezeDesk
