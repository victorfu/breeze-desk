#include "breezedesk/models/ModelFileOperations.h"

#include "breezedesk/core/FileHash.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace BreezeDesk {
namespace {

constexpr qint64 ImportBufferSize = 4 * 1024 * 1024;

bool cancellationRequested(const std::atomic_bool* cancellation) {
    return cancellation != nullptr && cancellation->load(std::memory_order_relaxed);
}

bool isValidSha256(const QByteArray& sha256) {
    return sha256.size() == 64 && std::all_of(sha256.cbegin(), sha256.cend(), [](const char character) {
               return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                      (character >= 'A' && character <= 'F');
           });
}

void failImport(PreparedCustomModelImport* prepared, const QString& error, const bool cancelled = false) {
    prepared->error = error;
    prepared->cancelled = cancelled;
    prepared->success = false;
    ModelFileOperations::cleanupPreparedImport(*prepared);
}

} // namespace

ModelVerificationResult ModelFileOperations::verify(const ModelVerificationSnapshot& snapshot,
                                                    const std::atomic_bool* cancellation) {
    ModelVerificationResult result;
    result.id = snapshot.id;
    if (cancellationRequested(cancellation)) {
        result.cancelled = true;
        result.error = QStringLiteral("Model verification was cancelled.");
        return result;
    }

    const QFileInfo file(snapshot.path);
    result.installed = !snapshot.path.isEmpty() && file.isFile();
    if (!result.installed) {
        result.error = QStringLiteral("Model is not installed.");
        return result;
    }
    if (!isValidSha256(snapshot.expectedSha256)) {
        result.error = QStringLiteral("Model does not have a trusted SHA-256.");
        return result;
    }
    if (snapshot.expectedFileSize.has_value() && file.size() != snapshot.expectedFileSize.value()) {
        result.error = QStringLiteral("Model checksum does not match the manifest.");
        return result;
    }

    const QString checksum = FileHash::sha256(snapshot.path, nullptr, cancellation);
    if (cancellationRequested(cancellation)) {
        result.cancelled = true;
        result.error = QStringLiteral("Model verification was cancelled.");
        return result;
    }
    if (checksum.toLatin1() != snapshot.expectedSha256.toLower()) {
        result.error = QStringLiteral("Model checksum does not match the manifest.");
        return result;
    }

    result.valid = true;
    return result;
}

PreparedCustomModelImport ModelFileOperations::prepareImport(const CustomModelImportRequest& request,
                                                             const std::atomic_bool* cancellation) {
    PreparedCustomModelImport prepared;
    prepared.request = request;
    if (cancellationRequested(cancellation)) {
        failImport(&prepared, QStringLiteral("Custom model import was cancelled."), true);
        return prepared;
    }
    if (QFileInfo(request.sourcePath).suffix().compare(QStringLiteral("bin"), Qt::CaseInsensitive) != 0) {
        failImport(&prepared, QStringLiteral("Custom whisper.cpp models must use the .bin extension."));
        return prepared;
    }

    QFile source(request.sourcePath);
    if (!source.open(QIODevice::ReadOnly) || source.size() <= 1024) {
        const QString sourceError = source.errorString();
        failImport(&prepared, sourceError.isEmpty() ? QStringLiteral("Custom model is not a valid GGML file.")
                                                    : sourceError);
        return prepared;
    }

    QSaveFile output(request.stagingPath);
    if (!output.open(QIODevice::WriteOnly)) {
        failImport(&prepared, output.errorString());
        return prepared;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!source.atEnd()) {
        if (cancellationRequested(cancellation)) {
            output.cancelWriting();
            failImport(&prepared, QStringLiteral("Custom model import was cancelled."), true);
            return prepared;
        }
        const QByteArray block = source.read(ImportBufferSize);
        if (block.isEmpty() || output.write(block) != block.size()) {
            output.cancelWriting();
            failImport(&prepared, QStringLiteral("Custom model could not be copied."));
            return prepared;
        }
        hash.addData(block);
        prepared.fileSize += block.size();
    }
    if (cancellationRequested(cancellation)) {
        output.cancelWriting();
        failImport(&prepared, QStringLiteral("Custom model import was cancelled."), true);
        return prepared;
    }
    if (!output.commit()) {
        failImport(&prepared, output.errorString());
        return prepared;
    }
    if (cancellationRequested(cancellation)) {
        failImport(&prepared, QStringLiteral("Custom model import was cancelled."), true);
        return prepared;
    }
    if (prepared.fileSize <= 1024) {
        failImport(&prepared, QStringLiteral("Custom model is not a valid GGML file."));
        return prepared;
    }

    prepared.sha256 = hash.result().toHex();
    prepared.success = true;
    return prepared;
}

void ModelFileOperations::cleanupPreparedImport(const PreparedCustomModelImport& prepared) {
    if (!prepared.request.stagingPath.isEmpty()) {
        QFile::remove(prepared.request.stagingPath);
    }
}

} // namespace BreezeDesk
