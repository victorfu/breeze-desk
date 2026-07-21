#include "breezedesk/models/ModelManager.h"

#include "breezedesk/core/StoragePaths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace BreezeDesk {

namespace {
constexpr auto CustomChecksumSuffix = ".sha256";

bool isValidSha256(const QByteArray& sha256) {
    return sha256.size() == 64 && std::all_of(sha256.cbegin(), sha256.cend(), [](const char character) {
               return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                      (character >= 'A' && character <= 'F');
           });
}

QByteArray sha256File(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(4 * 1024 * 1024));
    }
    return hash.result().toHex();
}

QString safeCustomModelName(QString name) {
    static const QRegularExpression unsafeCharacters(QStringLiteral(R"([<>:\"/\\|?*\x00-\x1f])"));
    name = name.simplified();
    name.replace(unsafeCharacters, QStringLiteral("_"));
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
        name.chop(1);
    }
    return name.isEmpty() ? QStringLiteral("Custom model") : name.left(64);
}
} // namespace

ModelManager::ModelManager(QObject* parent)
    : QObject(parent), m_manifest(ModelManifest::loadBundled()), m_network(new QNetworkAccessManager(this)) {
    QDir().mkpath(modelsDirectory());
    discoverCustomModels();
}

const ModelManifest& ModelManager::manifest() const {
    return m_manifest;
}

QString ModelManager::modelsDirectory() const {
    return StoragePaths::models();
}

QString ModelManager::defaultModelId() const {
    return m_defaultModelId;
}

void ModelManager::setDefaultModelId(const QString& id) {
    if (id == defaultModelId() || (m_manifest.find(id) == nullptr && !m_customModels.contains(id))) {
        return;
    }
    m_defaultModelId = id;
    emit defaultModelIdChanged();
}

bool ModelManager::isInstalled(const QString& id) const {
    const QString path = modelPath(id);
    return !path.isEmpty() && QFileInfo(path).isFile();
}

QString ModelManager::modelPath(const QString& id) const {
    if (const ModelManifestEntry* entry = m_manifest.find(id)) {
        return QDir(modelsDirectory()).filePath(entry->fileName);
    }
    return m_customModels.value(id);
}

QByteArray ModelManager::expectedSha256(const QString& id) const {
    if (const ModelManifestEntry* entry = m_manifest.find(id)) {
        return entry->sha256;
    }
    return m_customModelSha256.value(id);
}

QList<CustomModelInfo> ModelManager::customModels() const {
    QList<CustomModelInfo> models;
    models.reserve(m_customModels.size());
    for (auto iterator = m_customModels.cbegin(); iterator != m_customModels.cend(); ++iterator) {
        const QFileInfo file(iterator.value());
        if (file.isFile()) {
            models.append({iterator.key(), m_customModelNames.value(iterator.key(), iterator.key()),
                           file.absoluteFilePath(), file.size(), m_customModelSha256.value(iterator.key())});
        }
    }
    std::sort(models.begin(), models.end(), [](const CustomModelInfo& left, const CustomModelInfo& right) {
        return left.displayName.localeAwareCompare(right.displayName) < 0;
    });
    return models;
}

bool ModelManager::verify(const QString& id, QString* error) const {
    const ModelManifestEntry* entry = m_manifest.find(id);
    const QString path = modelPath(id);
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        if (error != nullptr) {
            *error = QStringLiteral("Model is not installed.");
        }
        return false;
    }
    const QByteArray expected = expectedSha256(id);
    if (!isValidSha256(expected)) {
        if (error != nullptr) {
            *error = QStringLiteral("Model does not have a trusted SHA-256.");
        }
        return false;
    }
    if ((entry != nullptr && QFileInfo(path).size() != entry->fileSize) ||
        sha256File(path) != expected.toLower()) {
        if (error != nullptr) {
            *error = QStringLiteral("Model checksum does not match the manifest.");
        }
        return false;
    }
    return true;
}

