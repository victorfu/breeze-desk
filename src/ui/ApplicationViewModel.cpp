#include "breezedesk/ui/ApplicationViewModel.h"

#include "breezedesk/app_config.h"
#include "breezedesk/audio/MediaFileSupport.h"
#include "breezedesk/audio/WaveformGenerator.h"
#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/platform/IPlatformService.h"
#include "breezedesk/transcript/ITranscriptRepository.h"
#include "breezedesk/transcript/TranscriptExporter.h"

#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>
#include <QUrl>
#include <QUuid>
#include <QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <memory>

namespace {

struct WaveformLoadResult {
    QString recordingId;
    QVariantList peaks;
    QString error;
};

struct ManagedCopyResult {
    QUrl originalUrl;
    QString managedPath;
    QString error;
    bool cancelled{false};
};

struct FolderScanResult {
    QStringList paths;
    QString error;
    bool cancelled{false};
};

bool isInsideDirectory(const QString& filePath, const QString& directoryPath) {
    const QFileInfo fileInfo(filePath);
    const QFileInfo directoryInfo(directoryPath);
    const QString file =
        fileInfo.canonicalFilePath().isEmpty() ? fileInfo.absoluteFilePath() : fileInfo.canonicalFilePath();
    const QString directory = directoryInfo.canonicalFilePath().isEmpty() ? directoryInfo.absoluteFilePath()
                                                                          : directoryInfo.canonicalFilePath();
    return file.startsWith(QDir::cleanPath(directory) + QDir::separator());
}

ManagedCopyResult copyManagedMedia(const QUrl& originalUrl, const QString& destinationPath,
                                   const std::shared_ptr<std::atomic_bool>& cancellation) {
    ManagedCopyResult result{originalUrl, destinationPath, {}, false};
    const QString sourcePath = originalUrl.toLocalFile();
    QFile source(sourcePath);
    const QFileInfo sourceInfo(sourcePath);
    if (!originalUrl.isLocalFile() || !sourceInfo.isFile() || !source.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("The selected media file could not be opened.");
        return result;
    }
    constexpr qint64 RequiredFreeSpaceReserve = 64LL * 1024LL * 1024LL;
    const QStorageInfo storage(QFileInfo(destinationPath).absolutePath());
    if (storage.isValid() && storage.isReady() &&
        storage.bytesAvailable() < sourceInfo.size() + RequiredFreeSpaceReserve) {
        result.error = QStringLiteral("There is not enough free disk space to copy this media file.");
        return result;
    }
    QSaveFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("The managed media copy could not be created.");
        return result;
    }
    constexpr qint64 CopyBlockSize = 4LL * 1024LL * 1024LL;
    while (!source.atEnd()) {
        if (cancellation && cancellation->load(std::memory_order_relaxed)) {
            destination.cancelWriting();
            result.cancelled = true;
            return result;
        }
        const QByteArray block = source.read(CopyBlockSize);
        if ((block.isEmpty() && source.error() != QFileDevice::NoError) ||
            destination.write(block) != block.size()) {
            destination.cancelWriting();
            result.error = QStringLiteral("The managed media copy could not be written completely.");
            return result;
        }
    }
    if (cancellation && cancellation->load(std::memory_order_relaxed)) {
        destination.cancelWriting();
        result.cancelled = true;
        return result;
    }
    if (!destination.commit()) {
        result.error = QStringLiteral("The managed media copy could not be saved atomically.");
    }
    return result;
}

FolderScanResult scanMediaFolder(const QString& folderPath,
                                 const std::shared_ptr<std::atomic_bool>& cancellation) {
    FolderScanResult result;
    const QFileInfo folder(folderPath);
    if (!folder.isDir() || !folder.isReadable()) {
        result.error = QStringLiteral("The selected folder cannot be read.");
        return result;
    }

    QDirIterator iterator(folder.absoluteFilePath(),
                          QDir::Files | QDir::Readable | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        if (cancellation && cancellation->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            result.paths.clear();
            return result;
        }
        const QString path = QFileInfo(iterator.next()).absoluteFilePath();
        if (BreezeDesk::MediaFileSupport::isSupportedPath(path)) {
            result.paths.append(path);
        }
    }
    std::sort(result.paths.begin(), result.paths.end(), [](const QString& left, const QString& right) {
        return QString::compare(left, right, Qt::CaseInsensitive) < 0;
    });
    return result;
}

WaveformLoadResult loadWaveform(const QString& recordingId, const QString& waveformPath) {
    WaveformLoadResult result;
    result.recordingId = recordingId;
    const QVector<BreezeDesk::WaveformLevel> levels =
        BreezeDesk::WaveformGenerator::read(waveformPath, &result.error);
    if (levels.isEmpty()) {
        return result;
    }

    const BreezeDesk::WaveformLevel& level = levels.constLast();
    const qsizetype count = qMin(level.minimums.size(), level.maximums.size());
    result.peaks.reserve(count);
    for (qsizetype index = 0; index < count; ++index) {
        const int minimumMagnitude = qAbs(static_cast<int>(level.minimums.at(index)));
        const int maximumMagnitude = qAbs(static_cast<int>(level.maximums.at(index)));
        result.peaks.append(static_cast<qreal>(qMax(minimumMagnitude, maximumMagnitude)) / 32768.0);
    }
    return result;
}

} // namespace

