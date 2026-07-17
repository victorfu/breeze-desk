#include "breezedesk/app/MaintenanceController.h"

#include "breezedesk/app_config.h"
#include "breezedesk/audio/FFmpegLocator.h"
#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/ipc/IAsrWorkerClient.h"
#include "breezedesk/ipc/Protocol.h"
#include "breezedesk/update/UpdateCoordinator.h"
#include "breezedesk/version.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QtConcurrent>
#include <QtGlobal>

#include <algorithm>
#include <limits>
#include <utility>

namespace BreezeDesk {
namespace {

constexpr int WorkerCapabilitiesTimeoutMs = 5'000;
constexpr qint64 MaximumLogBytesPerFile = 1024 * 1024;
constexpr qint64 MaximumLogBytesTotal = 5 * 1024 * 1024;
constexpr int MaximumLogFiles = 10;

struct OperationResult {
    bool success{false};
    qint64 byteCount{0};
    QString path;
    QString message;
    QString technicalDetails;
};

struct VersionResult {
    QString version;
    QString error;
};

QString normalizedPath(const QString& path) {
    if (path.trimmed().isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool pathsEqual(const QString& left, const QString& right) {
    return !left.isEmpty() && !right.isEmpty() && normalizedPath(left) == normalizedPath(right);
}

bool isAncestorOf(const QString& ancestor, const QString& descendant) {
    const QString normalizedAncestor = normalizedPath(ancestor);
    const QString normalizedDescendant = normalizedPath(descendant);
    return !normalizedAncestor.isEmpty() && !normalizedDescendant.isEmpty() &&
           normalizedDescendant.startsWith(normalizedAncestor + QDir::separator());
}

qint64 entrySize(const QFileInfo& info) {
    if (info.isSymLink() || info.isFile()) {
        return std::max<qint64>(0, info.size());
    }
    if (!info.isDir()) {
        return 0;
    }
    qint64 total = 0;
    const QFileInfoList entries =
        QDir(info.absoluteFilePath())
            .entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        const qint64 size = entrySize(entry);
        if (size > std::numeric_limits<qint64>::max() - total) {
            return std::numeric_limits<qint64>::max();
        }
        total += size;
    }
    return total;
}

bool removeEntryWithoutFollowingLinks(const QFileInfo& info, QString* error) {
    if (info.isSymLink() || info.isFile()) {
        if (QFile::remove(info.absoluteFilePath())) {
            return true;
        }
        if (error != nullptr) {
            *error = QStringLiteral("Unable to remove cache entry: %1").arg(info.absoluteFilePath());
        }
        return false;
    }
    if (!info.isDir()) {
        return true;
    }

    QDir directory(info.absoluteFilePath());
    const QFileInfoList entries =
        directory.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (!removeEntryWithoutFollowingLinks(entry, error)) {
            return false;
        }
    }
    QDir parent = directory;
    if (!parent.cdUp() || !parent.rmdir(info.fileName())) {
        if (error != nullptr) {
            *error = QStringLiteral("Unable to remove cache directory: %1").arg(info.absoluteFilePath());
        }
        return false;
    }
    return true;
}

OperationResult clearCacheDirectory(const QString& cachePath, const QString& dataRoot) {
    OperationResult result;
    const QString cache = normalizedPath(cachePath);
    if (cache.isEmpty()) {
        result.message = QStringLiteral("The cache location is empty.");
        return result;
    }

    const QStringList protectedPaths{
        QDir::rootPath(),
        QDir::homePath(),
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        QStandardPaths::writableLocation(QStandardPaths::TempLocation),
    };
    const bool isProtected = std::any_of(protectedPaths.cbegin(), protectedPaths.cend(),
                                         [&cache](const QString& path) { return pathsEqual(cache, path); });
    if (isProtected || pathsEqual(cache, dataRoot) || isAncestorOf(cache, dataRoot)) {
        result.message = QStringLiteral("The configured cache location is too broad to clear safely.");
        result.technicalDetails = cache;
        return result;
    }

    const QFileInfo cacheInfo(cache);
    if (cacheInfo.exists() && (cacheInfo.isSymLink() || !cacheInfo.isDir())) {
        result.message = QStringLiteral("The configured cache location is not a regular directory.");
        result.technicalDetails = cache;
        return result;
    }
    if (!QDir().mkpath(cache)) {
        result.message = QStringLiteral("The cache directory could not be created.");
        result.technicalDetails = cache;
        return result;
    }

    const QFileInfoList entries =
        QDir(cache).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        const qint64 size = entrySize(entry);
        QString error;
        if (!removeEntryWithoutFollowingLinks(entry, &error)) {
            result.message = QStringLiteral("Some cache files could not be removed.");
            result.technicalDetails = error;
            return result;
        }
        if (size > std::numeric_limits<qint64>::max() - result.byteCount) {
            result.byteCount = std::numeric_limits<qint64>::max();
        } else {
            result.byteCount += size;
        }
    }
    result.success = true;
    return result;
}

void appendLittleEndian16(QByteArray* output, const quint16 value) {
    output->append(static_cast<char>(value & 0x00ffU));
    output->append(static_cast<char>((value >> 8U) & 0x00ffU));
}

void appendLittleEndian32(QByteArray* output, const quint32 value) {
    output->append(static_cast<char>(value & 0x000000ffU));
    output->append(static_cast<char>((value >> 8U) & 0x000000ffU));
    output->append(static_cast<char>((value >> 16U) & 0x000000ffU));
    output->append(static_cast<char>((value >> 24U) & 0x000000ffU));
}

quint32 crc32(const QByteArray& data) {
    quint32 crc = 0xffffffffU;
    for (const char value : data) {
        crc ^= static_cast<quint32>(static_cast<unsigned char>(value));
        for (int bit = 0; bit < 8; ++bit) {
            const quint32 mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return crc ^ 0xffffffffU;
}

struct ZipEntry {
    QByteArray name;
    QByteArray data;
    quint32 checksum{0};
    quint32 localHeaderOffset{0};
};

bool buildStoredZip(QList<ZipEntry> entries, QByteArray* archive, QString* error) {
    if (archive == nullptr || entries.size() > std::numeric_limits<quint16>::max()) {
        if (error != nullptr) {
            *error = QStringLiteral("The diagnostics archive contains too many entries.");
        }
        return false;
    }
    archive->clear();
    for (ZipEntry& entry : entries) {
        if (entry.name.size() > std::numeric_limits<quint16>::max() ||
            entry.data.size() > std::numeric_limits<quint32>::max() ||
            archive->size() > std::numeric_limits<quint32>::max()) {
            if (error != nullptr) {
                *error = QStringLiteral("A diagnostics archive entry is too large.");
            }
            return false;
        }
        entry.checksum = crc32(entry.data);
        entry.localHeaderOffset = static_cast<quint32>(archive->size());
        appendLittleEndian32(archive, 0x04034b50U);
        appendLittleEndian16(archive, 20U);
        appendLittleEndian16(archive, 0x0800U);
        appendLittleEndian16(archive, 0U);
        appendLittleEndian16(archive, 0U);
        appendLittleEndian16(archive, 0U);
        appendLittleEndian32(archive, entry.checksum);
        appendLittleEndian32(archive, static_cast<quint32>(entry.data.size()));
        appendLittleEndian32(archive, static_cast<quint32>(entry.data.size()));
        appendLittleEndian16(archive, static_cast<quint16>(entry.name.size()));
        appendLittleEndian16(archive, 0U);
        archive->append(entry.name);
        archive->append(entry.data);
    }

    if (archive->size() > std::numeric_limits<quint32>::max()) {
        if (error != nullptr) {
            *error = QStringLiteral("The diagnostics archive is too large.");
        }
        return false;
    }
    const quint32 centralDirectoryOffset = static_cast<quint32>(archive->size());
    for (const ZipEntry& entry : std::as_const(entries)) {
        appendLittleEndian32(archive, 0x02014b50U);
        appendLittleEndian16(archive, 20U);
        appendLittleEndian16(archive, 20U);
        appendLittleEndian16(archive, 0x0800U);
        appendLittleEndian16(archive, 0U);
        appendLittleEndian16(archive, 0U);
        appendLittleEndian16(archive, 0U);
        appendLittleEndian32(archive, entry.checksum);
        appendLittleEndian32(archive, static_cast<quint32>(entry.data.size()));
        appendLittleEndian32(archive, static_cast<quint32>(entry.data.size()));
        appendLittleEndian16(archive, static_cast<quint16>(entry.name.size()));
        appendLittleEndian16(archive, 0U);
        appendLittleEndian16(archive, 0U);
        appendLittleEndian16(archive, 0U);
        appendLittleEndian16(archive, 0U);
        appendLittleEndian32(archive, 0U);
        appendLittleEndian32(archive, entry.localHeaderOffset);
        archive->append(entry.name);
    }
    const quint64 centralSize =
        static_cast<quint64>(archive->size()) - static_cast<quint64>(centralDirectoryOffset);
    if (centralSize > std::numeric_limits<quint32>::max()) {
        if (error != nullptr) {
            *error = QStringLiteral("The diagnostics archive directory is too large.");
        }
        return false;
    }
    appendLittleEndian32(archive, 0x06054b50U);
    appendLittleEndian16(archive, 0U);
    appendLittleEndian16(archive, 0U);
    appendLittleEndian16(archive, static_cast<quint16>(entries.size()));
    appendLittleEndian16(archive, static_cast<quint16>(entries.size()));
    appendLittleEndian32(archive, static_cast<quint32>(centralSize));
    appendLittleEndian32(archive, centralDirectoryOffset);
    appendLittleEndian16(archive, 0U);
    return true;
}

bool containsSensitiveContentKey(const QString& key) {
    const QString normalized = key.toLower();
    static const QStringList fragments{
        QStringLiteral("glossary"), QStringLiteral("transcript"), QStringLiteral("prompt"),
        QStringLiteral("context"),  QStringLiteral("password"),   QStringLiteral("secret"),
        QStringLiteral("token"),    QStringLiteral("api_key"),    QStringLiteral("apikey")};
    return std::any_of(fragments.cbegin(), fragments.cend(),
                       [&normalized](const QString& fragment) { return normalized.contains(fragment); });
}

bool containsPathKey(const QString& key) {
    const QString normalized = key.toLower();
    return normalized.contains(QStringLiteral("path")) || normalized.contains(QStringLiteral("directory")) ||
           normalized.contains(QStringLiteral("folder")) || normalized.contains(QStringLiteral("sourcefile"));
}

bool looksLikePersonalPath(const QString& value) {
    static const QRegularExpression PathPattern(QStringLiteral(R"((?:^[A-Za-z]:[\\/]|^/))"));
    return PathPattern.match(value).hasMatch();
}

QString redactedAbsolutePaths(QString text) {
    static const QRegularExpression AbsolutePathPattern(
        QStringLiteral(R"((?:[A-Za-z]:[\\/][^\s"'<>]+|(?<![:\w])/(?:[^\s"'<>]+)))"));
    text.replace(AbsolutePathPattern, QStringLiteral("<redacted-path>"));
    return text;
}

QJsonValue sanitizedJsonValue(const QString& key, const QJsonValue& value, bool includePaths) {
    if (containsSensitiveContentKey(key)) {
        return QStringLiteral("<redacted>");
    }
    if (!includePaths && containsPathKey(key)) {
        return QStringLiteral("<redacted-path>");
    }
    if (value.isObject()) {
        QJsonObject sanitized;
        const QJsonObject object = value.toObject();
        for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
            sanitized.insert(iterator.key(),
                             sanitizedJsonValue(iterator.key(), iterator.value(), includePaths));
        }
        return sanitized;
    }
    if (value.isArray()) {
        QJsonArray sanitized;
        const QJsonArray array = value.toArray();
        for (const QJsonValue& item : array) {
            sanitized.append(sanitizedJsonValue(key, item, includePaths));
        }
        return sanitized;
    }
    if (!includePaths && value.isString()) {
        const QString stringValue = value.toString();
        if (looksLikePersonalPath(stringValue)) {
            return QStringLiteral("<redacted-path>");
        }
        return redactedAbsolutePaths(stringValue);
    }
    return value;
}

