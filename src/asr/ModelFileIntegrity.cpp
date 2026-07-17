#include <breezedesk/asr/ModelFileIntegrity.h>

#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

#include <algorithm>

namespace BreezeDesk::Asr {

bool ModelFileIntegrity::isValidSha256(const QByteArray& sha256) noexcept {
    return sha256.size() == 64 && std::all_of(sha256.cbegin(), sha256.cend(), [](const char character) {
               return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                      (character >= 'A' && character <= 'F');
           });
}

AsrError ModelFileIntegrity::verifySha256(const QString& path, const QByteArray& expectedSha256,
                                          QByteArray* actualSha256) {
    if (!isValidSha256(expectedSha256)) {
        return {AsrErrorCode::InvalidRequest,
                QStringLiteral("A trusted SHA-256 is required before loading a model"),
                {}};
    }
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly)) {
        return {AsrErrorCode::ModelFileMissing, QStringLiteral("Model file does not exist or is unreadable"),
                file.errorString().isEmpty() ? path : file.errorString()};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(4 * 1024 * 1024);
        if (block.isEmpty() && !file.atEnd()) {
            return {AsrErrorCode::IoError, QStringLiteral("Unable to verify the model checksum"),
                    file.errorString()};
        }
        hash.addData(block);
    }
    if (file.error() != QFileDevice::NoError) {
        return {AsrErrorCode::IoError, QStringLiteral("Unable to verify the model checksum"),
                file.errorString()};
    }
    const QByteArray actual = hash.result().toHex();
    if (actualSha256 != nullptr) {
        *actualSha256 = actual;
    }
    if (actual != expectedSha256.toLower()) {
        return {AsrErrorCode::ModelChecksumMismatch,
                QStringLiteral("Model checksum does not match the trusted SHA-256"),
                QStringLiteral("Expected %1 but calculated %2")
                    .arg(QString::fromLatin1(expectedSha256.toLower()), QString::fromLatin1(actual))};
    }
    return {};
}

} // namespace BreezeDesk::Asr
