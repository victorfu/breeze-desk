#pragma once

#include "breezedesk/update/IUpdateService.h"

namespace BreezeDesk {

class NullUpdateService final : public IUpdateService {
    Q_OBJECT
  public:
    using IUpdateService::IUpdateService;
    [[nodiscard]] bool isAvailable() const override;
    void checkForUpdates(bool userInitiated) override;
};

} // namespace BreezeDesk
