#include "breezedesk/app/MaintenanceController.h"
#include "breezedesk/app/ModelTestController.h"
#include "breezedesk/app/TranscriptionCoordinator.h"
#include "breezedesk/app/WorkerProcessManager.h"
#include "breezedesk/app_config.h"
#include "breezedesk/audio/AudioCacheManager.h"
#include "breezedesk/audio/MicrophoneRecorder.h"
#include "breezedesk/core/ApplicationLogger.h"
#include "breezedesk/core/LoggingCategories.h"
#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/core/TemporaryFileJanitor.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/glossary/SqliteGlossaryRepository.h"
#include "breezedesk/ipc/SingleInstanceGuard.h"
#include "breezedesk/jobs/JobStateMachine.h"
#include "breezedesk/jobs/SqliteJobRepository.h"
#include "breezedesk/models/ModelManager.h"
#include "breezedesk/platform/IPlatformService.h"
#include "breezedesk/settings/SettingsManagers.h"
#include "breezedesk/settings/SettingsStore.h"
#include "breezedesk/transcript/SqliteTranscriptRepository.h"
#include "breezedesk/ui/ApplicationViewModel.h"
#include "breezedesk/ui/UiRegistration.h"
#include "breezedesk/update/UpdateCoordinator.h"
#include "breezedesk/version.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSignalBlocker>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QTranslator>
#include <QUrl>
#include <QtConcurrentRun>

#include <QSet>
#include <cstdio>
#include <memory>
#include <utility>

namespace {
constexpr int CliInvalidArgumentsExitCode = 2;
constexpr int CliSourceMissingExitCode = 3;
constexpr int CliMediaFailureExitCode = 5;
constexpr int CliDatabaseFailureExitCode = 8;
constexpr int ForwardedImportTimeoutMs = 10 * 60 * 1'000;
} // namespace

