#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QUrl>

#include "breezedesk/database/Recording.h"

namespace BreezeDesk {

class RecordingListModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        SourceUrlRole,
        DurationMsRole,
        CreatedAtRole,
        StatusRole,
        ModelRole,
        TagsRole,
        ReviewStateRole,
        ProgressRole,
        DeletedRole,
        NotesRole,
        SourceMissingRole
    };
    Q_ENUM(Role)

    struct Recording {
        QString id;
        QString title;
        QUrl sourceUrl;
        QString originalSourcePath;
        QString managedMediaPath;
        qint64 durationMs{0};
        QDateTime createdAt;
        QString status;
        QString model;
        QStringList tags;
        QString reviewState;
        qreal progress{0.0};
        bool deleted{false};
        QString notes;
        QString activeJobId;
        QString waveformPath;
    };

    explicit RecordingListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    QString addSource(const QUrl& source);
    bool addRecording(const ::BreezeDesk::Recording& recording);
    void replaceRecordings(const QList<::BreezeDesk::Recording>& recordings);
    [[nodiscard]] QString availableTitle(const QString& candidate, const QString& excludedId = {}) const;
    bool moveToTrash(const QString& id);
    bool restore(const QString& id);
    bool removePermanently(const QString& id);
    bool rename(const QString& id, const QString& title);
    bool relinkSource(const QString& id, const QString& sourcePath);
    bool setTags(const QString& id, const QStringList& tags);
    bool setNotes(const QString& id, const QString& notes);
    bool setReviewState(const QString& id, const QString& state);
    [[nodiscard]] QVariantMap recording(const QString& id) const;

  private:
    [[nodiscard]] int indexOf(const QString& id) const;
    [[nodiscard]] QString uniqueTitle(const QString& candidate, const QString& excludedId = {}) const;

    QList<Recording> m_recordings;
};

} // namespace BreezeDesk
