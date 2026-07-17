#pragma once

#include <QtCore/QString>

namespace BreezeDesk::Asr {

struct DeduplicationResult {
    QString text;
    qsizetype matchedUnits = 0;
    bool ambiguous = false;
    QString diagnostic;
};

class OverlapDeduplicator final {
  public:
    [[nodiscard]] static DeduplicationResult
    deduplicate(const QString& previousText, const QString& incomingText, bool timestampsOverlap = true);
};

} // namespace BreezeDesk::Asr