QJsonObject sanitizedObject(const QJsonObject& object, bool includePaths) {
    return sanitizedJsonValue({}, object, includePaths).toObject();
}

QByteArray sanitizedLog(QByteArray data, const QString& dataRoot, bool includePaths) {
    QString text = QString::fromUtf8(data);
    QStringList sanitizedLines;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString lower = line.toLower();
        if (lower.contains(QStringLiteral("partialsegment")) ||
            lower.contains(QStringLiteral("original_text")) ||
            lower.contains(QStringLiteral("edited_text")) ||
            lower.contains(QStringLiteral("initial_prompt")) || lower.contains(QStringLiteral("glossary"))) {
            sanitizedLines.append(QStringLiteral("[redacted sensitive log line]"));
        } else {
            sanitizedLines.append(line);
        }
    }
    text = sanitizedLines.join(QLatin1Char('\n'));
    if (!includePaths) {
        const QString home = normalizedPath(QDir::homePath());
        const QString root = normalizedPath(dataRoot);
        if (!home.isEmpty()) {
            text.replace(home, QStringLiteral("<redacted-path>"));
        }
        if (!root.isEmpty()) {
            text.replace(root, QStringLiteral("<redacted-path>"));
        }
        text = redactedAbsolutePaths(std::move(text));
    }
    return text.toUtf8();
}