namespace BreezeDesk {

ApplicationViewModel::ApplicationViewModel(QObject* parent)
    : ApplicationViewModel(nullptr, nullptr, parent) {}

ApplicationViewModel::ApplicationViewModel(IRecordingRepository* recordingRepository, QObject* parent)
    : ApplicationViewModel(recordingRepository, nullptr, parent) {}

ApplicationViewModel::ApplicationViewModel(IRecordingRepository* recordingRepository,
                                           ITranscriptRepository* transcriptRepository, QObject* parent)
    : QObject(parent), m_library(recordingRepository, this), m_recordingDetail(this), m_transcript(this),
      m_transcriptRevisions(this), m_jobQueue(this), m_player(this), m_modelManager(this),
      m_glossary(this), m_settings(this), m_diagnostics(this),
      m_transcriptRepository(transcriptRepository) {
    m_transcriptAutosaveTimer.setSingleShot(true);
    m_transcriptAutosaveTimer.setInterval(750);
    connect(&m_library, &LibraryViewModel::recordingActivated, this, &ApplicationViewModel::openRecording);
    connect(&m_library, &LibraryViewModel::importRejected, this,
            [this](const QUrl&, const QString& reason) { showToast(reason); });
    connect(&m_library, &LibraryViewModel::operationFailed, this, &ApplicationViewModel::showToast);
    connect(&m_library, &LibraryViewModel::recordingAboutToBePermanentlyDeleted, this,
            [this](const QString& recordingId) {
                if (m_activeRecordingId != recordingId) {
                    return;
                }
                m_transcriptAutosaveTimer.stop();
                m_player.stop();
                m_player.setSource({});
            });
    connect(&m_library, &LibraryViewModel::recordingPermanentlyDeleted, this,
            [this](const QString& recordingId) {
                if (m_activeRecordingId != recordingId) {
                    return;
                }
                m_activeRecordingId.clear();
                m_activeTranscriptJobId.clear();
                m_library.setSelectedRecordingId({});
                m_recordingDetail.clear();
                m_transcript.replaceSegments({});
                (void)m_transcriptRevisions.setRecording({}, {});
                m_player.setWaveformPeaks({});
                emit activeRecordingChanged();
                navigate(QStringLiteral("Library"));
            });
    connect(&m_player, &PlayerViewModel::playbackError, this, &ApplicationViewModel::showToast);
    connect(&m_player, &PlayerViewModel::positionChanged, this,
            [this] { m_transcript.updatePlaybackPosition(m_player.position()); });
    connect(&m_modelManager, &ModelManagerViewModel::commandRejected, this, &ApplicationViewModel::showToast);
    connect(&m_transcript, &TranscriptViewModel::validationError, this, &ApplicationViewModel::showToast);
    connect(&m_transcriptRevisions, &TranscriptRevisionViewModel::operationFailed, this,
            &ApplicationViewModel::showToast);
    connect(&m_transcript, &TranscriptViewModel::saveRequested, this,
            [this] { (void)saveActiveTranscript(); });
    connect(&m_recordingDetail, &RecordingDetailViewModel::notesEdited, this,
            [this](const QString& recordingId, const QString& notes) {
                if (!m_library.setNotes(recordingId, notes)) {
                    m_recordingDetail.setDetails(m_library.details(recordingId));
                }
            });
    connect(&m_transcript, &TranscriptViewModel::dirtyChanged, this, [this] {
        if (m_transcript.dirty()) {
            m_transcriptAutosaveTimer.start();
        }
    });
    connect(&m_transcriptAutosaveTimer, &QTimer::timeout, &m_transcript, &TranscriptViewModel::save);
    const auto prepareTranscriptResume = [this](const QString& jobId) {
        if (jobId != m_activeTranscriptJobId) {
            return;
        }
        if (m_transcript.dirty() && !saveActiveTranscript()) {
            showToast(tr("The transcript could not be saved before transcription resumed."));
        }
        m_transcript.setEditingLocked(true);
    };
    connect(&m_jobQueue, &JobQueueViewModel::retryRequested, this, prepareTranscriptResume);
    connect(&m_jobQueue, &JobQueueViewModel::resumeRequested, this, prepareTranscriptResume);
    m_folderImportBatchTimer.setSingleShot(true);
    connect(&m_folderImportBatchTimer, &QTimer::timeout, this,
            &ApplicationViewModel::processFolderImportBatch);
    connect(&m_library, &LibraryViewModel::recordingMetadataChanged, this,
            [this](const QString& recordingId) {
                if (m_activeRecordingId == recordingId) {
                    m_recordingDetail.setDetails(m_library.details(recordingId));
                }
            });
}

ApplicationViewModel::~ApplicationViewModel() {
    if (m_folderImportCancellation) {
        m_folderImportCancellation->store(true, std::memory_order_relaxed);
    }
}

LibraryViewModel* ApplicationViewModel::library() noexcept {
    return &m_library;
}
RecordingDetailViewModel* ApplicationViewModel::recordingDetail() noexcept {
    return &m_recordingDetail;
}
TranscriptViewModel* ApplicationViewModel::transcript() noexcept {
    return &m_transcript;
}
TranscriptRevisionViewModel* ApplicationViewModel::transcriptRevisions() noexcept {
    return &m_transcriptRevisions;
}
JobQueueViewModel* ApplicationViewModel::jobQueue() noexcept {
    return &m_jobQueue;
}
PlayerViewModel* ApplicationViewModel::player() noexcept {
    return &m_player;
}
ModelManagerViewModel* ApplicationViewModel::modelManager() noexcept {
    return &m_modelManager;
}
GlossaryViewModel* ApplicationViewModel::glossary() noexcept {
    return &m_glossary;
}
SettingsViewModel* ApplicationViewModel::settings() noexcept {
    return &m_settings;
}
DiagnosticsViewModel* ApplicationViewModel::diagnostics() noexcept {
    return &m_diagnostics;
}
QString ApplicationViewModel::currentPage() const {
    return m_currentPage;
}
QString ApplicationViewModel::displayName() const {
    return QString::fromLatin1(AppConfig::DisplayName);
}
QString ApplicationViewModel::activeRecordingId() const {
    return m_activeRecordingId;
}
QString ApplicationViewModel::toastMessage() const {
    return m_toastMessage;
}
bool ApplicationViewModel::folderImportRunning() const noexcept {
    return m_folderImportRunning;
}
int ApplicationViewModel::folderImportCompleted() const noexcept {
    return m_folderImportCompleted;
}
int ApplicationViewModel::folderImportTotal() const noexcept {
    return m_folderImportTotal;
}

void ApplicationViewModel::navigate(const QString& page) {
    static const QStringList pages{QStringLiteral("Library"),  QStringLiteral("Queue"),
                                   QStringLiteral("Trash"),    QStringLiteral("Models"),
                                   QStringLiteral("Glossary"), QStringLiteral("Settings"),
                                   QStringLiteral("Recording")};
    if (!pages.contains(page) || m_currentPage == page) {
        return;
    }
    m_currentPage = page;
    emit currentPageChanged();
}

int ApplicationViewModel::importUrls(const QVariantList& urls) {
    return importUrlsInternal(urls, 0);
}

int ApplicationViewModel::importUrlsInternal(const QVariantList& urls, const quint64 folderOperation) {
    const bool trackedFolderImport = folderOperation != 0;
    const auto cancellation =
        trackedFolderImport ? m_folderImportCancellation : std::shared_ptr<std::atomic_bool>{};
    QVariantList referencedUrls;
    int immediateCount = 0;
    int scheduledCount = 0;
    int folderSynchronousProcessed = 0;
    int folderSynchronousSucceeded = 0;
    for (const QVariant& value : urls) {
        const QUrl url = value.canConvert<QUrl>() ? value.toUrl() : QUrl::fromLocalFile(value.toString());
        const QFileInfo source(url.toLocalFile());
        if (url.isLocalFile() && source.isFile() &&
            isInsideDirectory(source.absoluteFilePath(), StoragePaths::recordings())) {
            const bool imported = !m_library.importManagedCopy(url, source.absoluteFilePath()).isEmpty();
            if (imported) {
                ++immediateCount;
            }
            if (trackedFolderImport) {
                ++folderSynchronousProcessed;
                folderSynchronousSucceeded += imported ? 1 : 0;
            }
            continue;
        }
        if (!m_managedMediaCopyEnabled) {
            referencedUrls.append(url);
            continue;
        }
        if (!url.isLocalFile() || !source.isFile()) {
            referencedUrls.append(url);
            continue;
        }
        const QRegularExpression unsafeFileNameCharacters(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));
        QString safeStem = source.completeBaseName().trimmed();
        QString safeSuffix = source.suffix().trimmed();
        safeStem.replace(unsafeFileNameCharacters, QStringLiteral("_"));
        safeSuffix.replace(unsafeFileNameCharacters, QStringLiteral("_"));
        while (safeStem.endsWith(QLatin1Char('.')) || safeStem.endsWith(QLatin1Char(' '))) {
            safeStem.chop(1);
        }
        if (safeStem.isEmpty()) {
            safeStem = QStringLiteral("media");
        }
        // Keep the path component comfortably below the limits used by Windows and common macOS
        // filesystems even when the original name consists entirely of multi-byte characters.
        safeStem = safeStem.left(48);
        safeSuffix = safeSuffix.left(16);
        const QString safeName =
            safeSuffix.isEmpty() ? safeStem : QStringLiteral("%1.%2").arg(safeStem, safeSuffix);
        const QString destination = QDir(StoragePaths::recordings())
                                        .filePath(QStringLiteral("%1-%2").arg(
                                            QUuid::createUuid().toString(QUuid::WithoutBraces), safeName));
        auto* watcher = new QFutureWatcher<ManagedCopyResult>(this);
        if (trackedFolderImport) {
            ++m_folderImportActiveCopies;
        }
        connect(watcher, &QFutureWatcher<ManagedCopyResult>::finished, this,
                [this, watcher, folderOperation, cancellation] {
                    const ManagedCopyResult result = watcher->result();
                    watcher->deleteLater();
                    const bool tracked = folderOperation != 0 && folderOperation == m_folderImportGeneration;
                    if (tracked) {
                        --m_folderImportActiveCopies;
                    }
                    const bool cancelled =
                        result.cancelled || (cancellation && cancellation->load(std::memory_order_relaxed));
                    if (cancelled) {
                        if (!result.managedPath.isEmpty() &&
                            isInsideDirectory(result.managedPath, StoragePaths::recordings())) {
                            (void)QFile::remove(result.managedPath);
                        }
                        if (tracked) {
                            completeFolderImportItems(folderOperation, 1, 0);
                        }
                        return;
                    }
                    if (!result.error.isEmpty()) {
                        if (!tracked) {
                            showToast(result.error);
                        }
                        if (tracked) {
                            completeFolderImportItems(folderOperation, 1, 0);
                        }
                        return;
                    }
                    if (m_library.importManagedCopy(result.originalUrl, result.managedPath).isEmpty()) {
                        if (isInsideDirectory(result.managedPath, StoragePaths::recordings())) {
                            (void)QFile::remove(result.managedPath);
                        }
                        if (tracked) {
                            completeFolderImportItems(folderOperation, 1, 0);
                        }
                        return;
                    }
                    if (tracked) {
                        completeFolderImportItems(folderOperation, 1, 1);
                    } else {
                        showToast(tr("Imported a managed media copy."));
                    }
                });
        watcher->setFuture(QtConcurrent::run(copyManagedMedia, url, destination, cancellation));
        ++scheduledCount;
    }
    const int referencedCount = static_cast<int>(referencedUrls.size());
    const int referencedImported = m_library.importUrls(referencedUrls);
    immediateCount += referencedImported;
    if (trackedFolderImport && referencedCount > 0) {
        folderSynchronousProcessed += referencedCount;
        folderSynchronousSucceeded += referencedImported;
    }
    if (trackedFolderImport && folderSynchronousProcessed > 0) {
        completeFolderImportItems(folderOperation, folderSynchronousProcessed, folderSynchronousSucceeded);
    }
    if (immediateCount > 0) {
        showToast(tr("Imported %n media file(s).", nullptr, immediateCount));
        navigate(QStringLiteral("Library"));
    }
    if (scheduledCount > 0) {
        showToast(tr("Copying %n media file(s) into managed storage…", nullptr, scheduledCount));
        navigate(QStringLiteral("Library"));
    }
    return immediateCount + scheduledCount;
}

