#pragma once

#include "breezedesk/core/Result.h"

namespace BreezeDesk {

class IJobRepository;

class JobRecoveryService final {
  public:
    explicit JobRecoveryService(IJobRepository& repository);
    [[nodiscard]] Result<int> recoverAfterAbnormalShutdown();

  private:
    IJobRepository& m_repository;
};

} // namespace BreezeDesk
