#pragma once

#include "breezedesk/transcript/ITranscriptRepository.h"

#include <QSqlDatabase>

namespace BreezeDesk {

class DatabaseManager;

class SqliteTranscriptRepository final : public ITranscriptRepository {
  public:
    explicit SqliteTranscriptRepository(DatabaseManager& databaseManager);

    [[nodiscard]] Result<QList<TranscriptSegment>>
    segmentsForJob(const QString& jobId, bool includeProvisional = true) const override;
    [[nodiscard]] Result<std::optional<TranscriptSegment>> segment(const QString& segmentId) const override;
    [[nodiscard]] Result<void> replaceTranscript(const QString& recordingId, const QString& jobId,
                                                 QList<TranscriptSegment> segments) override;
    [[nodiscard]] Result<void> saveEditedTranscript(const QString& recordingId, const QString& jobId,
                                                    QList<TranscriptSegment> segments) override;
    [[nodiscard]] Result<void> replaceChunk(const QString& recordingId, const QString& jobId,
                                            const QString& chunkId, QList<TranscriptSegment> segments,
                                            bool provisional, int attempt,
                                            const QString& ownerToken) override;
    [[nodiscard]] Result<void> saveEditedSegment(const TranscriptSegment& segment) override;
    [[nodiscard]] Result<void> deleteSegment(const QString& segmentId) override;

  private:
    [[nodiscard]] Result<void> insertSegments(QSqlDatabase& database, const QString& recordingId,
                                              const QString& jobId, QList<TranscriptSegment> segments,
                                              bool allowOverlap = false) const;
    DatabaseManager& m_databaseManager;
};

} // namespace BreezeDesk
