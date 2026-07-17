#include "breezedesk/transcript/TranscriptEditor.h"

#include <QUuid>

namespace BreezeDesk {
namespace {
QString joinText(const QString& left, const QString& right) {
    if (left.isEmpty())
        return right;
    if (right.isEmpty())
        return left;
    const QChar last = left.back();
    const QChar first = right.front();
    const bool space = last.script() == QChar::Script_Latin && first.script() == QChar::Script_Latin &&
                       !last.isSpace() && !first.isSpace();
    return left + (space ? QStringLiteral(" ") : QString()) + right;
}
} // namespace

void TranscriptEditor::setSegments(QList<TranscriptSegment> segments) {
    m_segments = std::move(segments);
    normalizeOrdinals();
    m_undo.clear();
    m_redo.clear();
}

void TranscriptEditor::beginChange() {
    m_undo.append(m_segments);
    if (m_undo.size() > MaximumUndoSteps)
        m_undo.removeFirst();
    m_redo.clear();
}

void TranscriptEditor::normalizeOrdinals() {
    for (int i = 0; i < m_segments.size(); ++i)
        m_segments[i].ordinal = i;
}

Result<void> TranscriptEditor::validateCandidate(const QList<TranscriptSegment>& candidate) const {
    qint64 previousEnd = -1;
    for (int i = 0; i < candidate.size(); ++i) {
        if (candidate.at(i).startMs < 0 || candidate.at(i).endMs <= candidate.at(i).startMs ||
            candidate.at(i).startMs < previousEnd)
            return Result<void>::failure(UserFacingError::validation(
                ErrorCode::InvalidArgument,
                QStringLiteral("Segment %1 has an invalid or overlapping time range.").arg(i + 1)));
        previousEnd = candidate.at(i).endMs;
    }
    return Result<void>::success();
}

Result<void> TranscriptEditor::validate() const {
    return validateCandidate(m_segments);
}

Result<void> TranscriptEditor::editText(const int index, const QString& text) {
    if (index < 0 || index >= m_segments.size())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The selected segment does not exist.")));
    beginChange();
    m_segments[index].editedText = text;
    m_segments[index].updatedAt = QDateTime::currentDateTimeUtc();
    return Result<void>::success();
}

Result<void> TranscriptEditor::split(const int index, const qint64 splitMs, const int textOffset) {
    if (index < 0 || index >= m_segments.size())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The selected segment does not exist.")));
    const TranscriptSegment source = m_segments.at(index);
    const QString display = source.displayText();
    if (splitMs <= source.startMs || splitMs >= source.endMs || textOffset <= 0 ||
        textOffset >= display.size())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument,
            QStringLiteral("Choose a split point inside both the segment time and text.")));
    beginChange();
    TranscriptSegment left = source;
    TranscriptSegment right = source;
    left.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    right.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    left.endMs = splitMs;
    right.startMs = splitMs;
    left.editedText = display.left(textOffset).trimmed();
    right.editedText = display.mid(textOffset).trimmed();
    const double splitRatio = static_cast<double>(textOffset) / static_cast<double>(display.size());
    const qsizetype estimatedOffset =
        static_cast<qsizetype>(qRound64(splitRatio * static_cast<double>(source.originalText.size())));
    const qsizetype originalOffset =
        qBound<qsizetype>(1, estimatedOffset, qMax<qsizetype>(1, source.originalText.size() - 1));
    left.originalText = source.originalText.left(originalOffset).trimmed();
    right.originalText = source.originalText.mid(originalOffset).trimmed();
    left.replacementAudit = {};
    right.replacementAudit = {};
    left.reviewed = false;
    right.reviewed = false;
    left.updatedAt = right.updatedAt = QDateTime::currentDateTimeUtc();
    m_segments[index] = left;
    m_segments.insert(index + 1, right);
    normalizeOrdinals();
    return Result<void>::success();
}

