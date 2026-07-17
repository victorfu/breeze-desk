#include "breezedesk/jobs/JobRecoveryService.h"

#include "breezedesk/app_config.h"
#include "breezedesk/jobs/IJobRepository.h"

namespace BreezeDesk {

JobRecoveryService::JobRecoveryService(IJobRepository& repository) : m_repository(repository) {}

Result<int> JobRecoveryService::recoverAfterAbnormalShutdown() {
    return m_repository.markRunningJobsInterrupted(
        QStringLiteral("%1 stopped before the transcription job finished. Completed chunks were preserved.")
            .arg(QString::fromLatin1(AppConfig::ProductName)));
}

} // namespace BreezeDesk
