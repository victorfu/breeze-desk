#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>

#include "breezedesk/ui/DiagnosticsViewModel.h"
#include "breezedesk/ui/GlossaryViewModel.h"
#include "breezedesk/ui/JobQueueViewModel.h"
#include "breezedesk/ui/LibraryViewModel.h"
#include "breezedesk/ui/ModelManagerViewModel.h"
#include "breezedesk/ui/PlayerViewModel.h"
#include "breezedesk/ui/RecordingDetailViewModel.h"
#include "breezedesk/ui/SettingsViewModel.h"
#include "breezedesk/ui/TranscriptRevisionViewModel.h"
#include "breezedesk/ui/TranscriptViewModel.h"

namespace BreezeDesk {

class IRecordingRepository;
class ITranscriptRepository;
class IJobRepository;
class IPlatformService;

class ApplicationViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(LibraryViewModel* library READ library CONSTANT)
    Q_PROPERTY(RecordingDetailViewModel* recordingDetail READ recordingDetail CONSTANT)
    Q_PROPERTY(TranscriptViewModel* transcript READ transcript CONSTANT)
    Q_PROPERTY(TranscriptRevisionViewModel* transcriptRevisions READ transcriptRevisions CONSTANT)
    Q_PROPERTY(JobQueueViewModel* jobQueue READ jobQueue CONSTANT)
    Q_PROPERTY(PlayerViewModel* player READ player CONSTANT)
    Q_PROPERTY(ModelManagerViewModel* modelManager READ modelManager CONSTANT)
    Q_PROPERTY(GlossaryViewModel* glossary READ glossary CONSTANT)
    Q_PROPERTY(SettingsViewModel* settings READ settings CONSTANT)
    Q_PROPERTY(DiagnosticsViewModel* diagnostics READ diagnostics CONSTANT)
    Q_PROPERTY(QString currentPage READ currentPage WRITE navigate NOTIFY currentPageChanged)
    Q_PROPERTY(QString displayName READ displayName CONSTANT)
    Q_PROPERTY(QString activeRecordingId READ activeRecordingId NOTIFY activeRecordingChanged)
    Q_PROPERTY(QString toastMessage READ toastMessage NOTIFY toastMessageChanged)
    Q_PROPERTY(bool folderImportRunning READ folderImportRunning NOTIFY folderImportChanged)
    Q_PROPERTY(int folderImportCompleted READ folderImportCompleted NOTIFY folderImportChanged)
    Q_PROPERTY(int folderImportTotal READ folderImportTotal NOTIFY folderImportChanged)

  public:
    explicit ApplicationViewModel(QObject* parent = nullptr);
    explicit ApplicationViewModel(IRecordingRepository* recordingRepository, QObject* parent = nullptr);
    ApplicationViewModel(IRecordingRepository* recordingRepository,
                         ITranscriptRepository* transcriptRepository, QObject* parent = nullptr);
    ~ApplicationViewModel() override;

