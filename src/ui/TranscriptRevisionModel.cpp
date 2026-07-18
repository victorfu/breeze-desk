#include "breezedesk/ui/TranscriptRevisionModel.h"

#include "breezedesk/jobs/JobStateMachine.h"

#include <algorithm>

namespace BreezeDesk {
namespace {
bool canDeleteRevision(const JobState state) {
    return JobStateMachine::isTerminal(state) || state == JobState::Interrupted;
}

QString latestText(const TranscriptRevisionSummary& revision) {
    return revision.latestSegment.has_value() ? revision.latestSegment->displayText() : QString{};
}
} // namespace

TranscriptRevisionModel::TranscriptRevisionModel(QObject* parent) : QAbstractListModel(parent) {}

int TranscriptRevisionModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_revisions.size());
}

QVariant TranscriptRevisionModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_revisions.size()) {
        return {};
    }
    return valueFor(m_revisions.at(index.row()), role);
}

QHash<int, QByteArray> TranscriptRevisionModel::roleNames() const {
    return {{JobIdRole, "jobId"},
            {RevisionNumberRole, "revisionNumber"},
            {StateRole, "jobState"},
            {StageRole, "stage"},
            {ProgressRole, "progress"},
            {ModelIdRole, "modelId"},
            {BackendRole, "backend"},
            {LanguageRole, "language"},
            {PresetRole, "preset"},
            {CreatedAtRole, "createdAt"},
            {CompletedAtRole, "completedAt"},
            {SegmentCountRole, "segmentCount"},
            {HasManualEditsRole, "hasManualEdits"},
            {HasProvisionalSegmentsRole, "hasProvisionalSegments"},
            {LatestTextRole, "latestText"},
            {QueueHiddenRole, "queueHidden"},
            {ActiveRole, "isActiveRevision"},
            {SelectedRole, "isSelectedRevision"},
            {RunningRole, "isRunning"},
            {CanDeleteRole, "canDelete"},
            {ErrorCodeRole, "errorCode"},
            {ErrorMessageRole, "errorMessage"},
            {DisplayLabelRole, "displayLabel"}};
}

void TranscriptRevisionModel::replaceRevisions(QList<TranscriptRevisionSummary> revisions,
                                                const QString& selectedJobId,
                                                const QString& activeJobId) {
    std::stable_sort(revisions.begin(), revisions.end(),
                     [](const TranscriptRevisionSummary& left, const TranscriptRevisionSummary& right) {
                         if (left.job.revisionNumber != right.job.revisionNumber) {
                             return left.job.revisionNumber > right.job.revisionNumber;
                         }
                         if (left.job.createdAt != right.job.createdAt) {
                             return left.job.createdAt > right.job.createdAt;
                         }
                         return left.job.id > right.job.id;
                     });
    beginResetModel();
    m_revisions = std::move(revisions);
    m_selectedJobId = selectedJobId;
    m_activeJobId = activeJobId;
    endResetModel();
}

void TranscriptRevisionModel::setSelectedJobId(const QString& jobId) {
    if (m_selectedJobId == jobId) {
        return;
    }
    const QString previous = m_selectedJobId;
    m_selectedJobId = jobId;
    emitFlagChanges(previous, m_selectedJobId, SelectedRole);
}

void TranscriptRevisionModel::setActiveJobId(const QString& jobId) {
    if (m_activeJobId == jobId) {
        return;
    }
    const QString previous = m_activeJobId;
    m_activeJobId = jobId;
    emitFlagChanges(previous, m_activeJobId, ActiveRole);
}

bool TranscriptRevisionModel::contains(const QString& jobId) const {
    return indexOf(jobId) >= 0;
}

int TranscriptRevisionModel::indexOf(const QString& jobId) const {
    if (jobId.isEmpty()) {
        return -1;
    }
    for (int row = 0; row < m_revisions.size(); ++row) {
        if (m_revisions.at(row).job.id == jobId) {
            return row;
        }
    }
    return -1;
}

QString TranscriptRevisionModel::newestJobId() const {
    return m_revisions.isEmpty() ? QString{} : m_revisions.constFirst().job.id;
}

bool TranscriptRevisionModel::hasRevisionNewerThan(const QString& jobId) const {
    const int row = indexOf(jobId);
    return row > 0;
}

std::optional<TranscriptRevisionSummary> TranscriptRevisionModel::revision(const QString& jobId) const {
    const int row = indexOf(jobId);
    return row < 0 ? std::nullopt
                   : std::optional<TranscriptRevisionSummary>{m_revisions.at(row)};
}

QVariantMap TranscriptRevisionModel::revisionMap(const QString& jobId) const {
    const int row = indexOf(jobId);
    if (row < 0) {
        return {};
    }
    const auto& item = m_revisions.at(row);
    QVariantMap result;
    const auto roles = roleNames();
    for (auto iterator = roles.cbegin(); iterator != roles.cend(); ++iterator) {
        result.insert(QString::fromLatin1(iterator.value()), valueFor(item, iterator.key()));
    }
    return result;
}

QVariant TranscriptRevisionModel::valueFor(const TranscriptRevisionSummary& revision, const int role) const {
    const TranscriptionJob& job = revision.job;
    switch (role) {
    case JobIdRole:
        return job.id;
    case RevisionNumberRole:
        return job.revisionNumber;
    case StateRole:
        return jobStateName(job.state);
    case StageRole:
        return jobStageName(job.stage);
    case ProgressRole:
        return job.progress;
    case ModelIdRole:
        return job.modelId;
    case BackendRole:
        return job.backend;
    case LanguageRole:
        return job.language;
    case PresetRole:
        return job.preset;
    case CreatedAtRole:
        return job.createdAt;
    case CompletedAtRole:
        return job.completedAt;
    case SegmentCountRole:
        return revision.segmentCount;
    case HasManualEditsRole:
        return revision.hasManualEdits;
    case HasProvisionalSegmentsRole:
        return revision.hasProvisionalSegments;
    case LatestTextRole:
        return latestText(revision);
    case QueueHiddenRole:
        return revision.queueHidden || job.queueHidden;
    case ActiveRole:
        return m_activeJobId.isEmpty() ? revision.active : job.id == m_activeJobId;
    case SelectedRole:
        return job.id == m_selectedJobId;
    case RunningRole:
        return JobStateMachine::isRunning(job.state);
    case CanDeleteRole:
        return canDeleteRevision(job.state);
    case ErrorCodeRole:
        return job.errorCode;
    case ErrorMessageRole:
        return job.errorMessage;
    case DisplayLabelRole:
        return tr("Version %1 · %2").arg(job.revisionNumber).arg(jobStateName(job.state));
    default:
        return {};
    }
}

void TranscriptRevisionModel::emitFlagChanges(const QString& previousJobId, const QString& currentJobId,
                                              const int role) {
    const int previousRow = indexOf(previousJobId);
    if (previousRow >= 0) {
        emit dataChanged(index(previousRow), index(previousRow), {role});
    }
    const int currentRow = indexOf(currentJobId);
    if (currentRow >= 0 && currentRow != previousRow) {
        emit dataChanged(index(currentRow), index(currentRow), {role});
    }
}

} // namespace BreezeDesk
