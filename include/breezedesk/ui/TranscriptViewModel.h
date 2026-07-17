#pragma once

#include <QObject>
#include <QSortFilterProxyModel>

#include "breezedesk/ui/TranscriptSegmentModel.h"

namespace BreezeDesk {

class TranscriptFilterProxyModel final : public QSortFilterProxyModel {
    Q_OBJECT

  public:
    explicit TranscriptFilterProxyModel(QObject* parent = nullptr);
    void setQuery(const QString& query);
    void setLowConfidenceOnly(bool enabled);

  protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

  private:
    QString m_query;
    bool m_lowConfidenceOnly{false};
};

class TranscriptViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* segments READ segments CONSTANT)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectedIndexChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(bool lowConfidenceOnly READ lowConfidenceOnly WRITE setLowConfidenceOnly NOTIFY
                   lowConfidenceOnlyChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool editingLocked READ editingLocked NOTIFY editingLockedChanged)
    Q_PROPERTY(int segmentCount READ segmentCount NOTIFY segmentCountChanged)
    Q_PROPERTY(int activePlaybackIndex READ activePlaybackIndex NOTIFY activePlaybackIndexChanged)

  public:
    explicit TranscriptViewModel(QObject* parent = nullptr);

    [[nodiscard]] QAbstractItemModel* segments() noexcept;
    [[nodiscard]] int selectedIndex() const noexcept;
    [[nodiscard]] QString searchText() const;
    [[nodiscard]] bool lowConfidenceOnly() const noexcept;
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] bool editingLocked() const noexcept;
    [[nodiscard]] int segmentCount() const;
    [[nodiscard]] int activePlaybackIndex() const noexcept;

    Q_INVOKABLE void editText(int sourceRow, const QString& text);
    Q_INVOKABLE void splitAt(int sourceRow, qint64 positionMs);
    Q_INVOKABLE void mergePrevious(int sourceRow);
    Q_INVOKABLE void mergeNext(int sourceRow);
    Q_INVOKABLE void insertAfter(int sourceRow, qint64 startMs, qint64 endMs, const QString& text);
    Q_INVOKABLE void remove(int sourceRow);
    Q_INVOKABLE void setTimes(int sourceRow, qint64 startMs, qint64 endMs);
    Q_INVOKABLE void markReviewed(int sourceRow, bool reviewed);
    Q_INVOKABLE void setGlossaryReplacementApplied(int sourceRow, int replacementIndex, bool applied);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE int findNext(int fromSourceRow) const;
    Q_INVOKABLE int findPrevious(int fromSourceRow) const;
    Q_INVOKABLE QString fullText() const;
    Q_INVOKABLE void save();
    Q_INVOKABLE void updatePlaybackPosition(qint64 positionMs);

    void replaceSegments(const QList<TranscriptSegmentModel::Segment>& segments);
    void markSaved();
    void setEditingLocked(bool locked);
    [[nodiscard]] QList<TranscriptSegmentModel::Segment> snapshot() const;

  public slots:
    void setSelectedIndex(int index);
    void setSearchText(const QString& text);
    void setLowConfidenceOnly(bool enabled);

  signals:
    void selectedIndexChanged();
    void searchTextChanged();
    void lowConfidenceOnlyChanged();
    void historyChanged();
    void dirtyChanged();
    void editingLockedChanged();
    void segmentCountChanged();
    void activePlaybackIndexChanged();
    void saveRequested();
    void seekRequested(qint64 positionMs);
    void validationError(const QString& message);

  private:
    void beforeMutation();
    void afterMutation(bool changed);
    [[nodiscard]] bool rejectIfEditingLocked();
    [[nodiscard]] int sourceRowForProxyRow(int proxyRow) const;

    TranscriptSegmentModel m_source;
    TranscriptFilterProxyModel m_proxy;
    QList<QList<TranscriptSegmentModel::Segment>> m_undo;
    QList<QList<TranscriptSegmentModel::Segment>> m_redo;
    int m_selectedIndex{-1};
    QString m_searchText;
    bool m_lowConfidenceOnly{false};
    bool m_dirty{false};
    bool m_editingLocked{false};
    int m_activePlaybackIndex{-1};
};

} // namespace BreezeDesk
