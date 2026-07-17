#pragma once

#include "breezedesk/core/Result.h"

#include <QList>
#include <QString>

namespace BreezeDesk {

class DatabaseManager;

struct SearchResult {
    QString recordingId;
    QString segmentId;
    qint64 startMs = 0;
    QString title;
    QString snippet;
};

class DatabaseSearchService final {
  public:
    explicit DatabaseSearchService(DatabaseManager& databaseManager);

    [[nodiscard]] Result<void> rebuildRecording(const QString& recordingId) const;
    [[nodiscard]] Result<void> rebuildAll() const;
    [[nodiscard]] Result<QList<SearchResult>> search(const QString& query, int limit = 100,
                                                     int offset = 0) const;

  private:
    DatabaseManager& m_databaseManager;
};

} // namespace BreezeDesk
