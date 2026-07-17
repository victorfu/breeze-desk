#include "breezedesk/ui/TranscriptSegmentModel.h"

#include "breezedesk/glossary/GlossaryPostProcessor.h"

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
    const bool needsSpace = last.script() == QChar::Script_Latin && first.script() == QChar::Script_Latin &&
                            !last.isSpace() && !first.isSpace();
    return left + (needsSpace ? QStringLiteral(" ") : QString()) + right;
}

qsizetype preferredSplitOffset(const QString& text) {
    if (text.size() < 2)
        return text.size();
    const qsizetype midpoint = text.size() / 2;
    const qsizetype previousSpace = text.lastIndexOf(QLatin1Char(' '), midpoint);
    const qsizetype nextSpace = text.indexOf(QLatin1Char(' '), midpoint);
    if (previousSpace > 0 && (nextSpace < 0 || midpoint - previousSpace <= nextSpace - midpoint)) {
        return previousSpace;
    }
    if (nextSpace > 0 && nextSpace < text.size())
        return nextSpace;
    return midpoint;
}
} // namespace

TranscriptSegmentModel::TranscriptSegmentModel(QObject* parent) : QAbstractListModel(parent) {}

int TranscriptSegmentModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_segments.size());
}

QVariant TranscriptSegmentModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_segments.size()) {
        return {};
    }
    const Segment& item = m_segments.at(index.row());
    switch (role) {
    case IdRole:
        return item.id;
    case StartMsRole:
        return item.startMs;
    case EndMsRole:
        return item.endMs;
    case OriginalTextRole:
        return item.originalText;
    case EditedTextRole:
        return item.editedText;
    case LowConfidenceRole:
        return item.lowConfidence;
    case EditedRole:
        return item.editedText != item.originalText;
    case GlossaryReplacementRole:
        return !item.replacementAudit.isEmpty();
    case GlossaryAuditRole:
        return item.replacementAudit.toVariantList();
    case ReviewedRole:
        return item.reviewed;
    default:
        return {};
    }
}

bool TranscriptSegmentModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_segments.size()) {
        return false;
    }
    switch (role) {
    case EditedTextRole:
        return editText(index.row(), value.toString());
    case ReviewedRole:
        return setReviewed(index.row(), value.toBool());
    default:
        return false;
    }
}

QHash<int, QByteArray> TranscriptSegmentModel::roleNames() const {
    return {{IdRole, "segmentId"},
            {StartMsRole, "startMs"},
            {EndMsRole, "endMs"},
            {OriginalTextRole, "originalText"},
            {EditedTextRole, "editedText"},
            {LowConfidenceRole, "lowConfidence"},
            {EditedRole, "edited"},
            {GlossaryReplacementRole, "glossaryReplacement"},
            {GlossaryAuditRole, "glossaryAudit"},
            {ReviewedRole, "reviewed"}};
}

QList<TranscriptSegmentModel::Segment> TranscriptSegmentModel::snapshot() const {
    return m_segments;
}

void TranscriptSegmentModel::restoreSnapshot(const QList<Segment>& segments) {
    replaceAll(segments);
}

void TranscriptSegmentModel::replaceAll(const QList<Segment>& segments) {
    beginResetModel();
    m_segments = segments;
    endResetModel();
}

bool TranscriptSegmentModel::editText(int row, const QString& text) {
    if (row < 0 || row >= m_segments.size() || m_segments.at(row).editedText == text) {
        return false;
    }
    m_segments[row].editedText = text;
    m_segments[row].updatedAt = QDateTime::currentDateTimeUtc();
    emit dataChanged(index(row), index(row), {EditedTextRole, EditedRole});
    return true;
}

