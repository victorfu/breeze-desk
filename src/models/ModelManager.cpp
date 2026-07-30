#include "breezedesk/models/ModelManager.h"

#include "breezedesk/core/StoragePaths.h"

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>
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

void ModelManager::assertOwnerThread(const char* operation) const {
    Q_ASSERT_X(QThread::currentThread() == thread(), "ModelManager", operation);
}

const ModelManifest& ModelManager::manifest() const {
    return m_manifest;
}

QString ModelManager::modelsDirectory() const {
    return StoragePaths::models();
}

QString ModelManager::defaultModelId() const {
    assertOwnerThread("defaultModelId must run on the ModelManager owner thread");
    return m_defaultModelId;
}

void ModelManager::setDefaultModelId(const QString& id) {
    assertOwnerThread("setDefaultModelId must run on the ModelManager owner thread");
    if (id == defaultModelId() || (m_manifest.find(id) == nullptr && !m_customModels.contains(id))) {
        return;
    }
    m_defaultModelId = id;
    emit defaultModelIdChanged();
}

bool ModelManager::isInstalled(const QString& id) const {
    assertOwnerThread("isInstalled must run on the ModelManager owner thread");
    const QString path = modelPath(id);
    return !path.isEmpty() && QFileInfo(path).isFile();
}

QString ModelManager::modelPath(const QString& id) const {
    assertOwnerThread("modelPath must run on the ModelManager owner thread");
    if (const ModelManifestEntry* entry = m_manifest.find(id)) {
        return QDir(modelsDirectory()).filePath(entry->fileName);
    }
    return m_customModels.value(id);
}

QByteArray ModelManager::expectedSha256(const QString& id) const {
    assertOwnerThread("expectedSha256 must run on the ModelManager owner thread");
    if (const ModelManifestEntry* entry = m_manifest.find(id)) {
        return entry->sha256;
    }
    return m_customModelSha256.value(id);
}

QList<CustomModelInfo> ModelManager::customModels() const {
    assertOwnerThread("customModels must run on the ModelManager owner thread");
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

ModelVerificationSnapshot ModelManager::verificationSnapshot(const QString& id) const {
    assertOwnerThread("verificationSnapshot must run on the ModelManager owner thread");
    ModelVerificationSnapshot snapshot;
    snapshot.id = id;
    snapshot.path = modelPath(id);
    snapshot.expectedSha256 = expectedSha256(id);
    if (const ModelManifestEntry* entry = m_manifest.find(id)) {
        snapshot.expectedFileSize = entry->fileSize;
    }
    return snapshot;
}

CustomModelImportRequest ModelManager::customModelImportRequest(const QString& sourcePath,
                                                                const QString& displayName) const {
    assertOwnerThread("customModelImportRequest must run on the ModelManager owner thread");
    CustomModelImportRequest request;
    request.sourcePath = sourcePath;
    request.displayName = safeCustomModelName(displayName);
    do {
        request.id = QStringLiteral("custom-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        request.destinationPath = QDir(modelsDirectory())
                                      .filePath(request.id + QStringLiteral("-") + request.displayName +
                                                QStringLiteral(".bin"));
        request.stagingPath = request.destinationPath + QStringLiteral(".importing");
        request.checksumPath = request.destinationPath + QString::fromLatin1(CustomChecksumSuffix);
    } while (m_customModels.contains(request.id) || QFileInfo::exists(request.destinationPath) ||
             QFileInfo::exists(request.stagingPath) || QFileInfo::exists(request.checksumPath));
    return request;
}

bool ModelManager::commitCustomModelImport(const PreparedCustomModelImport& prepared, QString* error) {
    assertOwnerThread("commitCustomModelImport must run on the ModelManager owner thread");
    const CustomModelImportRequest& request = prepared.request;
    if (!prepared.success || prepared.cancelled || request.id.isEmpty() || request.stagingPath.isEmpty() ||
        request.destinationPath.isEmpty() || request.checksumPath.isEmpty() ||
        prepared.fileSize <= 1024 || !isValidSha256(prepared.sha256) ||
        !QFileInfo(request.stagingPath).isFile() ||
        QFileInfo(request.stagingPath).size() != prepared.fileSize ||
        QFileInfo::exists(request.destinationPath) || QFileInfo::exists(request.checksumPath) ||
        m_customModels.contains(request.id)) {
        if (error != nullptr) {
            *error = prepared.error.isEmpty() ? QStringLiteral("The custom model import could not be published.")
                                              : prepared.error;
        }
        return false;
    }

    if (!QFile::rename(request.stagingPath, request.destinationPath)) {
        if (error != nullptr) {
            *error = QStringLiteral("The custom model could not be published.");
        }
        return false;
    }
    QSaveFile checksumFile(request.checksumPath);
    if (!checksumFile.open(QIODevice::WriteOnly) ||
        checksumFile.write(prepared.sha256 + '\n') != prepared.sha256.size() + 1 ||
        !checksumFile.commit()) {
        checksumFile.cancelWriting();
        QFile::remove(request.checksumPath);
        QFile::remove(request.destinationPath);
        if (error != nullptr) {
            *error = QStringLiteral("The custom model checksum could not be saved.");
        }
        return false;
    }

    m_customModels.insert(request.id, request.destinationPath);
    m_customModelNames.insert(request.id, request.displayName);
    m_customModelSha256.insert(request.id, prepared.sha256.toLower());
    emit modelsChanged();
    return true;
}

bool ModelManager::verify(const QString& id, QString* error) const {
    assertOwnerThread("verify must run on the ModelManager owner thread");
    const ModelVerificationResult result = ModelFileOperations::verify(verificationSnapshot(id));
    if (!result.valid && error != nullptr) {
        *error = result.error;
    }
    return result.valid;
}

bool ModelManager::importCustomModel(const QString& sourcePath, const QString& displayName, QString* modelId,
                                     QString* error) {
    assertOwnerThread("importCustomModel must run on the ModelManager owner thread");
    const CustomModelImportRequest request = customModelImportRequest(sourcePath, displayName);
    const PreparedCustomModelImport prepared = ModelFileOperations::prepareImport(request);
    if (!prepared.success) {
        if (error != nullptr) {
            *error = prepared.error;
        }
        return false;
    }
    if (!commitCustomModelImport(prepared, error)) {
        ModelFileOperations::cleanupPreparedImport(prepared);
        return false;
    }
    if (modelId != nullptr) {
        *modelId = request.id;
    }
    return true;
}

bool ModelManager::removeModel(const QString& id, QString* error) {
    assertOwnerThread("removeModel must run on the ModelManager owner thread");
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
    assertOwnerThread("setModelInUse must run on the ModelManager owner thread");
    if (inUse) {
        m_modelsInUse.insert(id);
    } else {
        m_modelsInUse.remove(id);
    }
    emit modelsChanged();
}

ModelDownloadOperation* ModelManager::download(const QString& id) {
    assertOwnerThread("download must run on the ModelManager owner thread");
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
