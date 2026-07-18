#include "breezedesk/ui/TranscriptRevisionViewModel.h"

#include "breezedesk/jobs/IJobRepository.h"
#include "breezedesk/transcript/ITranscriptRepository.h"

namespace BreezeDesk {

TranscriptRevisionViewModel::TranscriptRevisionViewModel(QObject* parent) : QObject(parent), m_model(this) {
    connect(&m_model, &QAbstractItemModel::modelReset, this, &TranscriptRevisionViewModel::revisionsChanged);
}

QAbstractItemModel* TranscriptRevisionViewModel::revisions() noexcept {
    return &m_model;
}

int TranscriptRevisionViewModel::count() const {
    return m_model.rowCount();
}

bool TranscriptRevisionViewModel::empty() const {
    return count() == 0;
}

QString TranscriptRevisionViewModel::recordingId() const {
    return m_recordingId;
}

QString TranscriptRevisionViewModel::selectedJobId() const {
    return m_selectedJobId;
}

int TranscriptRevisionViewModel::selectedIndex() const {
    return m_model.indexOf(m_selectedJobId);
}

QString TranscriptRevisionViewModel::activeJobId() const {
    return m_activeJobId;
}

bool TranscriptRevisionViewModel::followingLive() const noexcept {
    return m_followingLive;
}

bool TranscriptRevisionViewModel::selectionPinned() const noexcept {
    return !m_followingLive;
}

bool TranscriptRevisionViewModel::hasNewerRevision() const {
    return selectionPinned() && m_model.hasRevisionNewerThan(m_selectedJobId);
}

void TranscriptRevisionViewModel::installRepositories(IJobRepository* jobs,
                                                      ITranscriptRepository* transcripts) {
    m_jobs = jobs;
    m_transcripts = transcripts;
    if (!m_recordingId.isEmpty()) {
        refresh();
    }
}

bool TranscriptRevisionViewModel::setRecording(const QString& recordingId, const QString& activeJobId,
                                               const QString& preferredSelection,
                                               const bool preservePinnedSelection) {
    const bool changedRecording = m_recordingId != recordingId;
    const bool keepPinned = !changedRecording && preservePinnedSelection && selectionPinned();
    const QString previousSelection = m_selectedJobId;

    m_recordingId = recordingId;
    if (changedRecording) {
        m_liveJobId.clear();
        emit recordingChanged();
    }
    if (!keepPinned) {
        setFollowingLive(true);
    }
    setActiveJobId(activeJobId);
    if (!refresh()) {
        return false;
    }

    QString selection;
    if (m_jobs == nullptr) {
        selection = keepPinned ? previousSelection
                               : (!preferredSelection.isEmpty() ? preferredSelection : activeJobId);
    } else if (keepPinned && m_model.contains(previousSelection)) {
        selection = previousSelection;
    } else if (!preferredSelection.isEmpty() && m_model.contains(preferredSelection)) {
        selection = preferredSelection;
    } else {
        selection = followTargetJobId();
    }
    setSelectedJobId(selection);
    return true;
}

bool TranscriptRevisionViewModel::refresh() {
    if (m_jobs == nullptr || m_recordingId.isEmpty()) {
        m_model.replaceRevisions({}, m_selectedJobId, m_activeJobId);
        return true;
    }

    const auto revisions = m_jobs->listForRecording(m_recordingId);
    if (!revisions) {
        m_model.replaceRevisions({}, {}, {});
        setSelectedJobId({});
        setActiveJobId({});
        emit operationFailed(revisions.error().message);
        return false;
    }

    QString activeJobId;
    for (const auto& revision : revisions.value()) {
        if (revision.active) {
            activeJobId = revision.job.id;
            break;
        }
    }
    setActiveJobId(activeJobId);
    m_model.replaceRevisions(revisions.value(), m_selectedJobId, m_activeJobId);

    if (!m_selectedJobId.isEmpty() && !m_model.contains(m_selectedJobId)) {
        setSelectedJobId({});
    }
    emit revisionsChanged();
    return true;
}

bool TranscriptRevisionViewModel::selectRevision(const QString& jobId, const bool pinSelection) {
    if (!m_model.contains(jobId)) {
        return false;
    }
    setSelectedJobId(jobId);
    setFollowingLive(!pinSelection);
    emit revisionsChanged();
    return true;
}

QString TranscriptRevisionViewModel::followLive() {
    setFollowingLive(true);
    const QString target = followTargetJobId();
    setSelectedJobId(target);
    emit revisionsChanged();
    return target;
}

void TranscriptRevisionViewModel::noteLiveRevision(const QString& jobId) {
    if (jobId.isEmpty()) {
        return;
    }
    m_liveJobId = jobId;
    refresh();
    emit revisionsChanged();
}

QString TranscriptRevisionViewModel::finishLiveRevision(const QString& jobId, const bool succeeded) {
    if (m_liveJobId == jobId) {
        m_liveJobId.clear();
    }
    if (!refresh()) {
        return m_selectedJobId;
    }
    if (!succeeded && (!selectionPinned() || m_selectedJobId == jobId)) {
        QString completedJobId;
        if (m_jobs != nullptr) {
            const auto latest = m_jobs->latestForRecording(m_recordingId);
            if (!latest) {
                emit operationFailed(latest.error().message);
                return m_selectedJobId;
            }
            if (latest.value().has_value()) {
                completedJobId = latest.value()->job.id;
            }
        }
        setFollowingLive(true);
        setSelectedJobId(completedJobId);
    } else if (succeeded && followingLive()) {
        setSelectedJobId(followTargetJobId());
    }
    emit revisionsChanged();
    return m_selectedJobId;
}

Result<RevisionDeletionResult> TranscriptRevisionViewModel::deleteRevision(const QString& jobId) {
    if (m_jobs == nullptr || m_recordingId.isEmpty() || jobId.isEmpty()) {
        return Result<RevisionDeletionResult>::failure(UserFacingError::validation(
            ErrorCode::InvalidArgument, tr("Choose a transcript version to delete.")));
    }
    const auto result = m_jobs->deleteRevision(m_recordingId, jobId);
    if (!result) {
        emit operationFailed(result.error().message);
        return result;
    }

    const bool deletedSelection = m_selectedJobId == result.value().deletedJobId;
    setActiveJobId(result.value().activeJobId);
    if (m_liveJobId == result.value().deletedJobId) {
        m_liveJobId.clear();
    }
    if (!refresh()) {
        return result;
    }
    if (deletedSelection) {
        setFollowingLive(true);
        const QString replacement = followTargetJobId();
        setSelectedJobId(replacement);
    }
    emit revisionsChanged();
    return result;
}

QVariantMap TranscriptRevisionViewModel::revisionDetails(const QString& jobId) const {
    return m_model.revisionMap(jobId);
}

bool TranscriptRevisionViewModel::contains(const QString& jobId) const {
    return m_model.contains(jobId);
}

bool TranscriptRevisionViewModel::selectedRevisionIsRunning() const {
    return revisionDetails(m_selectedJobId).value(QStringLiteral("isRunning")).toBool();
}

void TranscriptRevisionViewModel::setSelectedJobId(const QString& jobId) {
    if (m_selectedJobId == jobId) {
        return;
    }
    m_selectedJobId = jobId;
    m_model.setSelectedJobId(jobId);
    emit selectionChanged();
}

void TranscriptRevisionViewModel::setActiveJobId(const QString& jobId) {
    if (m_activeJobId == jobId) {
        return;
    }
    m_activeJobId = jobId;
    m_model.setActiveJobId(jobId);
    emit activeRevisionChanged();
}

void TranscriptRevisionViewModel::setFollowingLive(const bool following) {
    if (m_followingLive == following) {
        return;
    }
    m_followingLive = following;
    emit followLiveChanged();
}

QString TranscriptRevisionViewModel::followTargetJobId() const {
    if (!m_liveJobId.isEmpty() && m_model.contains(m_liveJobId)) {
        return m_liveJobId;
    }
    if (!m_activeJobId.isEmpty() && m_model.contains(m_activeJobId)) {
        return m_activeJobId;
    }
    return {};
}

} // namespace BreezeDesk