int main(int argc, char* argv[]) {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication application(argc, argv);
    const QString productName = QString::fromLatin1(BreezeDesk::AppConfig::ProductName);
    application.setOrganizationName(QString::fromLatin1(BreezeDesk::AppConfig::OrganizationName));
    application.setOrganizationDomain(QString::fromLatin1(BreezeDesk::AppConfig::OrganizationDomain));
    application.setApplicationName(QString::fromLatin1(BreezeDesk::AppConfig::DataDirectoryName));
    application.setApplicationDisplayName(QString::fromLatin1(BreezeDesk::AppConfig::DisplayName));
    application.setApplicationVersion(QString::fromLatin1(BREEZEDESK_VERSION_STRING));
    application.setQuitOnLastWindowClosed(false);
    QTranslator uiTranslator;

    BreezeDesk::SettingsStore settingsStore(QSettings::UserScope,
                                            QString::fromLatin1(BreezeDesk::AppConfig::OrganizationName),
                                            QString::fromLatin1(BreezeDesk::AppConfig::DataDirectoryName));
    const auto settingsMigration = BreezeDesk::SettingsMigrationService::migrate(settingsStore);
    BreezeDesk::GeneralSettingsManager generalSettings(settingsStore);
    BreezeDesk::AppearanceSettingsManager appearanceSettings(settingsStore);
    BreezeDesk::TranscriptionSettingsManager transcriptionSettings(settingsStore);
    BreezeDesk::AudioSettingsManager audioSettings(settingsStore);
    BreezeDesk::ModelSettingsManager modelSettings(settingsStore);
    BreezeDesk::StorageSettingsManager storageSettings(settingsStore);
    BreezeDesk::UpdateSettingsManager updateSettings(settingsStore);
    BreezeDesk::PrivacySettingsManager privacySettings(settingsStore);
    BreezeDesk::WindowSettingsManager windowSettings(settingsStore);
    Q_UNUSED(windowSettings)
    if (!storageSettings.dataDirectoryOverride().isEmpty()) {
        qputenv("BREEZEDESK_DATA_ROOT", storageSettings.dataDirectoryOverride().toUtf8());
    }

    QString storageError;
    if (!BreezeDesk::StoragePaths::ensureLayout(&storageError)) {
        const QByteArray safeError = BreezeDesk::LogSanitizer::sanitize(storageError).toUtf8();
        std::fprintf(stderr, "%s storage initialization failed: %s\n", qUtf8Printable(productName),
                     safeError.constData());
        return 10;
    }

    BreezeDesk::LoggingConfiguration loggingConfiguration;
    loggingConfiguration.processName = QString::fromLatin1(BreezeDesk::AppConfig::DisplayName);
    loggingConfiguration.logDirectory = BreezeDesk::StoragePaths::logs();
    loggingConfiguration.redactFilePaths = privacySettings.redactPathsInLogs();
    BreezeDesk::ApplicationLogger logger(loggingConfiguration);
    const auto loggerResult = logger.install();
    if (!loggerResult) {
        const QByteArray safeError =
            BreezeDesk::LogSanitizer::sanitize(loggerResult.error().diagnosticString()).toUtf8();
        std::fprintf(stderr, "%s logging initialization failed: %s\n", qUtf8Printable(productName),
                     safeError.constData());
    }
    const BreezeDesk::TemporaryCleanupReport cleanup = BreezeDesk::TemporaryFileJanitor::clean();
    if (!cleanup.succeeded()) {
        qCWarning(BreezeDesk::logApplication, "Temporary cleanup reported %d failure(s): %s",
                  cleanup.failures, qUtf8Printable(cleanup.error));
    } else if (cleanup.filesRemoved > 0 || cleanup.directoriesRemoved > 0) {
        qCInfo(BreezeDesk::logApplication, "Removed %d expired temporary file(s) and %d directories",
               cleanup.filesRemoved, cleanup.directoriesRemoved);
    }
    BreezeDesk::AudioCacheManager::removeExpiredTemporaryFiles();
    if (!settingsMigration) {
        qCWarning(BreezeDesk::logApplication, "Settings migration failed: %s",
                  qUtf8Printable(settingsMigration.error().diagnosticString()));
    }

    QStringList initialPaths;
    for (const QString& argument : application.arguments().mid(1)) {
        if (!argument.startsWith(QLatin1Char('-')) && QFileInfo::exists(argument)) {
            initialPaths.push_back(QFileInfo(argument).absoluteFilePath());
        }
    }
    BreezeDesk::Ipc::SingleInstanceGuard instanceGuard(QString::fromLatin1(BreezeDesk::AppConfig::BundleId));
    const auto instanceResult = instanceGuard.acquire(initialPaths);
    if (instanceResult == BreezeDesk::Ipc::SingleInstanceGuard::AcquireResult::Forwarded) {
        return 0;
    }
    if (instanceResult == BreezeDesk::Ipc::SingleInstanceGuard::AcquireResult::Error) {
        qCCritical(BreezeDesk::logApplication, "Single-instance initialization failed: %s",
                   qUtf8Printable(instanceGuard.errorString()));
        return 12;
    }

    BreezeDesk::DatabaseManager database({BreezeDesk::StoragePaths::databaseFile()});
    const auto databaseResult = database.initialize();
    if (!databaseResult) {
        qCCritical(BreezeDesk::logApplication, "Database initialization failed: %s",
                   qUtf8Printable(databaseResult.error().technicalDetails));
        return 10;
    }

    std::unique_ptr<BreezeDesk::IPlatformService> platform(BreezeDesk::createPlatformService());
    BreezeDesk::MicrophoneRecorder microphoneRecorder;
    microphoneRecorder.setSelectedDeviceId(audioSettings.inputDeviceId());
    BreezeDesk::SqliteRecordingRepository recordingRepository(database);
    BreezeDesk::SqliteTranscriptRepository transcriptRepository(database);
    BreezeDesk::SqliteJobRepository jobRepository(database);
    BreezeDesk::SqliteGlossaryRepository glossaryRepository(database);
    BreezeDesk::ModelManager modelManager;
    const QString configuredModel = transcriptionSettings.defaultModelId();
    if (!configuredModel.isEmpty()) {
        modelManager.setDefaultModelId(configuredModel);
    }
    BreezeDesk::WorkerProcessManager worker;
    BreezeDesk::UpdateCoordinator updateCoordinator(platform.get(), &application);
    BreezeDesk::MaintenanceController maintenance(
        {&database,
         worker.client(),
         &updateCoordinator,
         {BreezeDesk::StoragePaths::root(), BreezeDesk::StoragePaths::cache(),
          BreezeDesk::StoragePaths::logs(), BreezeDesk::StoragePaths::exports()}},
        &application);
    BreezeDesk::registerUiTypes();

    QQmlApplicationEngine engine;
    std::unique_ptr<BreezeDesk::ApplicationViewModel> viewModel(
        BreezeDesk::createApplicationViewModel(&recordingRepository, &transcriptRepository, &engine));
    viewModel->setPlatformService(platform.get());
    viewModel->settings()->installManagers({&generalSettings, &appearanceSettings, &transcriptionSettings,
                                            &audioSettings, &modelSettings, &storageSettings,
                                            &updateSettings});
    const auto applyAudioDeviceSettings = [viewModel = viewModel.get(), &microphoneRecorder] {
        const QString inputId = viewModel->settings()->microphoneDevice();
        microphoneRecorder.setSelectedDeviceId(inputId == QLatin1String("Default") ? QString{} : inputId);
        viewModel->player()->setOutputDeviceId(viewModel->settings()->playbackDevice());
    };
    QObject::connect(viewModel->settings(), &BreezeDesk::SettingsViewModel::audioChanged, &application,
                     applyAudioDeviceSettings);
    applyAudioDeviceSettings();
    worker.setPreferredBackend(viewModel->settings()->backend());
    const auto applyManagedMediaPolicy = [viewModel = viewModel.get()] {
        viewModel->setManagedMediaCopyEnabled(viewModel->settings()->managedMediaPolicy() ==
                                              QLatin1String("CopyManaged"));
    };
    QObject::connect(viewModel->settings(), &BreezeDesk::SettingsViewModel::storageChanged, &application,
                     applyManagedMediaPolicy);
    applyManagedMediaPolicy();
    viewModel->modelManager()->installServices(&modelManager, &modelSettings);
    viewModel->glossary()->installRepository(&glossaryRepository);
    const QString configuredGlossary = transcriptionSettings.glossaryProfileId();
    if (!configuredGlossary.isEmpty()) {
        const auto configuredProfile = glossaryRepository.profile(configuredGlossary);
        if (configuredProfile && configuredProfile.value().has_value()) {
            viewModel->glossary()->setSelectedProfileId(configuredGlossary);
        }
    }
    transcriptionSettings.setGlossaryProfileId(viewModel->glossary()->selectedProfileId());
    QObject::connect(viewModel->glossary(), &BreezeDesk::GlossaryViewModel::selectedProfileIdChanged,
                     &application, [viewModel = viewModel.get(), &transcriptionSettings] {
                         transcriptionSettings.setGlossaryProfileId(
                             viewModel->glossary()->selectedProfileId());
                     });
    if (!modelManager.isInstalled(modelManager.defaultModelId())) {
        viewModel->navigate(QStringLiteral("Models"));
    }
    BreezeDesk::TranscriptionCoordinator transcriptionCoordinator(recordingRepository, jobRepository,
                                                                  transcriptRepository, modelManager, worker,
                                                                  &transcriptionSettings, &application);
    transcriptionCoordinator.setGlossaryRepository(&glossaryRepository);
    BreezeDesk::ModelTestDependencies modelTestDependencies;
    modelTestDependencies.models = &modelManager;
    modelTestDependencies.workerClient = worker.client();
    modelTestDependencies.ensureWorkerStarted = [&worker] { return worker.start(); };
    modelTestDependencies.workerReserved = [&transcriptionCoordinator] {
        return transcriptionCoordinator.isTranscriptionActive();
    };
    modelTestDependencies.setExternalWorkerReserved = [&transcriptionCoordinator](bool reserved) {
        transcriptionCoordinator.setExternalWorkerReserved(reserved);
    };
    modelTestDependencies.invalidateWorkerModelCache = [&transcriptionCoordinator] {
        transcriptionCoordinator.invalidateLoadedModel();
    };
    modelTestDependencies.abortWorker = [&worker] { worker.abortImmediately(); };
    modelTestDependencies.temporaryDirectory = BreezeDesk::StoragePaths::temporary();
    BreezeDesk::ModelTestController modelTest(std::move(modelTestDependencies), &application);
    modelTest.setBackendPreference(viewModel->settings()->backend(), viewModel->settings()->flashAttention());
    engine.rootContext()->setContextProperty(QStringLiteral("App"), viewModel.get());
    engine.rootContext()->setContextProperty(QStringLiteral("WorkerManager"), &worker);
    engine.rootContext()->setContextProperty(QStringLiteral("Recorder"), &microphoneRecorder);
    engine.rootContext()->setContextProperty(QStringLiteral("Maintenance"), &maintenance);

    const auto applyLanguage = [&application, &engine, &uiTranslator](const QString& language) {
        application.removeTranslator(&uiTranslator);
        const QString locale =
            language == QLatin1String("zh_TW") ? QStringLiteral("zh_TW") : QStringLiteral("en");
        if (uiTranslator.load(QStringLiteral(":/i18n/breezedesk_%1.qm").arg(locale))) {
            application.installTranslator(&uiTranslator);
        }
        engine.retranslate();
    };
    applyLanguage(viewModel->settings()->language());
    QObject::connect(
        viewModel->settings(), &BreezeDesk::SettingsViewModel::languageChanged, &application,
        [viewModel = viewModel.get(), applyLanguage] { applyLanguage(viewModel->settings()->language()); });
    bool applyingPlatformSettings = false;
    const auto applyPlatformSettings = [&applyingPlatformSettings, platform = platform.get(),
                                        viewModel = viewModel.get()] {
        if (applyingPlatformSettings || !platform->capabilities().supportsAutoLaunch) {
            return;
        }
        QString queryError;
        const bool actual = platform->launchAtLogin(&queryError);
        const bool requested = viewModel->settings()->launchAtStartup();
        if (actual == requested) {
            return;
        }
        QString error;
        if (!platform->setLaunchAtLogin(requested, &error)) {
            applyingPlatformSettings = true;
            viewModel->settings()->setLaunchAtStartup(actual);
            applyingPlatformSettings = false;
            viewModel->showToast(error.isEmpty() ? queryError : error);
        }
    };
    QObject::connect(viewModel->settings(), &BreezeDesk::SettingsViewModel::generalChanged, &application,
                     applyPlatformSettings);
    applyPlatformSettings();

    QObject::connect(
        &worker, &BreezeDesk::WorkerProcessManager::workerInterrupted, viewModel.get(),
        [viewModel = viewModel.get()](const QString& message) { viewModel->showToast(message); });
    QObject::connect(viewModel.get(), &BreezeDesk::ApplicationViewModel::transcriptionJobRequested,
                     &transcriptionCoordinator, &BreezeDesk::TranscriptionCoordinator::enqueue);
    QObject::connect(viewModel->jobQueue(), &BreezeDesk::JobQueueViewModel::cancelRequested,
                     &transcriptionCoordinator, &BreezeDesk::TranscriptionCoordinator::cancel);
    QObject::connect(viewModel->jobQueue(), &BreezeDesk::JobQueueViewModel::retryRequested,
                     &transcriptionCoordinator, &BreezeDesk::TranscriptionCoordinator::retry);
    QObject::connect(viewModel->jobQueue(), &BreezeDesk::JobQueueViewModel::resumeRequested,
                     &transcriptionCoordinator, &BreezeDesk::TranscriptionCoordinator::resume);
    QObject::connect(viewModel->jobQueue(), &BreezeDesk::JobQueueViewModel::reorderRequested,
                     &transcriptionCoordinator, &BreezeDesk::TranscriptionCoordinator::reorder);
    QObject::connect(viewModel->jobQueue(), &BreezeDesk::JobQueueViewModel::clearCompletedRequested,
                     &transcriptionCoordinator, &BreezeDesk::TranscriptionCoordinator::clearCompleted);
    QObject::connect(viewModel->jobQueue(), &BreezeDesk::JobQueueViewModel::pauseAfterCurrentChanged,
                     &transcriptionCoordinator, [viewModel = viewModel.get(), &transcriptionCoordinator] {
                         transcriptionCoordinator.setPauseAfterCurrent(
                             viewModel->jobQueue()->pauseAfterCurrent());
                     });
    QObject::connect(
        &transcriptionCoordinator, &BreezeDesk::TranscriptionCoordinator::jobChanged, viewModel.get(),
        [viewModel = viewModel.get()](const QString& jobId, const QString& recordingId, const QString& title,
                                      const QString& state, const QString& stage, double progress,
                                      const QString& error) {
            viewModel->jobQueue()->updateJob(jobId, recordingId, title, state, stage, progress, error);
        });
    QObject::connect(&transcriptionCoordinator, &BreezeDesk::TranscriptionCoordinator::transcriptChanged,
                     viewModel.get(),
                     [viewModel = viewModel.get()](const QString& recordingId, const QString& jobId,
                                                   const bool editingLocked) {
                         viewModel->reloadTranscriptForJob(recordingId, jobId, editingLocked);
                     });
    QObject::connect(&transcriptionCoordinator, &BreezeDesk::TranscriptionCoordinator::libraryChanged,
                     viewModel.get(), [viewModel = viewModel.get()] {
                         const QString activeRecording = viewModel->activeRecordingId();
                         viewModel->library()->refresh();
                         if (!activeRecording.isEmpty()) {
                             viewModel->openRecording(activeRecording);
                         }
                     });
    QObject::connect(&transcriptionCoordinator, &BreezeDesk::TranscriptionCoordinator::errorOccurred,
                     viewModel.get(), &BreezeDesk::ApplicationViewModel::showToast);
    QObject::connect(viewModel->settings(), &BreezeDesk::SettingsViewModel::persistenceError, viewModel.get(),
                     &BreezeDesk::ApplicationViewModel::showToast);
    QObject::connect(viewModel->settings(), &BreezeDesk::SettingsViewModel::clearCacheRequested, &maintenance,
                     &BreezeDesk::MaintenanceController::clearCache);
    QObject::connect(viewModel->settings(), &BreezeDesk::SettingsViewModel::backupDatabaseRequested,
                     &maintenance, &BreezeDesk::MaintenanceController::backupDatabase);
    QObject::connect(viewModel->settings(), &BreezeDesk::SettingsViewModel::updateCheckRequested,
                     &maintenance, &BreezeDesk::MaintenanceController::checkForUpdates);
    const auto updateMaintenanceBusyState = [&maintenance, viewModel = viewModel.get()] {
        maintenance.setCacheBusy(viewModel->jobQueue()->activeCount() > 0);
    };
    QObject::connect(viewModel->jobQueue(), &BreezeDesk::JobQueueViewModel::activeCountChanged, &maintenance,
                     updateMaintenanceBusyState);
    updateMaintenanceBusyState();
    QObject::connect(viewModel->diagnostics(), &BreezeDesk::DiagnosticsViewModel::refreshRequested,
                     &maintenance, &BreezeDesk::MaintenanceController::refreshDiagnostics);
    QObject::connect(&maintenance, &BreezeDesk::MaintenanceController::ffmpegVersionDetected,
                     viewModel->diagnostics(), &BreezeDesk::DiagnosticsViewModel::setFfmpegVersion);
    QObject::connect(&maintenance, &BreezeDesk::MaintenanceController::whisperVersionDetected,
                     viewModel->diagnostics(), &BreezeDesk::DiagnosticsViewModel::setWhisperVersion);
    QObject::connect(&maintenance, &BreezeDesk::MaintenanceController::backendDetected, viewModel.get(),
                     [viewModel = viewModel.get()](const QString& selected, const QString& actual) {
                         viewModel->diagnostics()->setSelectedBackend(selected);
                         viewModel->diagnostics()->setActualBackend(actual);
                         viewModel->modelManager()->updateBackend(selected, actual);
                     });
    QObject::connect(&maintenance, &BreezeDesk::MaintenanceController::operationSucceeded, viewModel.get(),
                     &BreezeDesk::ApplicationViewModel::showToast);
    QObject::connect(&maintenance, &BreezeDesk::MaintenanceController::operationFailed, viewModel.get(),
                     [viewModel = viewModel.get()](const QString&, const QString& message, const QString&) {
                         viewModel->showToast(message);
                     });
    QObject::connect(&maintenance, &BreezeDesk::MaintenanceController::updateAvailable, viewModel.get(),
                     [viewModel = viewModel.get()](const QString& version, const QString&) {
                         viewModel->showToast(
                             QObject::tr("%1 %2 is available.")
                                 .arg(QString::fromLatin1(BreezeDesk::AppConfig::ProductName), version));
                     });
    QObject::connect(&maintenance, &BreezeDesk::MaintenanceController::noUpdateAvailable, viewModel.get(),
                     [viewModel = viewModel.get()] {
                         viewModel->showToast(
                             QObject::tr("%1 is up to date.")
                                 .arg(QString::fromLatin1(BreezeDesk::AppConfig::ProductName)));
                     });
    QObject::connect(&maintenance, &BreezeDesk::MaintenanceController::updateError, viewModel.get(),
                     &BreezeDesk::ApplicationViewModel::showToast);
    QObject::connect(viewModel->modelManager(), &BreezeDesk::ModelManagerViewModel::testRequested, &modelTest,
                     &BreezeDesk::ModelTestController::testModel);
    QObject::connect(&modelTest, &BreezeDesk::ModelTestController::testStarted, viewModel.get(),
                     [viewModel = viewModel.get()](const QString& modelId) {
                         viewModel->modelManager()->updateDownload(modelId, QStringLiteral("Testing"), 0.0);
                     });
    QObject::connect(&modelTest, &BreezeDesk::ModelTestController::progressChanged, viewModel.get(),
                     [viewModel = viewModel.get()](const QString& modelId, double progress) {
                         viewModel->modelManager()->updateDownload(modelId, QStringLiteral("Testing"),
                                                                   progress);
                     });
    QObject::connect(
        &modelTest, &BreezeDesk::ModelTestController::modelLoaded, viewModel.get(),
        [viewModel = viewModel.get()](const QString& modelId, const QString& selectedBackend,
                                      const QString& actualBackend, const QString& runtimeVersion, qint64) {
            viewModel->modelManager()->updateLoaded(modelId, true, actualBackend);
            viewModel->modelManager()->updateRuntimeInfo(selectedBackend, actualBackend, runtimeVersion);
            viewModel->diagnostics()->setSelectedBackend(selectedBackend);
            viewModel->diagnostics()->setActualBackend(actualBackend);
            viewModel->diagnostics()->setWhisperVersion(runtimeVersion);
        });
    QObject::connect(&modelTest, &BreezeDesk::ModelTestController::modelUnloaded, viewModel.get(),
                     [viewModel = viewModel.get()](const QString& modelId) {
                         viewModel->modelManager()->updateLoaded(modelId, false, {});
                     });
    QObject::connect(
        &modelTest, &BreezeDesk::ModelTestController::testSucceeded, viewModel.get(),
        [viewModel = viewModel.get()](const QString& modelId, const QString&, const QString& actualBackend,
                                      const QString& runtimeVersion, qint64 loadTimeMs) {
            viewModel->modelManager()->updateDownload(modelId, QStringLiteral("Installed"), 1.0);
            viewModel->showToast(QObject::tr("Model test passed using %1 (whisper.cpp %2; loaded in %3 ms).")
                                     .arg(actualBackend, runtimeVersion)
                                     .arg(loadTimeMs));
        });
    QObject::connect(&modelTest, &BreezeDesk::ModelTestController::testFailed, viewModel.get(),
                     [viewModel = viewModel.get(), &modelManager](const QString& modelId,
                                                                  const QString& message, const QString&) {
                         viewModel->modelManager()->updateDownload(
                             modelId,
                             modelManager.isInstalled(modelId) ? QStringLiteral("Installed")
                                                               : QStringLiteral("NotInstalled"),
                             modelManager.isInstalled(modelId) ? 1.0 : 0.0);
                         viewModel->showToast(message);
                     });
    QObject::connect(&modelTest, &BreezeDesk::ModelTestController::testCancelled, viewModel.get(),
                     [viewModel = viewModel.get()](const QString& modelId) {
                         viewModel->modelManager()->updateDownload(modelId, QStringLiteral("Installed"), 1.0);
                         viewModel->showToast(QObject::tr("Model test cancelled."));
                     });
    QObject::connect(viewModel->modelManager(), &BreezeDesk::ModelManagerViewModel::operationSucceeded,
                     viewModel.get(), &BreezeDesk::ApplicationViewModel::showToast);
    QObject::connect(viewModel->glossary(), &BreezeDesk::GlossaryViewModel::validationError, viewModel.get(),
                     &BreezeDesk::ApplicationViewModel::showToast);
    QObject::connect(viewModel->glossary(), &BreezeDesk::GlossaryViewModel::operationSucceeded,
                     viewModel.get(), &BreezeDesk::ApplicationViewModel::showToast);
    QObject::connect(viewModel->modelManager(), &BreezeDesk::ModelManagerViewModel::defaultModelChanged,
                     &application, [viewModel = viewModel.get(), &transcriptionSettings, &modelManager] {
                         const QString id = viewModel->modelManager()->defaultModelId();
                         transcriptionSettings.setDefaultModelId(id);
                         (void)transcriptionSettings.sync();
                         modelManager.setDefaultModelId(id);
                     });
    QObject::connect(viewModel->settings(), &BreezeDesk::SettingsViewModel::transcriptionChanged,
                     &application, [viewModel = viewModel.get(), &modelManager, &worker, &modelTest] {
                         const QString id = viewModel->settings()->defaultModel();
                         modelManager.setDefaultModelId(id);
                         worker.setPreferredBackend(viewModel->settings()->backend());
                         modelTest.setBackendPreference(viewModel->settings()->backend(),
                                                        viewModel->settings()->flashAttention());
                         if (viewModel->modelManager()->defaultModelId() != id) {
                             viewModel->modelManager()->setDefaultModel(id);
                         }
                     });
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        [] { QCoreApplication::exit(11); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("BreezeDesk"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 11;
    }

    const QIcon applicationIcon(QStringLiteral(":/qt/qml/BreezeDesk/icons/breezedesk.svg"));
    application.setWindowIcon(applicationIcon);
    QSystemTrayIcon tray(applicationIcon);
    QMenu trayMenu;
    QAction showAction(QObject::tr("Show %1").arg(productName), &trayMenu);
    QAction importAction(QObject::tr("Import Files"), &trayMenu);
    QAction recordAction(QObject::tr("Start Recording"), &trayMenu);
    QAction queueAction(QObject::tr("Queue: idle"), &trayMenu);
    queueAction.setEnabled(false);
    QAction pauseAction(QObject::tr("Pause after current job"), &trayMenu);
    pauseAction.setCheckable(true);
    QAction quitAction(QObject::tr("Quit"), &trayMenu);
    trayMenu.addAction(&showAction);
    trayMenu.addAction(&importAction);
    trayMenu.addAction(&recordAction);
    trayMenu.addSeparator();
    trayMenu.addAction(&queueAction);
    trayMenu.addAction(&pauseAction);
    trayMenu.addSeparator();
    trayMenu.addAction(&quitAction);
    tray.setContextMenu(&trayMenu);
    tray.setToolTip(QString::fromLatin1(BreezeDesk::AppConfig::DisplayName));
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray.show();
    }
    const auto showWindow = [&engine, &platform] {
        if (!engine.rootObjects().isEmpty()) {
            QMetaObject::invokeMethod(engine.rootObjects().constFirst(), "show");
            QMetaObject::invokeMethod(engine.rootObjects().constFirst(), "raise");
            QMetaObject::invokeMethod(engine.rootObjects().constFirst(), "requestActivate");
            platform->activateApplication();
        }
    };
    const auto importPaths = [viewModel = viewModel.get()](const QStringList& paths) {
        QVariantList urls;
        for (const QString& path : paths) {
            urls.push_back(QUrl::fromLocalFile(path));
        }
        if (!urls.isEmpty()) {
            viewModel->importUrls(urls);
        }
    };
    QFutureWatcher<bool> microphonePermissionWatcher(&application);
    auto microphonePermissionError = std::make_shared<QString>();
    QString pendingRecordingPath;
    QObject::connect(
        viewModel.get(), &BreezeDesk::ApplicationViewModel::recordingRequested, &application,
        [&microphonePermissionWatcher, microphonePermissionError, &pendingRecordingPath,
         platform = platform.get(), viewModel = viewModel.get(), &microphoneRecorder] {
            if (microphoneRecorder.isRecording()) {
                viewModel->showToast(QObject::tr("A microphone recording is already in progress."));
                return;
            }
            if (microphonePermissionWatcher.isRunning()) {
                viewModel->showToast(QObject::tr("Waiting for microphone permission."));
                return;
            }
            pendingRecordingPath = QDir(BreezeDesk::StoragePaths::recordings())
                                       .filePath(QStringLiteral("Recording-%1.wav")
                                                     .arg(QDateTime::currentDateTimeUtc().toString(
                                                         QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
            microphonePermissionError->clear();
            viewModel->showToast(QObject::tr("Requesting microphone access…"));
            microphonePermissionWatcher.setFuture(QtConcurrent::run([platform, microphonePermissionError] {
                return platform->requestMicrophonePermission(microphonePermissionError.get());
            }));
        });
    QObject::connect(&microphonePermissionWatcher, &QFutureWatcher<bool>::finished, &application,
                     [&microphonePermissionWatcher, microphonePermissionError, &pendingRecordingPath,
                      viewModel = viewModel.get(), &microphoneRecorder] {
                         if (!microphonePermissionWatcher.result()) {
                             viewModel->showToast(microphonePermissionError->isEmpty()
                                                      ? QObject::tr("Microphone permission was not granted.")
                                                      : *microphonePermissionError);
                             return;
                         }
                         if (!microphoneRecorder.start(pendingRecordingPath)) {
                             viewModel->showToast(
                                 QObject::tr("The microphone recording could not be started."));
                         }
                     });
    QObject::connect(&microphoneRecorder, &BreezeDesk::MicrophoneRecorder::recordingError, viewModel.get(),
                     &BreezeDesk::ApplicationViewModel::showToast);
    QObject::connect(&microphoneRecorder, &BreezeDesk::MicrophoneRecorder::selectedDeviceIdChanged,
                     &application, [&microphoneRecorder, &audioSettings, viewModel = viewModel.get()] {
                         audioSettings.setInputDeviceId(microphoneRecorder.selectedDeviceId());
                         const auto result = audioSettings.sync();
                         if (!result) {
                             viewModel->showToast(result.error().message);
                         }
                     });
    QObject::connect(
        &microphoneRecorder, &BreezeDesk::MicrophoneRecorder::recordingFinished, &application,
        [viewModel = viewModel.get(), &recordingRepository, &audioSettings, showWindow](const QString& path) {
            showWindow();
            const int imported = viewModel->importUrls({QUrl::fromLocalFile(path)});
            if (imported == 0) {
                return;
            }
            const auto recording = recordingRepository.findBySourcePath(QFileInfo(path).absoluteFilePath());
            if (!recording || !recording.value().has_value()) {
                viewModel->showToast(QObject::tr("The recording was saved but could not be opened."));
                return;
            }
            const QString recordingId = recording.value()->id;
            viewModel->openRecording(recordingId);
            if (audioSettings.autoTranscribeRecording()) {
                viewModel->enqueueTranscription(recordingId);
            }
        });
    QObject::connect(&instanceGuard, &BreezeDesk::Ipc::SingleInstanceGuard::activationRequested, &application,
                     [showWindow, importPaths](const QStringList& paths) {
                         showWindow();
                         importPaths(paths);
                     });
    auto pendingForwardedTranscriptions = std::make_shared<QSet<QString>>();
    QObject::connect(viewModel->library(), &BreezeDesk::LibraryViewModel::recordingImported, &application,
                     [pendingForwardedTranscriptions,
                      viewModel = viewModel.get()](const QString& recordingId, const QString& sourcePath) {
                         const QString normalizedPath = QFileInfo(sourcePath).absoluteFilePath();
                         if (pendingForwardedTranscriptions->remove(normalizedPath) > 0) {
                             (void)viewModel->enqueueTranscription(recordingId);
                         }
                     });
    instanceGuard.setCommandHandler([showWindow, viewModel = viewModel.get(), &recordingRepository,
                                     &jobRepository, &modelManager, pendingForwardedTranscriptions,
                                     &application](const QStringList& commandArguments)
                                        -> BreezeDesk::Ipc::ApplicationCommandReply {
        BreezeDesk::Ipc::ApplicationCommandReply reply;
        if (commandArguments.isEmpty()) {
            return reply;
        }
        const bool json = commandArguments.contains(QStringLiteral("--json"));
        QStringList positional = commandArguments;
        positional.removeAll(QStringLiteral("--json"));
        const auto setOutput = [&reply, json](const QJsonObject& object, const QString& plainText) {
            if (json) {
                reply.standardOutput = QJsonDocument(object).toJson(QJsonDocument::Compact);
                reply.standardOutput.append('\n');
            } else if (!plainText.isEmpty()) {
                reply.standardOutput = plainText.toUtf8();
                if (!reply.standardOutput.endsWith('\n')) {
                    reply.standardOutput.append('\n');
                }
            }
        };
        const auto setError = [&reply](const int exitCode, const QString& message) {
            reply.handled = true;
            reply.exitCode = exitCode;
            reply.standardError = message.toUtf8();
            if (!reply.standardError.endsWith('\n')) {
                reply.standardError.append('\n');
            }
        };

        if (positional.first() == QLatin1String("import")) {
            const QStringList paths = positional.mid(1);
            if (paths.isEmpty()) {
                setError(CliInvalidArgumentsExitCode,
                         QStringLiteral("Import requires at least one media file."));
                return reply;
            }
            QVariantList urls;
            for (const QString& path : paths) {
                const QFileInfo file(path);
                if (!file.isFile()) {
                    setError(CliSourceMissingExitCode, QStringLiteral("Source file not found: %1").arg(path));
                    return reply;
                }
                urls.append(QUrl::fromLocalFile(file.absoluteFilePath()));
            }
            showWindow();
            const int accepted = viewModel->importUrls(urls);
            reply.handled = true;
            reply.exitCode = accepted == paths.size() ? 0 : CliMediaFailureExitCode;
            setOutput({{QStringLiteral("schemaVersion"), 1},
                       {QStringLiteral("forwarded"), true},
                       {QStringLiteral("accepted"), accepted}},
                      QStringLiteral("Accepted %1 media file(s) in the %2 GUI.")
                          .arg(accepted)
                          .arg(QString::fromLatin1(BreezeDesk::AppConfig::ProductName)));
            if (accepted != paths.size()) {
                reply.standardError =
                    QByteArrayLiteral("The GUI could not accept every requested media file.\n");
            }
            return reply;
        }

        if (positional.first() == QLatin1String("transcribe")) {
            // Option-bearing transcriptions retain the synchronous CLI/export contract and
            // therefore execute locally. A bare request is a GUI queue command.
            if (positional.size() != 2) {
                return reply;
            }
            const QFileInfo source(positional.at(1));
            if (!source.isFile()) {
                setError(CliSourceMissingExitCode,
                         QStringLiteral("Source file not found: %1").arg(positional.at(1)));
                return reply;
            }
            const QString sourcePath = source.absoluteFilePath();
            const auto existing = recordingRepository.findBySourcePath(sourcePath);
            if (!existing) {
                setError(CliDatabaseFailureExitCode, existing.error().diagnosticString());
                return reply;
            }
            QString recordingId;
            QString jobId;
            QString state = QStringLiteral("Queued");
            showWindow();
            if (existing.value().has_value()) {
                recordingId = existing.value()->id;
                jobId = viewModel->enqueueTranscription(recordingId);
                if (jobId.isEmpty()) {
                    setError(CliDatabaseFailureExitCode,
                             QStringLiteral("The GUI could not enqueue the transcription."));
                    return reply;
                }
            } else if (pendingForwardedTranscriptions->contains(sourcePath)) {
                state = QStringLiteral("Importing");
            } else {
                pendingForwardedTranscriptions->insert(sourcePath);
                const int accepted = viewModel->importUrls({QUrl::fromLocalFile(sourcePath)});
                if (accepted == 0) {
                    pendingForwardedTranscriptions->remove(sourcePath);
                    setError(CliMediaFailureExitCode,
                             QStringLiteral("The GUI could not import the media file."));
                    return reply;
                }
                state = pendingForwardedTranscriptions->contains(sourcePath) ? QStringLiteral("Importing")
                                                                             : QStringLiteral("Queued");
                const auto imported = recordingRepository.findBySourcePath(sourcePath);
                if (imported && imported.value().has_value()) {
                    recordingId = imported.value()->id;
                }
                QTimer::singleShot(ForwardedImportTimeoutMs, &application,
                                   [pendingForwardedTranscriptions, sourcePath] {
                                       pendingForwardedTranscriptions->remove(sourcePath);
                                   });
            }
            reply.handled = true;
            setOutput({{QStringLiteral("schemaVersion"), 1},
                       {QStringLiteral("forwarded"), true},
                       {QStringLiteral("state"), state},
                       {QStringLiteral("recordingId"), recordingId},
                       {QStringLiteral("jobId"), jobId}},
                      jobId.isEmpty()
                          ? QStringLiteral("Accepted by the %1 GUI (%2).")
                                .arg(QString::fromLatin1(BreezeDesk::AppConfig::ProductName), state)
                          : QStringLiteral("%1\t%2").arg(jobId, state));
            return reply;
        }

        if (positional.size() == 3 && positional.at(0) == QLatin1String("jobs") &&
            positional.at(1) == QLatin1String("cancel")) {
            const QString jobId = positional.at(2);
            const auto job = jobRepository.findById(jobId);
            if (!job || !job.value().has_value()) {
                setError(CliDatabaseFailureExitCode, job ? QStringLiteral("Job not found: %1").arg(jobId)
                                                         : job.error().diagnosticString());
                return reply;
            }
            const BreezeDesk::JobState state = job.value()->state;
            if (state != BreezeDesk::JobState::Cancelling &&
                !BreezeDesk::JobStateMachine::canTransition(
                    state, BreezeDesk::JobStateMachine::isRunning(state) ? BreezeDesk::JobState::Cancelling
                                                                         : BreezeDesk::JobState::Cancelled)) {
                setError(CliDatabaseFailureExitCode,
                         QStringLiteral("Job %1 cannot be cancelled from state %2.")
                             .arg(jobId, BreezeDesk::jobStateName(state)));
                return reply;
            }
            showWindow();
            viewModel->navigate(QStringLiteral("Queue"));
            if (state != BreezeDesk::JobState::Cancelling) {
                viewModel->jobQueue()->cancel(jobId);
            }
            const QString requestedState = BreezeDesk::JobStateMachine::isRunning(state)
                                               ? QStringLiteral("Cancelling")
                                               : QStringLiteral("Cancelled");
            reply.handled = true;
            setOutput({{QStringLiteral("schemaVersion"), 1},
                       {QStringLiteral("forwarded"), true},
                       {QStringLiteral("jobId"), jobId},
                       {QStringLiteral("state"), requestedState}},
                      QStringLiteral("%1\t%2").arg(jobId, requestedState));
            return reply;
        }

        if (positional.size() == 3 && positional.at(0) == QLatin1String("models") &&
            positional.at(1) == QLatin1String("download")) {
            const QString modelId = positional.at(2);
            if (modelManager.manifest().find(modelId) == nullptr) {
                setError(CliInvalidArgumentsExitCode, QStringLiteral("Unknown model id: %1").arg(modelId));
                return reply;
            }
            showWindow();
            viewModel->navigate(QStringLiteral("Models"));
            const bool installed = modelManager.isInstalled(modelId);
            if (!installed) {
                viewModel->modelManager()->download(modelId);
            }
            const QString state = installed ? QStringLiteral("Installed") : QStringLiteral("Downloading");
            reply.handled = true;
            setOutput({{QStringLiteral("schemaVersion"), 1},
                       {QStringLiteral("forwarded"), true},
                       {QStringLiteral("modelId"), modelId},
                       {QStringLiteral("state"), state}},
                      QStringLiteral("%1\t%2").arg(modelId, state));
            return reply;
        }

        // Read-only commands and synchronous export/transcription variants are safe to execute
        // locally and preserve the existing stdout schema, so the GUI explicitly declines them.
        return reply;
    });
    QObject::connect(&showAction, &QAction::triggered, &application, showWindow);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &application,
                     [showWindow](QSystemTrayIcon::ActivationReason reason) {
                         if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                             showWindow();
                         }
                     });
    QObject::connect(&importAction, &QAction::triggered, viewModel.get(),
                     &BreezeDesk::ApplicationViewModel::openImportDialogRequested);
    QObject::connect(&recordAction, &QAction::triggered, &application, [&engine, showWindow] {
        showWindow();
        if (!engine.rootObjects().isEmpty()) {
            QMetaObject::invokeMethod(engine.rootObjects().constFirst(), "openRecordingDialog");
        }
    });
    QObject::connect(&pauseAction, &QAction::toggled, viewModel->jobQueue(),
                     &BreezeDesk::JobQueueViewModel::setPauseAfterCurrent);
    QObject::connect(viewModel->jobQueue(), &BreezeDesk::JobQueueViewModel::pauseAfterCurrentChanged,
                     &pauseAction, [&pauseAction, viewModel = viewModel.get()] {
                         const QSignalBlocker blocker(&pauseAction);
                         pauseAction.setChecked(viewModel->jobQueue()->pauseAfterCurrent());
                     });
    const auto updateTrayQueue = [&queueAction, viewModel = viewModel.get()] {
        const int active = viewModel->jobQueue()->activeCount();
        queueAction.setText(active == 0 ? QObject::tr("Queue: idle")
                                        : QObject::tr("Queue: %n active job(s)", nullptr, active));
    };
    QObject::connect(viewModel->jobQueue(), &BreezeDesk::JobQueueViewModel::activeCountChanged, &queueAction,
                     updateTrayQueue);
    updateTrayQueue();
    QObject::connect(&quitAction, &QAction::triggered, &application, [&engine, showWindow] {
        showWindow();
        if (!engine.rootObjects().isEmpty()) {
            QMetaObject::invokeMethod(engine.rootObjects().constFirst(), "requestQuit");
        }
    });

    const bool workerStarted = worker.start();
    Q_UNUSED(workerStarted)
    transcriptionCoordinator.initialize();
    if (updateSettings.automaticCheck()) {
        QTimer::singleShot(10'000, &updateCoordinator,
                           [&updateCoordinator] { updateCoordinator.checkForUpdates(false); });
    }
    importPaths(initialPaths);
    const int result = application.exec();
    modelTest.cancel();
    transcriptionCoordinator.shutdown();
    worker.stop();
    return result;
}
