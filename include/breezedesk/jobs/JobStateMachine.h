#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/jobs/TranscriptionJob.h"

#include <QList>

namespace BreezeDesk {

class JobStateMachine final {
  public:
    [[nodiscard]] static bool canTransition(JobState from, JobState to);
    [[nodiscard]] static QList<JobState> allowedTransitions(JobState from);
    [[nodiscard]] static Result<void> validateTransition(JobState from, JobState to);
    [[nodiscard]] static bool isTerminal(JobState state);
    [[nodiscard]] static bool isRunning(JobState state);
};

class MonotonicJobProgress final {
  public:
    [[nodiscard]] double value() const noexcept { return m_value; }
    [[nodiscard]] JobStage stage() const noexcept { return m_stage; }
    [[nodiscard]] double advance(JobStage stage, double stageFraction);
    void restore(JobStage stage, double progress);

    [[nodiscard]] static double map(JobStage stage, double stageFraction);

  private:
    JobStage m_stage = JobStage::Preparing;
    double m_value = 0.0;
};

} // namespace BreezeDesk
