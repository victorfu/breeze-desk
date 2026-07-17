#include "breezedesk/jobs/JobStateMachine.h"

#include <QHash>
#include <QSet>

namespace BreezeDesk {
namespace {
const QHash<JobState, QSet<JobState>>& transitions() {
    static const QHash<JobState, QSet<JobState>> values = {
        {JobState::Queued, {JobState::Preparing, JobState::Cancelled}},
        {JobState::Preparing,
         {JobState::Normalizing, JobState::AnalyzingSpeech, JobState::WaitingForModel, JobState::LoadingModel,
          JobState::Cancelling, JobState::Failed, JobState::Interrupted}},
        {JobState::Normalizing,
         {JobState::WaitingForModel, JobState::LoadingModel, JobState::AnalyzingSpeech, JobState::Cancelling,
          JobState::Failed, JobState::Interrupted}},
        {JobState::WaitingForModel,
         {JobState::LoadingModel, JobState::Cancelling, JobState::Failed, JobState::Interrupted}},
        {JobState::LoadingModel,
         {JobState::AnalyzingSpeech, JobState::Transcribing, JobState::Cancelling, JobState::Failed,
          JobState::Interrupted}},
        {JobState::AnalyzingSpeech,
         {JobState::WaitingForModel, JobState::LoadingModel, JobState::Transcribing, JobState::Cancelling,
          JobState::Failed, JobState::Interrupted}},
        {JobState::Transcribing,
         {JobState::Finalizing, JobState::Cancelling, JobState::Failed, JobState::Interrupted}},
        {JobState::Finalizing,
         {JobState::Completed, JobState::Cancelling, JobState::Failed, JobState::Interrupted}},
        {JobState::Cancelling, {JobState::Cancelled, JobState::Interrupted, JobState::Failed}},
        {JobState::Interrupted, {JobState::Queued, JobState::Cancelled}},
        {JobState::Failed, {JobState::Queued}},
        {JobState::Completed, {}},
        {JobState::Cancelled, {JobState::Queued}},
    };
    return values;
}
} // namespace

bool JobStateMachine::canTransition(const JobState from, const JobState to) {
    return from == to || transitions().value(from).contains(to);
}

QList<JobState> JobStateMachine::allowedTransitions(const JobState from) {
    return transitions().value(from).values();
}

Result<void> JobStateMachine::validateTransition(const JobState from, const JobState to) {
    if (canTransition(from, to))
        return Result<void>::success();
    return Result<void>::failure(UserFacingError::validation(
        ErrorCode::InvalidStateTransition,
        QStringLiteral("A job cannot move from %1 to %2.").arg(jobStateName(from), jobStateName(to))));
}

bool JobStateMachine::isTerminal(const JobState state) {
    return state == JobState::Completed || state == JobState::Cancelled || state == JobState::Failed;
}

bool JobStateMachine::isRunning(const JobState state) {
    return state != JobState::Queued && !isTerminal(state) && state != JobState::Interrupted;
}

double MonotonicJobProgress::map(const JobStage stage, const double stageFraction) {
    struct Range {
        double begin;
        double end;
    };
    static const QHash<JobStage, Range> ranges = {
        {JobStage::Preparing, {0.00, 0.02}},        {JobStage::InspectingMedia, {0.02, 0.05}},
        {JobStage::NormalizingAudio, {0.05, 0.20}}, {JobStage::AnalyzingSpeech, {0.20, 0.30}},
        {JobStage::LoadingModel, {0.30, 0.35}},     {JobStage::Transcribing, {0.35, 0.95}},
        {JobStage::Finalizing, {0.95, 1.00}},       {JobStage::Completed, {1.00, 1.00}},
    };
    const Range range = ranges.value(stage, {0.0, 0.0});
    return range.begin + (range.end - range.begin) * qBound(0.0, stageFraction, 1.0);
}

double MonotonicJobProgress::advance(const JobStage stage, const double stageFraction) {
    m_stage = stage;
    m_value = qMax(m_value, map(stage, stageFraction));
    return m_value;
}

void MonotonicJobProgress::restore(const JobStage stage, const double progress) {
    m_stage = stage;
    m_value = qBound(0.0, progress, 1.0);
}

} // namespace BreezeDesk