Result<void> TranscriptEditor::mergeWithPrevious(const int index) {
    if (index <= 0 || index >= m_segments.size())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("There is no previous segment to merge.")));
    beginChange();
    TranscriptSegment merged = m_segments.at(index - 1);
    const TranscriptSegment current = m_segments.at(index);
    const qint64 mergedDuration = qMax<qint64>(1, merged.endMs - merged.startMs);
    const qint64 currentDuration = qMax<qint64>(1, current.endMs - current.startMs);
    const double combinedDuration = static_cast<double>(mergedDuration + currentDuration);
    merged.endMs = current.endMs;
    merged.originalText = joinText(merged.originalText, current.originalText);
    merged.editedText = joinText(merged.displayText(), current.displayText());
    merged.replacementAudit = {};
    merged.averageProbability = ((merged.averageProbability * static_cast<double>(mergedDuration)) +
                                 (current.averageProbability * static_cast<double>(currentDuration))) /
                                combinedDuration;
    merged.minimumProbability = qMin(merged.minimumProbability, current.minimumProbability);
    merged.noSpeechProbability = qMax(merged.noSpeechProbability, current.noSpeechProbability);
    merged.lowConfidence = merged.lowConfidence || current.lowConfidence;
    merged.reviewed = merged.reviewed && current.reviewed;
    merged.provisional = merged.provisional || current.provisional;
    merged.attempt = qMax(merged.attempt, current.attempt);
    if (merged.chunkId != current.chunkId)
        merged.chunkId.clear();
    merged.updatedAt = QDateTime::currentDateTimeUtc();
    m_segments[index - 1] = merged;
    m_segments.removeAt(index);
    normalizeOrdinals();
    return Result<void>::success();
}

Result<void> TranscriptEditor::mergeWithNext(const int index) {
    if (index < 0 || index + 1 >= m_segments.size())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("There is no next segment to merge.")));
    return mergeWithPrevious(index + 1);
}

Result<void> TranscriptEditor::insert(const int index, TranscriptSegment segment) {
    if (index < 0 || index > m_segments.size())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, QStringLiteral("The insertion position is invalid.")));
    if (segment.id.isEmpty())
        segment.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QList<TranscriptSegment> candidate = m_segments;
    candidate.insert(index, segment);
    auto valid = validateCandidate(candidate);
    if (!valid)
        return valid;
    beginChange();
    m_segments = std::move(candidate);
    normalizeOrdinals();
    return Result<void>::success();
}

Result<void> TranscriptEditor::remove(const int index) {
    if (index < 0 || index >= m_segments.size())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The selected segment does not exist.")));
    beginChange();
    m_segments.removeAt(index);
    normalizeOrdinals();
    return Result<void>::success();
}

Result<void> TranscriptEditor::setTimeRange(const int index, const qint64 startMs, const qint64 endMs) {
    if (index < 0 || index >= m_segments.size())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::NotFound, QStringLiteral("The selected segment does not exist.")));
    QList<TranscriptSegment> candidate = m_segments;
    candidate[index].startMs = startMs;
    candidate[index].endMs = endMs;
    auto valid = validateCandidate(candidate);
    if (!valid)
        return valid;
    beginChange();
    m_segments = std::move(candidate);
    return Result<void>::success();
}

Result<void> TranscriptEditor::undo() {
    if (m_undo.isEmpty())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidStateTransition, QStringLiteral("There is no transcript edit to undo.")));
    m_redo.append(m_segments);
    m_segments = m_undo.takeLast();
    return Result<void>::success();
}

Result<void> TranscriptEditor::redo() {
    if (m_redo.isEmpty())
        return Result<void>::failure(UserFacingError::validation(
            ErrorCode::InvalidStateTransition, QStringLiteral("There is no transcript edit to redo.")));
    m_undo.append(m_segments);
    m_segments = m_redo.takeLast();
    return Result<void>::success();
}

} // namespace BreezeDesk