bool TranscriptSegmentModel::split(int row, qint64 positionMs) {
    if (row < 0 || row >= m_segments.size()) {
        return false;
    }
    const Segment source = m_segments.at(row);
    if (positionMs <= source.startMs || positionMs >= source.endMs) {
        return false;
    }
    const QString displayText = source.editedText.isEmpty() ? source.originalText : source.editedText;
    const qsizetype displayOffset = preferredSplitOffset(displayText);
    const double ratio = displayText.isEmpty()
                             ? static_cast<double>(positionMs - source.startMs) /
                                   static_cast<double>(source.endMs - source.startMs)
                             : static_cast<double>(displayOffset) / static_cast<double>(displayText.size());
    const qsizetype originalOffset = qBound<qsizetype>(
        0, qRound64(ratio * static_cast<double>(source.originalText.size())), source.originalText.size());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    Segment first = source;
    Segment second = source;
    second.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    second.startMs = positionMs;
    first.endMs = positionMs;
    first.originalText = source.originalText.left(originalOffset).trimmed();
    second.originalText = source.originalText.mid(originalOffset).trimmed();
    first.editedText = displayText.left(displayOffset).trimmed();
    second.editedText = displayText.mid(displayOffset).trimmed();
    first.replacementAudit = {};
    second.replacementAudit = {};
    first.reviewed = false;
    second.reviewed = false;
    first.updatedAt = now;
    second.createdAt = now;
    second.updatedAt = now;
    m_segments[row] = first;
    emit dataChanged(index(row), index(row));
    beginInsertRows({}, row + 1, row + 1);
    m_segments.insert(row + 1, second);
    endInsertRows();
    normalizeOrdinals();
    return true;
}

bool TranscriptSegmentModel::mergePrevious(int row) {
    if (row <= 0 || row >= m_segments.size()) {
        return false;
    }
    Segment& previous = m_segments[row - 1];
    const Segment current = m_segments.at(row);
    const qint64 previousDuration = qMax<qint64>(1, previous.endMs - previous.startMs);
    const qint64 currentDuration = qMax<qint64>(1, current.endMs - current.startMs);
    const double combinedDuration = static_cast<double>(previousDuration + currentDuration);
    previous.endMs = current.endMs;
    previous.editedText =
        joinText(previous.editedText.isEmpty() ? previous.originalText : previous.editedText,
                 current.editedText.isEmpty() ? current.originalText : current.editedText);
    previous.originalText = joinText(previous.originalText, current.originalText);
    previous.averageProbability = ((previous.averageProbability * static_cast<double>(previousDuration)) +
                                   (current.averageProbability * static_cast<double>(currentDuration))) /
                                  combinedDuration;
    previous.minimumProbability = qMin(previous.minimumProbability, current.minimumProbability);
    previous.noSpeechProbability = qMax(previous.noSpeechProbability, current.noSpeechProbability);
    previous.lowConfidence = previous.lowConfidence || current.lowConfidence;
    previous.reviewed = previous.reviewed && current.reviewed;
    previous.replacementAudit = {};
    previous.provisional = previous.provisional || current.provisional;
    previous.attempt = qMax(previous.attempt, current.attempt);
    if (previous.chunkId != current.chunkId)
        previous.chunkId.clear();
    if (!previous.createdAt.isValid() ||
        (current.createdAt.isValid() && current.createdAt < previous.createdAt)) {
        previous.createdAt = current.createdAt;
    }
    previous.updatedAt = QDateTime::currentDateTimeUtc();
    emit dataChanged(index(row - 1), index(row - 1));
    beginRemoveRows({}, row, row);
    m_segments.removeAt(row);
    endRemoveRows();
    normalizeOrdinals();
    return true;
}

bool TranscriptSegmentModel::mergeNext(int row) {
    return row >= 0 && row + 1 < m_segments.size() ? mergePrevious(row + 1) : false;
}

bool TranscriptSegmentModel::insertAfter(int row, qint64 startMs, qint64 endMs, const QString& text) {
    const int insertionRow = qBound(0, row + 1, static_cast<int>(m_segments.size()));
    if (startMs < 0 || endMs <= startMs) {
        return false;
    }
    if (insertionRow > 0 && m_segments.at(insertionRow - 1).endMs > startMs) {
        return false;
    }
    if (insertionRow < m_segments.size() && m_segments.at(insertionRow).startMs < endMs) {
        return false;
    }
    Segment item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.startMs = startMs;
    item.endMs = endMs;
    item.originalText = text;
    item.editedText = text;
    item.createdAt = QDateTime::currentDateTimeUtc();
    item.updatedAt = item.createdAt;
    beginInsertRows({}, insertionRow, insertionRow);
    m_segments.insert(insertionRow, item);
    endInsertRows();
    normalizeOrdinals();
    return true;
}

