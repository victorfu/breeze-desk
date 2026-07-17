#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/platform/IPlatformService.h"
#include "breezedesk/ui/ApplicationViewModel.h"
#include "breezedesk/ui/LibraryViewModel.h"

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <utility>

namespace {

class EnvironmentVariableGuard final {
  public:
    explicit EnvironmentVariableGuard(QByteArray name)
        : m_name(std::move(name)), m_wasSet(qEnvironmentVariableIsSet(m_name.constData())),
          m_value(qgetenv(m_name.constData())) {}
    ~EnvironmentVariableGuard() {
        if (m_wasSet) {
            qputenv(m_name.constData(), m_value);
        } else {
            qunsetenv(m_name.constData());
        }
    }

  private:
    QByteArray m_name;
    bool m_wasSet{false};
    QByteArray m_value;
};

void createFile(const QString& path) {
    QVERIFY2(QDir().mkpath(QFileInfo(path).absolutePath()), qPrintable(path));
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write("media"), qint64{5});
}

class FakePlatformService final : public BreezeDesk::IPlatformService {
  public:
    [[nodiscard]] BreezeDesk::PlatformCapabilities capabilities() const override { return {}; }
    [[nodiscard]] bool revealInFileManager(const QString& path, QString*) const override {
        revealedPath = path;
        return revealSucceeds;
    }
    [[nodiscard]] bool setLaunchAtLogin(bool, QString*) override { return false; }
    [[nodiscard]] bool launchAtLogin(QString*) const override { return false; }
    [[nodiscard]] bool requestMicrophonePermission(QString*) override { return false; }
    [[nodiscard]] QString installSource() const override { return QStringLiteral("test"); }
    [[nodiscard]] QString gpuDescription() const override { return QStringLiteral("test"); }
    void activateApplication() override {}

    mutable QString revealedPath;
    bool revealSucceeds{true};
};

QString roleString(QAbstractItemModel* model, const int row, const int role) {
    return model->data(model->index(row, 0), role).toString();
}

} // namespace

class LibraryWorkflowsTest final : public QObject {
    Q_OBJECT

