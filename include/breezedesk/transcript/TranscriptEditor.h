#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/transcript/TranscriptSegment.h"

#include <QList>

namespace BreezeDesk {

class TranscriptEditor final {
  public:
    void setSegments(QList<TranscriptSegment> segments);
    [[nodiscard]] const QList<TranscriptSegment>& segments() const noexcept { return m_segments; }
    [[nodiscard]] bool canUndo() const noexcept { return !m_undo.isEmpty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !m_redo.isEmpty(); }

    [[nodiscard]] Result<void> editText(int index, const QString& text);
    [[nodiscard]] Result<void> split(int index, qint64 splitMs, int textOffset);
    [[nodiscard]] Result<void> mergeWithPrevious(int index);
    [[nodiscard]] Result<void> mergeWithNext(int index);
    [[nodiscard]] Result<void> insert(int index, TranscriptSegment segment);
    [[nodiscard]] Result<void> remove(int index);
    [[nodiscard]] Result<void> setTimeRange(int index, qint64 startMs, qint64 endMs);
    [[nodiscard]] Result<void> undo();
    [[nodiscard]] Result<void> redo();
    [[nodiscard]] Result<void> validate() const;

  private:
    void beginChange();
    void normalizeOrdinals();
    [[nodiscard]] Result<void> validateCandidate(const QList<TranscriptSegment>& candidate) const;

    QList<TranscriptSegment> m_segments;
    QList<QList<TranscriptSegment>> m_undo;
    QList<QList<TranscriptSegment>> m_redo;
    static constexpr int MaximumUndoSteps = 200;
};

} // namespace BreezeDesk
