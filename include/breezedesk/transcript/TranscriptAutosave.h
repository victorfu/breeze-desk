#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/transcript/TranscriptSegment.h"

#include <QTimer>

namespace BreezeDesk {

class ITranscriptRepository;

class TranscriptAutosave final {
  public:
    explicit TranscriptAutosave(ITranscriptRepository& repository, int delayMs = 500);
    ~TranscriptAutosave();

    void schedule(TranscriptSegment segment);
    [[nodiscard]] Result<void> flush();
    [[nodiscard]] bool hasPendingChanges() const noexcept { return !m_pending.isEmpty(); }

  private:
    ITranscriptRepository& m_repository;
    QTimer m_timer;
    QHash<QString, TranscriptSegment> m_pending;
    UserFacingError m_lastError;
};

} // namespace BreezeDesk