void ApplicationViewModel::importFolder(const QUrl& folder) {
    if (m_folderImportRunning) {
        showToast(tr("A folder import is already running."));
        return;
    }
    const QFileInfo folderInfo(folder.toLocalFile());
    if (!folder.isLocalFile() || !folderInfo.isDir() || !folderInfo.isReadable()) {
        showToast(tr("Choose a readable local folder."));
        return;
    }

    ++m_folderImportGeneration;
    const quint64 operation = m_folderImportGeneration;
    m_folderImportCancellation = std::make_shared<std::atomic_bool>(false);
    m_folderImportRunning = true;
    m_folderImportCompleted = 0;
    m_folderImportTotal = 0;
    m_folderImportSucceeded = 0;
    m_folderImportActiveCopies = 0;
    m_folderImportPendingPaths.clear();
    emit folderImportChanged();

    auto* watcher = new QFutureWatcher<FolderScanResult>(this);
    const auto cancellation = m_folderImportCancellation;
    connect(watcher, &QFutureWatcher<FolderScanResult>::finished, this, [this, watcher, operation] {
        const FolderScanResult result = watcher->result();
        watcher->deleteLater();
        if (operation != m_folderImportGeneration || !m_folderImportRunning) {
            return;
        }
        if (result.cancelled ||
            (m_folderImportCancellation && m_folderImportCancellation->load(std::memory_order_relaxed))) {
            finishFolderImport(operation, true);
            return;
        }
        if (!result.error.isEmpty()) {
            showToast(result.error);
            finishFolderImport(operation, false);
            return;
        }
        if (result.paths.isEmpty()) {
            showToast(tr("The folder does not contain supported audio or video files."));
            finishFolderImport(operation, false);
            return;
        }
        m_folderImportPendingPaths = result.paths;
        m_folderImportTotal = static_cast<int>(result.paths.size());
        emit folderImportChanged();
        m_folderImportBatchTimer.start(0);
    });
    watcher->setFuture(QtConcurrent::run(scanMediaFolder, folderInfo.absoluteFilePath(), cancellation));
}