    [[nodiscard]] LibraryViewModel* library() noexcept;
    [[nodiscard]] RecordingDetailViewModel* recordingDetail() noexcept;
    [[nodiscard]] TranscriptViewModel* transcript() noexcept;
    [[nodiscard]] TranscriptRevisionViewModel* transcriptRevisions() noexcept;
    [[nodiscard]] JobQueueViewModel* jobQueue() noexcept;
    [[nodiscard]] PlayerViewModel* player() noexcept;
    [[nodiscard]] ModelManagerViewModel* modelManager() noexcept;
    [[nodiscard]] GlossaryViewModel* glossary() noexcept;
    [[nodiscard]] SettingsViewModel* settings() noexcept;
    [[nodiscard]] DiagnosticsViewModel* diagnostics() noexcept;
    [[nodiscard]] QString currentPage() const;
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] QString activeRecordingId() const;
    [[nodiscard]] QString toastMessage() const;
    [[nodiscard]] bool folderImportRunning() const noexcept;
    [[nodiscard]] int folderImportCompleted() const noexcept;
    [[nodiscard]] int folderImportTotal() const noexcept;

    Q_INVOKABLE void navigate(const QString& page);
    Q_INVOKABLE int importUrls(const QVariantList& urls);
    Q_INVOKABLE void importFolder(const QUrl& folder);
    Q_INVOKABLE void cancelFolderImport();
    Q_INVOKABLE void revealRecording(const QString& recordingId);
    Q_INVOKABLE void openRecording(const QString& recordingId);
    Q_INVOKABLE QString requestTranscription(const QString& recordingId);
    Q_INVOKABLE QString enqueueTranscription(const QString& recordingId);
    Q_INVOKABLE void exportActiveRecording();
    Q_INVOKABLE void exportActiveRecordingTo(const QUrl& file, const QString& format,
                                             bool includeTimecodes = false);
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void dismissToast();
    Q_INVOKABLE void showToast(const QString& message);
    Q_INVOKABLE void copyToClipboard(const QString& text) const;
    Q_INVOKABLE void reloadActiveTranscript();
    Q_INVOKABLE void selectTranscriptRevision(const QString& jobId);
    Q_INVOKABLE void followLiveTranscript();
    Q_INVOKABLE QVariantMap transcriptRevisionDetails(const QString& jobId) const;
    Q_INVOKABLE void deleteTranscriptRevision(const QString& jobId);
    void reloadTranscriptForJob(const QString& recordingId, const QString& jobId, bool editingLocked);
    void finishLiveTranscriptRevision(const QString& recordingId, const QString& jobId, bool succeeded);
    void refreshAfterTranscriptRemoval(const QString& removedJobId = {});
    void installJobRepository(IJobRepository* repository);
    void setManagedMediaCopyEnabled(bool enabled);
    void setPlatformService(IPlatformService* platform) noexcept;

  signals:
    void currentPageChanged();
    void activeRecordingChanged();
    void toastMessageChanged();
    void folderImportChanged();
    void openImportDialogRequested();
    void exportRequested(const QString& recordingId);
    void recordingRequested();
    void transcriptionJobRequested(const QString& jobId, const QString& recordingId);

  private:
    [[nodiscard]] bool saveActiveTranscript();
    bool showTranscriptRevision(const QString& jobId, bool editingLocked, bool pinSelection);
    int importUrlsInternal(const QVariantList& urls, quint64 folderOperation, bool openSingleImport = false);
    void processFolderImportBatch();
    void completeFolderImportItems(quint64 operation, int processed, int succeeded);
    void finishFolderImport(quint64 operation, bool cancelled);

    LibraryViewModel m_library;
    RecordingDetailViewModel m_recordingDetail;
    TranscriptViewModel m_transcript;
    TranscriptRevisionViewModel m_transcriptRevisions;
    JobQueueViewModel m_jobQueue;
    PlayerViewModel m_player;
    ModelManagerViewModel m_modelManager;
    GlossaryViewModel m_glossary;
    SettingsViewModel m_settings;
    DiagnosticsViewModel m_diagnostics;
    QString m_currentPage{"Library"};
    QString m_activeRecordingId;
    QString m_activeTranscriptJobId;
    QStringList m_pendingTranscriptionRecordingIds;
    QString m_toastMessage;
    ITranscriptRepository* m_transcriptRepository{nullptr};
    IJobRepository* m_jobRepository{nullptr};
    QTimer m_transcriptAutosaveTimer;
    QTimer m_folderImportBatchTimer;
    bool m_managedMediaCopyEnabled{false};
    bool m_folderImportRunning{false};
    int m_folderImportCompleted{0};
    int m_folderImportTotal{0};
    int m_folderImportSucceeded{0};
    int m_folderImportActiveCopies{0};
    quint64 m_folderImportGeneration{0};
    QStringList m_folderImportPendingPaths;
    std::shared_ptr<std::atomic_bool> m_folderImportCancellation;
    IPlatformService* m_platformService{nullptr};
};

} // namespace BreezeDesk
