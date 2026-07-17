#include "breezedesk/ui/TranscriptViewModel.h"

namespace BreezeDesk {

TranscriptFilterProxyModel::TranscriptFilterProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
}

void TranscriptFilterProxyModel::setQuery(const QString& query) {
    if (m_query == query) {
        return;
    }
    m_query = query;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
}

void TranscriptFilterProxyModel::setLowConfidenceOnly(bool enabled) {
    if (m_lowConfidenceOnly == enabled) {
        return;
    }
    m_lowConfidenceOnly = enabled;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
}

bool TranscriptFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    const QModelIndex item = sourceModel()->index(sourceRow, 0, sourceParent);
    if (m_lowConfidenceOnly &&
        !sourceModel()->data(item, TranscriptSegmentModel::LowConfidenceRole).toBool()) {
        return false;
    }
    if (m_query.isEmpty()) {
        return true;
    }
    return sourceModel()
        ->data(item, TranscriptSegmentModel::EditedTextRole)
        .toString()
        .contains(m_query, Qt::CaseInsensitive);
}

TranscriptViewModel::TranscriptViewModel(QObject* parent) : QObject(parent), m_proxy(this) {
    m_proxy.setSourceModel(&m_source);
    connect(&m_source, &QAbstractItemModel::rowsInserted, this, &TranscriptViewModel::segmentCountChanged);
    connect(&m_source, &QAbstractItemModel::rowsRemoved, this, &TranscriptViewModel::segmentCountChanged);
    connect(&m_source, &QAbstractItemModel::modelReset, this, &TranscriptViewModel::segmentCountChanged);
}

QAbstractItemModel* TranscriptViewModel::segments() noexcept {
    return &m_proxy;
}
int TranscriptViewModel::selectedIndex() const noexcept {
    return m_selectedIndex;
}
QString TranscriptViewModel::searchText() const {
    return m_searchText;
}
bool TranscriptViewModel::lowConfidenceOnly() const noexcept {
    return m_lowConfidenceOnly;
}
bool TranscriptViewModel::canUndo() const noexcept {
    return !m_editingLocked && !m_undo.isEmpty();
}
bool TranscriptViewModel::canRedo() const noexcept {
    return !m_editingLocked && !m_redo.isEmpty();
}
bool TranscriptViewModel::dirty() const noexcept {
    return m_dirty;
}
bool TranscriptViewModel::editingLocked() const noexcept {
    return m_editingLocked;
}
int TranscriptViewModel::segmentCount() const {
    return m_source.rowCount();
}
int TranscriptViewModel::activePlaybackIndex() const noexcept {
    return m_activePlaybackIndex;
}

void TranscriptViewModel::editText(int proxyRow, const QString& text) {
    if (rejectIfEditingLocked())
        return;
    beforeMutation();
    afterMutation(m_source.editText(sourceRowForProxyRow(proxyRow), text));
}

void TranscriptViewModel::splitAt(int proxyRow, qint64 positionMs) {
    if (rejectIfEditingLocked())
        return;
    beforeMutation();
    const bool changed = m_source.split(sourceRowForProxyRow(proxyRow), positionMs);
    afterMutation(changed);
    if (!changed) {
        emit validationError(tr("The split position must be inside the selected segment."));
    }
}

void TranscriptViewModel::mergePrevious(int proxyRow) {
    if (rejectIfEditingLocked())
        return;
    beforeMutation();
    afterMutation(m_source.mergePrevious(sourceRowForProxyRow(proxyRow)));
}

void TranscriptViewModel::mergeNext(int proxyRow) {
    if (rejectIfEditingLocked())
        return;
    beforeMutation();
    afterMutation(m_source.mergeNext(sourceRowForProxyRow(proxyRow)));
}

void TranscriptViewModel::insertAfter(int proxyRow, qint64 startMs, qint64 endMs, const QString& text) {
    if (rejectIfEditingLocked())
        return;
    beforeMutation();
    const bool changed = m_source.insertAfter(sourceRowForProxyRow(proxyRow), startMs, endMs, text);
    afterMutation(changed);
    if (!changed) {
        emit validationError(tr("Segment times must be ordered and cannot overlap."));
    }
}

void TranscriptViewModel::remove(int proxyRow) {
    if (rejectIfEditingLocked())
        return;
    beforeMutation();
    afterMutation(m_source.removeSegment(sourceRowForProxyRow(proxyRow)));
}

void TranscriptViewModel::setTimes(int proxyRow, qint64 startMs, qint64 endMs) {
    if (rejectIfEditingLocked())
        return;
    beforeMutation();
    const bool changed = m_source.setTimes(sourceRowForProxyRow(proxyRow), startMs, endMs);
    afterMutation(changed);
    if (!changed) {
        emit validationError(tr("Segment times must be ordered and cannot overlap."));
    }
}

void TranscriptViewModel::markReviewed(int proxyRow, bool reviewed) {
    if (rejectIfEditingLocked())
        return;
    beforeMutation();
    afterMutation(m_source.setReviewed(sourceRowForProxyRow(proxyRow), reviewed));
}

