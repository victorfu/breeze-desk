#pragma once

#include "breezedesk/database/IRecordingRepository.h"

#include <functional>

namespace BreezeDesk {

class DatabaseManager;

class SqliteRecordingRepository final : public IRecordingRepository {
  public:
    using ArtifactFileRemover = std::function<bool(const QString&, QString*)>;
    using ManagedReferenceWriteHook = std::function<void()>;

    explicit SqliteRecordingRepository(DatabaseManager& databaseManager,
                                       ArtifactFileRemover artifactFileRemover = {},
                                       ManagedReferenceWriteHook managedReferenceWriteHook = {});

    [[nodiscard]] Result<void> create(Recording recording) override;
    [[nodiscard]] Result<void> update(const Recording& recording, const QString& jobId = {},
                                      const QString& ownerToken = {}) override;
    [[nodiscard]] Result<void> updateTitle(const QString& recordingId,
                                           const QString& title) override;
    [[nodiscard]] Result<void> updateNotes(const QString& recordingId,
                                           const QString& notes) override;
    [[nodiscard]] Result<void> updateReviewState(const QString& recordingId,
                                                 const QString& reviewState) override;
    [[nodiscard]] Result<void> relinkSource(const QString& recordingId, const QString& sourcePath,
                                            bool clearDerivedArtifacts) override;
    [[nodiscard]] Result<std::optional<Recording>> findById(const QString& id) const override;
    [[nodiscard]] Result<std::optional<Recording>> findBySourcePath(const QString& sourcePath) const override;
    [[nodiscard]] Result<RecordingPage> list(const RecordingQuery& query) const override;
    [[nodiscard]] Result<void> setTags(const QString& recordingId, const QStringList& tags) override;
    [[nodiscard]] Result<void> moveToTrash(const QString& id) override;
    [[nodiscard]] Result<void> restore(const QString& id) override;
    [[nodiscard]] Result<void> permanentlyDelete(const QString& id) override;
    [[nodiscard]] Result<PendingArtifactDeletionReport>
    drainPendingArtifactDeletions(const QString& recordingId = {}) override;
    [[nodiscard]] Result<void> setActiveTranscriptJob(const QString& recordingId,
                                                      const QString& jobId) override;

  private:
    DatabaseManager& m_databaseManager;
    ArtifactFileRemover m_artifactFileRemover;
    ManagedReferenceWriteHook m_managedReferenceWriteHook;
};

} // namespace BreezeDesk
