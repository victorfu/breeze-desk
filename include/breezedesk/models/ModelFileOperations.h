#pragma once

#include <QByteArray>
#include <QString>

#include <atomic>
#include <optional>

namespace BreezeDesk {

struct ModelVerificationSnapshot {
    QString id;
    QString path;
    QByteArray expectedSha256;
    std::optional<qint64> expectedFileSize;
};

struct ModelVerificationResult {
    QString id;
    QString error;
    bool installed{false};
    bool valid{false};
    bool cancelled{false};
};

struct CustomModelImportRequest {
    QString id;
    QString sourcePath;
    QString displayName;
    QString stagingPath;
    QString destinationPath;
    QString checksumPath;
};

struct PreparedCustomModelImport {
    CustomModelImportRequest request;
    QByteArray sha256;
    QString error;
    qint64 fileSize{0};
    bool success{false};
    bool cancelled{false};
};

class ModelFileOperations final {
  public:
    [[nodiscard]] static ModelVerificationResult
    verify(const ModelVerificationSnapshot& snapshot, const std::atomic_bool* cancellation = nullptr);

    [[nodiscard]] static PreparedCustomModelImport
    prepareImport(const CustomModelImportRequest& request,
                  const std::atomic_bool* cancellation = nullptr);

    static void cleanupPreparedImport(const PreparedCustomModelImport& prepared);
};

} // namespace BreezeDesk