void TranscriptViewModel::setGlossaryReplacementApplied(const int proxyRow, const int replacementIndex,
                                                        const bool applied) {
    if (rejectIfEditingLocked())
        return;
    beforeMutation();
    const bool changed =
        m_source.setGlossaryReplacementApplied(sourceRowForProxyRow(proxyRow), replacementIndex, applied);
    afterMutation(changed);
    if (!changed) {
        emit validationError(
            tr("This glossary replacement cannot be changed after the segment text was manually edited."));
    }
}

void TranscriptViewModel::undo() {
    if (rejectIfEditingLocked() || m_undo.isEmpty()) {
        return;
    }
    m_redo.append(m_source.snapshot());
    m_source.restoreSnapshot(m_undo.takeLast());
    m_dirty = true;
    emit historyChanged();
    emit dirtyChanged();
}

void TranscriptViewModel::redo() {
    if (rejectIfEditingLocked() || m_redo.isEmpty()) {
        return;
    }
    m_undo.append(m_source.snapshot());
    m_source.restoreSnapshot(m_redo.takeLast());
    m_dirty = true;
    emit historyChanged();
    emit dirtyChanged();
}

int TranscriptViewModel::findNext(int fromProxyRow) const {
    if (m_proxy.rowCount() == 0) {
        return -1;
    }
    return (qMax(-1, fromProxyRow) + 1) % m_proxy.rowCount();
}

int TranscriptViewModel::findPrevious(int fromProxyRow) const {
    if (m_proxy.rowCount() == 0) {
        return -1;
    }
    return (fromProxyRow <= 0) ? m_proxy.rowCount() - 1 : fromProxyRow - 1;
}

QString TranscriptViewModel::fullText() const {
    QStringList paragraphs;
    paragraphs.reserve(m_source.rowCount());
    for (int row = 0; row < m_source.rowCount(); ++row) {
        paragraphs.append(
            m_source.data(m_source.index(row), TranscriptSegmentModel::EditedTextRole).toString());
    }
    return paragraphs.join(QLatin1Char('\n'));
}

void TranscriptViewModel::save() {
    if (m_dirty) {
        emit saveRequested();
    }
}

void TranscriptViewModel::updatePlaybackPosition(qint64 positionMs) {
    int sourceRow = -1;
    for (int row = 0; row < m_source.rowCount(); ++row) {
        const qint64 start =
            m_source.data(m_source.index(row), TranscriptSegmentModel::StartMsRole).toLongLong();
        const qint64 end = m_source.data(m_source.index(row), TranscriptSegmentModel::EndMsRole).toLongLong();
        if (positionMs >= start && positionMs < end) {
            sourceRow = row;
            break;
        }
    }
    int proxyRow = -1;
    if (sourceRow >= 0) {
        proxyRow = m_proxy.mapFromSource(m_source.index(sourceRow)).row();
    }
    if (m_activePlaybackIndex != proxyRow) {
        m_activePlaybackIndex = proxyRow;
        emit activePlaybackIndexChanged();
    }
}

void TranscriptViewModel::replaceSegments(const QList<TranscriptSegmentModel::Segment>& segments) {
    m_source.replaceAll(segments);
    m_undo.clear();
    m_redo.clear();
    m_dirty = false;
    emit historyChanged();
    emit dirtyChanged();
}

void TranscriptViewModel::markSaved() {
    if (!m_dirty) {
        return;
    }
    m_dirty = false;
    emit dirtyChanged();
}

void TranscriptViewModel::setEditingLocked(const bool locked) {
    if (m_editingLocked == locked)
        return;
    m_editingLocked = locked;
    emit editingLockedChanged();
    emit historyChanged();
}

QList<TranscriptSegmentModel::Segment> TranscriptViewModel::snapshot() const {
    return m_source.snapshot();
}

void TranscriptViewModel::setSelectedIndex(int index) {
    if (m_selectedIndex == index) {
        return;
    }
    m_selectedIndex = index;
    emit selectedIndexChanged();
}

void TranscriptViewModel::setSearchText(const QString& text) {
    if (m_searchText == text) {
        return;
    }
    m_searchText = text;
    m_proxy.setQuery(text);
    emit searchTextChanged();
}

void TranscriptViewModel::setLowConfidenceOnly(bool enabled) {
    if (m_lowConfidenceOnly == enabled) {
        return;
    }
    m_lowConfidenceOnly = enabled;
    m_proxy.setLowConfidenceOnly(enabled);
    emit lowConfidenceOnlyChanged();
}

void TranscriptViewModel::beforeMutation() {
    m_undo.append(m_source.snapshot());
    if (m_undo.size() > 100) {
        m_undo.removeFirst();
    }
}

void TranscriptViewModel::afterMutation(bool changed) {
    if (!changed) {
        if (!m_undo.isEmpty()) {
            m_undo.removeLast();
        }
        return;
    }
    m_redo.clear();
    const bool wasDirty = m_dirty;
    m_dirty = true;
    emit historyChanged();
    if (!wasDirty) {
        emit dirtyChanged();
    }
}

bool TranscriptViewModel::rejectIfEditingLocked() {
    if (!m_editingLocked)
        return false;
    emit validationError(tr("Transcript editing is available after the active transcription stops."));
    return true;
}

int TranscriptViewModel::sourceRowForProxyRow(int proxyRow) const {
    const QModelIndex proxyIndex = m_proxy.index(proxyRow, 0);
    return proxyIndex.isValid() ? m_proxy.mapToSource(proxyIndex).row() : -1;
}

} // namespace BreezeDesk
