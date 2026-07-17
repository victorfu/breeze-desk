#pragma once

#include "breezedesk/database/IRecordingRepository.h"

namespace BreezeDesk {

class DatabaseManager;

class SqliteRecordingRepository final : public IRecordingRepository {
  public:
    explicit SqliteRecordingRepository(DatabaseManager& databaseManager);

    [[nodiscard]] Result<void> create(Recording recording) override;
    [[nodiscard]] Result<void> update(const Recording& recording) override;
    [[nodiscard]] Result<std::optional<Recording>> findById(const QString& id) const override;
    [[nodiscard]] Result<std::optional<Recording>> findBySourcePath(const QString& sourcePath) const override;
    [[nodiscard]] Result<RecordingPage> list(const RecordingQuery& query) const override;
    [[nodiscard]] Result<void> setTags(const QString& recordingId, const QStringList& tags) override;
    [[nodiscard]] Result<void> moveToTrash(const QString& id) override;
    [[nodiscard]] Result<void> restore(const QString& id) override;
    [[nodiscard]] Result<void> permanentlyDelete(const QString& id) override;
    [[nodiscard]] Result<void> setActiveTranscriptJob(const QString& recordingId,
                                                      const QString& jobId) override;

  private:
    DatabaseManager& m_databaseManager;
};

} // namespace BreezeDesk
