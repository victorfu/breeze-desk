#pragma once

#include <QObject>

#include "breezedesk/core/Result.h"
#include "breezedesk/ui/TranscriptRevisionModel.h"

namespace BreezeDesk {

class IJobRepository;
class ITranscriptRepository;

class TranscriptRevisionViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* revisions READ revisions CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY revisionsChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY revisionsChanged)
    Q_PROPERTY(QString recordingId READ recordingId NOTIFY recordingChanged)
    Q_PROPERTY(QString selectedJobId READ selectedJobId NOTIFY selectionChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY revisionsChanged)
    Q_PROPERTY(QString activeJobId READ activeJobId NOTIFY activeRevisionChanged)
    Q_PROPERTY(bool followingLive READ followingLive NOTIFY followLiveChanged)
    Q_PROPERTY(bool selectionPinned READ selectionPinned NOTIFY followLiveChanged)
    Q_PROPERTY(bool hasNewerRevision READ hasNewerRevision NOTIFY revisionsChanged)
    Q_PROPERTY(bool selectedRevisionRunning READ selectedRevisionIsRunning NOTIFY revisionsChanged)

  public:
    explicit TranscriptRevisionViewModel(QObject* parent = nullptr);

    [[nodiscard]] QAbstractItemModel* revisions() noexcept;
    [[nodiscard]] int count() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] QString recordingId() const;
    [[nodiscard]] QString selectedJobId() const;
    [[nodiscard]] int selectedIndex() const;
    [[nodiscard]] QString activeJobId() const;
    [[nodiscard]] bool followingLive() const noexcept;
    [[nodiscard]] bool selectionPinned() const noexcept;
    [[nodiscard]] bool hasNewerRevision() const;

    void installRepositories(IJobRepository* jobs, ITranscriptRepository* transcripts);
    bool setRecording(const QString& recordingId, const QString& activeJobId,
                      const QString& preferredSelection = {}, bool preservePinnedSelection = false);
    bool refresh();
    bool selectRevision(const QString& jobId, bool pinSelection);
    QString followLive();
    void noteLiveRevision(const QString& jobId);
    QString finishLiveRevision(const QString& jobId, bool succeeded);
    [[nodiscard]] Result<RevisionDeletionResult> deleteRevision(const QString& jobId);
    [[nodiscard]] QVariantMap revisionDetails(const QString& jobId) const;
    [[nodiscard]] bool contains(const QString& jobId) const;
    [[nodiscard]] bool selectedRevisionIsRunning() const;

  signals:
    void revisionsChanged();
    void recordingChanged();
    void selectionChanged();
    void activeRevisionChanged();
    void followLiveChanged();
    void operationFailed(const QString& message);

  private:
    void setSelectedJobId(const QString& jobId);
    void setActiveJobId(const QString& jobId);
    void setFollowingLive(bool following);
    [[nodiscard]] QString followTargetJobId() const;

    TranscriptRevisionModel m_model;
    IJobRepository* m_jobs{nullptr};
    ITranscriptRepository* m_transcripts{nullptr};
    QString m_recordingId;
    QString m_selectedJobId;
    QString m_activeJobId;
    QString m_liveJobId;
    bool m_followingLive{true};
};

} // namespace BreezeDesk
