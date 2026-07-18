#pragma once

#include <QObject>

#include "breezedesk/ui/JobListModel.h"

namespace BreezeDesk {

class JobQueueViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* jobs READ jobs CONSTANT)
    Q_PROPERTY(bool pauseAfterCurrent READ pauseAfterCurrent WRITE setPauseAfterCurrent NOTIFY
                   pauseAfterCurrentChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY emptyChanged)
    Q_PROPERTY(QString runningJobId READ runningJobId NOTIFY runningJobIdChanged)

  public:
    explicit JobQueueViewModel(QObject* parent = nullptr);

    [[nodiscard]] QAbstractItemModel* jobs() noexcept;
    [[nodiscard]] bool pauseAfterCurrent() const noexcept;
    [[nodiscard]] int activeCount() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] QString runningJobId() const;
    [[nodiscard]] bool isWritingTranscript(const QString& jobId) const;

    Q_INVOKABLE bool containsJob(const QString& jobId) const;
    Q_INVOKABLE QString allocateJobId() const;
    Q_INVOKABLE QString enqueue(const QString& recordingId, const QString& title);
    Q_INVOKABLE void cancel(const QString& jobId);
    Q_INVOKABLE void retry(const QString& jobId);
    Q_INVOKABLE void resume(const QString& jobId);
    Q_INVOKABLE void remove(const QString& jobId);
    Q_INVOKABLE void reorder(const QString& jobId, int destination);
    Q_INVOKABLE void moveUp(const QString& jobId);
    Q_INVOKABLE void moveDown(const QString& jobId);
    Q_INVOKABLE void clearCompleted();

    void confirmRemoved(const QString& jobId);
    void confirmCompletedRemoved();

    void updateJob(const QString& id, const QString& recordingId, const QString& title, const QString& state,
                   const QString& stage, qreal progress, const QString& error = {});

  public slots:
    void setPauseAfterCurrent(bool enabled);
    void setRunningJobId(const QString& jobId);
    void updateJobTelemetry(const QString& jobId, int currentChunk, int totalChunks,
                            const QString& latestPartialText);
    void appendJobEvent(const QString& jobId, const QString& title, const QString& detail = {},
                        const QString& severity = QStringLiteral("info"), const QDateTime& occurredAt = {});

  signals:
    void pauseAfterCurrentChanged();
    void activeCountChanged();
    void emptyChanged();
    void runningJobIdChanged();
    void cancelRequested(const QString& jobId);
    void retryRequested(const QString& jobId);
    void resumeRequested(const QString& jobId);
    void removeRequested(const QString& jobId);
    void reorderRequested(const QString& jobId, int destination);
    void clearCompletedRequested();

  private:
    JobListModel m_jobs;
    bool m_pauseAfterCurrent{false};
};

} // namespace BreezeDesk
