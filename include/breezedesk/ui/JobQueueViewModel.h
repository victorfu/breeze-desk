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
    Q_PROPERTY(bool empty READ empty NOTIFY activeCountChanged)

  public:
    explicit JobQueueViewModel(QObject* parent = nullptr);

    [[nodiscard]] QAbstractItemModel* jobs() noexcept;
    [[nodiscard]] bool pauseAfterCurrent() const noexcept;
    [[nodiscard]] int activeCount() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool isWritingTranscript(const QString& jobId) const;

    Q_INVOKABLE QString enqueue(const QString& recordingId, const QString& title);
    Q_INVOKABLE void cancel(const QString& jobId);
    Q_INVOKABLE void retry(const QString& jobId);
    Q_INVOKABLE void resume(const QString& jobId);
    Q_INVOKABLE void reorder(const QString& jobId, int destination);
    Q_INVOKABLE void clearCompleted();

    void updateJob(const QString& id, const QString& recordingId, const QString& title, const QString& state,
                   const QString& stage, qreal progress, const QString& error = {});

  public slots:
    void setPauseAfterCurrent(bool enabled);

  signals:
    void pauseAfterCurrentChanged();
    void activeCountChanged();
    void cancelRequested(const QString& jobId);
    void retryRequested(const QString& jobId);
    void resumeRequested(const QString& jobId);
    void reorderRequested(const QString& jobId, int destination);
    void clearCompletedRequested();

  private:
    JobListModel m_jobs;
    bool m_pauseAfterCurrent{false};
};

} // namespace BreezeDesk
