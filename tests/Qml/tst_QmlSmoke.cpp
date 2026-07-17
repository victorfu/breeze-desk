#include "breezedesk/audio/WaveformGenerator.h"
#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/glossary/SqliteGlossaryRepository.h"
#include "breezedesk/models/ModelManager.h"
#include "breezedesk/settings/SettingsManagers.h"
#include "breezedesk/transcript/ITranscriptRepository.h"
#include "breezedesk/ui/ApplicationViewModel.h"
#include "breezedesk/ui/UiRegistration.h"

#include <QDataStream>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

#include <atomic>
#include <utility>

namespace {

QStringList qmlMessages;

class EnvironmentVariableGuard final {
  public:
    explicit EnvironmentVariableGuard(QByteArray name)
        : m_name(std::move(name)), m_wasSet(qEnvironmentVariableIsSet(m_name.constData())),
          m_originalValue(qgetenv(m_name.constData())) {}

    ~EnvironmentVariableGuard() {
        if (m_wasSet) {
            qputenv(m_name.constData(), m_originalValue);
        } else {
            qunsetenv(m_name.constData());
        }
    }

    EnvironmentVariableGuard(const EnvironmentVariableGuard&) = delete;
    EnvironmentVariableGuard& operator=(const EnvironmentVariableGuard&) = delete;

  private:
    QByteArray m_name;
    bool m_wasSet{false};
    QByteArray m_originalValue;
};

void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& message) {
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        qmlMessages.append(message);
    }
}

} // namespace

class FakeRecorder final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY durationChanged)
    Q_PROPERTY(double level READ level NOTIFY levelChanged)
    Q_PROPERTY(QVariantList inputDevices READ inputDevices CONSTANT)
    Q_PROPERTY(QString selectedDeviceId READ selectedDeviceId WRITE setSelectedDeviceId NOTIFY
                   selectedDeviceIdChanged)

  public:
    [[nodiscard]] bool recording() const noexcept { return false; }
    [[nodiscard]] bool paused() const noexcept { return false; }
    [[nodiscard]] qint64 durationMs() const noexcept { return 0; }
    [[nodiscard]] double level() const noexcept { return 0.0; }
    [[nodiscard]] QVariantList inputDevices() const {
        return {QVariantMap{{QStringLiteral("id"), QStringLiteral("fixture")},
                            {QStringLiteral("description"), QStringLiteral("Fixture microphone")}}};
    }
    [[nodiscard]] QString selectedDeviceId() const { return m_selectedDeviceId; }
    void setSelectedDeviceId(const QString& value) {
        if (m_selectedDeviceId != value) {
            m_selectedDeviceId = value;
            emit selectedDeviceIdChanged();
        }
    }

    Q_INVOKABLE void pause() {}
    Q_INVOKABLE void resume() {}
    Q_INVOKABLE void stop() {}

  signals:
    void recordingChanged();
    void pausedChanged();
    void durationChanged();
    void levelChanged();
    void selectedDeviceIdChanged();
    void recordingFinished(const QString& path);
    void recordingError(const QString& message);

  private:
    QString m_selectedDeviceId;
};

class FakeTranscriptRepository final : public BreezeDesk::ITranscriptRepository {
  public:
    [[nodiscard]] BreezeDesk::Result<QList<BreezeDesk::TranscriptSegment>>
    segmentsForJob(const QString&, bool) const override {
        return BreezeDesk::Result<QList<BreezeDesk::TranscriptSegment>>::success(m_segments);
    }

    [[nodiscard]] BreezeDesk::Result<std::optional<BreezeDesk::TranscriptSegment>>
    segment(const QString&) const override {
        return BreezeDesk::Result<std::optional<BreezeDesk::TranscriptSegment>>::success(std::nullopt);
    }

    [[nodiscard]] BreezeDesk::Result<void>
    replaceRevision(const QString&, const QString&, QList<BreezeDesk::TranscriptSegment> segments) override {
        m_segments = std::move(segments);
        return BreezeDesk::Result<void>::success();
    }

