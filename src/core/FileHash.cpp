#include "breezedesk/core/FileHash.h"

#include <QCryptographicHash>
#include <QFile>

namespace BreezeDesk {

QString FileHash::sha256(const QString& path, QString* error) {
    if (error != nullptr) {
        error->clear();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    constexpr qint64 ReadBufferSize = 1024 * 1024;
    while (!file.atEnd()) {
        const QByteArray data = file.read(ReadBufferSize);
        if (data.isEmpty()) {
            if (file.error() != QFileDevice::NoError && error != nullptr) {
                *error = file.errorString();
            }
            if (file.error() != QFileDevice::NoError) {
                return {};
            }
            break;
        }
        hash.addData(data);
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace BreezeDesk
