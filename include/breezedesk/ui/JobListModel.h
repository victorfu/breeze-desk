#pragma once

#include <QAbstractListModel>

namespace BreezeDesk {

class JobListModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        RecordingIdRole,
        TitleRole,
        StateRole,
        StageRole,
        ProgressRole,
        ErrorRole,
        CanCancelRole,
        CanRetryRole,
        CanResumeRole,
        CanRemoveRole
    };
    Q_ENUM(Role)

    struct Job {
        QString id;
        QString recordingId;
        QString title;
        QString state{"Queued"};
        QString stage{"Preparing"};
        qreal progress{0.0};
        QString error;
    };

    explicit JobListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    QString enqueue(const QString& recordingId, const QString& title);
    void upsert(const QString& id, const QString& recordingId, const QString& title, const QString& state,
                const QString& stage, qreal progress, const QString& error);
    bool cancel(const QString& id);
    bool retry(const QString& id);
    bool resume(const QString& id);
    bool remove(const QString& id);
    bool move(const QString& id, int destination);
    void clearCompleted();
    [[nodiscard]] int activeCount() const;
    [[nodiscard]] bool isWritingTranscript(const QString& id) const;

  private:
    [[nodiscard]] int indexOf(const QString& id) const;
    void emitRowChanged(int row);

    QList<Job> m_jobs;
};

} // namespace BreezeDesk