    [[nodiscard]] BreezeDesk::Result<void>
    saveEditedRevision(const QString&, const QString&,
                       QList<BreezeDesk::TranscriptSegment> segments) override {
        ++saveAttempts;
        if (failWrites) {
            return BreezeDesk::Result<void>::failure(BreezeDesk::UserFacingError::database(
                BreezeDesk::ErrorCode::DatabaseQueryFailed, QStringLiteral("Fixture save failed.")));
        }
        m_segments = std::move(segments);
        return BreezeDesk::Result<void>::success();
    }

    [[nodiscard]] BreezeDesk::Result<void> replaceChunk(const QString&, const QString&, const QString&,
                                                        QList<BreezeDesk::TranscriptSegment> segments, bool,
                                                        int) override {
        m_segments = std::move(segments);
        return BreezeDesk::Result<void>::success();
    }

    [[nodiscard]] BreezeDesk::Result<void> saveEditedSegment(const BreezeDesk::TranscriptSegment&) override {
        return BreezeDesk::Result<void>::success();
    }

    [[nodiscard]] BreezeDesk::Result<void> deleteSegment(const QString&) override {
        return BreezeDesk::Result<void>::success();
    }

    QList<BreezeDesk::TranscriptSegment> m_segments;
    bool failWrites{false};
    int saveAttempts{0};
};