  private slots:
    void tenThousandRecordingsRemainAccessibleThroughVirtualizedModel() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        BreezeDesk::DatabaseManager database(
            {directory.filePath(QStringLiteral("large-library.sqlite3")), 5'000, true, false});
        QVERIFY(database.initialize());
        const auto connection = database.connection();
        QVERIFY(connection);
        QSqlDatabase sql = connection.value();
        QVERIFY(sql.transaction());
        QSqlQuery insert(sql);
        insert.prepare(
            QStringLiteral("INSERT INTO recordings(id,title,created_at,updated_at) VALUES(?,?,?,?)"));
        const QString timestamp = QStringLiteral("2026-01-01T00:00:00.000Z");
        for (int index = 0; index < 10'000; ++index) {
            const QString id = QStringLiteral("recording-%1").arg(index, 5, 10, QLatin1Char('0'));
            insert.bindValue(0, id);
            insert.bindValue(1, QStringLiteral("Recording %1").arg(index));
            insert.bindValue(2, timestamp);
            insert.bindValue(3, timestamp);
            QVERIFY2(insert.exec(), qPrintable(insert.lastError().text()));
        }
        QVERIFY(sql.commit());

        BreezeDesk::SqliteRecordingRepository repository(database);
        BreezeDesk::LibraryViewModel library(&repository);
        QCOMPARE(library.recordings()->rowCount(), 10'000);
        QCOMPARE(roleString(library.recordings(), 9'999, BreezeDesk::RecordingListModel::IdRole),
                 QStringLiteral("recording-09999"));

        BreezeDesk::RecordingQuery finalPage;
        finalPage.offset = 9'500;
        finalPage.limit = 1'000;
        finalPage.sortColumn = QStringLiteral("updated_at");
        finalPage.sortOrder = Qt::DescendingOrder;
        const auto page = repository.list(finalPage);
        QVERIFY(page);
        QCOMPARE(page.value().totalCount, 10'000);
        QCOMPARE(page.value().items.size(), 500);
        QCOMPARE(page.value().items.constLast().id, QStringLiteral("recording-09999"));
    }

    void tagsReviewAndTranscriptSearchArePersistent() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        BreezeDesk::DatabaseManager database(
            {directory.filePath(QStringLiteral("metadata-library.sqlite3")), 5'000, true, false});
        QVERIFY(database.initialize());
        BreezeDesk::SqliteRecordingRepository repository(database);

        BreezeDesk::Recording first;
        first.id = QStringLiteral("first");
        first.title = QStringLiteral("Planning Session");
        first.createdAt = QDateTime::currentDateTimeUtc().addSecs(-1);
        QVERIFY(repository.create(first));
        BreezeDesk::Recording second;
        second.id = QStringLiteral("second");
        second.title = QStringLiteral("Other Session");
        second.createdAt = QDateTime::currentDateTimeUtc();
        QVERIFY(repository.create(second));

        const auto connection = database.connection();
        QVERIFY(connection);
        QSqlQuery job(connection.value());
        job.prepare(QStringLiteral(
            "INSERT INTO transcription_jobs(id,recording_id,state,stage,progress,model_id,created_at) "
            "VALUES(?,?,?,?,?,?,?)"));
        job.addBindValue(QStringLiteral("job-first"));
        job.addBindValue(first.id);
        job.addBindValue(QStringLiteral("Transcribing"));
        job.addBindValue(QStringLiteral("Transcribing"));
        job.addBindValue(0.42);
        job.addBindValue(QStringLiteral("breeze-q5"));
        job.addBindValue(QStringLiteral("2026-01-02T00:00:00.000Z"));
        QVERIFY2(job.exec(), qPrintable(job.lastError().text()));

        QSqlQuery segment(connection.value());
        segment.prepare(
            QStringLiteral("INSERT INTO transcript_segments(id,recording_id,job_id,ordinal,start_ms,end_ms,"
                           "original_text,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?)"));
        segment.addBindValue(QStringLiteral("segment-first"));
        segment.addBindValue(first.id);
        segment.addBindValue(QStringLiteral("job-first"));
        segment.addBindValue(0);
        segment.addBindValue(0);
        segment.addBindValue(1'000);
        segment.addBindValue(QStringLiteral("unique transcript phrase"));
        segment.addBindValue(QStringLiteral("2026-01-02T00:00:00.000Z"));
        segment.addBindValue(QStringLiteral("2026-01-02T00:00:00.000Z"));
        QVERIFY2(segment.exec(), qPrintable(segment.lastError().text()));

        BreezeDesk::LibraryViewModel library(&repository);
        library.setTagsText(first.id, QStringLiteral(" Roadmap; customer, ROADMAP "));
        QCOMPARE(library.details(first.id).value(QStringLiteral("tags")).toStringList(),
                 QStringList({QStringLiteral("Roadmap"), QStringLiteral("customer")}));

        library.setSearchText(QStringLiteral("roadmap"));
        QTRY_COMPARE_WITH_TIMEOUT(library.recordings()->rowCount(), 1, 2'000);
        QCOMPARE(roleString(library.recordings(), 0, BreezeDesk::RecordingListModel::IdRole), first.id);
        QCOMPARE(roleString(library.recordings(), 0, BreezeDesk::RecordingListModel::StatusRole),
                 QStringLiteral("Transcribing"));
        QCOMPARE(roleString(library.recordings(), 0, BreezeDesk::RecordingListModel::ModelRole),
                 QStringLiteral("breeze-q5"));
        QCOMPARE(library.recordings()
                     ->data(library.recordings()->index(0, 0), BreezeDesk::RecordingListModel::ProgressRole)
                     .toDouble(),
                 0.42);

        library.setReviewState(first.id, true);
        const auto persisted = repository.findById(first.id);
        QVERIFY(persisted && persisted.value().has_value());
        QCOMPARE(persisted.value()->reviewState, QStringLiteral("reviewed"));
        library.setReviewFilter(QStringLiteral("Reviewed"));
        QCOMPARE(library.recordings()->rowCount(), 1);
        library.setReviewFilter(QStringLiteral("Unreviewed"));
        QCOMPARE(library.recordings()->rowCount(), 0);

        BreezeDesk::RecordingQuery tagQuery;
        tagQuery.searchText = QStringLiteral("customer");
        const auto byTag = repository.list(tagQuery);
        QVERIFY(byTag);
        QCOMPARE(byTag.value().totalCount, 1);
        QCOMPARE(byTag.value().items.constFirst().id, first.id);

        BreezeDesk::RecordingQuery transcriptQuery;
        transcriptQuery.searchText = QStringLiteral("transcript phrase");
        const auto byTranscript = repository.list(transcriptQuery);
        QVERIFY(byTranscript);
        QCOMPARE(byTranscript.value().totalCount, 1);
        QCOMPARE(byTranscript.value().items.constFirst().id, first.id);
    }

    void recursiveFolderImportFiltersMediaAndPreservesUnicode() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString first = directory.filePath(QStringLiteral("會議/第一段.WAV"));
        const QString second = directory.filePath(QStringLiteral("巢狀/產品 demo.m4a"));
        createFile(first);
        createFile(second);
        createFile(directory.filePath(QStringLiteral("巢狀/notes.txt")));

        BreezeDesk::ApplicationViewModel viewModel;
        viewModel.importFolder(QUrl::fromLocalFile(directory.path()));
        QVERIFY(viewModel.folderImportRunning());
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.folderImportRunning(), 5'000);
        QCOMPARE(viewModel.folderImportTotal(), 2);
        QCOMPARE(viewModel.folderImportCompleted(), 2);
        QCOMPARE(viewModel.library()->recordings()->rowCount(), 2);

        QStringList paths;
        for (int row = 0; row < viewModel.library()->recordings()->rowCount(); ++row) {
            const QModelIndex index = viewModel.library()->recordings()->index(row, 0);
            paths.append(viewModel.library()
                             ->recordings()
                             ->data(index, BreezeDesk::RecordingListModel::SourceUrlRole)
                             .toUrl()
                             .toLocalFile());
        }
        QVERIFY(paths.contains(QFileInfo(first).absoluteFilePath()));
        QVERIFY(paths.contains(QFileInfo(second).absoluteFilePath()));
    }

    void folderImportCanBeCancelledBeforeResultsAreApplied() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        for (int index = 0; index < 40; ++index) {
            createFile(directory.filePath(QStringLiteral("nested/%1.wav").arg(index)));
        }

        BreezeDesk::ApplicationViewModel viewModel;
        viewModel.importFolder(QUrl::fromLocalFile(directory.path()));
        viewModel.cancelFolderImport();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.folderImportRunning(), 2'000);
        QCOMPARE(viewModel.library()->recordings()->rowCount(), 0);
    }

    void managedFolderImportCopiesAtomicallyBeforeCreatingRecords() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const EnvironmentVariableGuard dataRoot(QByteArrayLiteral("BREEZEDESK_DATA_ROOT"));
        qputenv("BREEZEDESK_DATA_ROOT", directory.filePath(QStringLiteral("application-data")).toUtf8());
        QVERIFY(BreezeDesk::StoragePaths::ensureLayout());

        const QString source = directory.filePath(QStringLiteral("來源/會議錄音.wav"));
        createFile(source);
        BreezeDesk::DatabaseManager database(
            {directory.filePath(QStringLiteral("managed-library.sqlite3")), 5'000, true, false});
        QVERIFY(database.initialize());
        BreezeDesk::SqliteRecordingRepository repository(database);
        BreezeDesk::ApplicationViewModel viewModel(&repository);
        viewModel.setManagedMediaCopyEnabled(true);

        viewModel.importFolder(QUrl::fromLocalFile(QFileInfo(source).absolutePath()));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.folderImportRunning(), 5'000);
        QCOMPARE(viewModel.folderImportCompleted(), 1);
        const auto recordings = repository.list({});
        QVERIFY(recordings);
        QCOMPARE(recordings.value().items.size(), 1);
        const BreezeDesk::Recording stored = recordings.value().items.constFirst();
        QCOMPARE(stored.sourcePath, QFileInfo(source).absoluteFilePath());
        QVERIFY(QFileInfo(stored.managedMediaPath).isFile());
        QCOMPARE(QFileInfo(stored.managedMediaPath).absolutePath(),
                 QFileInfo(BreezeDesk::StoragePaths::recordings()).absoluteFilePath());
        QFile managed(stored.managedMediaPath);
        QVERIFY(managed.open(QIODevice::ReadOnly));
        QCOMPARE(managed.readAll(), QByteArrayLiteral("media"));
    }

    void renameRelinkSortFilterAndRevealArePersistent() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        BreezeDesk::DatabaseManager database(
            {directory.filePath(QStringLiteral("library.sqlite3")), 5'000, true, false});
        QVERIFY(database.initialize());
        BreezeDesk::SqliteRecordingRepository repository(database);

        const QString alpha = directory.filePath(QStringLiteral("alpha.wav"));
        const QString beta = directory.filePath(QStringLiteral("beta.wav"));
        const QString replacement = directory.filePath(QStringLiteral("重新連結.m4a"));
        createFile(alpha);
        createFile(beta);
        createFile(replacement);

        BreezeDesk::ApplicationViewModel viewModel(&repository);
        QCOMPARE(viewModel.importUrls({QUrl::fromLocalFile(beta), QUrl::fromLocalFile(alpha)}), 2);
        QCOMPARE(viewModel.library()->recordings()->rowCount(), 2);

        QString alphaId;
        QString betaId;
        for (int row = 0; row < 2; ++row) {
            const QString id =
                roleString(viewModel.library()->recordings(), row, BreezeDesk::RecordingListModel::IdRole);
            const QString title =
                roleString(viewModel.library()->recordings(), row, BreezeDesk::RecordingListModel::TitleRole);
            if (title == QLatin1String("alpha")) {
                alphaId = id;
            } else if (title == QLatin1String("beta")) {
                betaId = id;
            }
        }
        QVERIFY(!alphaId.isEmpty());
        QVERIFY(!betaId.isEmpty());

        viewModel.library()->rename(alphaId, QStringLiteral("alpha"));
        QCOMPARE(viewModel.library()->details(alphaId).value(QStringLiteral("title")).toString(),
                 QStringLiteral("alpha"));
        viewModel.library()->rename(betaId, QStringLiteral("alpha"));
        QCOMPARE(viewModel.library()->details(betaId).value(QStringLiteral("title")).toString(),
                 QStringLiteral("alpha (2)"));

        auto prepared = repository.findById(alphaId);
        QVERIFY(prepared && prepared.value().has_value());
        BreezeDesk::Recording preparedRecording = prepared.value().value();
        preparedRecording.sourceHash = QStringLiteral("stale-hash");
        preparedRecording.normalizedPcmPath = directory.filePath(QStringLiteral("stale.pcm"));
        preparedRecording.waveformPath = directory.filePath(QStringLiteral("stale.bwpk"));
        preparedRecording.durationMs = 1234;
        preparedRecording.sampleRate = 16'000;
        preparedRecording.channelCount = 1;
        QVERIFY(repository.update(preparedRecording));
        viewModel.library()->refresh();

        QVERIFY(QFile::remove(alpha));
        viewModel.library()->refresh();
        QVERIFY(viewModel.library()->details(alphaId).value(QStringLiteral("sourceMissing")).toBool());
        viewModel.library()->relinkSource(alphaId, QUrl::fromLocalFile(replacement));
        const auto persisted = repository.findById(alphaId);
        QVERIFY(persisted && persisted.value().has_value());
        QCOMPARE(persisted.value()->sourcePath, QFileInfo(replacement).absoluteFilePath());
        QVERIFY(persisted.value()->sourceHash.isEmpty());
        QVERIFY(persisted.value()->normalizedPcmPath.isEmpty());
        QVERIFY(persisted.value()->waveformPath.isEmpty());
        QCOMPARE(persisted.value()->durationMs, qint64{0});
        QCOMPARE(persisted.value()->sampleRate, 0);
        QCOMPARE(persisted.value()->channelCount, 0);
        QVERIFY(!viewModel.library()->details(alphaId).value(QStringLiteral("sourceMissing")).toBool());

        viewModel.library()->setSortMode(QStringLiteral("TitleAZ"));
        QCOMPARE(roleString(viewModel.library()->recordings(), 0, BreezeDesk::RecordingListModel::TitleRole),
                 QStringLiteral("alpha"));
        viewModel.library()->setSortMode(QStringLiteral("TitleZA"));
        QCOMPARE(roleString(viewModel.library()->recordings(), 0, BreezeDesk::RecordingListModel::TitleRole),
                 QStringLiteral("alpha (2)"));
        viewModel.library()->setReviewFilter(QStringLiteral("Reviewed"));
        QCOMPARE(viewModel.library()->recordings()->rowCount(), 0);
        viewModel.library()->setReviewFilter(QStringLiteral("Unreviewed"));
        QCOMPARE(viewModel.library()->recordings()->rowCount(), 2);

        FakePlatformService platform;
        viewModel.setPlatformService(&platform);
        viewModel.revealRecording(alphaId);
        QCOMPARE(platform.revealedPath, QFileInfo(replacement).absoluteFilePath());
    }
};

QTEST_GUILESS_MAIN(LibraryWorkflowsTest)

#include "tst_LibraryWorkflows.moc"