bool ModelManager::importCustomModel(const QString& sourcePath, const QString& displayName, QString* modelId,
                                     QString* error) {
    if (QFileInfo(sourcePath).suffix().compare(QStringLiteral("bin"), Qt::CaseInsensitive) != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("Custom whisper.cpp models must use the .bin extension.");
        }
        return false;
    }
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly) || source.size() <= 1024) {
        if (error != nullptr) {
            *error = source.errorString().isEmpty() ? QStringLiteral("Custom model is not a valid GGML file.")
                                                    : source.errorString();
        }
        return false;
    }
    const QString id = QStringLiteral("custom-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString safeName = safeCustomModelName(displayName);
    const QString destination =
        QDir(modelsDirectory()).filePath(id + QStringLiteral("-") + safeName + QStringLiteral(".bin"));
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = output.errorString();
        }
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!source.atEnd()) {
        const QByteArray block = source.read(4 * 1024 * 1024);
        if (block.isEmpty() || output.write(block) != block.size()) {
            output.cancelWriting();
            if (error != nullptr) {
                *error = QStringLiteral("Custom model could not be copied.");
            }
            return false;
        }
        hash.addData(block);
    }
    if (!output.commit()) {
        if (error != nullptr) {
            *error = output.errorString();
        }
        return false;
    }
    const QByteArray sha256 = hash.result().toHex();
    QSaveFile checksumFile(destination + QString::fromLatin1(CustomChecksumSuffix));
    if (!checksumFile.open(QIODevice::WriteOnly) || checksumFile.write(sha256 + '\n') != sha256.size() + 1 ||
        !checksumFile.commit()) {
        checksumFile.cancelWriting();
        QFile::remove(destination);
        if (error != nullptr) {
            *error = QStringLiteral("The custom model checksum could not be saved.");
        }
        return false;
    }
    m_customModels.insert(id, destination);
    m_customModelNames.insert(id, safeName);
    m_customModelSha256.insert(id, sha256);
    if (modelId != nullptr) {
        *modelId = id;
    }
    emit modelsChanged();
    return true;
}

bool ModelManager::removeModel(const QString& id, QString* error) {
    if (m_modelsInUse.contains(id)) {
        if (error != nullptr) {
            *error =
                QStringLiteral("The model is currently loaded by the ASR worker. Unload it before deleting.");
        }
        return false;
    }
    const QString path = modelPath(id);
    if (path.isEmpty() || (!QFile::remove(path) && QFileInfo::exists(path))) {
        if (error != nullptr) {
            *error = QStringLiteral("The model file could not be removed.");
        }
        return false;
    }
    QFile::remove(path + QString::fromLatin1(CustomChecksumSuffix));
    m_customModels.remove(id);
    m_customModelNames.remove(id);
    m_customModelSha256.remove(id);
    emit modelsChanged();
    return true;
}

void ModelManager::discoverCustomModels() {
    static const QRegularExpression filePattern(QStringLiteral(
        R"(^(custom-[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})-(.+)\.bin$)"));
    const QFileInfoList files =
        QDir(modelsDirectory())
            .entryInfoList({QStringLiteral("custom-*.bin")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& file : files) {
        const QRegularExpressionMatch match = filePattern.match(file.fileName());
        if (!match.hasMatch() || file.size() <= 1'024) {
            continue;
        }
        QFile checksumFile(file.absoluteFilePath() + QString::fromLatin1(CustomChecksumSuffix));
        if (!checksumFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray sha256 = checksumFile.readAll().trimmed();
        if (!isValidSha256(sha256)) {
            continue;
        }
        const QString id = match.captured(1);
        m_customModels.insert(id, file.absoluteFilePath());
        m_customModelNames.insert(id, match.captured(2));
        m_customModelSha256.insert(id, sha256.toLower());
    }
}

void ModelManager::setModelInUse(const QString& id, bool inUse) {
    if (inUse) {
        m_modelsInUse.insert(id);
    } else {
        m_modelsInUse.remove(id);
    }
    emit modelsChanged();
}

ModelDownloadOperation* ModelManager::download(const QString& id) {
    const ModelManifestEntry* entry = m_manifest.find(id);
    if (entry == nullptr) {
        return nullptr;
    }
    if (ModelDownloadOperation* existing = m_downloads.value(id); existing != nullptr) {
        return existing;
    }
    m_downloads.remove(id);
    auto* operation = new ModelDownloadOperation(*entry, modelsDirectory(), m_network, this);
    m_downloads.insert(id, operation);
    connect(operation, &ModelDownloadOperation::finished, this, [this, id, operation](bool, const QString&) {
        if (m_downloads.value(id) == operation) {
            m_downloads.remove(id);
        }
        emit modelsChanged();
        operation->deleteLater();
    });
    QTimer::singleShot(0, operation, &ModelDownloadOperation::start);
    return operation;
}

} // namespace BreezeDesk