void ApplicationViewModel::cancelFolderImport() {
    if (!m_folderImportRunning) {
        return;
    }
    if (m_folderImportCancellation) {
        m_folderImportCancellation->store(true, std::memory_order_relaxed);
    }
    m_folderImportBatchTimer.stop();
    m_folderImportPendingPaths.clear();
    if (m_folderImportActiveCopies == 0) {
        finishFolderImport(m_folderImportGeneration, true);
    } else {
        emit folderImportChanged();
    }
}

void ApplicationViewModel::processFolderImportBatch() {
    if (!m_folderImportRunning || !m_folderImportCancellation ||
        m_folderImportCancellation->load(std::memory_order_relaxed)) {
        if (m_folderImportActiveCopies == 0) {
            finishFolderImport(m_folderImportGeneration, true);
        }
        return;
    }

    const int availableSlots = m_managedMediaCopyEnabled
                                   ? qMax(0, 2 - m_folderImportActiveCopies)
                                   : qMin(24, static_cast<int>(m_folderImportPendingPaths.size()));
    if (availableSlots == 0) {
        return;
    }
    QVariantList batch;
    const int count = qMin(availableSlots, static_cast<int>(m_folderImportPendingPaths.size()));
    batch.reserve(count);
    for (int index = 0; index < count; ++index) {
        batch.append(QUrl::fromLocalFile(m_folderImportPendingPaths.takeFirst()));
    }
    (void)importUrlsInternal(batch, m_folderImportGeneration);

    if (!m_folderImportPendingPaths.isEmpty() &&
        (!m_managedMediaCopyEnabled || m_folderImportActiveCopies < 2)) {
        m_folderImportBatchTimer.start(0);
    } else if (m_folderImportPendingPaths.isEmpty() && m_folderImportActiveCopies == 0) {
        finishFolderImport(m_folderImportGeneration, false);
    }
}

