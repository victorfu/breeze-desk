#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/database/Recording.h"

#include <optional>

namespace BreezeDesk {

class IRecordingRepository {
  public:
    virtual ~IRecordingRepository() = default;

    [[nodiscard]] virtual Result<void> create(Recording recording) = 0;
    [[nodiscard]] virtual Result<void> update(const Recording& recording) = 0;
    [[nodiscard]] virtual Result<std::optional<Recording>> findById(const QString& id) const = 0;
    [[nodiscard]] virtual Result<std::optional<Recording>>
    findBySourcePath(const QString& sourcePath) const = 0;
    [[nodiscard]] virtual Result<RecordingPage> list(const RecordingQuery& query) const = 0;
    [[nodiscard]] virtual Result<void> setTags(const QString& recordingId, const QStringList& tags) = 0;
    [[nodiscard]] virtual Result<void> moveToTrash(const QString& id) = 0;
    [[nodiscard]] virtual Result<void> restore(const QString& id) = 0;
    [[nodiscard]] virtual Result<void> permanentlyDelete(const QString& id) = 0;
    [[nodiscard]] virtual Result<void> setActiveTranscriptJob(const QString& recordingId,
                                                              const QString& jobId) = 0;
};

} // namespace BreezeDesk
