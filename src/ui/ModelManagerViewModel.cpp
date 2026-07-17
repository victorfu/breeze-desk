#include "breezedesk/ui/ModelManagerViewModel.h"

#include "breezedesk/models/ModelDownloadOperation.h"
#include "breezedesk/models/ModelManager.h"
#include "breezedesk/settings/SettingsManagers.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrentRun>

namespace BreezeDesk {
namespace {

constexpr auto RecommendedModelId = "breeze-asr-25-q5";

QString normalizedModelId(const QString& id) {
    if (id == QLatin1String("breeze-q5")) {
        return QStringLiteral("breeze-asr-25-q5");
    }
    if (id == QLatin1String("breeze-q8")) {
        return QStringLiteral("breeze-asr-25-q8");
    }
    if (id == QLatin1String("silero-vad")) {
        return QStringLiteral("silero-vad-v6.2.0");
    }
    return id;
}

QString downloadStateName(ModelDownloadOperation::State state) {
    switch (state) {
    case ModelDownloadOperation::State::Pending:
        return QStringLiteral("Requested");
    case ModelDownloadOperation::State::Downloading:
        return QStringLiteral("Downloading");
    case ModelDownloadOperation::State::Paused:
        return QStringLiteral("Paused");
    case ModelDownloadOperation::State::Verifying:
        return QStringLiteral("Verifying");
    case ModelDownloadOperation::State::Completed:
        return QStringLiteral("Installed");
    case ModelDownloadOperation::State::Cancelled:
        return QStringLiteral("Cancelled");
    case ModelDownloadOperation::State::Failed:
        return QStringLiteral("Failed");
    }
    return QStringLiteral("Failed");
}

bool isTransientOperationState(const QString& state) {
    return state == QLatin1String("Requested") || state == QLatin1String("Downloading") ||
           state == QLatin1String("Paused") || state == QLatin1String("Verifying") ||
           state == QLatin1String("Testing");
}

ModelListModel::ModelItem modelItem(const ModelManifestEntry& entry) {
    ModelListModel::ModelItem item;
    item.id = entry.id;
    item.displayName = entry.displayName;
    item.description = entry.description;
    item.quantization = entry.quantization;
    item.fileSize = entry.fileSize;
    item.sha256 = QString::fromLatin1(entry.sha256);
    item.licenseName = entry.licenseName;
    item.licenseUrl = QUrl(entry.licenseUrl);
    item.sourceUrl = QUrl(entry.sourceRepository + QStringLiteral("/tree/") + entry.sourceRevision);
    item.recommended = entry.isRecommended;
    item.defaultCandidate = entry.capabilities.contains(QStringLiteral("transcription"));
    item.isDefault = entry.id == QLatin1String(RecommendedModelId);
    return item;
}

QList<ModelListModel::ModelItem> bundledManifestItems() {
    QList<ModelListModel::ModelItem> items;
    const ModelManifest manifest = ModelManifest::loadBundled();
    for (const ModelManifestEntry& entry : manifest.entries()) {
        items.append(modelItem(entry));
    }
    return items;
}

struct VerifyOutcome {
    QString id;
    QString error;
    bool installed{false};
    bool valid{false};
};

struct ImportOutcome {
    QString id;
    QString path;
    QString displayName;
    QString error;
    bool success{false};
};

} // namespace

ModelListModel::ModelListModel(QObject* parent)
    : QAbstractListModel(parent), m_models(bundledManifestItems()) {}

int ModelListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_models.size());
}

QVariant ModelListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_models.size()) {
        return {};
    }
    const ModelItem& item = m_models.at(index.row());
    switch (role) {
    case IdRole:
        return item.id;
    case DisplayNameRole:
        return item.displayName;
    case DescriptionRole:
        return item.description;
    case QuantizationRole:
        return item.quantization;
    case FileSizeRole:
        return item.fileSize;
    case Sha256Role:
        return item.sha256;
    case LicenseNameRole:
        return item.licenseName;
    case LicenseUrlRole:
        return item.licenseUrl;
    case SourceUrlRole:
        return item.sourceUrl;
    case RecommendedRole:
        return item.recommended;
    case DefaultCandidateRole:
        return item.defaultCandidate;
    case InstalledRole:
        return item.installed;
    case StateRole:
        return item.state;
    case ProgressRole:
        return item.progress;
    case DownloadSpeedRole:
        return item.downloadSpeed;
    case RemainingSecondsRole:
        return item.remainingSeconds;
    case ChecksumValidRole:
        return item.checksumValid;
    case LoadedRole:
        return item.loaded;
    case BackendRole:
        return item.backend;
    case DefaultRole:
        return item.isDefault;
    default:
        return {};
    }
}