void ApplicationViewModel::completeFolderImportItems(const quint64 operation, const int processed,
                                                     const int succeeded) {
    if (operation != m_folderImportGeneration || !m_folderImportRunning) {
        return;
    }
    m_folderImportCompleted = qMin(m_folderImportTotal, m_folderImportCompleted + qMax(0, processed));
    m_folderImportSucceeded += qMax(0, succeeded);
    emit folderImportChanged();
    if (m_folderImportCancellation && m_folderImportCancellation->load(std::memory_order_relaxed)) {
        if (m_folderImportActiveCopies == 0) {
            finishFolderImport(operation, true);
        }
        return;
    }
    if (!m_folderImportPendingPaths.isEmpty() &&
        (!m_managedMediaCopyEnabled || m_folderImportActiveCopies < 2)) {
        m_folderImportBatchTimer.start(0);
    } else if (m_folderImportPendingPaths.isEmpty() && m_folderImportActiveCopies == 0) {
        finishFolderImport(operation, false);
    }
}

void ApplicationViewModel::finishFolderImport(const quint64 operation, const bool cancelled) {
    if (operation != m_folderImportGeneration || !m_folderImportRunning) {
        return;
    }
    m_folderImportBatchTimer.stop();
    m_folderImportPendingPaths.clear();
    m_folderImportRunning = false;
    emit folderImportChanged();
    if (cancelled) {
        showToast(tr("Folder import cancelled."));
    } else if (m_folderImportTotal > 0) {
        const int failed = qMax(0, m_folderImportTotal - m_folderImportSucceeded);
        if (failed == 0) {
            showToast(tr("Imported %n media file(s) from the folder.", nullptr, m_folderImportSucceeded));
        } else {
            showToast(tr("Imported %1 media file(s); %2 could not be imported.")
                          .arg(m_folderImportSucceeded)
                          .arg(failed));
        }
        navigate(QStringLiteral("Library"));
    }
}

void ApplicationViewModel::revealRecording(const QString& recordingId) {
    if (m_platformService == nullptr) {
        showToast(tr("File manager integration is unavailable."));
        return;
    }
    const QVariantMap recording = m_library.details(recordingId);
    const QString sourcePath = recording.value(QStringLiteral("sourcePath")).toString();
    const QString playbackPath = recording.value(QStringLiteral("playbackPath")).toString();
    const QString path = QFileInfo(sourcePath).isFile() ? sourcePath : playbackPath;
    if (!QFileInfo(path).isFile()) {
        showToast(tr("The recording source is missing. Relink it to continue."));
        return;
    }
    QString error;
    if (!m_platformService->revealInFileManager(path, &error)) {
        showToast(error.isEmpty() ? tr("The recording could not be revealed in the file manager.") : error);
    }
}

void ApplicationViewModel::setManagedMediaCopyEnabled(const bool enabled) {
    m_managedMediaCopyEnabled = enabled;
}

void ApplicationViewModel::setPlatformService(IPlatformService* platform) noexcept {
    m_platformService = platform;
}

