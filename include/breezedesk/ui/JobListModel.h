#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QVariantList>

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
        CanRemoveRole,
        IsRunningNowRole,
        QueuePositionRole,
        WaitingAheadRole,
        CurrentChunkRole,
        TotalChunksRole,
        LatestPartialTextRole,
        EventTimelineRole,
        CanMoveUpRole,
        CanMoveDownRole
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
        int currentChunk{0};
        int totalChunks{0};
        QString latestPartialText;
        QVariantList eventTimeline;
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
    [[nodiscard]] bool canRemove(const QString& id) const;
    bool moveQueued(const QString& id, int destination);
    void clearCompleted();
    [[nodiscard]] int activeCount() const;
    [[nodiscard]] bool isWritingTranscript(const QString& id) const;
    [[nodiscard]] bool contains(const QString& id) const;
    [[nodiscard]] QString runningJobId() const;
    [[nodiscard]] int queuePosition(const QString& id) const;
    void setRunningJobId(const QString& id);
    void updateTelemetry(const QString& id, int currentChunk, int totalChunks,
                         const QString& latestPartialText);
    void appendEvent(const QString& id, const QString& title, const QString& detail = {},
                     const QString& severity = QStringLiteral("info"), const QDateTime& occurredAt = {});

  signals:
    void runningJobIdChanged();

  private:
    [[nodiscard]] int indexOf(const QString& id) const;
    [[nodiscard]] int queuePositionForRow(int row) const;
    [[nodiscard]] int queuedCount() const;
    [[nodiscard]] bool canMoveQueued(int row, int delta) const;
    void appendEvent(Job& job, const QString& title, const QString& detail, const QString& severity,
                     const QDateTime& occurredAt);
    void emitRowChanged(int row, const QList<int>& roles = {});
    void emitQueueMetadataChanged();

    QList<Job> m_jobs;
    QString m_runningJobId;
};

} // namespace BreezeDesk
