#pragma once

#include <atomic>

namespace BreezeDesk::Asr {

class CancellationFlag final {
  public:
    void request() noexcept { m_requested.store(true, std::memory_order_relaxed); }
    void reset() noexcept { m_requested.store(false, std::memory_order_relaxed); }
    [[nodiscard]] bool isRequested() const noexcept { return m_requested.load(std::memory_order_relaxed); }

  private:
    std::atomic_bool m_requested = false;
};

} // namespace BreezeDesk::Asr