OperationResult createDiagnosticsArchive(const QString& destinationPath, const QString& logDirectory,
                                         const QString& dataRoot, const QJsonObject& report,
                                         bool includePaths) {
    OperationResult result;
    const QFileInfo destinationInfo(destinationPath);
    if (destinationPath.trimmed().isEmpty()) {
        result.message = QStringLiteral("The diagnostics destination is empty.");
        return result;
    }
    if (destinationInfo.exists()) {
        result.message = QStringLiteral("The diagnostics destination already exists.");
        result.technicalDetails = destinationInfo.absoluteFilePath();
        return result;
    }
    if (!QDir().mkpath(destinationInfo.absolutePath())) {
        result.message = QStringLiteral("The diagnostics destination directory could not be created.");
        result.technicalDetails = destinationInfo.absolutePath();
        return result;
    }

    QList<ZipEntry> entries;
    entries.append(
        {QByteArrayLiteral("diagnostics.json"), QJsonDocument(report).toJson(QJsonDocument::Indented)});

    qint64 includedLogBytes = 0;
    int includedLogFiles = 0;
    const QFileInfoList logs =
        QDir(logDirectory)
            .entryInfoList({QStringLiteral("*.log"), QStringLiteral("*.txt")},
                           QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Time);
    for (const QFileInfo& log : logs) {
        if (includedLogFiles >= MaximumLogFiles || includedLogBytes >= MaximumLogBytesTotal) {
            break;
        }
        QFile file(log.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const qint64 remaining = MaximumLogBytesTotal - includedLogBytes;
        const qint64 wanted = std::min(MaximumLogBytesPerFile, remaining);
        if (file.size() > wanted) {
            file.seek(file.size() - wanted);
        }
        QByteArray content = file.read(wanted);
        content = sanitizedLog(std::move(content), dataRoot, includePaths);
        QByteArray archiveFileName = log.fileName().toUtf8();
        archiveFileName.replace('\\', '_');
        archiveFileName.replace('/', '_');
        const QByteArray archiveName = QByteArrayLiteral("logs/") + archiveFileName;
        entries.append({archiveName, content});
        includedLogBytes += content.size();
        ++includedLogFiles;
    }

    QByteArray archive;
    QString zipError;
    if (!buildStoredZip(std::move(entries), &archive, &zipError)) {
        result.message = QStringLiteral("The diagnostics archive could not be assembled.");
        result.technicalDetails = zipError;
        return result;
    }
    QSaveFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly) || destination.write(archive) != archive.size()) {
        destination.cancelWriting();
        result.message = QStringLiteral("The diagnostics archive could not be written.");
        result.technicalDetails = destination.errorString();
        return result;
    }
    if (!destination.commit()) {
        result.message = QStringLiteral("The diagnostics archive could not be committed atomically.");
        result.technicalDetails = destination.errorString();
        return result;
    }
    result.success = true;
    result.path = destinationInfo.absoluteFilePath();
    return result;
}

} // namespace