QHash<int, QByteArray> ModelListModel::roleNames() const {
    return {{IdRole, "modelId"},
            {DisplayNameRole, "displayName"},
            {DescriptionRole, "description"},
            {QuantizationRole, "quantization"},
            {FileSizeRole, "fileSize"},
            {Sha256Role, "sha256"},
            {LicenseNameRole, "licenseName"},
            {LicenseUrlRole, "licenseUrl"},
            {SourceUrlRole, "sourceUrl"},
            {RecommendedRole, "recommended"},
            {DefaultCandidateRole, "defaultCandidate"},
            {InstalledRole, "installed"},
            {StateRole, "modelState"},
            {ProgressRole, "progress"},
            {DownloadSpeedRole, "downloadSpeed"},
            {RemainingSecondsRole, "remainingSeconds"},
            {ChecksumValidRole, "checksumValid"},
            {LoadedRole, "loaded"},
            {BackendRole, "backend"},
            {DefaultRole, "isDefault"}};
}

bool ModelListModel::setDefaultModel(const QString& id) {
    const int selected = indexOf(id);
    if (selected < 0) {
        return false;
    }
    for (int row = 0; row < m_models.size(); ++row) {
        const bool shouldBeDefault = row == selected;
        if (m_models[row].isDefault != shouldBeDefault) {
            m_models[row].isDefault = shouldBeDefault;
            emit dataChanged(index(row), index(row), {DefaultRole});
        }
    }
    return true;
}

bool ModelListModel::setState(const QString& id, const QString& state, qreal progress) {
    return setDownloadMetrics(id, state, progress, 0, -1);
}

bool ModelListModel::setDownloadMetrics(const QString& id, const QString& state, qreal progress,
                                        qint64 bytesPerSecond, qint64 remainingSeconds) {
    const int row = indexOf(id);
    if (row < 0) {
        return false;
    }
    auto& item = m_models[row];
    item.state = state;
    item.progress = qBound(0.0, progress, 1.0);
    item.downloadSpeed = qMax<qint64>(0, bytesPerSecond);
    item.remainingSeconds = remainingSeconds;
    emitRowChanged(row);
    return true;
}

bool ModelListModel::setInstalled(const QString& id, bool installed, bool checksumValid) {
    const int row = indexOf(id);
    if (row < 0) {
        return false;
    }
    auto& item = m_models[row];
    item.installed = installed;
    item.checksumValid = checksumValid;
    item.state = installed ? QStringLiteral("Installed") : QStringLiteral("NotInstalled");
    item.progress = installed ? 1.0 : 0.0;
    item.downloadSpeed = 0;
    item.remainingSeconds = -1;
    emitRowChanged(row);
    return true;
}

bool ModelListModel::setLoaded(const QString& id, bool loaded, const QString& backend) {
    const int row = indexOf(id);
    if (row < 0) {
        return false;
    }
    m_models[row].loaded = loaded;
    m_models[row].backend = backend;
    emitRowChanged(row);
    return true;
}

void ModelListModel::replaceModels(QList<ModelItem> models) {
    QHash<QString, ModelItem> previous;
    for (const auto& item : std::as_const(m_models)) {
        previous.insert(item.id, item);
    }
    for (auto& item : models) {
        const auto old = previous.constFind(item.id);
        if (old == previous.cend()) {
            continue;
        }
        if (isTransientOperationState(old->state)) {
            item.state = old->state;
            item.progress = old->progress;
            item.downloadSpeed = old->downloadSpeed;
            item.remainingSeconds = old->remainingSeconds;
        }
        item.checksumValid = item.installed && old->installed && old->checksumValid;
        item.loaded = old->loaded;
        item.backend = old->backend;
        item.isDefault = old->isDefault;
    }
    beginResetModel();
    m_models = std::move(models);
    endResetModel();
}

bool ModelListModel::appendModel(ModelItem model) {
    if (model.id.isEmpty() || contains(model.id)) {
        return false;
    }
    const int row = static_cast<int>(m_models.size());
    beginInsertRows({}, row, row);
    m_models.append(std::move(model));
    endInsertRows();
    return true;
}