void ApplicationViewModel::openRecording(const QString& recordingId) {
    if (m_transcript.dirty() && !saveActiveTranscript()) {
        return;
    }
    const bool reopeningActiveRecording = m_activeRecordingId == recordingId;
    const QString preferredRevision =
        reopeningActiveRecording ? m_transcriptRevisions.selectedJobId() : QString{};
    const QVariantMap details = m_library.details(recordingId);
    if (details.isEmpty()) {
        showToast(tr("The selected recording is no longer available."));
        return;
    }
    m_activeRecordingId = recordingId;
    m_library.setSelectedRecordingId(recordingId);
    m_recordingDetail.setDetails(details);
    const QString playbackPath =
        details.value(QStringLiteral("playbackPath"), details.value(QStringLiteral("sourcePath"))).toString();
    m_player.setSource(QUrl::fromLocalFile(playbackPath));
    m_player.setWaveformPeaks({});
    const QString waveformPath = details.value(QStringLiteral("waveformPath")).toString();
    if (!waveformPath.isEmpty() && QFileInfo::exists(waveformPath)) {
        auto* watcher = new QFutureWatcher<WaveformLoadResult>(this);
        connect(watcher, &QFutureWatcher<WaveformLoadResult>::finished, this, [this, watcher] {
            const WaveformLoadResult result = watcher->result();
            watcher->deleteLater();
            if (m_activeRecordingId != result.recordingId) {
                return;
            }
            m_player.setWaveformPeaks(result.peaks);
            if (!result.error.isEmpty()) {
                showToast(tr("The waveform preview could not be loaded: %1").arg(result.error));
            }
        });
        watcher->setFuture(QtConcurrent::run(loadWaveform, recordingId, waveformPath));
    }
    const QString canonicalRevision = details.value(QStringLiteral("activeJobId")).toString();
    (void)m_transcriptRevisions.setRecording(recordingId, canonicalRevision, preferredRevision,
                                             reopeningActiveRecording);
    m_activeTranscriptJobId = m_transcriptRevisions.selectedJobId();
    const bool revisionRunning = m_transcriptRevisions.contains(m_activeTranscriptJobId)
                                     ? m_transcriptRevisions.selectedRevisionIsRunning()
                                     : m_jobQueue.isWritingTranscript(m_activeTranscriptJobId);
    m_transcript.setEditingLocked(revisionRunning);
    reloadActiveTranscript();
    emit activeRecordingChanged();
    navigate(QStringLiteral("Recording"));
}

QString ApplicationViewModel::enqueueTranscription(const QString& recordingId) {
    const QVariantMap details = m_library.details(recordingId);
    if (details.isEmpty()) {
        showToast(tr("Choose an imported recording first."));
        return {};
    }
    const QString jobId = m_jobQueue.allocateJobId();
    emit transcriptionJobRequested(jobId, recordingId);
    if (!m_jobQueue.containsJob(jobId)) {
        return {};
    }
    showToast(tr("Transcription added to the queue."));
    navigate(QStringLiteral("Queue"));
    return jobId;
}

void ApplicationViewModel::exportActiveRecording() {
    if (m_activeRecordingId.isEmpty()) {
        showToast(tr("Open a recording before exporting."));
        return;
    }
    emit exportRequested(m_activeRecordingId);
}

void ApplicationViewModel::exportActiveRecordingTo(const QUrl& file, const QString& format,
                                                   const bool includeTimecodes) {
    if (m_activeRecordingId.isEmpty() || m_activeTranscriptJobId.isEmpty() ||
        m_transcriptRepository == nullptr) {
        showToast(tr("Transcribe and open a recording before exporting."));
        return;
    }
    if (!file.isLocalFile()) {
        showToast(tr("Choose a local destination for the export."));
        return;
    }

    static const QHash<QString, TranscriptExportFormat> formats{
        {QStringLiteral("txt"), TranscriptExportFormat::Txt},
        {QStringLiteral("md"), TranscriptExportFormat::Markdown},
        {QStringLiteral("srt"), TranscriptExportFormat::Srt},
        {QStringLiteral("vtt"), TranscriptExportFormat::Vtt},
        {QStringLiteral("json"), TranscriptExportFormat::Json},
        {QStringLiteral("csv"), TranscriptExportFormat::Csv}};
    const QString normalizedFormat = format.trimmed().toLower();
    if (!formats.contains(normalizedFormat)) {
        showToast(tr("The selected export format is not supported."));
        return;
    }

    if (m_transcript.dirty() && !saveActiveTranscript()) {
        return;
    }
    const auto segments = m_transcriptRepository->segmentsForJob(m_activeTranscriptJobId, true);
    if (!segments) {
        showToast(segments.error().message);
        return;
    }
    const QVariantMap details = m_library.details(m_activeRecordingId);
    TranscriptExportMetadata metadata;
    metadata.recordingId = m_activeRecordingId;
    metadata.title = details.value(QStringLiteral("title")).toString();
    metadata.durationMs = details.value(QStringLiteral("durationMs")).toLongLong();
    metadata.modelId = details.value(QStringLiteral("model")).toString();

    QString destination = file.toLocalFile();
    if (QFileInfo(destination).suffix().isEmpty()) {
        destination += QLatin1Char('.') + normalizedFormat;
    }
    TranscriptExportOptions options;
    options.includeTimecodes = includeTimecodes;
    const auto result = TranscriptExporter::writeFile(destination, formats.value(normalizedFormat), metadata,
                                                      segments.value(), options);
    if (!result) {
        showToast(result.error().message);
        return;
    }
    showToast(tr("Transcript exported to %1").arg(QDir::toNativeSeparators(destination)));
}

