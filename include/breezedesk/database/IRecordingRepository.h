#pragma once

#include "breezedesk/core/Result.h"
#include "breezedesk/database/Recording.h"

#include <optional>

namespace BreezeDesk {

class IRecordingRepository {
  public:
    virtual ~IRecordingRepository() = default;

    [[nodiscard]] virtual Result<void> create(Recording recording) = 0;
    // A leased update writes only execution-derived media/cache fields. An ownerless update is a
    // full-row replacement and is rejected while this recording has an active execution lease.
    [[nodiscard]] virtual Result<void> update(const Recording& recording, const QString& jobId = {},
                                              const QString& ownerToken = {}) = 0;
    [[nodiscard]] virtual Result<void> updateTitle(const QString& recordingId,
                                                   const QString& title) = 0;
    [[nodiscard]] virtual Result<void> updateNotes(const QString& recordingId,
                                                   const QString& notes) = 0;
    [[nodiscard]] virtual Result<void> updateReviewState(const QString& recordingId,
                                                         const QString& reviewState) = 0;
    [[nodiscard]] virtual Result<void> relinkSource(const QString& recordingId,
                                                    const QString& sourcePath,
                                                    bool clearDerivedArtifacts) = 0;
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