bool ModelListModel::contains(const QString& id) const {
    return indexOf(id) >= 0;
}

bool ModelListModel::isDefaultCandidate(const QString& id) const {
    const int row = indexOf(id);
    return row >= 0 && m_models.at(row).defaultCandidate;
}

bool ModelListModel::isLoaded(const QString& id) const {
    const int row = indexOf(id);
    return row >= 0 && m_models.at(row).loaded;
}

int ModelListModel::indexOf(const QString& id) const {
    for (int i = 0; i < m_models.size(); ++i) {
        if (m_models.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

void ModelListModel::emitRowChanged(int row) {
    emit dataChanged(index(row), index(row));
}

ModelManagerViewModel::ModelManagerViewModel(QObject* parent) : QObject(parent) {}

void ModelManagerViewModel::installServices(ModelManager* modelManager,
                                            ModelSettingsManager* settingsManager) {
    if (m_modelManager != nullptr) {
        disconnect(m_modelManager, nullptr, this, nullptr);
    }
    m_modelManager = modelManager;
    m_settingsManager = settingsManager;
    if (m_modelManager == nullptr) {
        return;
    }
    connect(m_modelManager, &ModelManager::modelsChanged, this, &ModelManagerViewModel::refreshFromService);
    connect(m_modelManager, &ModelManager::defaultModelIdChanged, this, [this] {
        if (m_modelManager == nullptr) {
            return;
        }
        const QString id = normalizedModelId(m_modelManager->defaultModelId());
        if (m_models.setDefaultModel(id) && m_defaultModelId != id) {
            m_defaultModelId = id;
            persistDefaultModel();
            emit defaultModelChanged();
        }
    });
    refreshFromService();
}

QAbstractItemModel* ModelManagerViewModel::models() noexcept {
    return &m_models;
}

QString ModelManagerViewModel::defaultModelId() const {
    return m_defaultModelId;
}

QString ModelManagerViewModel::selectedBackend() const {
    return m_selectedBackend;
}

QString ModelManagerViewModel::actualBackend() const {
    return m_actualBackend;
}

QString ModelManagerViewModel::runtimeVersion() const {
    return m_runtimeVersion;
}

void ModelManagerViewModel::download(const QString& id) {
    const QString normalizedId = normalizedModelId(id);
    emit downloadRequested(normalizedId);
    if (m_modelManager == nullptr) {
        m_models.setState(normalizedId, QStringLiteral("Requested"), 0.0);
        return;
    }
    if (const auto existing = m_downloads.value(normalizedId); existing != nullptr) {
        if (existing->state() == ModelDownloadOperation::State::Paused) {
            existing->resume();
        }
        return;
    }
    ModelDownloadOperation* operation = m_modelManager->download(normalizedId);
    if (operation == nullptr) {
        emit commandRejected(tr("This model is not present in the signed model manifest."));
        return;
    }
    attachDownload(operation);
}

void ModelManagerViewModel::pause(const QString& id) {
    const QString normalizedId = normalizedModelId(id);
    emit pauseRequested(normalizedId);
    if (const auto operation = m_downloads.value(normalizedId); operation != nullptr) {
        operation->pause();
    } else if (m_modelManager == nullptr) {
        m_models.setState(normalizedId, QStringLiteral("Paused"), 0.0);
    }
}

void ModelManagerViewModel::resume(const QString& id) {
    const QString normalizedId = normalizedModelId(id);
    emit resumeRequested(normalizedId);
    if (const auto operation = m_downloads.value(normalizedId); operation != nullptr) {
        operation->resume();
    } else {
        download(normalizedId);
    }
}

void ModelManagerViewModel::cancel(const QString& id) {
    const QString normalizedId = normalizedModelId(id);
    emit cancelRequested(normalizedId);
    if (const auto operation = m_downloads.value(normalizedId); operation != nullptr) {
        operation->cancel();
    } else if (m_modelManager == nullptr) {
        m_models.setState(normalizedId, QStringLiteral("Cancelled"), 0.0);
    }
}

void ModelManagerViewModel::remove(const QString& id) {
    const QString normalizedId = normalizedModelId(id);
    if (m_models.isLoaded(normalizedId)) {
        emit commandRejected(tr("Unload this model before deleting it."));
        return;
    }
    emit deleteRequested(normalizedId);
    if (m_modelManager == nullptr) {
        return;
    }
    QString error;
    if (!m_modelManager->removeModel(normalizedId, &error)) {
        emit commandRejected(error);
        return;
    }
    refreshFromService();
    emit operationSucceeded(tr("Model deleted."));
}

void ModelManagerViewModel::verify(const QString& id) {
    const QString normalizedId = normalizedModelId(id);
    emit verifyRequested(normalizedId);
    if (m_modelManager == nullptr) {
        return;
    }
    m_models.setState(normalizedId, QStringLiteral("Verifying"), 1.0);
    auto* watcher = new QFutureWatcher<VerifyOutcome>(this);
    const QPointer<ModelManager> manager = m_modelManager;
    connect(watcher, &QFutureWatcher<VerifyOutcome>::finished, this, [this, watcher] {
        const VerifyOutcome outcome = watcher->result();
        watcher->deleteLater();
        m_models.setInstalled(outcome.id, outcome.installed, outcome.valid);
        if (outcome.valid) {
            emit operationSucceeded(tr("Model checksum verified."));
        } else {
            emit commandRejected(outcome.error);
        }
    });
    watcher->setFuture(QtConcurrent::run([manager, normalizedId] {
        VerifyOutcome outcome;
        outcome.id = normalizedId;
        if (manager == nullptr) {
            outcome.error = QStringLiteral("Model service is no longer available.");
            return outcome;
        }
        outcome.installed = manager->isInstalled(normalizedId);
        outcome.valid = manager->verify(normalizedId, &outcome.error);
        return outcome;
    }));
}

void ModelManagerViewModel::testModel(const QString& id) {
    emit testRequested(normalizedModelId(id));
}

void ModelManagerViewModel::importCustom(const QUrl& file) {
    emit customImportRequested(file);
    if (m_modelManager == nullptr) {
        return;
    }
    const QString sourcePath = file.toLocalFile();
    const QFileInfo source(sourcePath);
    if (sourcePath.isEmpty() || !source.isFile()) {
        emit commandRejected(tr("Choose an existing local GGML .bin file."));
        return;
    }
    auto* watcher = new QFutureWatcher<ImportOutcome>(this);
    const QPointer<ModelManager> manager = m_modelManager;
    const QString displayName = source.completeBaseName();
    connect(watcher, &QFutureWatcher<ImportOutcome>::finished, this, [this, watcher] {
        const ImportOutcome outcome = watcher->result();
        watcher->deleteLater();
        if (!outcome.success) {
            emit commandRejected(outcome.error);
            return;
        }
        refreshFromService();
        emit operationSucceeded(tr("Custom model imported."));
    });
    watcher->setFuture(QtConcurrent::run([manager, sourcePath, displayName] {
        ImportOutcome outcome;
        outcome.displayName = displayName;
        if (manager == nullptr) {
            outcome.error = QStringLiteral("Model service is no longer available.");
            return outcome;
        }
        outcome.success = manager->importCustomModel(sourcePath, displayName, &outcome.id, &outcome.error);
        if (outcome.success) {
            outcome.path = manager->modelPath(outcome.id);
        }
        return outcome;
    }));
}

void ModelManagerViewModel::setDefaultModel(const QString& id) {
    const QString normalizedId = normalizedModelId(id);
    if (!m_models.isDefaultCandidate(normalizedId) || !m_models.setDefaultModel(normalizedId)) {
        emit commandRejected(tr("Choose a transcription model as the default."));
        return;
    }
    if (m_modelManager != nullptr) {
        m_modelManager->setDefaultModelId(normalizedId);
    }
    if (m_defaultModelId == normalizedId) {
        persistDefaultModel();
        return;
    }
    m_defaultModelId = normalizedId;
    persistDefaultModel();
    emit defaultModelChanged();
}

void ModelManagerViewModel::updateDownload(const QString& id, const QString& state, qreal progress) {
    m_models.setState(normalizedModelId(id), state, progress);
}

void ModelManagerViewModel::updateInstalled(const QString& id, bool installed, bool checksumValid) {
    m_models.setInstalled(normalizedModelId(id), installed, checksumValid);
}

void ModelManagerViewModel::updateLoaded(const QString& id, bool loaded, const QString& backend) {
    m_models.setLoaded(normalizedModelId(id), loaded, backend);
}

void ModelManagerViewModel::updateBackend(const QString& selected, const QString& actual) {
    if (m_selectedBackend == selected && m_actualBackend == actual) {
        return;
    }
    m_selectedBackend = selected;
    m_actualBackend = actual;
    emit backendChanged();
}

void ModelManagerViewModel::updateRuntimeInfo(const QString& selected, const QString& actual,
                                              const QString& runtimeVersion) {
    if (m_selectedBackend == selected && m_actualBackend == actual && m_runtimeVersion == runtimeVersion) {
        return;
    }
    m_selectedBackend = selected;
    m_actualBackend = actual;
    m_runtimeVersion = runtimeVersion.isEmpty() ? tr("Unknown") : runtimeVersion;
    emit backendChanged();
}

void ModelManagerViewModel::refreshFromService() {
    if (m_modelManager == nullptr) {
        return;
    }
    QList<ModelListModel::ModelItem> items;
    for (const auto& entry : m_modelManager->manifest().entries()) {
        auto item = modelItem(entry);
        item.installed = m_modelManager->isInstalled(entry.id);
        item.state = item.installed ? QStringLiteral("Installed") : QStringLiteral("NotInstalled");
        item.progress = item.installed ? 1.0 : 0.0;
        items.append(std::move(item));
    }
    if (items.isEmpty()) {
        items = bundledManifestItems();
        for (auto& item : items) {
            item.installed = m_modelManager->isInstalled(item.id);
            item.state = item.installed ? QStringLiteral("Installed") : QStringLiteral("NotInstalled");
            item.progress = item.installed ? 1.0 : 0.0;
        }
    }
    for (const CustomModelInfo& custom : m_modelManager->customModels()) {
        ModelListModel::ModelItem item;
        item.id = custom.id;
        item.displayName = custom.displayName;
        item.description = tr("Custom whisper.cpp GGML model imported from local storage.");
        item.quantization = tr("Custom");
        item.fileSize = custom.fileSize;
        item.licenseName = tr("User supplied");
        item.sourceUrl = QUrl::fromLocalFile(custom.path);
        item.defaultCandidate = true;
        item.installed = true;
        item.state = QStringLiteral("Installed");
        item.progress = 1.0;
        item.checksumValid = true;
        items.append(std::move(item));
    }
    m_models.replaceModels(std::move(items));

    QString requestedDefault = m_settingsManager != nullptr
                                   ? normalizedModelId(m_settingsManager->defaultModelId())
                                   : normalizedModelId(m_modelManager->defaultModelId());
    if (!m_models.contains(requestedDefault)) {
        requestedDefault = QString::fromLatin1(RecommendedModelId);
    }
    const bool changed = m_defaultModelId != requestedDefault;
    m_defaultModelId = requestedDefault;
    m_models.setDefaultModel(requestedDefault);
    if (m_modelManager->defaultModelId() != requestedDefault) {
        m_modelManager->setDefaultModelId(requestedDefault);
    }
    persistDefaultModel();
    if (changed) {
        emit defaultModelChanged();
    }
}

void ModelManagerViewModel::attachDownload(ModelDownloadOperation* operation) {
    if (operation == nullptr) {
        return;
    }
    const QString id = operation->modelId();
    m_downloads.insert(id, operation);
    const auto refresh = [this, operation] {
        m_models.setDownloadMetrics(operation->modelId(), downloadStateName(operation->state()),
                                    operation->progress(), qRound64(operation->bytesPerSecond()),
                                    operation->estimatedRemainingSeconds());
    };
    connect(operation, &ModelDownloadOperation::stateChanged, this, refresh);
    connect(operation, &ModelDownloadOperation::progressChanged, this, refresh);
    connect(operation, &ModelDownloadOperation::finished, this,
            [this, operation](bool success, const QString&) {
                const QString id = operation->modelId();
                const bool installed = m_modelManager != nullptr && m_modelManager->isInstalled(id);
                m_models.setInstalled(id, installed, success && installed);
                if (success) {
                    emit operationSucceeded(tr("Model downloaded and verified."));
                } else if (operation->state() == ModelDownloadOperation::State::Failed) {
                    emit commandRejected(operation->error());
                }
                m_downloads.remove(id);
                operation->deleteLater();
            });
    refresh();
}

void ModelManagerViewModel::persistDefaultModel() {
    if (m_settingsManager == nullptr) {
        return;
    }
    m_settingsManager->setDefaultModelId(m_defaultModelId);
    const auto result = m_settingsManager->sync();
    if (!result) {
        emit commandRejected(result.error().message);
    }
}

} // namespace BreezeDesk