class tst_QmlSmoke final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QStandardPaths::setTestModeEnabled(true);
        BreezeDesk::registerUiTypes();
        qInstallMessageHandler(messageHandler);
    }

    void cleanup() { qmlMessages.clear(); }

    void loadsMainAndEveryPage() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QVERIFY(root->findChild<QObject*>(QStringLiteral("pageStack")));
        const QStringList pages{QStringLiteral("libraryPage"),  QStringLiteral("queuePage"),
                                QStringLiteral("trashPage"),    QStringLiteral("modelsPage"),
                                QStringLiteral("glossaryPage"), QStringLiteral("settingsPage"),
                                QStringLiteral("recordingPage")};
        for (const QString& page : pages) {
            QVERIFY2(root->findChild<QObject*>(page),
                     qPrintable(QStringLiteral("Missing page: %1").arg(page)));
        }
        QVERIFY(root->findChild<QObject*>(QStringLiteral("muteToggle")));
        QVERIFY(root->findChild<QObject*>(QStringLiteral("volumeSlider")));
        QVERIFY(root->findChild<QObject*>(QStringLiteral("notesEditor")));
        const auto failures = qmlMessages.filter(
            QRegularExpression(QStringLiteral("qrc:|ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void navigationThemeAndTranslationSwitch() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/Main.qml")));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* vm = root->findChild<BreezeDesk::ApplicationViewModel*>();
        QVERIFY(vm);
        for (const QString& page :
             {QStringLiteral("Queue"), QStringLiteral("Trash"), QStringLiteral("Models"),
              QStringLiteral("Glossary"), QStringLiteral("Settings"), QStringLiteral("Library")}) {
            vm->navigate(page);
            QCoreApplication::processEvents();
            QCOMPARE(vm->currentPage(), page);
        }
        vm->settings()->setTheme(QStringLiteral("Dark"));
        vm->settings()->setTheme(QStringLiteral("Light"));
        vm->settings()->setTheme(QStringLiteral("System"));
        vm->settings()->setLanguage(QStringLiteral("en"));
        vm->settings()->setLanguage(QStringLiteral("zh_TW"));
        QCoreApplication::processEvents();
        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void loadsStandaloneDialogs() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        FakeRecorder recorder;
        QQmlComponent recording(&engine,
                                QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/dialogs/RecordingDialog.qml")));
        QVERIFY2(recording.isReady(), qPrintable(recording.errorString()));
        QScopedPointer<QObject> recordingDialog(recording.createWithInitialProperties(
            {{QStringLiteral("recorder"), QVariant::fromValue<QObject*>(&recorder)}}));
        QVERIFY2(recordingDialog, qPrintable(recording.errorString()));

        QQmlComponent diagnostics(
            &engine, QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/dialogs/DiagnosticsExportDialog.qml")));
        QVERIFY2(diagnostics.isReady(), qPrintable(diagnostics.errorString()));
        QScopedPointer<QObject> diagnosticsDialog(diagnostics.create());
        QVERIFY2(diagnosticsDialog, qPrintable(diagnostics.errorString()));
    }

    void viewModelCommandsHaveObservableState() {
        BreezeDesk::ApplicationViewModel vm;
        QTemporaryFile media;
        QVERIFY(media.open());
        const QVariantList urls{QUrl::fromLocalFile(media.fileName())};
        QCOMPARE(vm.importUrls(urls), 1);
        QCOMPARE(vm.library()->recordings()->rowCount(), 1);
        const QModelIndex first = vm.library()->recordings()->index(0, 0);
        const QString id =
            vm.library()->recordings()->data(first, BreezeDesk::RecordingListModel::IdRole).toString();
        QVERIFY(!id.isEmpty());
        vm.openRecording(id);
        QCOMPARE(vm.activeRecordingId(), id);
        QCOMPARE(vm.currentPage(), QStringLiteral("Recording"));
        vm.player()->setVolume(0.35);
        QVERIFY(qAbs(vm.player()->volume() - 0.35) < 0.001);
        vm.player()->setMuted(true);
        QVERIFY(vm.player()->muted());
        QVERIFY(!vm.settings()->microphoneDevices().isEmpty());
        QCOMPARE(vm.settings()->microphoneDevices().constFirst().toMap().value(QStringLiteral("id")),
                 QStringLiteral("Default"));
        QVERIFY(!vm.settings()->playbackDevices().isEmpty());
        vm.player()->setOutputDeviceId(QStringLiteral("Default"));
        QCOMPARE(vm.player()->outputDeviceId(), QStringLiteral("Default"));
        vm.enqueueTranscription(id);
        QCOMPARE(vm.jobQueue()->jobs()->rowCount(), 1);
        QCOMPARE(vm.currentPage(), QStringLiteral("Queue"));
        vm.library()->moveToTrash(id);
        QCOMPARE(vm.library()->trash()->rowCount(), 1);
        vm.library()->restore(id);
        QCOMPARE(vm.library()->recordings()->rowCount(), 1);
    }

    void libraryStateSurvivesViewModelRecreation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        BreezeDesk::DatabaseManager database(
            {directory.filePath(QStringLiteral("library.sqlite3")), 5'000, true, false});
        QVERIFY(database.initialize());
        BreezeDesk::SqliteRecordingRepository repository(database);
        QTemporaryFile media;
        QVERIFY(media.open());

        QString recordingId;
        {
            BreezeDesk::ApplicationViewModel first(&repository);
            QCOMPARE(first.importUrls({QUrl::fromLocalFile(media.fileName())}), 1);
            QCOMPARE(first.library()->recordings()->rowCount(), 1);
            const QModelIndex item = first.library()->recordings()->index(0, 0);
            recordingId =
                first.library()->recordings()->data(item, BreezeDesk::RecordingListModel::IdRole).toString();
            QVERIFY(!recordingId.isEmpty());
            first.library()->setTags(recordingId, {QStringLiteral("meeting")});
            first.openRecording(recordingId);
            first.recordingDetail()->setNotes(QStringLiteral("Decision log: ship the native worker."));
            QCOMPARE(first.library()->details(recordingId).value(QStringLiteral("notes")).toString(),
                     QStringLiteral("Decision log: ship the native worker."));
        }

        BreezeDesk::ApplicationViewModel second(&repository);
        QCOMPARE(second.library()->recordings()->rowCount(), 1);
        const QVariantMap details = second.library()->details(recordingId);
        QCOMPARE(details.value(QStringLiteral("tags")).toStringList(),
                 QStringList{QStringLiteral("meeting")});
        QCOMPARE(details.value(QStringLiteral("notes")).toString(),
                 QStringLiteral("Decision log: ship the native worker."));
        second.library()->moveToTrash(recordingId);
        QCOMPARE(second.library()->trash()->rowCount(), 1);
    }

    void managedMediaCopyPersistsAndPermanentDeleteIsScoped() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const EnvironmentVariableGuard dataRootGuard(QByteArrayLiteral("BREEZEDESK_DATA_ROOT"));
        const QString dataRoot = directory.filePath(QStringLiteral("application-data"));
        qputenv("BREEZEDESK_DATA_ROOT", dataRoot.toUtf8());
        QVERIFY(BreezeDesk::StoragePaths::ensureLayout());

        const QString externalDirectory = directory.filePath(QStringLiteral("external"));
        QVERIFY(QDir().mkpath(externalDirectory));
        const QString originalPath =
            QDir(externalDirectory).filePath(QStringLiteral("original-會議 音訊.m4a"));
        QFile original(originalPath);
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write("managed-copy-fixture"), qint64{20});
        original.close();

        BreezeDesk::DatabaseManager database(
            {directory.filePath(QStringLiteral("managed-media.sqlite3")), 5'000, true, false});
        QVERIFY(database.initialize());
        BreezeDesk::SqliteRecordingRepository repository(database);

        QString recordingId;
        QString managedPath;
        {
            BreezeDesk::ApplicationViewModel first(&repository);
            first.setManagedMediaCopyEnabled(true);
            QCOMPARE(first.importUrls({QUrl::fromLocalFile(originalPath)}), 1);
            QTRY_COMPARE(first.library()->recordings()->rowCount(), 1);

            const auto records = repository.list({});
            QVERIFY(records);
            QCOMPARE(records.value().items.size(), 1);
            const BreezeDesk::Recording stored = records.value().items.constFirst();
            recordingId = stored.id;
            managedPath = stored.managedMediaPath;
            QCOMPARE(stored.sourcePath, QFileInfo(originalPath).absoluteFilePath());
            QVERIFY(!managedPath.isEmpty());
            QVERIFY(QFileInfo(managedPath).isFile());
            QCOMPARE(QFileInfo(managedPath).absolutePath(),
                     QFileInfo(BreezeDesk::StoragePaths::recordings()).absoluteFilePath());

            QFile managed(managedPath);
            QVERIFY(managed.open(QIODevice::ReadOnly));
            QCOMPARE(managed.readAll(), QByteArrayLiteral("managed-copy-fixture"));
        }

        const QString normalizedPath =
            QDir(BreezeDesk::StoragePaths::cache()).filePath(QStringLiteral("normalized.pcm"));
        const QString waveformPath =
            QDir(BreezeDesk::StoragePaths::cache()).filePath(QStringLiteral("waveform.bwpk"));
        for (const QString& path : {normalizedPath, waveformPath}) {
            QFile artifact(path);
            QVERIFY(artifact.open(QIODevice::WriteOnly));
            QCOMPARE(artifact.write("artifact"), qint64{8});
        }
        auto persisted = repository.findById(recordingId);
        QVERIFY(persisted && persisted.value().has_value());
        BreezeDesk::Recording recording = persisted.value().value();
        recording.normalizedPcmPath = normalizedPath;
        recording.waveformPath = waveformPath;
        QVERIFY(repository.update(recording));

        BreezeDesk::ApplicationViewModel second(&repository);
        const QVariantMap details = second.library()->details(recordingId);
        QCOMPARE(details.value(QStringLiteral("sourcePath")).toString(),
                 QFileInfo(originalPath).absoluteFilePath());
        QCOMPARE(details.value(QStringLiteral("managedMediaPath")).toString(), managedPath);
        QCOMPARE(details.value(QStringLiteral("playbackPath")).toString(), managedPath);
        second.openRecording(recordingId);
        QCOMPARE(second.activeRecordingId(), recordingId);
        second.library()->moveToTrash(recordingId);
        second.library()->deletePermanently(recordingId);

        QVERIFY(second.activeRecordingId().isEmpty());
        QVERIFY(second.player()->source().isEmpty());
        QCOMPARE(second.currentPage(), QStringLiteral("Library"));
        QVERIFY(QFileInfo(originalPath).isFile());
        QVERIFY(!QFileInfo::exists(managedPath));
        QVERIFY(!QFileInfo::exists(normalizedPath));
        QVERIFY(!QFileInfo::exists(waveformPath));
        const auto removed = repository.findById(recordingId);
        QVERIFY(removed && !removed.value().has_value());

        const QString originalInsideRoot =
            QDir(BreezeDesk::StoragePaths::exports()).filePath(QStringLiteral("original.wav"));
        const QString externalManaged =
            QDir(externalDirectory).filePath(QStringLiteral("misconfigured-managed.wav"));
        const QString externalWaveform =
            QDir(externalDirectory).filePath(QStringLiteral("misconfigured-waveform.bwpk"));
        for (const QString& path : {originalInsideRoot, externalManaged, externalWaveform}) {
            QFile protectedFile(path);
            QVERIFY(protectedFile.open(QIODevice::WriteOnly));
            QCOMPARE(protectedFile.write("protected"), qint64{9});
        }
        BreezeDesk::Recording protectedRecording;
        protectedRecording.id = QStringLiteral("protected-recording");
        protectedRecording.title = QStringLiteral("Protected original");
        protectedRecording.sourcePath = originalInsideRoot;
        protectedRecording.managedMediaPath = externalManaged;
        protectedRecording.normalizedPcmPath = originalInsideRoot;
        protectedRecording.waveformPath = externalWaveform;
        QVERIFY(repository.create(protectedRecording));
        second.library()->refresh();
        second.library()->moveToTrash(protectedRecording.id);
        second.library()->deletePermanently(protectedRecording.id);
        QVERIFY(QFileInfo(originalInsideRoot).isFile());
        QVERIFY(QFileInfo(externalManaged).isFile());
        QVERIFY(QFileInfo(externalWaveform).isFile());

        const QString microphoneRecording =
            QDir(BreezeDesk::StoragePaths::recordings()).filePath(QStringLiteral("recorded.wav"));
        QFile microphoneFile(microphoneRecording);
        QVERIFY(microphoneFile.open(QIODevice::WriteOnly));
        QCOMPARE(microphoneFile.write("recording"), qint64{9});
        microphoneFile.close();
        QCOMPARE(second.importUrls({QUrl::fromLocalFile(microphoneRecording)}), 1);
        const auto microphoneRecord = repository.findBySourcePath(microphoneRecording);
        QVERIFY(microphoneRecord && microphoneRecord.value().has_value());
        QCOMPARE(microphoneRecord.value()->managedMediaPath, microphoneRecording);
        second.library()->moveToTrash(microphoneRecord.value()->id);
        second.library()->deletePermanently(microphoneRecord.value()->id);
        QVERIFY(!QFileInfo::exists(microphoneRecording));
    }

    void waveformCacheLoadsWithoutBlockingRecordingOpen() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString pcmPath = directory.filePath(QStringLiteral("fixture.pcm"));
        QFile pcm(pcmPath);
        QVERIFY(pcm.open(QIODevice::WriteOnly));
        QDataStream samples(&pcm);
        samples.setByteOrder(QDataStream::LittleEndian);
        for (int index = 0; index < 4096; ++index) {
            const qint16 sample =
                (index % 512) < 256 ? static_cast<qint16>(12'000) : static_cast<qint16>(-12'000);
            samples << sample;
        }
        pcm.close();

        const QString waveformPath = directory.filePath(QStringLiteral("fixture.bwpk"));
        std::atomic_bool cancelled{false};
        QString waveformError;
        QVERIFY2(BreezeDesk::WaveformGenerator::generate(pcmPath, waveformPath, &cancelled, &waveformError),
                 qPrintable(waveformError));

        BreezeDesk::DatabaseManager database(
            {directory.filePath(QStringLiteral("waveform.sqlite3")), 5'000, true, false});
        QVERIFY(database.initialize());
        BreezeDesk::SqliteRecordingRepository repository(database);
        BreezeDesk::Recording recording;
        recording.id = QStringLiteral("recording-with-waveform");
        recording.title = QStringLiteral("Waveform fixture");
        recording.sourcePath = pcmPath;
        recording.waveformPath = waveformPath;
        QVERIFY(repository.create(recording));

        BreezeDesk::ApplicationViewModel vm(&repository);
        vm.openRecording(recording.id);
        QCOMPARE(vm.activeRecordingId(), recording.id);
        QTRY_VERIFY_WITH_TIMEOUT(!vm.player()->waveformPeaks().isEmpty(), 3'000);
        QVERIFY(vm.player()->waveformPeaks().constFirst().toReal() > 0.3);
    }

    void failedTranscriptSaveRemainsDirtyUntilRepositorySucceeds() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        BreezeDesk::DatabaseManager database(
            {directory.filePath(QStringLiteral("transcript-state.sqlite3")), 5'000, true, false});
        QVERIFY(database.initialize());
        BreezeDesk::SqliteRecordingRepository recordingRepository(database);
        BreezeDesk::Recording recording;
        recording.id = QStringLiteral("transcript-recording");
        recording.title = QStringLiteral("Transcript fixture");
        recording.sourcePath = directory.filePath(QStringLiteral("fixture.wav"));
        recording.activeJobId = QStringLiteral("fixture-job");
        QVERIFY(recordingRepository.create(recording));

        FakeTranscriptRepository transcriptRepository;
        BreezeDesk::TranscriptSegment segment;
        segment.id = QStringLiteral("segment-1");
        segment.recordingId = recording.id;
        segment.jobId = recording.activeJobId;
        segment.startMs = 0;
        segment.endMs = 1'000;
        segment.originalText = QStringLiteral("Original text");
        transcriptRepository.m_segments = {segment};

        BreezeDesk::ApplicationViewModel vm(&recordingRepository, &transcriptRepository);
        vm.openRecording(recording.id);
        QCOMPARE(vm.transcript()->segmentCount(), 1);
        vm.transcript()->editText(0, QStringLiteral("Edited text"));
        QVERIFY(vm.transcript()->dirty());

        transcriptRepository.failWrites = true;
        vm.transcript()->save();
        QCOMPARE(transcriptRepository.saveAttempts, 1);
        QVERIFY(vm.transcript()->dirty());
        QCOMPARE(vm.toastMessage(), QStringLiteral("Fixture save failed."));

        transcriptRepository.failWrites = false;
        vm.transcript()->save();
        QCOMPARE(transcriptRepository.saveAttempts, 2);
        QVERIFY(!vm.transcript()->dirty());
        QCOMPARE(transcriptRepository.m_segments.constFirst().editedText, QStringLiteral("Edited text"));
    }

    void liveTranscriptSwitchesRevisionAndLocksEditing() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        BreezeDesk::DatabaseManager database(
            {directory.filePath(QStringLiteral("live-transcript.sqlite3")), 5'000, true, false});
        QVERIFY(database.initialize());
        BreezeDesk::SqliteRecordingRepository recordingRepository(database);
        BreezeDesk::Recording recording;
        recording.id = QStringLiteral("live-recording");
        recording.title = QStringLiteral("Live transcript fixture");
        recording.sourcePath = directory.filePath(QStringLiteral("fixture.wav"));
        recording.activeJobId = QStringLiteral("previous-job");
        QVERIFY(recordingRepository.create(recording));

        FakeTranscriptRepository transcriptRepository;
        BreezeDesk::TranscriptSegment previous;
        previous.id = QStringLiteral("previous-segment");
        previous.recordingId = recording.id;
        previous.jobId = recording.activeJobId;
        previous.startMs = 0;
        previous.endMs = 1'000;
        previous.originalText = QStringLiteral("Previous revision");
        transcriptRepository.m_segments = {previous};

        BreezeDesk::ApplicationViewModel vm(&recordingRepository, &transcriptRepository);
        vm.openRecording(recording.id);
        QVERIFY(!vm.transcript()->editingLocked());
        QCOMPARE(vm.transcript()->fullText(), QStringLiteral("Previous revision"));

        BreezeDesk::TranscriptSegment partial = previous;
        partial.id = QStringLiteral("partial-segment");
        partial.jobId = QStringLiteral("live-job");
        partial.originalText = QStringLiteral("Live partial result");
        partial.provisional = true;
        transcriptRepository.m_segments = {partial};
        vm.reloadTranscriptForJob(recording.id, partial.jobId, true);
        QVERIFY(vm.transcript()->editingLocked());
        QCOMPARE(vm.transcript()->fullText(), QStringLiteral("Live partial result"));

        vm.transcript()->editText(0, QStringLiteral("Edit while running"));
        QVERIFY(!vm.transcript()->dirty());
        QCOMPARE(vm.transcript()->fullText(), QStringLiteral("Live partial result"));

        partial.originalText = QStringLiteral("Final result");
        partial.provisional = false;
        transcriptRepository.m_segments = {partial};
        vm.reloadTranscriptForJob(recording.id, partial.jobId, false);
        QVERIFY(!vm.transcript()->editingLocked());
        QCOMPARE(vm.transcript()->fullText(), QStringLiteral("Final result"));
        vm.transcript()->editText(0, QStringLiteral("Reviewed final result"));
        QVERIFY(vm.transcript()->dirty());
    }

    void diagnosticsUsesCentralizedStoragePaths() {
        BreezeDesk::DiagnosticsViewModel diagnostics;
        QCOMPARE(diagnostics.databasePath(), BreezeDesk::StoragePaths::database());
        QCOMPARE(diagnostics.modelPath(), BreezeDesk::StoragePaths::models());
        QCOMPARE(diagnostics.cachePath(), BreezeDesk::StoragePaths::cache());
        QCOMPARE(diagnostics.logPath(), BreezeDesk::StoragePaths::logs());
    }

    void settingsStateSurvivesViewModelRecreation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));
        const QString dataPath = directory.filePath(QStringLiteral("data"));
        const QString exportPath = directory.filePath(QStringLiteral("exports"));

        {
            BreezeDesk::SettingsStore store(settingsPath);
            BreezeDesk::GeneralSettingsManager general(store);
            BreezeDesk::AppearanceSettingsManager appearance(store);
            BreezeDesk::TranscriptionSettingsManager transcription(store);
            BreezeDesk::AudioSettingsManager audio(store);
            BreezeDesk::ModelSettingsManager models(store);
            BreezeDesk::StorageSettingsManager storage(store);
            BreezeDesk::UpdateSettingsManager updates(store);
            BreezeDesk::SettingsViewModel first;
            first.installManagers(
                {&general, &appearance, &transcription, &audio, &models, &storage, &updates});
            first.setLanguage(QStringLiteral("en"));
            first.setTheme(QStringLiteral("Dark"));
            first.setCloseBehavior(QStringLiteral("Quit"));
            first.setLaunchAtStartup(true);
            first.setImportBehavior(QStringLiteral("CopyManaged"));
            first.setTextScale(1.3);
            first.setCompactMode(true);
            first.setWaveformDensity(QStringLiteral("Dense"));
            first.setDefaultModel(QStringLiteral("breeze-asr-25-q8"));
            first.setPreset(QStringLiteral("Accurate"));
            first.setBackend(QStringLiteral("CPU"));
            first.setMicrophoneDevice(QStringLiteral("microphone-id"));
            first.setStoragePath(dataPath);
            first.setExportPath(exportPath);
            first.setAutomaticUpdates(true);
            first.setUpdateChannel(QStringLiteral("Beta"));
        }

        BreezeDesk::SettingsStore store(settingsPath);
        BreezeDesk::GeneralSettingsManager general(store);
        BreezeDesk::AppearanceSettingsManager appearance(store);
        BreezeDesk::TranscriptionSettingsManager transcription(store);
        BreezeDesk::AudioSettingsManager audio(store);
        BreezeDesk::ModelSettingsManager models(store);
        BreezeDesk::StorageSettingsManager storage(store);
        BreezeDesk::UpdateSettingsManager updates(store);
        BreezeDesk::SettingsViewModel second;
        second.installManagers({&general, &appearance, &transcription, &audio, &models, &storage, &updates});
        QCOMPARE(second.language(), QStringLiteral("en"));
        QCOMPARE(second.theme(), QStringLiteral("Dark"));
        QCOMPARE(second.closeBehavior(), QStringLiteral("Quit"));
        QVERIFY(second.launchAtStartup());
        QCOMPARE(second.importBehavior(), QStringLiteral("CopyManaged"));
        QCOMPARE(second.textScale(), 1.3);
        QVERIFY(second.compactMode());
        QCOMPARE(second.waveformDensity(), QStringLiteral("Dense"));
        QCOMPARE(second.defaultModel(), QStringLiteral("breeze-asr-25-q8"));
        QCOMPARE(second.preset(), QStringLiteral("Accurate"));
        QCOMPARE(second.backend(), QStringLiteral("CPU"));
        QCOMPARE(second.microphoneDevice(), QStringLiteral("microphone-id"));
        QCOMPARE(second.storagePath(), dataPath);
        QCOMPARE(second.exportPath(), exportPath);
        QVERIFY(second.automaticUpdates());
        QCOMPARE(second.updateChannel(), QStringLiteral("Beta"));
    }

    void modelDefaultSurvivesServiceRecreation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString settingsPath = directory.filePath(QStringLiteral("models.ini"));
        {
            BreezeDesk::SettingsStore store(settingsPath);
            BreezeDesk::ModelSettingsManager settings(store);
            BreezeDesk::ModelManager manager;
            BreezeDesk::ModelManagerViewModel first;
            first.installServices(&manager, &settings);
            first.setDefaultModel(QStringLiteral("breeze-asr-25-q8"));
            QCOMPARE(manager.defaultModelId(), QStringLiteral("breeze-asr-25-q8"));
        }

        BreezeDesk::SettingsStore store(settingsPath);
        BreezeDesk::ModelSettingsManager settings(store);
        BreezeDesk::ModelManager manager;
        BreezeDesk::ModelManagerViewModel second;
        second.installServices(&manager, &settings);
        QCOMPARE(second.defaultModelId(), QStringLiteral("breeze-asr-25-q8"));
        QCOMPARE(manager.defaultModelId(), QStringLiteral("breeze-asr-25-q8"));
    }

    void glossaryStateSurvivesViewModelRecreation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        BreezeDesk::DatabaseManager database(
            {directory.filePath(QStringLiteral("glossary.sqlite3")), 5'000, true, false});
        QVERIFY(database.initialize());
        BreezeDesk::SqliteGlossaryRepository repository(database);

        QString profileId;
        QString termId;
        {
            BreezeDesk::GlossaryViewModel first;
            first.installRepository(&repository);
            profileId = first.createProfile(QStringLiteral("Product"), QStringLiteral("Names"),
                                            QStringLiteral("Engineering meeting"));
            QVERIFY(!profileId.isEmpty());
            termId = first.addTerm(QStringLiteral("BreezeDesk"), {QStringLiteral("Breeze Desk")}, 90);
            QVERIFY(!termId.isEmpty());
        }

        BreezeDesk::GlossaryViewModel second;
        second.installRepository(&repository);
        QCOMPARE(second.selectedProfileId(), profileId);
        QCOMPARE(second.profiles()->rowCount(), 1);
        QCOMPARE(second.terms()->rowCount(), 1);
        const QModelIndex term = second.terms()->index(0, 0);
        QCOMPARE(second.terms()->data(term, BreezeDesk::GlossaryTermListModel::CanonicalTextRole).toString(),
                 QStringLiteral("BreezeDesk"));
        second.setTermEnabled(termId, false);
        const auto storedTerms = repository.terms(profileId);
        QVERIFY(storedTerms);
        QCOMPARE(storedTerms.value().constFirst().enabled, false);
    }

    void ordinaryComponentsDoNotUsePrimitiveTokens() {
        const QString qmlRoot = QStringLiteral(BREEZEDESK_SOURCE_DIR "/src/qml");
        const QStringList guardedDirectories{QStringLiteral("components"), QStringLiteral("controls"),
                                             QStringLiteral("dialogs"), QStringLiteral("pages")};
        QStringList violations;
        for (const QString& directory : guardedDirectories) {
            QDirIterator iterator(qmlRoot + QLatin1Char('/') + directory, {QStringLiteral("*.qml")},
                                  QDir::Files, QDirIterator::Subdirectories);
            while (iterator.hasNext()) {
                const QString fileName = iterator.next();
                QFile file(fileName);
                QVERIFY(file.open(QIODevice::ReadOnly));
                if (QString::fromUtf8(file.readAll()).contains(QStringLiteral("PrimitiveTokens"))) {
                    violations.append(fileName);
                }
            }
        }
        QVERIFY2(violations.isEmpty(), qPrintable(violations.join(QLatin1Char('\n'))));
    }
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    tst_QmlSmoke test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_QmlSmoke.moc"
