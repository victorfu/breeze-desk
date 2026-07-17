#include "breezedesk/transcript/TranscriptAutosave.h"

#include "breezedesk/transcript/ITranscriptRepository.h"

namespace BreezeDesk {

TranscriptAutosave::TranscriptAutosave(ITranscriptRepository& repository, const int delayMs)
    : m_repository(repository) {
    m_timer.setSingleShot(true);
    m_timer.setInterval(qMax(0, delayMs));
    QObject::connect(&m_timer, &QTimer::timeout, [&]() {
        const auto result = flush();
        if (!result)
            m_lastError = result.error();
    });
}

TranscriptAutosave::~TranscriptAutosave() {
    if (!m_pending.isEmpty())
        (void)flush();
}

void TranscriptAutosave::schedule(TranscriptSegment segment) {
    if (!segment.id.isEmpty()) {
        m_pending.insert(segment.id, std::move(segment));
        m_timer.start();
    }
}

Result<void> TranscriptAutosave::flush() {
    m_timer.stop();
    const auto pending = m_pending;
    for (auto iterator = pending.cbegin(); iterator != pending.cend(); ++iterator) {
        auto result = m_repository.saveEditedSegment(iterator.value());
        if (!result)
            return result;
        m_pending.remove(iterator.key());
    }
    m_lastError = {};
    return Result<void>::success();
}

} // namespace BreezeDesk