bool TranscriptSegmentModel::removeSegment(int row) {
    if (row < 0 || row >= m_segments.size()) {
        return false;
    }
    beginRemoveRows({}, row, row);
    m_segments.removeAt(row);
    endRemoveRows();
    normalizeOrdinals();
    return true;
}

bool TranscriptSegmentModel::setTimes(int row, qint64 startMs, qint64 endMs) {
    if (!validTimesForRow(row, startMs, endMs)) {
        return false;
    }
    Segment& item = m_segments[row];
    if (item.startMs == startMs && item.endMs == endMs) {
        return false;
    }
    item.startMs = startMs;
    item.endMs = endMs;
    item.updatedAt = QDateTime::currentDateTimeUtc();
    emit dataChanged(index(row), index(row), {StartMsRole, EndMsRole});
    return true;
}

bool TranscriptSegmentModel::setReviewed(int row, bool reviewed) {
    if (row < 0 || row >= m_segments.size() || m_segments.at(row).reviewed == reviewed) {
        return false;
    }
    m_segments[row].reviewed = reviewed;
    m_segments[row].updatedAt = QDateTime::currentDateTimeUtc();
    emit dataChanged(index(row), index(row), {ReviewedRole});
    return true;
}

bool TranscriptSegmentModel::setGlossaryReplacementApplied(const int row, const int replacementIndex,
                                                           const bool applied) {
    if (row < 0 || row >= m_segments.size())
        return false;
    Segment& item = m_segments[row];
    QList<GlossaryReplacement> replacements = GlossaryPostProcessor::auditFromJson(item.replacementAudit);
    if (replacementIndex < 0 || replacementIndex >= replacements.size() ||
        replacements.at(replacementIndex).applied == applied) {
        return false;
    }
    GlossaryPostProcessor processor;
    const auto currentRendering = processor.renderAudit(item.originalText, replacements);
    const QString currentText = item.editedText.isEmpty() ? item.originalText : item.editedText;
    if (!currentRendering || *currentRendering != currentText)
        return false;
    replacements[replacementIndex].applied = applied;
    const auto updatedRendering = processor.renderAudit(item.originalText, replacements);
    if (!updatedRendering)
        return false;
    item.editedText = *updatedRendering;
    item.replacementAudit = GlossaryPostProcessor::auditToJson(replacements);
    item.updatedAt = QDateTime::currentDateTimeUtc();
    emit dataChanged(index(row), index(row),
                     {EditedTextRole, EditedRole, GlossaryReplacementRole, GlossaryAuditRole});
    return true;
}

QVariantMap TranscriptSegmentModel::segmentAt(int row) const {
    if (row < 0 || row >= m_segments.size()) {
        return {};
    }
    const auto& item = m_segments.at(row);
    return {{"id", item.id},
            {"startMs", item.startMs},
            {"endMs", item.endMs},
            {"originalText", item.originalText},
            {"editedText", item.editedText},
            {"lowConfidence", item.lowConfidence},
            {"reviewed", item.reviewed},
            {"glossaryAudit", item.replacementAudit.toVariantList()}};
}

bool TranscriptSegmentModel::validTimesForRow(int row, qint64 startMs, qint64 endMs) const {
    if (row < 0 || row >= m_segments.size() || startMs < 0 || endMs <= startMs) {
        return false;
    }
    if (row > 0 && startMs < m_segments.at(row - 1).endMs) {
        return false;
    }
    return row + 1 >= m_segments.size() || endMs <= m_segments.at(row + 1).startMs;
}

void TranscriptSegmentModel::normalizeOrdinals() {
    for (int row = 0; row < m_segments.size(); ++row)
        m_segments[row].ordinal = row;
}

void TranscriptSegmentModel::emitRowChanged(int row) {
    if (row >= 0 && row < m_segments.size()) {
        emit dataChanged(index(row), index(row));
    }
}

} // namespace BreezeDesk