MaintenanceController::MaintenanceController(MaintenanceDependencies dependencies, QObject* parent)
    : QObject(parent), m_database(dependencies.database), m_workerClient(dependencies.workerClient),
      m_updateCoordinator(dependencies.updateCoordinator), m_paths(std::move(dependencies.paths)) {
    if (m_paths.dataRoot.isEmpty()) {
        m_paths.dataRoot = StoragePaths::root();
    }
    if (m_paths.cacheDirectory.isEmpty()) {
        m_paths.cacheDirectory = StoragePaths::cache();
    }
    if (m_paths.logDirectory.isEmpty()) {
        m_paths.logDirectory = StoragePaths::logs();
    }
    if (m_paths.exportDirectory.isEmpty()) {
        m_paths.exportDirectory = StoragePaths::exports();
    }
    m_threadPool.setMaxThreadCount(2);
    m_threadPool.setExpiryTimeout(30'000);
    m_workerTimeout.setSingleShot(true);
    m_workerTimeout.setInterval(WorkerCapabilitiesTimeoutMs);

    m_diagnostics.insert(QStringLiteral("schemaVersion"), 1);
    m_diagnostics.insert(QStringLiteral("appVersion"), QString::fromLatin1(BREEZEDESK_VERSION_STRING));
    m_diagnostics.insert(QStringLiteral("qtVersion"), QString::fromLatin1(qVersion()));
    m_diagnostics.insert(QStringLiteral("os"), QSysInfo::prettyProductName());
    m_diagnostics.insert(QStringLiteral("kernel"),
                         QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion());
    m_diagnostics.insert(QStringLiteral("cpuArchitecture"), QSysInfo::currentCpuArchitecture());
    m_diagnostics.insert(QStringLiteral("buildArchitecture"), QSysInfo::buildCpuArchitecture());
    m_diagnostics.insert(QStringLiteral("protocolVersion"), Ipc::kProtocolVersion);
    m_diagnostics.insert(QStringLiteral("databasePath"),
                         m_database == nullptr ? QString() : m_database->filePath());
    m_diagnostics.insert(QStringLiteral("modelPath"),
                         QDir(m_paths.dataRoot).filePath(QStringLiteral("models")));
    m_diagnostics.insert(QStringLiteral("cachePath"), m_paths.cacheDirectory);
    m_diagnostics.insert(QStringLiteral("logPath"), m_paths.logDirectory);
    m_diagnostics.insert(QStringLiteral("ffmpegVersion"), QStringLiteral("Not detected"));
    m_diagnostics.insert(QStringLiteral("whisperVersion"), QStringLiteral("Not loaded"));
    m_diagnostics.insert(QStringLiteral("workerVersion"), QStringLiteral("Not connected"));
    m_diagnostics.insert(QStringLiteral("selectedBackend"), QStringLiteral("Auto"));
    m_diagnostics.insert(QStringLiteral("actualBackend"), QStringLiteral("Not loaded"));

    connect(&m_workerTimeout, &QTimer::timeout, this, [this] {
        if (!m_workerRefreshPending) {
            return;
        }
        m_workerRefreshPending = false;
        m_capabilitiesRequestId.clear();
        m_diagnostics.insert(QStringLiteral("workerStatus"), QStringLiteral("Timed out"));
        finishDiagnosticsRefreshIfReady();
    });
    if (m_workerClient != nullptr) {
        connect(m_workerClient, &Ipc::IAsrWorkerClient::envelopeReceived, this,
                &MaintenanceController::handleWorkerEnvelope);
        connect(m_workerClient, &Ipc::IAsrWorkerClient::disconnected, this, [this] {
            m_workerTimeout.stop();
            m_workerRefreshPending = false;
            m_capabilitiesRequestId.clear();
            m_diagnostics.insert(QStringLiteral("workerStatus"), QStringLiteral("Disconnected"));
            m_diagnostics.insert(QStringLiteral("actualBackend"), QStringLiteral("Not loaded"));
            publishDiagnostics();
            finishDiagnosticsRefreshIfReady();
        });
    }
    if (m_updateCoordinator != nullptr) {
        connect(m_updateCoordinator, &UpdateCoordinator::availabilityChanged, this,
                [this] { emit updateAvailabilityChanged(m_updateCoordinator->isAvailable()); });
        connect(m_updateCoordinator, &UpdateCoordinator::updateAvailable, this,
                &MaintenanceController::updateAvailable);
        connect(m_updateCoordinator, &UpdateCoordinator::noUpdateAvailable, this,
                &MaintenanceController::noUpdateAvailable);
        connect(m_updateCoordinator, &UpdateCoordinator::error, this, &MaintenanceController::updateError);
    }
}

MaintenanceController::~MaintenanceController() {
    m_workerTimeout.stop();
    m_threadPool.waitForDone();
}

QJsonObject MaintenanceController::diagnosticsSnapshot() const {
    return m_diagnostics;
}

void MaintenanceController::clearCache() {
    const QString operation = QStringLiteral("ClearCache");
    if (m_cacheOperationRunning) {
        emit operationFailed(operation, tr("Cache cleanup is already running."), {});
        return;
    }
    if (m_cacheBusy) {
        emit operationFailed(
            operation, tr("Cache cannot be cleared while media preparation or transcription is active."), {});
        return;
    }
    m_cacheOperationRunning = true;
    emit operationStarted(operation);
    auto* watcher = new QFutureWatcher<OperationResult>(this);
    connect(watcher, &QFutureWatcher<OperationResult>::finished, this, [this, watcher, operation] {
        const OperationResult result = watcher->result();
        watcher->deleteLater();
        m_cacheOperationRunning = false;
        if (!result.success) {
            emit operationFailed(operation, result.message, result.technicalDetails);
            return;
        }
        emit cacheCleared(result.byteCount);
        emit operationSucceeded(tr("Cache cleared."));
    });
    watcher->setFuture(
        QtConcurrent::run(&m_threadPool, clearCacheDirectory, m_paths.cacheDirectory, m_paths.dataRoot));
}

void MaintenanceController::backupDatabase() {
    const QString baseName =
        QStringLiteral("%1-Database").arg(QString::fromLatin1(AppConfig::DataDirectoryName));
    backupDatabaseTo(uniqueExportPath(baseName, QStringLiteral(".sqlite3")));
}

void MaintenanceController::backupDatabaseTo(const QString& destinationPath) {
    const QString operation = QStringLiteral("BackupDatabase");
    if (m_backupOperationRunning) {
        emit operationFailed(operation, tr("A database backup is already running."), {});
        return;
    }
    if (m_database == nullptr) {
        emit operationFailed(operation, tr("The library database is unavailable."), {});
        return;
    }
    if (destinationPath.trimmed().isEmpty()) {
        emit operationFailed(operation, tr("Choose a destination for the database backup."), {});
        return;
    }
    m_backupOperationRunning = true;
    emit operationStarted(operation);
    DatabaseManager* const database = m_database;
    const QString destination = QFileInfo(destinationPath).absoluteFilePath();
    auto* watcher = new QFutureWatcher<OperationResult>(this);
    connect(watcher, &QFutureWatcher<OperationResult>::finished, this, [this, watcher, operation] {
        const OperationResult result = watcher->result();
        watcher->deleteLater();
        m_backupOperationRunning = false;
        if (!result.success) {
            emit operationFailed(operation, result.message, result.technicalDetails);
            return;
        }
        emit databaseBackupCreated(result.path);
        emit operationSucceeded(tr("Database backup created."));
    });
    watcher->setFuture(QtConcurrent::run(&m_threadPool, [database, destination] {
        OperationResult result;
        const auto backup = database->createBackup(destination);
        if (!backup) {
            result.message = backup.error().message;
            result.technicalDetails = backup.error().technicalDetails;
            return result;
        }
        result.success = true;
        result.path = backup.value();
        return result;
    }));
}

void MaintenanceController::backupDatabaseToUrl(const QUrl& destinationUrl) {
    if (!destinationUrl.isValid() || !destinationUrl.isLocalFile()) {
        emit operationFailed(QStringLiteral("BackupDatabase"),
                             tr("Database backups can only be saved to a local file."),
                             destinationUrl.toString());
        return;
    }
    backupDatabaseTo(destinationUrl.toLocalFile());
}

void MaintenanceController::refreshDiagnostics() {
    if (m_diagnosticsRefreshRunning) {
        return;
    }
    m_diagnosticsRefreshRunning = true;
    m_ffmpegRefreshPending = true;
    emit operationStarted(QStringLiteral("RefreshDiagnostics"));

    auto* watcher = new QFutureWatcher<VersionResult>(this);
    connect(watcher, &QFutureWatcher<VersionResult>::finished, this, [this, watcher] {
        const VersionResult result = watcher->result();
        watcher->deleteLater();
        m_ffmpegRefreshPending = false;
        const QString display = !result.version.isEmpty() ? result.version
                                : result.error.isEmpty()
                                    ? QStringLiteral("Not detected")
                                    : QStringLiteral("Not detected: %1").arg(result.error);
        m_diagnostics.insert(QStringLiteral("ffmpegVersion"), display);
        emit ffmpegVersionDetected(display);
        publishDiagnostics();
        finishDiagnosticsRefreshIfReady();
    });
    watcher->setFuture(QtConcurrent::run(&m_threadPool, [] {
        VersionResult result;
        const FFmpegLocator::Tools tools = FFmpegLocator::locate();
        if (!tools.isValid()) {
            result.error = tools.error;
            return result;
        }
        result.version = FFmpegLocator::version(tools.ffmpegPath, &result.error);
        return result;
    }));

    if (m_workerClient != nullptr && m_workerClient->isReady()) {
        m_workerRefreshPending = true;
        m_capabilitiesRequestId = m_workerClient->sendRequest(Ipc::MessageType::GetCapabilities, {}, {});
        if (m_capabilitiesRequestId.isEmpty()) {
            m_workerRefreshPending = false;
            m_diagnostics.insert(QStringLiteral("workerStatus"), QStringLiteral("Request failed"));
        } else {
            m_workerTimeout.start();
        }
    } else {
        m_workerRefreshPending = false;
        m_diagnostics.insert(QStringLiteral("workerStatus"), QStringLiteral("Not running"));
        m_diagnostics.insert(QStringLiteral("actualBackend"), QStringLiteral("Not loaded"));
        publishDiagnostics();
    }
    finishDiagnosticsRefreshIfReady();
}

void MaintenanceController::exportDiagnostics(bool includePaths) {
    const QString baseName =
        QStringLiteral("%1-Diagnostics").arg(QString::fromLatin1(AppConfig::DataDirectoryName));
    exportDiagnosticsTo(uniqueExportPath(baseName, QStringLiteral(".zip")), includePaths);
}

void MaintenanceController::exportDiagnosticsTo(const QString& destinationPath, bool includePaths) {
    const QString operation = QStringLiteral("ExportDiagnostics");
    if (m_exportOperationRunning) {
        emit operationFailed(operation, tr("A diagnostics export is already running."), {});
        return;
    }
    if (destinationPath.trimmed().isEmpty()) {
        emit operationFailed(operation, tr("Choose a destination for the diagnostics archive."), {});
        return;
    }
    m_exportOperationRunning = true;
    emit operationStarted(operation);
    const QString destination = QFileInfo(destinationPath).absoluteFilePath();
    const QString logDirectory = m_paths.logDirectory;
    const QString dataRoot = m_paths.dataRoot;
    const QJsonObject report = diagnosticsForExport(includePaths);
    auto* watcher = new QFutureWatcher<OperationResult>(this);
    connect(watcher, &QFutureWatcher<OperationResult>::finished, this, [this, watcher, operation] {
        const OperationResult result = watcher->result();
        watcher->deleteLater();
        m_exportOperationRunning = false;
        if (!result.success) {
            emit operationFailed(operation, result.message, result.technicalDetails);
            return;
        }
        emit diagnosticsExported(result.path);
        emit operationSucceeded(tr("Sanitized diagnostics exported."));
    });
    watcher->setFuture(QtConcurrent::run(&m_threadPool, createDiagnosticsArchive, destination, logDirectory,
                                         dataRoot, report, includePaths));
}

void MaintenanceController::exportDiagnosticsToUrl(const QUrl& destinationUrl, bool includePaths) {
    if (!destinationUrl.isValid() || !destinationUrl.isLocalFile()) {
        emit operationFailed(QStringLiteral("ExportDiagnostics"),
                             tr("Diagnostics can only be saved to a local file."), destinationUrl.toString());
        return;
    }
    exportDiagnosticsTo(destinationUrl.toLocalFile(), includePaths);
}

void MaintenanceController::checkForUpdates() {
    if (m_updateCoordinator == nullptr) {
        const QString message = tr("Automatic updates are unavailable for this installation.");
        emit updateError(message);
        emit operationFailed(QStringLiteral("CheckForUpdates"), message, {});
        return;
    }
    m_updateCoordinator->checkForUpdates(true);
}

void MaintenanceController::setCacheBusy(bool busy) {
    m_cacheBusy = busy;
}

void MaintenanceController::setSelectedBackend(const QString& backend) {
    const QString value = backend.trimmed().isEmpty() ? QStringLiteral("Auto") : backend.trimmed();
    if (m_diagnostics.value(QStringLiteral("selectedBackend")).toString() == value) {
        return;
    }
    m_diagnostics.insert(QStringLiteral("selectedBackend"), value);
    publishDiagnostics();
}

void MaintenanceController::setActualBackend(const QString& backend) {
    const QString value = backend.trimmed().isEmpty() ? QStringLiteral("Not loaded") : backend.trimmed();
    if (m_diagnostics.value(QStringLiteral("actualBackend")).toString() == value) {
        return;
    }
    m_diagnostics.insert(QStringLiteral("actualBackend"), value);
    publishDiagnostics();
}

void MaintenanceController::setSanitizedSettings(const QJsonObject& settings) {
    m_settings = sanitizedObject(settings, true);
}

void MaintenanceController::handleWorkerEnvelope(const Ipc::Envelope& envelope) {
    if (envelope.type == Ipc::MessageType::ModelLoaded) {
        m_diagnostics.insert(QStringLiteral("selectedBackend"),
                             envelope.payload.value(QStringLiteral("selectedBackend")).toString());
        m_diagnostics.insert(QStringLiteral("actualBackend"),
                             envelope.payload.value(QStringLiteral("actualBackend")).toString());
        const QString runtimeVersion = envelope.payload.value(QStringLiteral("runtimeVersion")).toString();
        if (!runtimeVersion.isEmpty()) {
            m_diagnostics.insert(QStringLiteral("whisperVersion"), runtimeVersion);
        }
        m_diagnostics.insert(QStringLiteral("modelLoadTimeMs"),
                             envelope.payload.value(QStringLiteral("loadTimeMs")).toInteger());
        publishDiagnostics();
        return;
    }
    if (envelope.type != Ipc::MessageType::Capabilities || envelope.requestId != m_capabilitiesRequestId) {
        return;
    }
    m_workerTimeout.stop();
    m_workerRefreshPending = false;
    m_capabilitiesRequestId.clear();
    m_diagnostics.insert(QStringLiteral("workerStatus"), QStringLiteral("Ready"));
    m_diagnostics.insert(QStringLiteral("workerVersion"), envelope.workerVersion);
    m_diagnostics.insert(QStringLiteral("whisperVersion"),
                         envelope.payload.value(QStringLiteral("whisperVersion")).toString());
    m_diagnostics.insert(QStringLiteral("compiledBackend"),
                         envelope.payload.value(QStringLiteral("compiledBackend")).toString());
    m_diagnostics.insert(QStringLiteral("runtimeAvailable"),
                         envelope.payload.value(QStringLiteral("runtimeAvailable")).toBool());
    publishDiagnostics();
    finishDiagnosticsRefreshIfReady();
}

void MaintenanceController::finishDiagnosticsRefreshIfReady() {
    if (!m_diagnosticsRefreshRunning || m_ffmpegRefreshPending || m_workerRefreshPending) {
        return;
    }
    m_diagnosticsRefreshRunning = false;
    m_diagnostics.insert(QStringLiteral("refreshedAt"),
                         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    publishDiagnostics();
    emit operationSucceeded(tr("Diagnostics refreshed."));
}

void MaintenanceController::publishDiagnostics() {
    emit whisperVersionDetected(m_diagnostics.value(QStringLiteral("whisperVersion")).toString());
    emit backendDetected(m_diagnostics.value(QStringLiteral("selectedBackend")).toString(),
                         m_diagnostics.value(QStringLiteral("actualBackend")).toString());
    emit diagnosticsChanged(m_diagnostics);
}

QString MaintenanceController::uniqueExportPath(const QString& baseName, const QString& suffix) const {
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz"));
    const QString candidate =
        QDir(m_paths.exportDirectory).filePath(baseName + QLatin1Char('-') + timestamp + suffix);
    if (!QFileInfo::exists(candidate)) {
        return candidate;
    }
    for (int index = 2; index < 1'000; ++index) {
        const QString alternative = QDir(m_paths.exportDirectory)
                                        .filePath(baseName + QLatin1Char('-') + timestamp + QLatin1Char('-') +
                                                  QString::number(index) + suffix);
        if (!QFileInfo::exists(alternative)) {
            return alternative;
        }
    }
    return QDir(m_paths.exportDirectory)
        .filePath(baseName + QLatin1Char('-') + timestamp + QStringLiteral("-unique") + suffix);
}

QJsonObject MaintenanceController::diagnosticsForExport(bool includePaths) const {
    QJsonObject report;
    report.insert(QStringLiteral("schemaVersion"), 1);
    report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    report.insert(QStringLiteral("application"), QString::fromLatin1(AppConfig::DisplayName));
    report.insert(QStringLiteral("diagnostics"), sanitizedObject(m_diagnostics, includePaths));
    report.insert(QStringLiteral("settings"), sanitizedObject(m_settings, includePaths));
    report.insert(QStringLiteral("privacy"), QJsonObject{{QStringLiteral("containsAudio"), false},
                                                         {QStringLiteral("containsTranscripts"), false},
                                                         {QStringLiteral("containsGlossary"), false},
                                                         {QStringLiteral("pathsIncluded"), includePaths}});
    return report;
}

} // namespace BreezeDesk
