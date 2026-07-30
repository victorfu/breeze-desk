#pragma once

#include <breezedesk/asr/CancellationFlag.h>

#include <QString>

#include <memory>

namespace BreezeDesk::Asr {

class WorkerOperationState final {
  public:
    [[nodiscard]] bool claim(const QString& jobId) {
        if (active()) {
            return false;
        }
        m_jobId = jobId;
        m_cancellation = std::make_shared<CancellationFlag>();
        return true;
    }

    [[nodiscard]] bool cancel(const QString& jobId = {}) {
        if (!active() || (!jobId.isEmpty() && jobId != m_jobId)) {
            return false;
        }
        m_cancellation->request();
        return true;
    }

    void finish() {
        m_jobId.clear();
        m_cancellation.reset();
    }

    [[nodiscard]] bool active() const noexcept { return m_cancellation != nullptr; }
    [[nodiscard]] const QString& jobId() const noexcept { return m_jobId; }
    [[nodiscard]] std::shared_ptr<const CancellationFlag> cancellation() const noexcept {
        return m_cancellation;
    }

  private:
    QString m_jobId;
    std::shared_ptr<CancellationFlag> m_cancellation;
};

} // namespace BreezeDesk::Asr
