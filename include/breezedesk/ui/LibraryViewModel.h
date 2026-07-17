#pragma once

#include <QObject>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QVariantList>

#include "breezedesk/ui/RecordingListModel.h"

namespace BreezeDesk {

class IRecordingRepository;

class RecordingFilterProxyModel final : public QSortFilterProxyModel {
    Q_OBJECT

  public:
    explicit RecordingFilterProxyModel(bool deleted, QObject* parent = nullptr);
    void setQuery(const QString& query);
    void setReviewFilter(const QString& reviewFilter);

  protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

  private:
    bool m_deleted{false};
    QString m_query;
    QString m_reviewFilter;
};

class LibraryViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* recordings READ recordings CONSTANT)
    Q_PROPERTY(QAbstractItemModel* trash READ trash CONSTANT)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(QString reviewFilter READ reviewFilter WRITE setReviewFilter NOTIFY reviewFilterChanged)
    Q_PROPERTY(QString selectedRecordingId READ selectedRecordingId WRITE setSelectedRecordingId NOTIFY
                   selectedRecordingIdChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY emptyChanged)
    Q_PROPERTY(bool trashEmpty READ trashEmpty NOTIFY emptyChanged)

  public:
    explicit LibraryViewModel(QObject* parent = nullptr);
    explicit LibraryViewModel(IRecordingRepository* repository, QObject* parent = nullptr);

    [[nodiscard]] QAbstractItemModel* recordings() noexcept;
    [[nodiscard]] QAbstractItemModel* trash() noexcept;
    [[nodiscard]] QString searchText() const;
    [[nodiscard]] QString sortMode() const;
    [[nodiscard]] QString reviewFilter() const;
    [[nodiscard]] QString selectedRecordingId() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool trashEmpty() const;

    Q_INVOKABLE int importUrls(const QVariantList& urls);
    [[nodiscard]] QString importManagedCopy(const QUrl& originalUrl, const QString& managedPath);
    Q_INVOKABLE void moveToTrash(const QString& id);
    Q_INVOKABLE void restore(const QString& id);
    Q_INVOKABLE void deletePermanently(const QString& id);
    Q_INVOKABLE void rename(const QString& id, const QString& title);
    Q_INVOKABLE void relinkSource(const QString& id, const QUrl& source);
    Q_INVOKABLE void setTags(const QString& id, const QStringList& tags);
    Q_INVOKABLE void setTagsText(const QString& id, const QString& tags);
    Q_INVOKABLE void setReviewState(const QString& id, bool reviewed);
    Q_INVOKABLE bool setNotes(const QString& id, const QString& notes);
    Q_INVOKABLE QVariantMap details(const QString& id) const;
    Q_INVOKABLE void refresh();

    void setRepository(IRecordingRepository* repository);

  public slots:
    void setSearchText(const QString& text);
    void setSortMode(const QString& mode);
    void setReviewFilter(const QString& filter);
    void setSelectedRecordingId(const QString& id);

  signals:
    void searchTextChanged();
    void sortModeChanged();
    void reviewFilterChanged();
    void selectedRecordingIdChanged();
    void emptyChanged();
    void filesImported(int count);
    void recordingImported(const QString& recordingId, const QString& sourcePath);
    void importRejected(const QUrl& url, const QString& reason);
    void operationFailed(const QString& reason);
    void recordingActivated(const QString& id);
    void recordingAboutToBePermanentlyDeleted(const QString& id);
    void recordingPermanentlyDeleted(const QString& id);
    void recordingMetadataChanged(const QString& id);

  private:
    RecordingListModel m_source;
    RecordingFilterProxyModel m_libraryProxy;
    RecordingFilterProxyModel m_trashProxy;
    QString m_searchText;
    QString m_sortMode{QStringLiteral("Newest")};
    QString m_reviewFilter{QStringLiteral("All")};
    QString m_selectedRecordingId;
    IRecordingRepository* m_repository{nullptr};
    QTimer m_searchDebounce;
};

} // namespace BreezeDesk