void ApplicationViewModel::startRecording() {
    emit recordingRequested();
}

void ApplicationViewModel::dismissToast() {
    if (!m_toastMessage.isEmpty()) {
        m_toastMessage.clear();
        emit toastMessageChanged();
    }
}

void ApplicationViewModel::showToast(const QString& message) {
    if (message.isEmpty()) {
        return;
    }
    m_toastMessage = message;
    emit toastMessageChanged();
}

void ApplicationViewModel::copyToClipboard(const QString& text) const {
    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

void ApplicationViewModel::reloadActiveTranscript() {
    if (m_transcriptRepository == nullptr || m_activeTranscriptJobId.isEmpty()) {
        m_transcript.replaceSegments({});
        return;
    }
    const auto result = m_transcriptRepository->segmentsForJob(m_activeTranscriptJobId, true);
    if (!result) {
        showToast(result.error().message);
        return;
    }
    QList<TranscriptSegmentModel::Segment> items;
    items.reserve(result.value().size());
    for (const TranscriptSegment& segment : result.value()) {
        TranscriptSegmentModel::Segment item;
        item.id = segment.id;
        item.recordingId = segment.recordingId;
        item.jobId = segment.jobId;
        item.chunkId = segment.chunkId;
        item.ordinal = segment.ordinal;
        item.startMs = segment.startMs;
        item.endMs = segment.endMs;
        item.originalText = segment.originalText;
        item.editedText = segment.displayText();
        item.averageProbability = segment.averageProbability;
        item.minimumProbability = segment.minimumProbability;
        item.noSpeechProbability = segment.noSpeechProbability;
        item.lowConfidence = segment.lowConfidence;
        item.reviewed = segment.reviewed;
        item.replacementAudit = segment.replacementAudit;
        item.provisional = segment.provisional;
        item.attempt = segment.attempt;
        item.createdAt = segment.createdAt;
        item.updatedAt = segment.updatedAt;
        items.append(std::move(item));
    }
    m_transcript.replaceSegments(items);
}

void ApplicationViewModel::reloadTranscriptForJob(const QString& recordingId, const QString& jobId,
                                                  const bool editingLocked) {
    if (recordingId != m_activeRecordingId || jobId.isEmpty()) {
        return;
    }

    m_transcriptRevisions.noteLiveRevision(jobId);
    if (m_transcriptRevisions.selectionPinned() &&
        m_transcriptRevisions.selectedJobId() != jobId) {
        return;
    }
    if (jobId == m_activeTranscriptJobId && m_transcript.dirty()) {
        showToast(tr("Save or discard the current transcript edits before refreshing live results."));
        return;
    }

    (void)showTranscriptRevision(jobId, editingLocked, false);
}

void ApplicationViewModel::finishLiveTranscriptRevision(const QString& recordingId,
                                                        const QString& jobId,
                                                        const bool succeeded) {
    if (recordingId != m_activeRecordingId || jobId.isEmpty()) {
        return;
    }
    const bool pinnedOlderRevision = m_transcriptRevisions.selectionPinned() &&
                                     m_transcriptRevisions.selectedJobId() != jobId;
    const QString selectedJobId = m_transcriptRevisions.finishLiveRevision(jobId, succeeded);
    if (pinnedOlderRevision) {
        return;
    }

    m_transcriptAutosaveTimer.stop();
    m_activeTranscriptJobId = selectedJobId;
    if (selectedJobId.isEmpty()) {
        m_transcript.setEditingLocked(false);
        m_transcript.replaceSegments({});
        return;
    }
    m_transcript.setEditingLocked(m_transcriptRevisions.selectedRevisionIsRunning());
    reloadActiveTranscript();
}

void ApplicationViewModel::selectTranscriptRevision(const QString& jobId) {
    if (!m_transcriptRevisions.contains(jobId)) {
        showToast(tr("The selected transcript version is no longer available."));
        return;
    }
    const bool editingLocked =
        m_transcriptRevisions.revisionDetails(jobId).value(QStringLiteral("isRunning")).toBool();
    (void)showTranscriptRevision(jobId, editingLocked, true);
}

void ApplicationViewModel::followLiveTranscript() {
    if (m_transcript.dirty() && !saveActiveTranscript()) {
        return;
    }
    m_transcriptAutosaveTimer.stop();
    const QString jobId = m_transcriptRevisions.followLive();
    m_activeTranscriptJobId = jobId;
    if (jobId.isEmpty()) {
        m_transcript.setEditingLocked(false);
        m_transcript.replaceSegments({});
        return;
    }
    m_transcript.setEditingLocked(m_transcriptRevisions.selectedRevisionIsRunning());
    reloadActiveTranscript();
}

QVariantMap ApplicationViewModel::transcriptRevisionDetails(const QString& jobId) const {
    return m_transcriptRevisions.revisionDetails(jobId);
}

void ApplicationViewModel::deleteTranscriptRevision(const QString& jobId) {
    const QVariantMap details = m_transcriptRevisions.revisionDetails(jobId);
    if (details.isEmpty()) {
        showToast(tr("The selected transcript version is no longer available."));
        return;
    }
    if (!details.value(QStringLiteral("canDelete")).toBool()) {
        showToast(tr("A transcript version can only be deleted after it has stopped running."));
        return;
    }

    const bool deletingSelection = m_activeTranscriptJobId == jobId;
    const bool selectionWasDirty = deletingSelection && m_transcript.dirty();
    if (deletingSelection) {
        m_transcriptAutosaveTimer.stop();
    }
    const auto deleted = m_transcriptRevisions.deleteRevision(jobId);
    if (!deleted) {
        if (selectionWasDirty) {
            m_transcriptAutosaveTimer.start();
        }
        return;
    }

    m_library.refresh();
    m_recordingDetail.setDetails(m_library.details(m_activeRecordingId));
    if (deletingSelection) {
        m_transcript.markSaved();
        m_activeTranscriptJobId = m_transcriptRevisions.selectedJobId();
        if (m_activeTranscriptJobId.isEmpty()) {
            m_transcript.setEditingLocked(false);
            m_transcript.replaceSegments({});
        } else {
            m_transcript.setEditingLocked(m_transcriptRevisions.selectedRevisionIsRunning());
            reloadActiveTranscript();
        }
    }
    showToast(tr("Transcript version deleted."));
}

void ApplicationViewModel::installJobRepository(IJobRepository* repository) {
    m_jobRepository = repository;
    m_transcriptRevisions.installRepositories(repository, m_transcriptRepository);
}

bool ApplicationViewModel::showTranscriptRevision(const QString& jobId, const bool editingLocked,
                                                  const bool pinSelection) {
    if (jobId.isEmpty() || (m_jobRepository != nullptr && !m_transcriptRevisions.contains(jobId))) {
        return false;
    }
    if (jobId == m_activeTranscriptJobId) {
        if (m_transcriptRevisions.contains(jobId)) {
            (void)m_transcriptRevisions.selectRevision(jobId, pinSelection);
        }
        m_transcript.setEditingLocked(editingLocked);
        if (!m_transcript.dirty()) {
            reloadActiveTranscript();
        }
        return true;
    }
    if (m_transcript.dirty() && !saveActiveTranscript()) {
        return false;
    }
    m_transcriptAutosaveTimer.stop();
    if (m_transcriptRevisions.contains(jobId)) {
        (void)m_transcriptRevisions.selectRevision(jobId, pinSelection);
    }
    m_activeTranscriptJobId = jobId;
    m_transcript.setEditingLocked(editingLocked);
    reloadActiveTranscript();
    return true;
}

bool ApplicationViewModel::saveActiveTranscript() {
    if (m_transcriptRepository == nullptr || m_activeRecordingId.isEmpty() ||
        m_activeTranscriptJobId.isEmpty()) {
        showToast(tr("The transcript could not be saved because its recording is unavailable."));
        return false;
    }
    QList<TranscriptSegment> segments;
    const auto items = m_transcript.snapshot();
    segments.reserve(items.size());
    int ordinal = 0;
    for (const auto& item : items) {
        TranscriptSegment segment;
        segment.id = item.id;
        segment.recordingId = m_activeRecordingId;
        segment.jobId = m_activeTranscriptJobId;
        segment.chunkId = item.chunkId;
        segment.ordinal = ordinal++;
        segment.startMs = item.startMs;
        segment.endMs = item.endMs;
        segment.originalText = item.originalText;
        segment.editedText = item.editedText == item.originalText ? QString{} : item.editedText;
        segment.averageProbability = item.averageProbability;
        segment.minimumProbability = item.minimumProbability;
        segment.noSpeechProbability = item.noSpeechProbability;
        segment.lowConfidence = item.lowConfidence;
        segment.reviewed = item.reviewed;
        segment.replacementAudit = item.replacementAudit;
        segment.provisional = item.provisional;
        segment.attempt = item.attempt;
        segment.createdAt = item.createdAt;
        segment.updatedAt = item.updatedAt;
        segments.append(std::move(segment));
    }
    const auto result = m_transcriptRepository->saveEditedRevision(
        m_activeRecordingId, m_activeTranscriptJobId, std::move(segments));
    if (!result) {
        showToast(result.error().message);
        return false;
    }
    m_transcript.markSaved();
    return true;
}

} // namespace BreezeDesk
