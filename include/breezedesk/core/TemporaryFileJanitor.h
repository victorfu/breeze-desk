#pragma once

#include <QString>
#include <QStringList>

namespace BreezeDesk {

struct TemporaryCleanupPolicy {
    QString directory;
    qint64 maximumAgeMs = 24LL * 60LL * 60LL * 1000LL;
    QStringList protectedPaths;
    bool allowOutsideApplicationTemporaryDirectory = false;
};

struct TemporaryCleanupReport {
    int entriesScanned = 0;
    int filesRemoved = 0;
    int directoriesRemoved = 0;
    int failures = 0;
    qint64 bytesReleased = 0;
    QString error;

    [[nodiscard]] bool succeeded() const noexcept { return error.isEmpty() && failures == 0; }
};

class TemporaryFileJanitor final {
  public:
    [[nodiscard]] static TemporaryCleanupReport clean(TemporaryCleanupPolicy policy = {});

  private:
    TemporaryFileJanitor() = delete;
};

} // namespace BreezeDesk
