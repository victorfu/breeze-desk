#include "breezedesk/audio/WaveformGenerator.h"
#include "breezedesk/core/StoragePaths.h"
#include "breezedesk/database/DatabaseManager.h"
#include "breezedesk/database/SqliteRecordingRepository.h"
#include "breezedesk/glossary/SqliteGlossaryRepository.h"
#include "breezedesk/jobs/SqliteJobRepository.h"
#include "breezedesk/models/ModelManager.h"
#include "breezedesk/settings/SettingsManagers.h"
#include "breezedesk/transcript/ITranscriptRepository.h"
#include "breezedesk/transcript/SqliteTranscriptRepository.h"
#include "breezedesk/ui/ApplicationViewModel.h"
#include "breezedesk/ui/BrandIcons.h"
#include "breezedesk/ui/GlossaryViewModel.h"
#include "breezedesk/ui/UiRegistration.h"

#include <QAccessible>
#include <QColor>
#include <QDataStream>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTranslator>
#include <QtTest>

#include <atomic>
#include <tuple>
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

QList<QQuickItem*> visualDescendantsNamed(QQuickItem* root, const QString& objectName) {
    QList<QQuickItem*> matches;
    const auto visit = [&matches, &objectName](auto&& self, QQuickItem* item) -> void {
        for (QQuickItem* child : item->childItems()) {
            if (child->objectName() == objectName) {
                matches.append(child);
            }
            self(self, child);
        }
    };
    visit(visit, root);
    return matches;
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

    void brandIconRendersAtNativeWindowsSizes() {
        const QList<QIcon> icons{BreezeDesk::brandIcon(), BreezeDesk::windowsTrayIcon()};
        const QList<QSize> expectedSizes = BreezeDesk::nativeBrandIconSizes();

#ifdef Q_OS_WIN
        const QList<QSize> embeddedSizes = icons.constFirst().availableSizes();
        for (const QSize& expectedSize : expectedSizes) {
            QVERIFY2(
                embeddedSizes.contains(expectedSize),
                qPrintable(
                    QStringLiteral("The Windows ICO is missing its %1 px frame.").arg(expectedSize.width())));
        }
#endif

        for (const QIcon& icon : icons) {
            QVERIFY(!icon.isNull());
            for (const QSize& expectedSize : expectedSizes) {
                const QImage image =
                    icon.pixmap(expectedSize).toImage().convertToFormat(QImage::Format_RGBA8888);
                QCOMPARE(image.size(), expectedSize);

                int opaquePixels = 0;
                int whitePixels = 0;
                int bluePixels = 0;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        const QColor pixel = image.pixelColor(x, y);
                        if (pixel.alpha() < 180) {
                            continue;
                        }
                        ++opaquePixels;
                        if (pixel.red() > 225 && pixel.green() > 225 && pixel.blue() > 225) {
                            ++whitePixels;
                        }
                        if (pixel.blue() > pixel.red() + 80 && pixel.blue() > pixel.green() + 60) {
                            ++bluePixels;
                        }
                    }
                }
                const int pixelCount = expectedSize.width() * expectedSize.height();
                QVERIFY(opaquePixels > pixelCount / 2);
                // The macOS app icon intentionally reserves a larger transparent safe area so its
                // App Switcher tile matches native icons. Keep enough white pixels to prove the mark
                // survives small-size rasterization without assuming the old full-canvas coverage.
                QVERIFY2(
                    whitePixels >= pixelCount / 64,
                    qPrintable(QStringLiteral("The %1 px icon retained only %2 white pixels; expected %3.")
                                   .arg(expectedSize.width())
                                   .arg(whitePixels)
                                   .arg(pixelCount / 64)));
                QVERIFY(bluePixels > pixelCount / 3);
            }
        }
    }

    void platformBrandAssetsArePackaged() {
        const auto loadImage = [](const QString& path, const QSize& expectedSize) {
            const QImage image(path);
            QVERIFY2(!image.isNull(), qPrintable(QStringLiteral("Could not load %1").arg(path)));
            QCOMPARE(image.size(), expectedSize);
        };

        loadImage(QStringLiteral(":/qt/qml/BreezeDesk/icons/breezedesk.png"), {1024, 1024});
        loadImage(QStringLiteral(":/qt/qml/BreezeDesk/icons/breezedesk-macos.png"), {1024, 1024});
        loadImage(QStringLiteral(":/qt/qml/BreezeDesk/icons/breezedesk-sidebar.png"), {512, 512});
        loadImage(QStringLiteral(":/qt/qml/BreezeDesk/icons/breezedesk-tray.png"), {256, 256});
        loadImage(QStringLiteral(":/qt/qml/BreezeDesk/icons/breezedesk-menubar-Template.png"), {18, 18});
        loadImage(QStringLiteral(":/qt/qml/BreezeDesk/icons/breezedesk-menubar-Template@2x.png"), {36, 36});

        const QIcon menuBarIcon = BreezeDesk::macMenuBarIcon();
        QVERIFY(!menuBarIcon.isNull());
        QVERIFY(menuBarIcon.isMask());
    }

    void macBrandIconKeepsAppSwitcherSafeArea() {
        const QImage source(QStringLiteral(":/qt/qml/BreezeDesk/icons/breezedesk-macos.png"));
        QVERIFY(!source.isNull());

        const auto alphaBounds = [](const QImage& image) {
            QRect bounds;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (image.pixelColor(x, y).alpha() > 10) {
                        bounds |= QRect(x, y, 1, 1);
                    }
                }
            }
            return bounds;
        };

        const QRect visibleBounds = alphaBounds(source);
        QVERIFY(visibleBounds.isValid());
        QVERIFY(visibleBounds.left() >= 85);
        QVERIFY(visibleBounds.top() >= 85);
        QVERIFY(source.width() - visibleBounds.right() - 1 >= 85);
        QVERIFY(source.height() - visibleBounds.bottom() - 1 >= 85);
        QVERIFY(qAbs(visibleBounds.width() - visibleBounds.height()) <= 2);

        const int cornerSampleY = visibleBounds.top() + 20;
        int firstVisibleX = -1;
        for (int x = visibleBounds.left(); x <= visibleBounds.right(); ++x) {
            if (source.pixelColor(x, cornerSampleY).alpha() > 10) {
                firstVisibleX = x;
                break;
            }
        }
        QVERIFY(firstVisibleX >= 0);
        QVERIFY(firstVisibleX - visibleBounds.left() >= 100);

#ifdef Q_OS_MACOS
        const QImage runtimeIcon = BreezeDesk::brandIcon().pixmap(QSize(1024, 1024)).toImage();
        QCOMPARE(runtimeIcon.size(), source.size());
        QCOMPARE(alphaBounds(runtimeIcon), visibleBounds);
#endif
    }

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
        QVERIFY(root->findChild<QObject*>(QStringLiteral("transcriptRevisionPicker")));
        QVERIFY(root->findChild<QObject*>(QStringLiteral("transcriptHistoryButton")));
        QVERIFY(root->findChild<QObject*>(QStringLiteral("deleteDirtyTranscriptRevisionWarning")));
        const auto failures = qmlMessages.filter(
            QRegularExpression(QStringLiteral("qrc:|ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void injectedApplicationViewModelLoadsAndTearsDownWithoutNullBindings() {
        QScopedPointer<BreezeDesk::ApplicationViewModel> injectedViewModel(
            BreezeDesk::createApplicationViewModel());
        FakeRecorder injectedRecorder;
        QObject injectedMaintenance;

        {
            QQmlApplicationEngine engine;
            engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
            engine.setInitialProperties(
                {{QStringLiteral("injectedApplicationViewModel"),
                  QVariant::fromValue(static_cast<QObject*>(injectedViewModel.data()))},
                 {QStringLiteral("injectedRecorder"),
                  QVariant::fromValue(static_cast<QObject*>(&injectedRecorder))},
                 {QStringLiteral("injectedMaintenance"), QVariant::fromValue(&injectedMaintenance)}});
            engine.loadFromModule(QStringLiteral("BreezeDesk"), QStringLiteral("Main"));
            QVERIFY2(!engine.rootObjects().isEmpty(), "Main.qml did not create a root object.");
            QObject* root = engine.rootObjects().constFirst();
            QCOMPARE(root->property("vm").value<QObject*>(), static_cast<QObject*>(injectedViewModel.data()));
            QCOMPARE(root->property("injectedRecorder").value<QObject*>(),
                     static_cast<QObject*>(&injectedRecorder));
            QCOMPARE(root->property("injectedMaintenance").value<QObject*>(), &injectedMaintenance);
            QCoreApplication::processEvents();
        }

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
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
        QCOMPARE(vm->settings()->theme(), QStringLiteral("Light"));
        QCOMPARE(root->property("color").value<QColor>(), QColor(QStringLiteral("#FAFAFB")));
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

    void dynamicPresentationTextRetranslates() {
        QTranslator translator;
        QVERIFY2(translator.load(QStringLiteral(":/i18n-test/breezedesk_zh_TW.qm")),
                 "The embedded Traditional Chinese catalog could not be loaded.");
        QVERIFY(QCoreApplication::installTranslator(&translator));

        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import BreezeDesk

            QtObject {
                property string importedStatus: UiText.recordingStatus("Imported")
                property string cancelledState: UiText.jobState("Cancelled")
                property string preparingStage: UiText.jobStage("Preparing")
                property string downloadingState: UiText.modelState("Downloading")
                property string builtInModelDescription:
                    UiText.modelDescription("breeze-asr-25-q5", "")
                property string technicalName: UiText.modelState("Metal")
                property string localizedDateTime:
                    UiText.shortDateTime(new Date(2026, 6, 17, 23, 13))
            }
        )",
                          QUrl(QStringLiteral("inline:DynamicPresentationTextTest.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 1'000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        QCOMPARE(root->property("importedStatus").toString(), QStringLiteral("已匯入"));
        QCOMPARE(root->property("cancelledState").toString(), QStringLiteral("已取消"));
        QCOMPARE(root->property("preparingStage").toString(), QStringLiteral("準備中"));
        QCOMPARE(root->property("downloadingState").toString(), QStringLiteral("下載中"));
        QCOMPARE(root->property("builtInModelDescription").toString(),
                 QStringLiteral("建議用於 Apple Silicon 與 8 GB 記憶體系統的離線模型。"));
        QCOMPARE(root->property("technicalName").toString(), QStringLiteral("Metal"));
        QVERIFY(!root->property("localizedDateTime").toString().contains(QStringLiteral("PM")));
        QVERIFY(!root->property("localizedDateTime").toString().contains(QStringLiteral("AM")));

        QVERIFY(QCoreApplication::removeTranslator(&translator));
        engine.retranslate();
        QCOMPARE(root->property("importedStatus").toString(), QStringLiteral("Imported"));
        QCOMPARE(root->property("cancelledState").toString(), QStringLiteral("Cancelled"));
        QCOMPARE(root->property("downloadingState").toString(), QStringLiteral("Downloading"));
        QVERIFY(root->property("localizedDateTime").toString().contains(QStringLiteral("PM")));
    }

    void modelDownloadActionIsUnavailableWhileDownloadIsBusy() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            import BreezeDesk

            ApplicationWindow {
                id: host
                width: 800
                height: 400
                visible: true
                property string fixtureState: "NotInstalled"

                ModelCard {
                    anchors.fill: parent
                    modelId: "fixture-model"
                    displayName: "Fixture model with a complete name that must wrap instead of using an ellipsis"
                    description: "Fixture description"
                    quantization: "Q5"
                    fileSize: 1000000000
                    licenseName: "Fixture license"
                    recommended: false
                    defaultCandidate: true
                    installed: false
                    loaded: false
                    isDefault: false
                    modelState: host.fixtureState
                    progress: 1.0
                    licenseUrl: ""
                    sourceUrl: ""
                }
            }
        )",
                          QUrl(QStringLiteral("inline:ModelDownloadBusyStateHost.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 1'000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString() + qmlMessages.join(QLatin1Char('\n'))));

        auto* downloadButton = root->findChild<QQuickItem*>(QStringLiteral("modelDownloadButton"));
        auto* downloadSpinner = root->findChild<QQuickItem*>(QStringLiteral("modelDownloadSpinner"));
        auto* displayName = root->findChild<QQuickItem*>(QStringLiteral("modelDisplayName"));
        QVERIFY(downloadButton);
        QVERIFY(downloadSpinner);
        QVERIFY(displayName);
        QCOMPARE(displayName->property("elide").toInt(), static_cast<int>(Qt::ElideNone));
        QVERIFY(downloadButton->property("visible").toBool());
        QVERIFY(downloadButton->property("enabled").toBool());
        QVERIFY(!downloadSpinner->property("visible").toBool());

        for (const QString& busyState :
             {QStringLiteral("Requested"), QStringLiteral("Downloading"), QStringLiteral("Verifying")}) {
            QVERIFY(root->setProperty("fixtureState", busyState));
            QCoreApplication::processEvents();
            QVERIFY2(!downloadButton->property("visible").toBool(),
                     qPrintable(QStringLiteral("Download action stayed visible in state %1").arg(busyState)));
            QVERIFY2(!downloadButton->property("enabled").toBool(),
                     qPrintable(QStringLiteral("Download action stayed enabled in state %1").arg(busyState)));
            QVERIFY(downloadSpinner->property("visible").toBool());
            QVERIFY(downloadSpinner->property("running").toBool());
        }

        for (const QString& availableState :
             {QStringLiteral("Paused"), QStringLiteral("Failed"), QStringLiteral("Cancelled")}) {
            QVERIFY(root->setProperty("fixtureState", availableState));
            QCoreApplication::processEvents();
            QVERIFY(downloadButton->property("visible").toBool());
            QVERIFY(downloadButton->property("enabled").toBool());
            QVERIFY(!downloadSpinner->property("visible").toBool());
            QCOMPARE(downloadButton->property("text").toString(), availableState == QLatin1String("Paused")
                                                                      ? QStringLiteral("Resume")
                                                                      : QStringLiteral("Download"));
        }
    }

    void primaryButtonIconUsesAccentForeground() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/Main.qml")));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* window = qobject_cast<QQuickWindow*>(root.data());
        auto* icon = root->findChild<QQuickItem*>(QStringLiteral("sidebarImportButtonIcon"));
        QVERIFY(window);
        QVERIFY(icon);
        QCOMPARE(icon->property("color").value<QColor>(), QColor(QStringLiteral("#FFFFFF")));

        window->show();
        QCoreApplication::processEvents();
        const QSharedPointer<QQuickItemGrabResult> grab = icon->grabToImage();
        QVERIFY(grab);
        QSignalSpy readySpy(grab.data(), &QQuickItemGrabResult::ready);
        if (grab->image().isNull()) {
            QVERIFY2(readySpy.wait(1'000), "Timed out while rendering the primary button icon.");
        }

        const QImage image = grab->image().convertToFormat(QImage::Format_RGBA8888);
        QVERIFY(!image.isNull());
        int lightPixelCount = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() > 96 && pixel.red() > 220 && pixel.green() > 220 && pixel.blue() > 220) {
                    ++lightPixelCount;
                }
            }
        }
        QVERIFY2(lightPixelCount > 4, "The primary button icon was not rendered with a light tint.");
    }

    void removalActionsUseOneAccessibleIcon() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            import BreezeDesk

            ApplicationWindow {
                width: 640
                height: 320
                visible: true
                property int openRequests: 0
                property real expectedCardPadding: ComponentTokens.cardPadding

                RemoveButton {
                    id: removeButton
                    objectName: "sharedRemoveButton"
                    accessibleName: "Remove fixture"
                }

                RecordingCard {
                    objectName: "fixtureRecordingCard"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    recordingId: "recording-id"
                    title: "Fixture recording"
                    durationMs: 1000
                    createdAt: new Date(0)
                    status: "Imported"
                    modelName: ""
                    tags: []
                    reviewState: "Unreviewed"
                    progress: 0
                    sourceMissing: false
                    onOpenRequested: function(id) {
                        if (id === "recording-id") {
                            openRequests += 1
                        }
                    }
                }
            }
        )",
                          QUrl(QStringLiteral("inline:RemovalActionsTestHost.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 1'000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString() + qmlMessages.join(QLatin1Char('\n'))));

        auto* removeButton = root->findChild<QQuickItem*>(QStringLiteral("sharedRemoveButton"));
        auto* recordingCard = root->findChild<QQuickItem*>(QStringLiteral("fixtureRecordingCard"));
        auto* recordingActionRow = root->findChild<QQuickItem*>(QStringLiteral("recordingActionRow"));
        auto* recordingActions = root->findChild<QQuickItem*>(QStringLiteral("recordingActionsButton"));
        auto* recordingTrash = root->findChild<QQuickItem*>(QStringLiteral("recordingTrashButton"));
        auto* openMenuItem = root->findChild<QObject*>(QStringLiteral("recordingOpenMenuItem"));
        QVERIFY(removeButton);
        QVERIFY(recordingCard);
        QVERIFY(recordingActionRow);
        QVERIFY(recordingActions);
        QVERIFY(recordingTrash);
        QVERIFY(openMenuItem);

        QCoreApplication::processEvents();
        QVERIFY2(recordingCard->height() <= 88.0,
                 "A library recording row should remain compact at the default text scale.");
        const QRectF actionRowRect = recordingActionRow->mapRectToItem(
            recordingCard, QRectF(0.0, 0.0, recordingActionRow->width(), recordingActionRow->height()));
        const qreal cardPadding = recordingCard->property("padding").toReal();
        QCOMPARE(cardPadding, root->property("expectedCardPadding").toReal());
        QVERIFY2(actionRowRect.top() <= cardPadding + 0.5,
                 "Recording action icons must be aligned to the card's top edge.");
        QVERIFY2(recordingCard->width() - actionRowRect.right() <= cardPadding + 0.5,
                 "Recording action icons must be aligned to the card's right edge.");
        const qreal actionsCenterY =
            recordingActions
                ->mapToItem(recordingActionRow,
                            QPointF(recordingActions->width() / 2.0, recordingActions->height() / 2.0))
                .y();
        const qreal trashCenterY =
            recordingTrash
                ->mapToItem(recordingActionRow,
                            QPointF(recordingTrash->width() / 2.0, recordingTrash->height() / 2.0))
                .y();
        QVERIFY2(qAbs(actionsCenterY - trashCenterY) <= 0.5,
                 "The recording Actions and Trash controls must share one horizontal row.");

        const QUrl expectedIcon(QStringLiteral("qrc:/qt/qml/BreezeDesk/icons/lucide/trash-2.svg"));
        const QUrl expectedActionsIcon(QStringLiteral("qrc:/qt/qml/BreezeDesk/icons/lucide/ellipsis.svg"));
        QCOMPARE(removeButton->property("iconSource").toUrl(), expectedIcon);
        QCOMPARE(recordingTrash->property("iconSource").toUrl(), expectedIcon);
        QCOMPARE(recordingActions->property("iconSource").toUrl(), expectedActionsIcon);
        QVERIFY(recordingActions->property("text").toString().isEmpty());
        QVERIFY(recordingActions->width() <= 40.0);
        QVERIFY(recordingTrash->width() <= 40.0);
        QCOMPARE(removeButton->property("iconColor").value<QColor>(), QColor(QStringLiteral("#C83D4B")));
        QCOMPARE(removeButton->property("accessibleName").toString(), QStringLiteral("Remove fixture"));
        QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(removeButton);
        QVERIFY(interface);
        QCOMPARE(interface->text(QAccessible::Name), QStringLiteral("Remove fixture"));
        QAccessibleInterface* actionsInterface = QAccessible::queryAccessibleInterface(recordingActions);
        QVERIFY(actionsInterface);
        QCOMPARE(actionsInterface->text(QAccessible::Name), QStringLiteral("Actions for Fixture recording"));

        QVERIFY(QMetaObject::invokeMethod(openMenuItem, "triggered", Qt::DirectConnection));
        QCOMPARE(root->property("openRequests").toInt(), 1);

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void settingsLayoutStaysWithinViewport() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/Main.qml")));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* window = qobject_cast<QQuickWindow*>(root.data());
        auto* vm = root->findChild<BreezeDesk::ApplicationViewModel*>();
        auto* settingsScroll = root->findChild<QQuickItem*>(QStringLiteral("settingsScroll"));
        auto* settingsViewport = root->findChild<QQuickItem*>(QStringLiteral("settingsViewport"));
        auto* settingsContent = root->findChild<QQuickItem*>(QStringLiteral("settingsContent"));
        QVERIFY(window);
        QVERIFY(vm);
        QVERIFY(settingsScroll);
        QVERIFY(settingsViewport);
        QVERIFY(settingsContent);

        vm->navigate(QStringLiteral("Settings"));

        const auto verifyWidth = [&](int width) {
            window->setWidth(width);
            window->setHeight(720);
            QCoreApplication::processEvents();

            QVERIFY(settingsScroll->width() > 0.0);
            QVERIFY(settingsViewport->width() > 0.0);
            QVERIFY(settingsContent->width() > 0.0);
            QVERIFY(settingsContent->width() <= 924.5);

            const qreal leftInset = settingsContent->x();
            const qreal rightInset =
                settingsViewport->width() - settingsContent->x() - settingsContent->width();
            QVERIFY(leftInset >= 23.5);
            QVERIFY(rightInset >= 23.5);
            QVERIFY(qAbs(leftInset - rightInset) <= 1.0);
        };

        verifyWidth(980);
        const qreal narrowContentWidth = settingsContent->width();
        verifyWidth(1280);
        QVERIFY(settingsContent->width() > narrowContentWidth);

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void mainShellKeepsSidebarAndPagesWithinViewport() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/Main.qml")));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* window = qobject_cast<QQuickWindow*>(root.data());
        auto* vm = root->findChild<BreezeDesk::ApplicationViewModel*>();
        auto* sidebar = root->findChild<QQuickItem*>(QStringLiteral("mainSidebar"));
        auto* brandRow = root->findChild<QQuickItem*>(QStringLiteral("sidebarBrandRow"));
        auto* brandLogo = root->findChild<QQuickItem*>(QStringLiteral("sidebarBrandLogo"));
        auto* brandText = root->findChild<QQuickItem*>(QStringLiteral("sidebarBrandText"));
        auto* pages = root->findChild<QQuickItem*>(QStringLiteral("pageStack"));
        QVERIFY(window);
        QVERIFY(vm);
        QVERIFY(sidebar);
        QVERIFY(brandRow);
        QVERIFY(brandLogo);
        QVERIFY(brandText);
        QVERIFY(pages);

        QCOMPARE(brandLogo->property("source").toUrl(),
                 QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/icons/breezedesk-sidebar.png")));
        QTRY_COMPARE_WITH_TIMEOUT(brandLogo->property("status").toInt(), 1, 1'000);
        QCOMPARE(brandLogo->width(), 32.0);
        QCOMPARE(brandLogo->height(), 32.0);

        vm->settings()->setTextScale(1.5);
        vm->settings()->setCompactMode(false);

        const auto verifyWidth = [&](int width, qreal expectedSidebarWidth) {
            window->setWidth(width);
            window->setHeight(720);
            QCoreApplication::processEvents();

            QCOMPARE(sidebar->x(), 0.0);
            QTRY_COMPARE_WITH_TIMEOUT(sidebar->width(), expectedSidebarWidth, 1'000);
            QCOMPARE(pages->x(), sidebar->width());
            QCOMPARE(pages->width(), window->width() - sidebar->width());
            QCOMPARE(pages->x() + pages->width(), window->width());
            QVERIFY(brandText->width() > 0.0);
            QVERIFY(brandText->x() >= 0.0);
            QVERIFY(brandText->x() + brandText->width() <= brandRow->width() + 0.5);
            QVERIFY(brandText->property("paintedWidth").toReal() <= brandText->width() + 0.5);
            QTRY_VERIFY_WITH_TIMEOUT(
                brandText->property("paintedHeight").toReal() <= brandText->height() + 0.5, 1'000);
        };

        verifyWidth(980, 216.0);
        verifyWidth(1280, 216.0);
        vm->settings()->setCompactMode(true);
        verifyWidth(980, 188.0);

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void minimumWidthTextScaleRoundTripKeepsLayoutResponsive() {
        int geometryChanges = 0;
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/Main.qml")));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* window = qobject_cast<QQuickWindow*>(root.data());
        auto* vm = root->findChild<BreezeDesk::ApplicationViewModel*>();
        auto* pages = root->findChild<QQuickItem*>(QStringLiteral("pageStack"));
        auto* settingsContent = root->findChild<QQuickItem*>(QStringLiteral("settingsContent"));
        QVERIFY(window);
        QVERIFY(vm);
        QVERIFY(pages);
        QVERIFY(settingsContent);

        vm->settings()->setCompactMode(false);
        window->setWidth(980);
        window->setHeight(720);
        vm->navigate(QStringLiteral("Settings"));
        QCoreApplication::processEvents();

        const auto items = root->findChildren<QQuickItem*>();
        for (QQuickItem* item : items) {
            const auto countChange = [&geometryChanges] { ++geometryChanges; };
            connect(item, &QQuickItem::widthChanged, root.data(), countChange);
            connect(item, &QQuickItem::heightChanged, root.data(), countChange);
            connect(item, &QQuickItem::implicitWidthChanged, root.data(), countChange);
            connect(item, &QQuickItem::implicitHeightChanged, root.data(), countChange);
        }

        const auto processTransition = [&geometryChanges] {
            geometryChanges = 0;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 250);
            return geometryChanges;
        };
        constexpr int maximumExpectedGeometryChanges = 5'000;

        vm->settings()->setTextScale(1.5);
        const int enlargedChanges = processTransition();
        QVERIFY2(
            enlargedChanges < maximumExpectedGeometryChanges,
            qPrintable(
                QStringLiteral("Text scale 1.5 did not settle (%1 geometry changes).").arg(enlargedChanges)));

        vm->settings()->setTextScale(1.0);
        const int restoredChanges = processTransition();
        QVERIFY2(
            restoredChanges < maximumExpectedGeometryChanges,
            qPrintable(
                QStringLiteral("Text scale 1.0 did not settle (%1 geometry changes).").arg(restoredChanges)));

        vm->navigate(QStringLiteral("Library"));
        const int navigationChanges = processTransition();
        QVERIFY2(navigationChanges < maximumExpectedGeometryChanges,
                 qPrintable(QStringLiteral("Library navigation did not settle (%1 geometry changes).")
                                .arg(navigationChanges)));
        QCOMPARE(vm->currentPage(), QStringLiteral("Library"));
        QVERIFY(settingsContent->width() > 0.0);
        QCOMPARE(pages->x() + pages->width(), window->width());

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void sidebarFooterStaysAlignedAndWithinViewport() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/Main.qml")));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* window = qobject_cast<QQuickWindow*>(root.data());
        auto* vm = root->findChild<BreezeDesk::ApplicationViewModel*>();
        auto* sidebar = root->findChild<QQuickItem*>(QStringLiteral("mainSidebar"));
        auto* navigation = root->findChild<QQuickItem*>(QStringLiteral("sidebarNavigation"));
        auto* footer = root->findChild<QQuickItem*>(QStringLiteral("sidebarFooter"));
        auto* importButton = root->findChild<QQuickItem*>(QStringLiteral("sidebarImportButton"));
        auto* recordButton = root->findChild<QQuickItem*>(QStringLiteral("sidebarRecordButton"));
        auto* settingsButton = root->findChild<QQuickItem*>(QStringLiteral("sidebarSettingsButton"));
        auto* importIcon = root->findChild<QQuickItem*>(QStringLiteral("sidebarImportButtonIcon"));
        auto* recordIcon = root->findChild<QQuickItem*>(QStringLiteral("sidebarRecordButtonIcon"));
        auto* settingsIcon = root->findChild<QQuickItem*>(QStringLiteral("sidebarSettingsButtonIcon"));
        QVERIFY(window);
        QVERIFY(vm);
        QVERIFY(sidebar);
        QVERIFY(navigation);
        QVERIFY(footer);
        QVERIFY(importButton);
        QVERIFY(recordButton);
        QVERIFY(settingsButton);
        QVERIFY(importIcon);
        QVERIFY(recordIcon);
        QVERIFY(settingsIcon);

        const auto childOrigin = [](QQuickItem* parent, QQuickItem* child) {
            return child->mapToItem(parent, QPointF{});
        };
        const auto labelForButton = [](QQuickItem* button) -> QQuickItem* {
            const QString expectedText = button->property("text").toString();
            for (QQuickItem* candidate : button->findChildren<QQuickItem*>()) {
                if (candidate->property("text").toString() == expectedText) {
                    return candidate;
                }
            }
            return nullptr;
        };
        const auto verifyInside = [&childOrigin](QQuickItem* parent, QQuickItem* child,
                                                 const QString& context) {
            const auto contained = [&childOrigin, parent, child] {
                const QPointF origin = childOrigin(parent, child);
                return origin.x() >= -0.5 && origin.y() >= -0.5 &&
                       origin.x() + child->width() <= parent->width() + 0.5 &&
                       origin.y() + child->height() <= parent->height() + 0.5;
            };
            const auto failureMessage = [&childOrigin, parent, child, &context] {
                const QPointF origin = childOrigin(parent, child);
                return QStringLiteral("%1 is outside its parent: x=%2 y=%3 w=%4 h=%5; parent w=%6 h=%7")
                    .arg(context)
                    .arg(origin.x())
                    .arg(origin.y())
                    .arg(child->width())
                    .arg(child->height())
                    .arg(parent->width())
                    .arg(parent->height());
            };
            QTRY_VERIFY2_WITH_TIMEOUT(contained(), qPrintable(failureMessage()), 1'000);
        };

        for (const QString& language : {QStringLiteral("en"), QStringLiteral("zh_TW")}) {
            vm->settings()->setLanguage(language);
            for (const bool compact : {false, true}) {
                vm->settings()->setCompactMode(compact);
                for (const double textScale : {1.0, 1.5}) {
                    vm->settings()->setTextScale(textScale);
                    for (const int height : {640, 820}) {
                        window->setWidth(980);
                        window->setHeight(height);
                        QCoreApplication::processEvents();

                        verifyInside(sidebar, navigation, QStringLiteral("navigation"));
                        verifyInside(sidebar, footer, QStringLiteral("footer"));
                        verifyInside(footer, importButton, QStringLiteral("import button"));
                        verifyInside(footer, recordButton, QStringLiteral("record button"));
                        verifyInside(footer, settingsButton, QStringLiteral("settings button"));

                        const QPointF navigationOrigin = childOrigin(sidebar, navigation);
                        const QPointF footerOrigin = childOrigin(sidebar, footer);
                        QVERIFY(navigationOrigin.y() + navigation->height() <= footerOrigin.y() + 0.5);

                        const QPointF importOrigin = childOrigin(footer, importButton);
                        const QPointF recordOrigin = childOrigin(footer, recordButton);
                        const QPointF settingsOrigin = childOrigin(footer, settingsButton);
                        QVERIFY(importOrigin.y() + importButton->height() <= recordOrigin.y() + 0.5);
                        QVERIFY(recordOrigin.y() + recordButton->height() <= settingsOrigin.y() + 0.5);
                        QCOMPARE(importButton->height(), recordButton->height());
                        QCOMPARE(importButton->height(), settingsButton->height());

                        const qreal importIconX = childOrigin(footer, importIcon).x();
                        const qreal recordIconX = childOrigin(footer, recordIcon).x();
                        const qreal settingsIconX = childOrigin(footer, settingsIcon).x();
                        QVERIFY(qAbs(importIconX - recordIconX) <= 0.5);
                        QVERIFY2(qAbs(importIconX - settingsIconX) <= 2.5,
                                 qPrintable(QStringLiteral("Sidebar icons are misaligned: import=%1, "
                                                           "record=%2, settings=%3")
                                                .arg(importIconX)
                                                .arg(recordIconX)
                                                .arg(settingsIconX)));

                        QList<qreal> labelPositions;
                        for (QQuickItem* button : {importButton, recordButton, settingsButton}) {
                            QQuickItem* label = labelForButton(button);
                            QVERIFY(label);
                            verifyInside(button, label, button->property("text").toString());
                            labelPositions.append(childOrigin(footer, label).x());
                            QTRY_VERIFY_WITH_TIMEOUT(
                                label->property("paintedWidth").toReal() <= label->width() + 0.5, 1'000);
                            QTRY_VERIFY_WITH_TIMEOUT(
                                label->property("paintedHeight").toReal() <= label->height() + 0.5, 1'000);
                        }
                        QCOMPARE(labelPositions.size(), 3);
                        QVERIFY(qAbs(labelPositions.at(0) - labelPositions.at(1)) <= 0.5);
                        QVERIFY(qAbs(labelPositions.at(0) - labelPositions.at(2)) <= 0.5);
                    }
                }
            }
        }

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void responsivePageHeadersStayWithinViewport() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/Main.qml")));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* window = qobject_cast<QQuickWindow*>(root.data());
        auto* vm = root->findChild<BreezeDesk::ApplicationViewModel*>();
        QVERIFY(window);
        QVERIFY(vm);

        vm->settings()->setLanguage(QStringLiteral("zh_TW"));
        vm->settings()->setTextScale(1.5);

        const auto verifyContained = [](QQuickItem* parent, QQuickItem* child, const QString& context) {
            QVERIFY(parent);
            QVERIFY(child);
            const auto isContained = [parent, child] {
                const QPointF origin = child->mapToItem(parent, QPointF{});
                return origin.x() >= -0.5 && origin.x() + child->width() <= parent->width() + 0.5;
            };
            const auto failureMessage = [parent, child, &context] {
                const QPointF origin = child->mapToItem(parent, QPointF{});
                return QStringLiteral("%1: action x=%2, width=%3, right=%4, page width=%5")
                    .arg(context)
                    .arg(origin.x())
                    .arg(child->width())
                    .arg(origin.x() + child->width())
                    .arg(parent->width());
            };
            QTRY_VERIFY2_WITH_TIMEOUT(isContained(), qPrintable(failureMessage()), 1'000);
            QVERIFY(child->width() > 0.0);
            QVERIFY(child->height() > 0.0);
        };

        const QList<std::tuple<QString, QString, QString>> pages{
            {QStringLiteral("Library"), QStringLiteral("libraryPage"),
             QStringLiteral("libraryHeaderActions")},
            {QStringLiteral("Queue"), QStringLiteral("queuePage"), QStringLiteral("queueHeaderActions")},
            {QStringLiteral("Models"), QStringLiteral("modelsPage"), QStringLiteral("modelsHeaderActions")},
            {QStringLiteral("Glossary"), QStringLiteral("glossaryPage"),
             QStringLiteral("glossaryHeaderActions")},
        };

        for (const int width : {980, 1280}) {
            window->setWidth(width);
            window->setHeight(720);
            for (const auto& [pageName, pageObjectName, actionsObjectName] : pages) {
                vm->navigate(pageName);
                QCoreApplication::processEvents();
                auto* page = root->findChild<QQuickItem*>(pageObjectName);
                auto* actions = root->findChild<QQuickItem*>(actionsObjectName);
                verifyContained(page, actions, QStringLiteral("%1 at %2 px").arg(pageName).arg(width));
            }
        }

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void glossaryProfileDialogIsAccessibleAndResponsive() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        BreezeDesk::GlossaryViewModel glossaryViewModel;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            import BreezeDesk

            ApplicationWindow {
                id: host
                required property var glossaryVm
                property bool compactMode: false
                property real scale: 1.0
                property bool darkMode: false
                width: 980
                height: 720
                visible: true

                function applyAppearance() {
                    DesignSystem.compact = compactMode
                    DesignSystem.textScale = scale
                    DesignSystem.theme = darkMode ? DesignSystem.Dark : DesignSystem.Light
                }

                Component.onCompleted: applyAppearance()
                onCompactModeChanged: applyAppearance()
                onScaleChanged: applyAppearance()
                onDarkModeChanged: applyAppearance()

                GlossaryPage {
                    anchors.fill: parent
                    vm: host.glossaryVm
                }
            }
        )",
                          QUrl(QStringLiteral("inline:GlossaryDialogTestHost.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 1'000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{QStringLiteral("glossaryVm"), QVariant::fromValue<QObject*>(&glossaryViewModel)}}));
        QVERIFY2(root, qPrintable(component.errorString() + qmlMessages.join(QLatin1Char('\n'))));

        auto* window = qobject_cast<QQuickWindow*>(root.data());
        auto* newProfileButton = root->findChild<QQuickItem*>(QStringLiteral("glossaryNewProfileButton"));
        QObject* dialog = root->findChild<QObject*>(QStringLiteral("glossaryProfileDialog"));
        auto* surface = root->findChild<QQuickItem*>(QStringLiteral("glossaryProfileDialogSurface"));
        auto* header = root->findChild<QQuickItem*>(QStringLiteral("glossaryProfileDialogHeader"));
        auto* content = root->findChild<QQuickItem*>(QStringLiteral("glossaryProfileDialogContent"));
        auto* footer = root->findChild<QQuickItem*>(QStringLiteral("glossaryProfileDialogFooter"));
        auto* nameField = root->findChild<QQuickItem*>(QStringLiteral("glossaryProfileNameField"));
        auto* descriptionField =
            root->findChild<QQuickItem*>(QStringLiteral("glossaryProfileDescriptionField"));
        auto* contextField = root->findChild<QQuickItem*>(QStringLiteral("glossaryProfileContextField"));
        auto* cancelButton = root->findChild<QQuickItem*>(QStringLiteral("glossaryProfileCancelButton"));
        auto* createButton = root->findChild<QQuickItem*>(QStringLiteral("glossaryProfileCreateButton"));
        QVERIFY(window);
        QVERIFY(newProfileButton);
        QVERIFY(dialog);
        QVERIFY(surface);
        QVERIFY(header);
        QVERIFY(content);
        QVERIFY(footer);
        QVERIFY(nameField);
        QVERIFY(descriptionField);
        QVERIFY(contextField);
        QVERIFY(cancelButton);
        QVERIFY(createButton);

        window->show();
        QCoreApplication::processEvents();
        QVERIFY2(QMetaObject::invokeMethod(newProfileButton, "clicked", Qt::DirectConnection),
                 "The New Profile action does not expose its click boundary.");
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(), 1'000);

        for (QObject* accessibleControl :
             {static_cast<QObject*>(nameField), static_cast<QObject*>(descriptionField),
              static_cast<QObject*>(contextField), static_cast<QObject*>(cancelButton),
              static_cast<QObject*>(createButton)}) {
            QVERIFY2(
                !accessibleControl->property("accessibleName").toString().trimmed().isEmpty(),
                qPrintable(accessibleControl->objectName() + QStringLiteral(" has no accessible name.")));
            QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(accessibleControl);
            QVERIFY2(interface, qPrintable(accessibleControl->objectName() +
                                           QStringLiteral(" has no accessibility interface.")));
            QVERIFY2(!interface->text(QAccessible::Name).trimmed().isEmpty(),
                     qPrintable(accessibleControl->objectName() +
                                QStringLiteral(" exposes an empty accessibility name.")));
        }

        QQmlComponent tokenProbe(&engine);
        tokenProbe.setData(R"(
            import QtQuick
            import BreezeDesk
            QtObject {
                property color surfaceRaised: SemanticTokens.surfaceRaised
            }
        )",
                           QUrl(QStringLiteral("inline:GlossaryDialogTokenProbe.qml")));
        QScopedPointer<QObject> tokens(tokenProbe.create());
        QVERIFY2(tokens, qPrintable(tokenProbe.errorString()));

        const auto verifyContained = [](QQuickItem* parent, QQuickItem* child, const QString& context) {
            const auto contained = [parent, child] {
                const QPointF origin = child->mapToItem(parent, QPointF{});
                return origin.x() >= -0.5 && origin.y() >= -0.5 &&
                       origin.x() + child->width() <= parent->width() + 0.5 &&
                       origin.y() + child->height() <= parent->height() + 0.5;
            };
            const auto failureMessage = [parent, child, &context] {
                const QPointF origin = child->mapToItem(parent, QPointF{});
                return QStringLiteral("%1 is outside its parent: x=%2 y=%3 w=%4 h=%5; parent w=%6 h=%7")
                    .arg(context)
                    .arg(origin.x())
                    .arg(origin.y())
                    .arg(child->width())
                    .arg(child->height())
                    .arg(parent->width())
                    .arg(parent->height());
            };
            QTRY_VERIFY2_WITH_TIMEOUT(contained(), qPrintable(failureMessage()), 1'000);
        };

        for (const bool darkMode : {false, true}) {
            root->setProperty("darkMode", darkMode);
            QCoreApplication::processEvents();
            QCOMPARE(surface->property("color").value<QColor>(),
                     tokens->property("surfaceRaised").value<QColor>());

            for (const bool compact : {false, true}) {
                root->setProperty("compactMode", compact);
                for (const double textScale : {1.0, 1.5}) {
                    root->setProperty("scale", textScale);
                    for (const int width : {560, 980}) {
                        window->setWidth(width);
                        window->setHeight(720);
                        QCoreApplication::processEvents();

                        QVERIFY(surface->width() > 0.0);
                        QVERIFY(surface->height() > 0.0);
                        verifyContained(window->contentItem(), surface,
                                        QStringLiteral("dialog surface at %1 px").arg(width));
                        verifyContained(surface, header, QStringLiteral("dialog header"));
                        verifyContained(surface, content, QStringLiteral("dialog content"));
                        verifyContained(surface, footer, QStringLiteral("dialog footer"));
                        verifyContained(content, nameField, QStringLiteral("profile name field"));
                        verifyContained(content, descriptionField,
                                        QStringLiteral("profile description field"));
                        verifyContained(content, contextField, QStringLiteral("profile context field"));
                        verifyContained(footer, cancelButton, QStringLiteral("dialog cancel button"));
                        verifyContained(footer, createButton, QStringLiteral("dialog create button"));

                        const QPointF headerOrigin = header->mapToItem(surface, QPointF{});
                        const QPointF contentOrigin = content->mapToItem(surface, QPointF{});
                        const QPointF footerOrigin = footer->mapToItem(surface, QPointF{});
                        QVERIFY(headerOrigin.y() + header->height() <= contentOrigin.y() + 0.5);
                        QVERIFY(contentOrigin.y() + content->height() <= footerOrigin.y() + 0.5);
                    }
                }
            }
        }

        QCOMPARE(createButton->property("enabled").toBool(), false);
        nameField->setProperty("text", QStringLiteral("Fixture profile"));
        QTRY_COMPARE_WITH_TIMEOUT(createButton->property("enabled").toBool(), true, 1'000);
        QVERIFY2(QMetaObject::invokeMethod(cancelButton, "clicked", Qt::DirectConnection),
                 "The Cancel action does not expose its click boundary.");
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 1'000);
        root->setProperty("darkMode", false);
        root->setProperty("compactMode", false);
        root->setProperty("scale", 1.0);
        QCoreApplication::processEvents();

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void recordingControlsStayWithinViewport() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/Main.qml")));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* window = qobject_cast<QQuickWindow*>(root.data());
        auto* vm = root->findChild<BreezeDesk::ApplicationViewModel*>();
        auto* page = root->findChild<QQuickItem*>(QStringLiteral("recordingPage"));
        auto* workspace = root->findChild<QQuickItem*>(QStringLiteral("recordingWorkspace"));
        auto* inspector = root->findChild<QQuickItem*>(QStringLiteral("recordingInspector"));
        auto* inspectorButton = root->findChild<QQuickItem*>(QStringLiteral("recordingInspectorButton"));
        auto* mainPane = root->findChild<QQuickItem*>(QStringLiteral("recordingMainPane"));
        auto* header = root->findChild<QQuickItem*>(QStringLiteral("recordingHeader"));
        auto* waveform = root->findChild<QQuickItem*>(QStringLiteral("recordingWaveformCard"));
        auto* transport = root->findChild<QQuickItem*>(QStringLiteral("recordingTransportCard"));
        auto* timeline = root->findChild<QQuickItem*>(QStringLiteral("recordingPlaybackTimeline"));
        auto* playPauseButton = root->findChild<QQuickItem*>(QStringLiteral("recordingPlayPauseButton"));
        auto* playPauseIcon = root->findChild<QQuickItem*>(QStringLiteral("recordingPlayPauseButtonIcon"));
        auto* positionSlider = root->findChild<QQuickItem*>(QStringLiteral("playbackPositionSlider"));
        auto* rateCombo = root->findChild<QQuickItem*>(QStringLiteral("playbackRateComboBox"));
        auto* options = root->findChild<QQuickItem*>(QStringLiteral("recordingTransportOptions"));
        auto* transcriptToolbar = root->findChild<QQuickItem*>(QStringLiteral("recordingTranscriptToolbar"));
        QVERIFY(window);
        QVERIFY(vm);
        QVERIFY(page);
        QVERIFY(workspace);
        QVERIFY(inspector);
        QVERIFY(inspectorButton);
        QVERIFY(mainPane);
        QVERIFY(header);
        QVERIFY(waveform);
        QVERIFY(transport);
        QVERIFY(timeline);
        QVERIFY(playPauseButton);
        QVERIFY(playPauseIcon);
        QVERIFY(positionSlider);
        QVERIFY(rateCombo);
        QVERIFY(options);
        QVERIFY(transcriptToolbar);

        auto* rateIndicator = qobject_cast<QQuickItem*>(rateCombo->property("indicator").value<QObject*>());
        QVERIFY(rateIndicator);

        vm->settings()->setCompactMode(true);
        vm->settings()->setCompactMode(false);
        vm->settings()->setTextScale(1.0);
        vm->settings()->setTextScale(1.5);
        vm->navigate(QStringLiteral("Recording"));
        window->show();

        const auto verifyWidth = [&](int width, bool compactInspector) {
            window->setWidth(width);
            window->setHeight(720);
            QCOMPARE(vm->currentPage(), QStringLiteral("Recording"));
            QTRY_COMPARE_WITH_TIMEOUT(page->property("compactInspector").toBool(), compactInspector, 1'000);
            QTRY_VERIFY_WITH_TIMEOUT(qAbs(workspace->width() - page->width()) <= 0.5, 1'000);
            QTRY_VERIFY_WITH_TIMEOUT(inspector->isVisible() == !compactInspector, 1'000);
            QTRY_VERIFY_WITH_TIMEOUT(mainPane->width() > 700.0, 1'000);
            QTRY_VERIFY_WITH_TIMEOUT(transport->width() > 700.0, 1'000);
            QTRY_VERIFY_WITH_TIMEOUT(timeline->width() <= transport->width() + 0.5, 1'000);
            QVERIFY2(inspector->isVisible() == !compactInspector,
                     qPrintable(QStringLiteral("Inspector visibility mismatch at %1 px: page=%2, "
                                               "compact=%3, inspector=%4/%5")
                                    .arg(width)
                                    .arg(page->width())
                                    .arg(page->property("compactInspector").toBool())
                                    .arg(inspector->property("visible").toBool())
                                    .arg(inspector->isVisible())));
            QCOMPARE(inspectorButton->isVisible(), compactInspector);
            QVERIFY(mainPane->width() > 0.0);
            QVERIFY(header->height() <= 72.0);
            QVERIFY(transport->width() <= mainPane->width() + 0.5);
            QVERIFY(timeline->width() > 0.0);
            QVERIFY2(timeline->width() <= transport->width() + 0.5,
                     qPrintable(QStringLiteral("Playback timeline exceeds transport at %1 px: %2 of %3 px "
                                               "(main pane %4, workspace %5, page %6, inspector %7/%8)")
                                    .arg(width)
                                    .arg(timeline->width())
                                    .arg(transport->width())
                                    .arg(mainPane->width())
                                    .arg(workspace->width())
                                    .arg(page->width())
                                    .arg(inspector->width())
                                    .arg(inspector->property("visible").toBool())));
            QVERIFY(positionSlider->width() >= 160.0);
            const QPointF rateIndicatorOrigin = rateIndicator->mapToItem(rateCombo, QPointF{});
            QVERIFY(rateIndicatorOrigin.x() >= -0.5 && rateIndicatorOrigin.y() >= -0.5);
            QVERIFY(rateIndicatorOrigin.x() + rateIndicator->width() <= rateCombo->width() + 0.5);
            QVERIFY(rateIndicatorOrigin.y() + rateIndicator->height() <= rateCombo->height() + 0.5);
            QCOMPARE(playPauseButton->width(), 40.0);
            QCOMPARE(playPauseButton->height(), 40.0);
            QCOMPARE(playPauseIcon->width(), 20.0);
            QCOMPARE(playPauseIcon->height(), 20.0);
            const QPointF playIconOrigin = playPauseIcon->mapToItem(playPauseButton, QPointF{});
            QVERIFY2(playIconOrigin.x() >= -0.5 && playIconOrigin.y() >= -0.5 &&
                         playIconOrigin.x() + playPauseIcon->width() <= playPauseButton->width() + 0.5 &&
                         playIconOrigin.y() + playPauseIcon->height() <= playPauseButton->height() + 0.5,
                     qPrintable(QStringLiteral("Play/pause icon is clipped: button=%1x%2, "
                                               "icon=(%3,%4) %5x%6")
                                    .arg(playPauseButton->width())
                                    .arg(playPauseButton->height())
                                    .arg(playIconOrigin.x())
                                    .arg(playIconOrigin.y())
                                    .arg(playPauseIcon->width())
                                    .arg(playPauseIcon->height())));
            QVERIFY(options->width() <= transport->width() + 0.5);
            QVERIFY(transcriptToolbar->width() <= mainPane->width() + 0.5);
            QVERIFY(waveform->height() <= 68.0);
            QVERIFY(transport->height() <= 132.0);
            QVERIFY(transcriptToolbar->height() <= 144.0);

            const QPointF paneOrigin = mainPane->mapToItem(page, QPointF{});
            QVERIFY(paneOrigin.x() >= -0.5);
            QVERIFY(paneOrigin.x() + mainPane->width() <= page->width() + 0.5);
        };

        verifyWidth(980, true);
        verifyWidth(1280, false);
        QCOMPARE(playPauseButton->property("text").toString(), QString());
        QCOMPARE(playPauseIcon->property("source").toUrl(),
                 QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/icons/lucide/play.svg")));

        const QSharedPointer<QQuickItemGrabResult> playIconGrab = playPauseIcon->grabToImage();
        QVERIFY(playIconGrab);
        QSignalSpy playIconReadySpy(playIconGrab.data(), &QQuickItemGrabResult::ready);
        if (playIconGrab->image().isNull()) {
            QVERIFY2(playIconReadySpy.wait(1'000), "Timed out while rendering the play button icon.");
        }
        const QImage playIconImage = playIconGrab->image().convertToFormat(QImage::Format_RGBA8888);
        QVERIFY(!playIconImage.isNull());
        QRect paintedBounds;
        int paintedPixelCount = 0;
        for (int y = 0; y < playIconImage.height(); ++y) {
            for (int x = 0; x < playIconImage.width(); ++x) {
                if (playIconImage.pixelColor(x, y).alpha() <= 32) {
                    continue;
                }
                paintedBounds |= QRect(x, y, 1, 1);
                ++paintedPixelCount;
            }
        }
        QVERIFY2(paintedPixelCount > 4, "The play button icon did not render.");
        QVERIFY2(paintedBounds.left() > 0 && paintedBounds.top() > 0 &&
                     paintedBounds.right() < playIconImage.width() - 1 &&
                     paintedBounds.bottom() < playIconImage.height() - 1,
                 qPrintable(QStringLiteral("The play icon touches its raster edge: image=%1x%2, "
                                           "painted=(%3,%4) %5x%6")
                                .arg(playIconImage.width())
                                .arg(playIconImage.height())
                                .arg(paintedBounds.x())
                                .arg(paintedBounds.y())
                                .arg(paintedBounds.width())
                                .arg(paintedBounds.height())));
        vm->settings()->setTextScale(0.9);
        vm->settings()->setTextScale(1.0);
        QCoreApplication::processEvents();

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void compactTranscriptKeepsContentDenseAccessibleAndResponsive() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/Main.qml")));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto* window = qobject_cast<QQuickWindow*>(root.data());
        auto* vm = root->findChild<BreezeDesk::ApplicationViewModel*>();
        auto* page = root->findChild<QQuickItem*>(QStringLiteral("recordingPage"));
        auto* mainPane = root->findChild<QQuickItem*>(QStringLiteral("recordingMainPane"));
        auto* waveform = root->findChild<QQuickItem*>(QStringLiteral("recordingWaveformCard"));
        auto* transport = root->findChild<QQuickItem*>(QStringLiteral("recordingTransportCard"));
        auto* toolbar = root->findChild<QQuickItem*>(QStringLiteral("recordingTranscriptToolbar"));
        auto* list = root->findChild<QQuickItem*>(QStringLiteral("segmentList"));
        auto* noMatchesState = root->findChild<QQuickItem*>(QStringLiteral("recordingNoMatchesState"));
        QVERIFY(window);
        QVERIFY(vm);
        QVERIFY(page);
        QVERIFY(mainPane);
        QVERIFY(waveform);
        QVERIFY(transport);
        QVERIFY(toolbar);
        QVERIFY(list);
        QVERIFY(noMatchesState);

        QList<BreezeDesk::TranscriptSegmentModel::Segment> segments;
        constexpr int segmentFixtureCount = 20;
        segments.reserve(segmentFixtureCount);
        for (int index = 0; index < segmentFixtureCount; ++index) {
            BreezeDesk::TranscriptSegmentModel::Segment segment;
            segment.id = QStringLiteral("compact-segment-%1").arg(index);
            segment.ordinal = index;
            segment.startMs = index * 3'000;
            segment.endMs = segment.startMs + 2'800;
            segment.originalText = QStringLiteral("Compact transcript fixture %1").arg(index);
            segment.editedText = segment.originalText;
            segment.lowConfidence = index == 1;
            segment.reviewed = index == 2;
            segments.append(std::move(segment));
        }
        vm->transcript()->replaceSegments(segments);
        vm->settings()->setTextScale(1.5);
        vm->settings()->setTextScale(1.0);
        vm->settings()->setCompactMode(true);
        vm->settings()->setCompactMode(false);
        vm->navigate(QStringLiteral("Recording"));
        window->show();

        const auto verifyContainedHorizontally = [](QQuickItem* parent, QQuickItem* child,
                                                    const QString& context) {
            const QPointF origin = child->mapToItem(parent, QPointF{});
            const bool contained = origin.x() >= -0.5 && origin.x() + child->width() <= parent->width() + 0.5;
            QVERIFY2(contained,
                     qPrintable(QStringLiteral("%1 is clipped horizontally: x=%2 width=%3; parent=%4")
                                    .arg(context)
                                    .arg(origin.x())
                                    .arg(child->width())
                                    .arg(parent->width())));
        };

        const auto verifyGeometry = [&](int width, int height, bool compactInspector) {
            window->setWidth(width);
            window->setHeight(height);
            QCoreApplication::processEvents();
            QTRY_VERIFY_WITH_TIMEOUT(list->height() > 0.0, 1'000);

            QTRY_COMPARE_WITH_TIMEOUT(page->property("compactInspector").toBool(), compactInspector, 1'000);
            verifyContainedHorizontally(page, mainPane,
                                        QStringLiteral("recording main pane at %1 px").arg(width));
            for (QQuickItem* item : {waveform, transport, toolbar, list}) {
                verifyContainedHorizontally(mainPane, item, item->objectName());
            }

            const QPointF waveformOrigin = waveform->mapToItem(mainPane, QPointF{});
            const QPointF transportOrigin = transport->mapToItem(mainPane, QPointF{});
            const QPointF toolbarOrigin = toolbar->mapToItem(mainPane, QPointF{});
            const QPointF listOrigin = list->mapToItem(mainPane, QPointF{});
            QVERIFY(waveformOrigin.y() + waveform->height() <= transportOrigin.y() + 0.5);
            QVERIFY(transportOrigin.y() + transport->height() <= toolbarOrigin.y() + 0.5);
            QVERIFY(toolbarOrigin.y() + toolbar->height() <= listOrigin.y() + 0.5);
            QVERIFY(listOrigin.y() + list->height() <= mainPane->height() + 0.5);

            QVERIFY2(list->height() >= mainPane->height() * 0.42,
                     qPrintable(QStringLiteral("Transcript viewport is too short at %1x%2: %3 of %4 px")
                                    .arg(width)
                                    .arg(height)
                                    .arg(list->height())
                                    .arg(mainPane->height())));
            QVERIFY(waveform->height() <= 68.0);
            QVERIFY(transport->height() <= 104.0);
            QTRY_VERIFY_WITH_TIMEOUT(toolbar->height() <= (compactInspector ? 96.0 : 56.0), 1'000);

            const auto editors = visualDescendantsNamed(list, QStringLiteral("segmentEditor"));
            QVERIFY2(!editors.isEmpty(), "The transcript viewport did not instantiate segment delegates.");
            int fullyVisibleEditors = 0;
            qreal timeColumnWidth = -1.0;
            for (QQuickItem* editor : editors) {
                const QPointF origin = editor->mapToItem(list, QPointF{});
                verifyContainedHorizontally(list, editor, QStringLiteral("transcript segment"));
                QCOMPARE(editor->property("radius").toReal(), 0.0);
                QVERIFY(visualDescendantsNamed(editor, QStringLiteral("segmentReviewedControl")).isEmpty());
                auto* timeColumn =
                    visualDescendantsNamed(editor, QStringLiteral("segmentTimeColumn")).value(0);
                QVERIFY(timeColumn);
                if (timeColumnWidth < 0.0) {
                    timeColumnWidth = timeColumn->width();
                } else {
                    QVERIFY(qAbs(timeColumn->width() - timeColumnWidth) <= 0.5);
                }
                if (origin.y() >= -0.5 && origin.y() + editor->height() <= list->height() + 0.5) {
                    ++fullyVisibleEditors;
                    QVERIFY2(editor->height() <= 76.0,
                             qPrintable(QStringLiteral("Unselected segment is not compact: %1 px")
                                            .arg(editor->height())));
                }
            }
            QVERIFY2(fullyVisibleEditors >= 4,
                     qPrintable(QStringLiteral("Only %1 transcript segments fit at %2x%3")
                                    .arg(fullyVisibleEditors)
                                    .arg(width)
                                    .arg(height)));

            auto* editor = editors.constFirst();
            auto* textEditor = visualDescendantsNamed(editor, QStringLiteral("segmentTextEditor")).value(0);
            auto* separator = visualDescendantsNamed(editor, QStringLiteral("segmentSeparator")).value(0);
            QVERIFY(textEditor);
            QVERIFY(separator);
            QVERIFY(visualDescendantsNamed(editor, QStringLiteral("segmentReviewedControl")).isEmpty());
            verifyContainedHorizontally(editor, textEditor, QStringLiteral("segment text editor"));
            verifyContainedHorizontally(editor, separator, QStringLiteral("segment separator"));

            for (QQuickItem* accessibleItem : {editor, textEditor}) {
                QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(accessibleItem);
                QVERIFY2(interface, qPrintable(accessibleItem->objectName() +
                                               QStringLiteral(" has no accessibility interface.")));
                QVERIFY2(
                    !interface->text(QAccessible::Name).trimmed().isEmpty(),
                    qPrintable(accessibleItem->objectName() + QStringLiteral(" has no accessible name.")));
            }
        };

        verifyGeometry(980, 720, true);
        verifyGeometry(1'280, 720, false);
        verifyGeometry(1'600, 1'080, false);

        vm->transcript()->setSelectedIndex(5);
        vm->transcript()->updatePlaybackPosition(15'500);
        QCOMPARE(vm->transcript()->selectedIndex(), 5);
        QCOMPARE(vm->transcript()->activePlaybackIndex(), 5);
        vm->transcript()->setSearchText(QStringLiteral("no segment contains this phrase"));
        QTRY_COMPARE_WITH_TIMEOUT(vm->transcript()->visibleSegmentCount(), 0, 1'000);
        QTRY_VERIFY_WITH_TIMEOUT(noMatchesState->isVisible(), 1'000);
        QVERIFY(!list->isVisible());
        QCOMPARE(vm->transcript()->selectedIndex(), -1);
        QCOMPARE(vm->transcript()->activePlaybackIndex(), -1);
        vm->transcript()->setSearchText(QString{});
        QTRY_COMPARE_WITH_TIMEOUT(vm->transcript()->visibleSegmentCount(), segmentFixtureCount, 1'000);
        QCOMPARE(vm->transcript()->selectedIndex(), 5);
        QCOMPARE(vm->transcript()->activePlaybackIndex(), 5);

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void selectedTranscriptSegmentKeepsActionsCompactAndAccessible() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            import BreezeDesk

            ApplicationWindow {
                width: 640
                height: 240
                visible: true
                property real uiScale: 1.0
                Component.onCompleted: DesignSystem.textScale = uiScale
                onUiScaleChanged: DesignSystem.textScale = uiScale

                SegmentEditor {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    height: implicitHeight
                    proxyRow: 0
                    startMs: 4_078_000
                    endMs: 14_400_000
                    originalText: "Compact selected segment"
                    editedText: originalText
                    lowConfidence: true
                    edited: false
                    glossaryReplacement: false
                    glossaryAudit: []
                    reviewed: false
                    editingLocked: false
                    selected: true
                }
            }
        )",
                          QUrl(QStringLiteral("inline:SelectedSegmentTestHost.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 1'000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> host(component.create());
        QVERIFY2(host, qPrintable(component.errorString() + qmlMessages.join(QLatin1Char('\n'))));

        auto* window = qobject_cast<QQuickWindow*>(host.data());
        QVERIFY(window);
        const auto segments = visualDescendantsNamed(window->contentItem(), QStringLiteral("segmentEditor"));
        QVERIFY(!segments.isEmpty());
        QQuickItem* segment = segments.constFirst();
        auto* timeColumn = visualDescendantsNamed(segment, QStringLiteral("segmentTimeColumn")).value(0);
        auto* textEditor = visualDescendantsNamed(segment, QStringLiteral("segmentTextEditor")).value(0);
        auto* statusRow = visualDescendantsNamed(segment, QStringLiteral("segmentStatusRow")).value(0);
        auto* lowConfidenceMarker =
            visualDescendantsNamed(segment, QStringLiteral("segmentLowConfidenceMarker")).value(0);
        auto* separator = visualDescendantsNamed(segment, QStringLiteral("segmentSeparator")).value(0);
        auto* actionsRow = visualDescendantsNamed(segment, QStringLiteral("segmentActionsRow")).value(0);
        auto* editButton = visualDescendantsNamed(segment, QStringLiteral("segmentEditButton")).value(0);
        auto* deleteButton = visualDescendantsNamed(segment, QStringLiteral("segmentDeleteButton")).value(0);
        auto* startTimeCode =
            visualDescendantsNamed(segment, QStringLiteral("segmentStartTimeCode")).value(0);
        auto* endTimeCode = visualDescendantsNamed(segment, QStringLiteral("segmentEndTimeCode")).value(0);
        QVERIFY(timeColumn);
        QVERIFY(textEditor);
        QVERIFY(statusRow);
        QVERIFY(lowConfidenceMarker);
        QVERIFY(separator);
        QVERIFY(visualDescendantsNamed(segment, QStringLiteral("segmentReviewedControl")).isEmpty());
        QVERIFY(actionsRow);
        QVERIFY(editButton);
        QVERIFY(deleteButton);
        QVERIFY(startTimeCode);
        QVERIFY(endTimeCode);

        const auto verifySegment = [&](int width) {
            window->setWidth(width);
            QCoreApplication::processEvents();
            QTRY_VERIFY_WITH_TIMEOUT(segment->height() > 0.0, 1'000);
            QVERIFY2(segment->height() >= 48.0 && segment->height() <= 88.0,
                     qPrintable(QStringLiteral("Selected segment has unexpected height at %1 px: %2")
                                    .arg(width)
                                    .arg(segment->height())));
            QVERIFY(lowConfidenceMarker->isVisible());
            QVERIFY2(
                !statusRow->isVisible(),
                qPrintable(QStringLiteral("Low confidence must not add a badge row at %1 px").arg(width)));
            QVERIFY(actionsRow->isVisible());
            QVERIFY2(deleteButton->height() <= 32.0,
                     qPrintable(QStringLiteral("Segment action buttons must stay compact at %1 px: "
                                               "delete button height=%2")
                                    .arg(width)
                                    .arg(deleteButton->height())));

            const QPointF selectedTextOrigin = textEditor->mapToItem(segment, QPointF{});
            const QPointF selectedActionsOrigin = actionsRow->mapToItem(segment, QPointF{});
            const qreal actionsCenterOffset =
                selectedActionsOrigin.y() + actionsRow->height() / 2.0 - segment->height() / 2.0;
            QVERIFY2(std::abs(actionsCenterOffset) <= 2.0,
                     qPrintable(QStringLiteral("Segment actions must be vertically centered at %1 px: "
                                               "offset=%2")
                                    .arg(width)
                                    .arg(actionsCenterOffset)));
            QVERIFY2(selectedActionsOrigin.x() >= selectedTextOrigin.x() + textEditor->width() - 0.5,
                     qPrintable(QStringLiteral("Segment actions must sit to the right of the transcript text "
                                               "at %1 px: actions x=%2, text right=%3")
                                    .arg(width)
                                    .arg(selectedActionsOrigin.x())
                                    .arg(selectedTextOrigin.x() + textEditor->width())));

            for (QQuickItem* item :
                 {timeColumn, textEditor, lowConfidenceMarker, separator, actionsRow, deleteButton}) {
                const QPointF origin = item->mapToItem(segment, QPointF{});
                const bool contained = origin.x() >= -0.5 && origin.y() >= -0.5 &&
                                       origin.x() + item->width() <= segment->width() + 0.5 &&
                                       origin.y() + item->height() <= segment->height() + 0.5;
                QVERIFY2(contained, qPrintable(QStringLiteral("%1 is clipped at %2 px: x=%3 y=%4 w=%5 h=%6; "
                                                              "segment=%7x%8")
                                                   .arg(item->objectName())
                                                   .arg(width)
                                                   .arg(origin.x())
                                                   .arg(origin.y())
                                                   .arg(item->width())
                                                   .arg(item->height())
                                                   .arg(segment->width())
                                                   .arg(segment->height())));
            }
        };

        verifySegment(560);
        verifySegment(920);

        QSignalSpy selectionSpy(segment, SIGNAL(selectedRequested(int)));
        QSignalSpy textEditedSpy(segment, SIGNAL(textEdited(int, QString)));
        textEditor->forceActiveFocus();
        QTRY_VERIFY_WITH_TIMEOUT(!selectionSpy.isEmpty(), 1'000);

        const auto clickEditButton = [editButton, window] {
            const QPointF center =
                editButton->mapToScene(QPointF(editButton->width() / 2.0, editButton->height() / 2.0));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
        };

        QVERIFY2(textEditor->property("readOnly").toBool(),
                 "Selecting a segment must not enter editing mode.");
        clickEditButton();
        QTRY_VERIFY_WITH_TIMEOUT(!textEditor->property("readOnly").toBool(), 1'000);
        QVERIFY(textEditor->hasActiveFocus());
        textEditor->setProperty("text", QStringLiteral("Edited with the Done Editing button"));
        clickEditButton();
        QTRY_VERIFY_WITH_TIMEOUT(textEditor->property("readOnly").toBool(), 1'000);
        QVERIFY(!segment->property("editing").toBool());
        QVERIFY(!textEditor->hasActiveFocus());
        QTRY_COMPARE_WITH_TIMEOUT(textEditedSpy.count(), 1, 1'000);
        QCOMPARE(textEditedSpy.constFirst().at(0).toInt(), 0);
        QCOMPARE(textEditedSpy.constFirst().at(1).toString(),
                 QStringLiteral("Edited with the Done Editing button"));

        host->setProperty("uiScale", 1.5);
        QTRY_VERIFY_WITH_TIMEOUT(timeColumn->width() >= 90.0, 1'000);
        for (QQuickItem* timeCode : {startTimeCode, endTimeCode}) {
            const QPointF origin = timeCode->mapToItem(timeColumn, QPointF{});
            QVERIFY(origin.x() >= -0.5);
            QVERIFY(origin.x() + timeCode->width() <= timeColumn->width() + 0.5);
            auto* contentItem = qvariant_cast<QQuickItem*>(timeCode->property("contentItem"));
            QVERIFY(contentItem);
            const qreal requiredHeight = contentItem->implicitHeight() +
                                         timeCode->property("topPadding").toReal() +
                                         timeCode->property("bottomPadding").toReal();
            const auto heightMessage = [timeCode, contentItem, requiredHeight] {
                return QStringLiteral("Long timecode is vertically clipped: height=%1, required=%2, "
                                      "implicit=%3, content=%4")
                    .arg(timeCode->height())
                    .arg(requiredHeight)
                    .arg(timeCode->implicitHeight())
                    .arg(contentItem->implicitHeight());
            };
            QTRY_VERIFY2_WITH_TIMEOUT(timeCode->height() + 0.5 >= requiredHeight, qPrintable(heightMessage()),
                                      1'000);
        }
        host->setProperty("uiScale", 1.0);
        QCoreApplication::processEvents();

        for (QQuickItem* accessibleItem : {segment, textEditor, editButton, deleteButton}) {
            QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(accessibleItem);
            QVERIFY2(interface, qPrintable(accessibleItem->objectName() +
                                           QStringLiteral(" has no accessibility interface.")));
            QVERIFY2(!interface->text(QAccessible::Name).trimmed().isEmpty(),
                     qPrintable(accessibleItem->objectName() + QStringLiteral(" has no accessible name.")));
        }

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

    void sharedQmlPopupsUseSemanticSurfaces() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            import BreezeDesk

            ApplicationWindow {
                width: 640
                height: 480
                visible: true

                AppComboBox {
                    id: combo
                    objectName: "popupStyleCombo"
                    x: 24
                    y: 24
                    width: 220
                    model: ["Newest", "Oldest"]
                }
                AppSlider {
                    objectName: "semanticSlider"
                    x: 24
                    y: 80
                    width: 220
                    value: 0.5
                }
                AppMenu {
                    id: menu
                    objectName: "popupStyleMenu"
                    AppMenuItem { objectName: "firstPopupMenuItem"; text: "Rename" }
                    AppMenuSeparator { }
                    AppMenuItem { objectName: "secondPopupMenuItem"; text: "Edit tags" }
                }
                AppDialog {
                    id: dialog
                    objectName: "popupStyleDialog"
                    title: "Rename Recording"
                    standardButtons: Dialog.Ok | Dialog.Cancel
                    AppTextField { width: parent.width; text: "Fixture" }
                }
            }
        )",
                          QUrl(QStringLiteral("inline:SharedPopupStyleHost.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 1'000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString() + qmlMessages.join(QLatin1Char('\n'))));

        auto* combo = root->findChild<QObject*>(QStringLiteral("popupStyleCombo"));
        auto* slider = root->findChild<QQuickItem*>(QStringLiteral("semanticSlider"));
        auto* menu = root->findChild<QObject*>(QStringLiteral("popupStyleMenu"));
        auto* dialog = root->findChild<QObject*>(QStringLiteral("popupStyleDialog"));
        QVERIFY(combo);
        QVERIFY(slider);
        QVERIFY(menu);
        QVERIFY(dialog);

        QQmlComponent tokenProbe(&engine);
        tokenProbe.setData(R"(
            import QtQuick
            import BreezeDesk
            QtObject {
                property color surfaceRaised: SemanticTokens.surfaceRaised
                property color accentMuted: SemanticTokens.accentMuted
                property color accent: SemanticTokens.accent
                property color borderStrong: SemanticTokens.borderStrong
            }
        )",
                           QUrl(QStringLiteral("inline:SharedPopupTokenProbe.qml")));
        QScopedPointer<QObject> tokens(tokenProbe.create());
        QVERIFY2(tokens, qPrintable(tokenProbe.errorString()));
        const QColor expectedSurface = tokens->property("surfaceRaised").value<QColor>();
        const QColor expectedHighlight = tokens->property("accentMuted").value<QColor>();
        const QColor expectedAccent = tokens->property("accent").value<QColor>();
        const QColor expectedTrack = tokens->property("borderStrong").value<QColor>();

        auto* comboIndicator = qobject_cast<QQuickItem*>(combo->property("indicator").value<QObject*>());
        QVERIFY(comboIndicator);
        QCOMPARE(comboIndicator->objectName(), QStringLiteral("appComboBoxIndicator"));
        QCOMPARE(comboIndicator->width(), 16.0);
        QCOMPARE(comboIndicator->height(), 16.0);
        QCOMPARE(comboIndicator->property("source").toUrl(),
                 QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/icons/lucide/chevron-down.svg")));
        const QPointF indicatorOrigin =
            comboIndicator->mapToItem(qobject_cast<QQuickItem*>(combo), QPointF{});
        QVERIFY(indicatorOrigin.x() >= -0.5 && indicatorOrigin.y() >= -0.5);
        QVERIFY(indicatorOrigin.x() + comboIndicator->width() <=
                qobject_cast<QQuickItem*>(combo)->width() + 0.5);
        QVERIFY(indicatorOrigin.y() + comboIndicator->height() <=
                qobject_cast<QQuickItem*>(combo)->height() + 0.5);

        auto* sliderTrack = qobject_cast<QQuickItem*>(slider->property("background").value<QObject*>());
        auto* sliderHandle = qobject_cast<QQuickItem*>(slider->property("handle").value<QObject*>());
        QVERIFY(sliderTrack);
        QVERIFY(sliderHandle);
        auto* sliderProgress = sliderTrack->findChild<QQuickItem*>(QStringLiteral("appSliderProgress"));
        QVERIFY(sliderProgress);
        QCOMPARE(sliderTrack->objectName(), QStringLiteral("appSliderTrack"));
        QCOMPARE(sliderHandle->objectName(), QStringLiteral("appSliderHandle"));
        QCOMPARE(sliderTrack->height(), 6.0);
        QCOMPARE(sliderHandle->width(), 20.0);
        QCOMPARE(sliderHandle->height(), 20.0);
        QCOMPARE(sliderTrack->property("color").value<QColor>(), expectedTrack);
        QCOMPARE(sliderProgress->property("color").value<QColor>(), expectedAccent);
        QCOMPARE(sliderHandle->property("color").value<QColor>(), expectedSurface);
        QVERIFY(qAbs(sliderProgress->width() - sliderTrack->width() / 2.0) <= 0.5);
        const QPointF handleOrigin = sliderHandle->mapToItem(slider, QPointF{});
        QVERIFY(handleOrigin.x() >= -0.5 && handleOrigin.y() >= -0.5);
        QVERIFY(handleOrigin.x() + sliderHandle->width() <= slider->width() + 0.5);
        QVERIFY(handleOrigin.y() + sliderHandle->height() <= slider->height() + 0.5);

        QVERIFY(QMetaObject::invokeMethod(dialog, "open", Qt::DirectConnection));
        QCoreApplication::processEvents();
        auto* dialogSurface = dialog->findChild<QQuickItem*>(QStringLiteral("appDialogSurface"));
        auto* dialogFooter = dialog->findChild<QQuickItem*>(QStringLiteral("appDialogFooter"));
        QVERIFY(dialogSurface);
        QVERIFY(dialogFooter);
        QCOMPARE(dialogSurface->property("color").value<QColor>(), expectedSurface);
        QCOMPARE(dialogFooter->property("count").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close", Qt::DirectConnection));

        QObject* comboPopup = combo->property("popup").value<QObject*>();
        QVERIFY(comboPopup);
        QVERIFY(QMetaObject::invokeMethod(comboPopup, "open", Qt::DirectConnection));
        QCoreApplication::processEvents();
        QCOMPARE(comboIndicator->property("source").toUrl(),
                 QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/icons/lucide/chevron-up.svg")));
        auto* comboSurface = comboPopup->findChild<QQuickItem*>(QStringLiteral("appComboBoxPopupSurface"));
        QVERIFY(comboSurface);
        QCOMPARE(comboSurface->property("color").value<QColor>(), expectedSurface);
        QVERIFY(QMetaObject::invokeMethod(comboPopup, "close", Qt::DirectConnection));
        QCoreApplication::processEvents();
        QCOMPARE(comboIndicator->property("source").toUrl(),
                 QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/icons/lucide/chevron-down.svg")));

        QVERIFY(QMetaObject::invokeMethod(menu, "open", Qt::DirectConnection));
        QCoreApplication::processEvents();
        auto* menuSurface = menu->findChild<QQuickItem*>(QStringLiteral("appMenuSurface"));
        auto* firstMenuItem = root->findChild<QQuickItem*>(QStringLiteral("firstPopupMenuItem"));
        auto* secondMenuItem = root->findChild<QQuickItem*>(QStringLiteral("secondPopupMenuItem"));
        QVERIFY(menuSurface);
        QVERIFY(firstMenuItem);
        QVERIFY(secondMenuItem);
        QCOMPARE(menuSurface->property("color").value<QColor>(), expectedSurface);

        auto* firstBackground =
            qobject_cast<QQuickItem*>(firstMenuItem->property("background").value<QObject*>());
        auto* secondBackground =
            qobject_cast<QQuickItem*>(secondMenuItem->property("background").value<QObject*>());
        QVERIFY(firstBackground);
        QVERIFY(secondBackground);

        const auto movePointerTo = [](QQuickItem* item) {
            const QPointF center = item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
            QTest::mouseMove(item->window(), center.toPoint());
        };
        movePointerTo(firstMenuItem);
        QTRY_VERIFY_WITH_TIMEOUT(firstMenuItem->property("highlighted").toBool(), 1'000);
        QTest::qWait(150);
        QCOMPARE(firstBackground->property("color").value<QColor>(), expectedHighlight);

        movePointerTo(secondMenuItem);
        QTRY_VERIFY_WITH_TIMEOUT(secondMenuItem->property("highlighted").toBool(), 1'000);
        QTest::qWait(20);
        QCOMPARE(firstBackground->property("color").value<QColor>().alpha(), 0);
        QCOMPARE(secondBackground->property("color").value<QColor>(), expectedHighlight);
        QVERIFY(QMetaObject::invokeMethod(menu, "close", Qt::DirectConnection));

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
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
        QCOMPARE(vm.activeRecordingId(), id);
        QCOMPARE(vm.currentPage(), QStringLiteral("Recording"));
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
        const QString toastBeforeRejectedEnqueue = vm.toastMessage();
        QVERIFY(vm.enqueueTranscription(id).isEmpty());
        QCOMPARE(vm.jobQueue()->jobs()->rowCount(), 0);
        QCOMPARE(vm.currentPage(), QStringLiteral("Recording"));
        QCOMPARE(vm.toastMessage(), toastBeforeRejectedEnqueue);
        connect(&vm, &BreezeDesk::ApplicationViewModel::transcriptionJobRequested, &vm,
                [&vm](const QString& jobId, const QString& recordingId) {
                    vm.jobQueue()->updateJob(jobId, recordingId, QStringLiteral("Fixture job"),
                                             QStringLiteral("Queued"), QStringLiteral("Preparing"), 0.0);
                });
        const QString jobId = vm.enqueueTranscription(id);
        QVERIFY(!jobId.isEmpty());
        // Enqueue is persistence-first: this standalone test simulates the coordinator's
        // durable job confirmation in the signal handler above.
        QCOMPARE(vm.jobQueue()->jobs()->rowCount(), 1);
        QCOMPARE(vm.currentPage(), QStringLiteral("Queue"));
        const QModelIndex queuedJob = vm.jobQueue()->jobs()->index(0, 0);
        QCOMPARE(vm.jobQueue()->jobs()->data(queuedJob, BreezeDesk::JobListModel::IdRole).toString(), jobId);
        vm.jobQueue()->updateJob(jobId, id, QStringLiteral("Fixture job"), QStringLiteral("Failed"),
                                 QStringLiteral("Transcribing"), 0.2, QStringLiteral("Fixture failure"));
        QVERIFY(vm.jobQueue()->jobs()->data(queuedJob, BreezeDesk::JobListModel::CanRemoveRole).toBool());
        QSignalSpy removeRequested(vm.jobQueue(), &BreezeDesk::JobQueueViewModel::removeRequested);
        vm.jobQueue()->remove(jobId);
        QCOMPARE(removeRequested.count(), 1);
        QCOMPARE(vm.jobQueue()->jobs()->rowCount(), 1);
        vm.jobQueue()->confirmRemoved(jobId);
        QCOMPARE(vm.jobQueue()->jobs()->rowCount(), 0);
        vm.library()->moveToTrash(id);
        QCOMPARE(vm.library()->trash()->rowCount(), 1);
        vm.library()->restore(id);
        QCOMPARE(vm.library()->recordings()->rowCount(), 1);
    }

    void sameRecordingCanBeOpenedAgainAfterReturningToLibrary() {
        BreezeDesk::ApplicationViewModel vm;
        QTemporaryFile media;
        QVERIFY(media.open());
        QCOMPARE(vm.importUrls({QUrl::fromLocalFile(media.fileName())}), 1);

        const QModelIndex first = vm.library()->recordings()->index(0, 0);
        const QString id =
            vm.library()->recordings()->data(first, BreezeDesk::RecordingListModel::IdRole).toString();
        QVERIFY(!id.isEmpty());

        vm.library()->activateRecording(id);
        QCOMPARE(vm.currentPage(), QStringLiteral("Recording"));
        QCOMPARE(vm.activeRecordingId(), id);
        QCOMPARE(vm.library()->selectedRecordingId(), id);

        vm.navigate(QStringLiteral("Library"));
        QCOMPARE(vm.currentPage(), QStringLiteral("Library"));

        vm.library()->activateRecording(id);
        QCOMPARE(vm.currentPage(), QStringLiteral("Recording"));
        QCOMPARE(vm.activeRecordingId(), id);
        QCOMPARE(vm.library()->selectedRecordingId(), id);
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

    void transcriptHistoryPinsSelectionFallsBackAndDeletesPermanently() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        BreezeDesk::DatabaseManager database(
            {directory.filePath(QStringLiteral("transcript-history.sqlite3")), 5'000, true, false});
        QVERIFY(database.initialize());
        BreezeDesk::SqliteRecordingRepository recordingRepository(database);
        BreezeDesk::SqliteJobRepository jobRepository(database);
        BreezeDesk::SqliteTranscriptRepository transcriptRepository(database);

        BreezeDesk::Recording recording;
        recording.id = QStringLiteral("history-recording");
        recording.title = QStringLiteral("Transcript history fixture");
        recording.sourcePath = directory.filePath(QStringLiteral("fixture.wav"));
        QVERIFY(recordingRepository.create(recording));

        BreezeDesk::TranscriptionJob completedJob;
        completedJob.id = QStringLiteral("completed-revision");
        completedJob.recordingId = recording.id;
        completedJob.modelId = QStringLiteral("fixture-model");
        completedJob.backend = QStringLiteral("cpu");
        const auto createdCompleted = jobRepository.createQueued(completedJob);
        QVERIFY(createdCompleted);
        QVERIFY(jobRepository.transition(completedJob.id, BreezeDesk::JobState::Preparing));
        QVERIFY(jobRepository.transition(completedJob.id, BreezeDesk::JobState::LoadingModel));
        QVERIFY(jobRepository.transition(completedJob.id, BreezeDesk::JobState::Transcribing));
        QVERIFY(jobRepository.transition(completedJob.id, BreezeDesk::JobState::Finalizing));

        BreezeDesk::TranscriptSegment completedSegment;
        completedSegment.id = QStringLiteral("completed-segment");
        completedSegment.recordingId = recording.id;
        completedSegment.jobId = completedJob.id;
        completedSegment.startMs = 0;
        completedSegment.endMs = 1'000;
        completedSegment.originalText = QStringLiteral("Completed revision");
        QVERIFY(transcriptRepository.replaceRevision(recording.id, completedJob.id, {completedSegment}));
        QVERIFY(jobRepository.completeAndActivate(recording.id, completedJob.id));

        BreezeDesk::TranscriptionJob liveJob;
        liveJob.id = QStringLiteral("live-revision");
        liveJob.recordingId = recording.id;
        liveJob.modelId = QStringLiteral("fixture-model");
        liveJob.backend = QStringLiteral("cpu");
        const auto createdLive = jobRepository.createQueued(liveJob);
        QVERIFY(createdLive);
        QVERIFY(jobRepository.transition(liveJob.id, BreezeDesk::JobState::Preparing));
        QVERIFY(jobRepository.transition(liveJob.id, BreezeDesk::JobState::LoadingModel));
        QVERIFY(jobRepository.transition(liveJob.id, BreezeDesk::JobState::Transcribing));

        BreezeDesk::TranscriptSegment liveSegment = completedSegment;
        liveSegment.id = QStringLiteral("live-segment");
        liveSegment.jobId = liveJob.id;
        liveSegment.originalText = QStringLiteral("Live partial revision");
        liveSegment.provisional = true;
        QVERIFY(transcriptRepository.replaceRevision(recording.id, liveJob.id, {liveSegment}));

        BreezeDesk::ApplicationViewModel vm(&recordingRepository, &transcriptRepository);
        vm.installJobRepository(&jobRepository);
        vm.openRecording(recording.id);
        QCOMPARE(vm.transcriptRevisions()->count(), 2);
        QCOMPARE(vm.transcriptRevisions()->selectedJobId(), completedJob.id);
        QCOMPARE(vm.transcript()->fullText(), QStringLiteral("Completed revision"));

        vm.reloadTranscriptForJob(recording.id, liveJob.id, true);
        QCOMPARE(vm.transcriptRevisions()->selectedJobId(), liveJob.id);
        QVERIFY(vm.transcript()->editingLocked());
        QCOMPARE(vm.transcript()->fullText(), QStringLiteral("Live partial revision"));

        vm.selectTranscriptRevision(completedJob.id);
        QVERIFY(vm.transcriptRevisions()->selectionPinned());
        QCOMPARE(vm.transcript()->fullText(), QStringLiteral("Completed revision"));

        liveSegment.originalText = QStringLiteral("Newer live partial revision");
        QVERIFY(transcriptRepository.replaceRevision(recording.id, liveJob.id, {liveSegment}));
        vm.reloadTranscriptForJob(recording.id, liveJob.id, true);
        QCOMPARE(vm.transcriptRevisions()->selectedJobId(), completedJob.id);
        QCOMPARE(vm.transcript()->fullText(), QStringLiteral("Completed revision"));
        QVERIFY(vm.transcriptRevisions()->hasNewerRevision());

        vm.followLiveTranscript();
        QCOMPARE(vm.transcriptRevisions()->selectedJobId(), liveJob.id);
        QCOMPARE(vm.transcript()->fullText(), QStringLiteral("Newer live partial revision"));
        QVERIFY(jobRepository.transition(liveJob.id, BreezeDesk::JobState::Failed,
                                         QStringLiteral("FixtureFailure"),
                                         QStringLiteral("Fixture failure")));
        vm.finishLiveTranscriptRevision(recording.id, liveJob.id, false);
        QCOMPARE(vm.transcriptRevisions()->selectedJobId(), completedJob.id);
        QCOMPARE(vm.transcript()->fullText(), QStringLiteral("Completed revision"));
        QVERIFY(!vm.transcript()->editingLocked());

        vm.transcript()->editText(0, QStringLiteral("Unsaved edit to be discarded"));
        QVERIFY(vm.transcript()->dirty());
        vm.deleteTranscriptRevision(completedJob.id);
        QCOMPARE(vm.transcriptRevisions()->count(), 1);
        QVERIFY(vm.transcriptRevisions()->selectedJobId().isEmpty());
        QCOMPARE(vm.transcript()->segmentCount(), 0);
        QVERIFY(!vm.transcript()->dirty());

        vm.openRecording(recording.id);
        QVERIFY(vm.transcriptRevisions()->selectedJobId().isEmpty());
        QCOMPARE(vm.transcript()->segmentCount(), 0);
        vm.selectTranscriptRevision(liveJob.id);
        QCOMPARE(vm.transcriptRevisions()->selectedJobId(), liveJob.id);
        QCOMPARE(vm.transcript()->fullText(), QStringLiteral("Newer live partial revision"));

        vm.transcript()->editText(0, QStringLiteral("Dirty failed revision"));
        QVERIFY(vm.transcript()->dirty());
        vm.navigate(QStringLiteral("Queue"));
        QVERIFY(jobRepository.deleteTerminalJob(liveJob.id));
        vm.refreshAfterTranscriptRemoval(liveJob.id);
        QCOMPARE(vm.currentPage(), QStringLiteral("Queue"));
        QCOMPARE(vm.transcriptRevisions()->count(), 0);
        QVERIFY(vm.transcriptRevisions()->selectedJobId().isEmpty());
        QCOMPARE(vm.transcript()->segmentCount(), 0);
        QVERIFY(!vm.transcript()->dirty());

        BreezeDesk::TranscriptionJob bulkJob;
        bulkJob.id = QStringLiteral("bulk-completed-revision");
        bulkJob.recordingId = recording.id;
        QVERIFY(jobRepository.createQueued(bulkJob));
        QVERIFY(jobRepository.transition(bulkJob.id, BreezeDesk::JobState::Preparing));
        QVERIFY(jobRepository.transition(bulkJob.id, BreezeDesk::JobState::LoadingModel));
        QVERIFY(jobRepository.transition(bulkJob.id, BreezeDesk::JobState::Transcribing));
        QVERIFY(jobRepository.transition(bulkJob.id, BreezeDesk::JobState::Finalizing));

        BreezeDesk::TranscriptSegment bulkSegment = completedSegment;
        bulkSegment.id = QStringLiteral("bulk-completed-segment");
        bulkSegment.jobId = bulkJob.id;
        bulkSegment.originalText = QStringLiteral("Bulk completed revision");
        QVERIFY(transcriptRepository.replaceRevision(recording.id, bulkJob.id, {bulkSegment}));
        QVERIFY(jobRepository.completeAndActivate(recording.id, bulkJob.id));

        vm.openRecording(recording.id);
        QCOMPARE(vm.transcriptRevisions()->selectedJobId(), bulkJob.id);
        vm.transcript()->editText(0, QStringLiteral("Dirty bulk revision"));
        QVERIFY(vm.transcript()->dirty());
        vm.navigate(QStringLiteral("Queue"));
        const auto cleared = jobRepository.clearCompleted();
        QVERIFY(cleared);
        QCOMPARE(cleared.value(), 1);
        vm.refreshAfterTranscriptRemoval();
        QCOMPARE(vm.currentPage(), QStringLiteral("Queue"));
        QCOMPARE(vm.transcriptRevisions()->count(), 0);
        QCOMPARE(vm.transcript()->segmentCount(), 0);
        QVERIFY(!vm.transcript()->dirty());
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

    void backendOptionsFilterByPlatform() {
        BreezeDesk::SettingsViewModel viewModel;
        const QStringList backends = viewModel.availableBackends();

        // Auto and CPU are offered everywhere; Auto falls back to CPU at runtime.
        QVERIFY(backends.contains(QStringLiteral("Auto")));
        QVERIFY(backends.contains(QStringLiteral("CPU")));

        // Each platform only offers the accelerator its worker is compiled with.
#if defined(Q_OS_MACOS)
        QVERIFY(backends.contains(QStringLiteral("Metal")));
        QVERIFY(!backends.contains(QStringLiteral("Vulkan")));
        const QString unavailable = QStringLiteral("Vulkan");
#elif defined(Q_OS_WIN)
        QVERIFY(backends.contains(QStringLiteral("Vulkan")));
        QVERIFY(!backends.contains(QStringLiteral("Metal")));
        const QString unavailable = QStringLiteral("Metal");
#else
        QVERIFY(!backends.contains(QStringLiteral("Metal")));
        QVERIFY(!backends.contains(QStringLiteral("Vulkan")));
        const QString unavailable = QStringLiteral("Vulkan");
#endif

        // A backend the current platform cannot host is rejected, not stored.
        viewModel.setBackend(QStringLiteral("CPU"));
        QCOMPARE(viewModel.backend(), QStringLiteral("CPU"));
        viewModel.setBackend(unavailable);
        QCOMPARE(viewModel.backend(), QStringLiteral("CPU"));
    }

    void dangerStylingIsAppliedToDestructiveActions() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            import BreezeDesk

            ApplicationWindow {
                width: 640
                height: 480
                visible: true

                AppButton {
                    objectName: "fixtureDangerButton"
                    x: 24
                    y: 24
                    danger: true
                    text: "Delete"
                }
                AppDialog {
                    objectName: "fixtureDestructiveDialog"
                    title: "Delete permanently?"
                    destructive: true
                    standardButtons: Dialog.Cancel | Dialog.Ok
                }
            }
        )",
                          QUrl(QStringLiteral("inline:DangerStylingHost.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 1'000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString() + qmlMessages.join(QLatin1Char('\n'))));

        QQmlComponent tokenProbe(&engine);
        tokenProbe.setData(R"(
            import QtQuick
            import BreezeDesk
            QtObject {
                property color danger: SemanticTokens.danger
                property color textOnAccent: SemanticTokens.textOnAccent
            }
        )",
                           QUrl(QStringLiteral("inline:DangerTokenProbe.qml")));
        QScopedPointer<QObject> tokens(tokenProbe.create());
        QVERIFY2(tokens, qPrintable(tokenProbe.errorString()));
        const QColor dangerColor = tokens->property("danger").value<QColor>();

        auto* dangerButton = root->findChild<QQuickItem*>(QStringLiteral("fixtureDangerButton"));
        QVERIFY(dangerButton);
        auto* dangerBackground = dangerButton->property("background").value<QQuickItem*>();
        QVERIFY(dangerBackground);
        QCOMPARE(dangerBackground->property("color").value<QColor>(), dangerColor);

        QObject* dialog = root->findChild<QObject*>(QStringLiteral("fixtureDestructiveDialog"));
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(), 1'000);
        QCoreApplication::processEvents();

        auto* footer = root->findChild<QQuickItem*>(QStringLiteral("appDialogFooter"));
        QVERIFY(footer);
        QList<QQuickItem*> footerButtons;
        for (QQuickItem* candidate : footer->findChildren<QQuickItem*>()) {
            if (candidate->property("danger").isValid() && candidate->property("primary").isValid()) {
                footerButtons.append(candidate);
            }
        }
        QCOMPARE(footerButtons.size(), 2);
        QQuickItem* destructiveDelegate = nullptr;
        for (QQuickItem* button : std::as_const(footerButtons)) {
            QVERIFY2(!button->property("primary").toBool(),
                     "A destructive dialog must not render an accent-primary confirm button.");
            if (button->property("danger").toBool()) {
                QVERIFY2(destructiveDelegate == nullptr,
                         "Only the accept-role delegate may carry the danger style.");
                destructiveDelegate = button;
            }
        }
        QVERIFY2(destructiveDelegate, "The destructive dialog confirm button lost its danger style.");
        auto* delegateBackground = destructiveDelegate->property("background").value<QQuickItem*>();
        QVERIFY(delegateBackground);
        QCOMPARE(delegateBackground->property("color").value<QColor>(), dangerColor);

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void iconButtonsProvideBuiltInTooltips() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            import BreezeDesk

            ApplicationWindow {
                width: 640
                height: 320
                visible: true

                IconButton {
                    objectName: "fixtureIconButton"
                    accessibleName: "Fixture action"
                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/save.svg"
                }
                RemoveButton {
                    objectName: "fixtureRemoveButton"
                    y: 48
                    accessibleName: "Remove fixture"
                }
                AppButton {
                    objectName: "fixtureLabeledButton"
                    y: 96
                    text: "Save"
                }
            }
        )",
                          QUrl(QStringLiteral("inline:TooltipDefaultsHost.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 1'000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString() + qmlMessages.join(QLatin1Char('\n'))));

        auto* iconButton = root->findChild<QQuickItem*>(QStringLiteral("fixtureIconButton"));
        auto* removeButton = root->findChild<QQuickItem*>(QStringLiteral("fixtureRemoveButton"));
        auto* labeledButton = root->findChild<QQuickItem*>(QStringLiteral("fixtureLabeledButton"));
        QVERIFY(iconButton);
        QVERIFY(removeButton);
        QVERIFY(labeledButton);
        QCOMPARE(iconButton->property("toolTipText").toString(), QStringLiteral("Fixture action"));
        QCOMPARE(removeButton->property("toolTipText").toString(), QStringLiteral("Remove fixture"));
        QVERIFY2(labeledButton->property("toolTipText").toString().isEmpty(),
                 "Labelled buttons must not sprout tooltips by default.");
    }

    void recordingCardOffersTranscribeShortcut() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            import BreezeDesk

            ApplicationWindow {
                width: 640
                height: 320
                visible: true
                property int transcribeRequests: 0

                RecordingCard {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    recordingId: "recording-id"
                    title: "Fixture recording"
                    durationMs: 1000
                    createdAt: new Date(0)
                    status: "Imported"
                    modelName: ""
                    tags: []
                    reviewState: "Unreviewed"
                    progress: 0
                    sourceMissing: false
                    onTranscribeRequested: function(id) {
                        if (id === "recording-id") {
                            transcribeRequests += 1
                        }
                    }
                }
            }
        )",
                          QUrl(QStringLiteral("inline:TranscribeShortcutHost.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 1'000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString() + qmlMessages.join(QLatin1Char('\n'))));

        QObject* transcribeMenuItem =
            root->findChild<QObject*>(QStringLiteral("recordingTranscribeMenuItem"));
        QVERIFY(transcribeMenuItem);
        QVERIFY(QMetaObject::invokeMethod(transcribeMenuItem, "triggered", Qt::DirectConnection));
        QCOMPARE(root->property("transcribeRequests").toInt(), 1);
    }

    void toastQueuePresentsSeveritiesSequentially() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/BreezeDesk/Main.qml")));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.data());
        QVERIFY(window);
        window->show();
        QCoreApplication::processEvents();

        QQmlComponent tokenProbe(&engine);
        tokenProbe.setData(R"(
            import QtQuick
            import BreezeDesk
            QtObject {
                property color accent: SemanticTokens.accent
                property color success: SemanticTokens.success
                property color surfaceRaised: SemanticTokens.surfaceRaised
            }
        )",
                           QUrl(QStringLiteral("inline:ToastTokenProbe.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(tokenProbe.status() != QQmlComponent::Loading, 1'000);
        QScopedPointer<QObject> tokens(tokenProbe.create());
        QVERIFY2(tokens, qPrintable(tokenProbe.errorString()));

        const auto showToast = [&root](const QString& message, const QString& severity,
                                       const QString& actionText) {
            return QMetaObject::invokeMethod(root.data(), "showToast", Q_ARG(QVariant, message),
                                             Q_ARG(QVariant, severity), Q_ARG(QVariant, actionText),
                                             Q_ARG(QVariant, QVariant()));
        };

        QObject* toast = root->findChild<QObject*>(QStringLiteral("appToast"));
        QVERIFY(toast);
        QVERIFY(showToast(QStringLiteral("first"), QStringLiteral("info"), QString()));
        QTRY_VERIFY_WITH_TIMEOUT(toast->property("opened").toBool(), 1'000);
        QCOMPARE(toast->property("message").toString(), QStringLiteral("first"));

        auto* strip = root->findChild<QQuickItem*>(QStringLiteral("appToastSeverityStrip"));
        QVERIFY(strip);
        QCOMPARE(strip->property("color").value<QColor>(), tokens->property("accent").value<QColor>());
        auto* background = toast->property("background").value<QQuickItem*>();
        QVERIFY(background);
        QCOMPARE(background->property("color").value<QColor>(),
                 tokens->property("surfaceRaised").value<QColor>());

        QVERIFY(showToast(QStringLiteral("second"), QStringLiteral("success"), QStringLiteral("Undo")));
        QCOMPARE(toast->property("message").toString(), QStringLiteral("first"));

        QVERIFY(QMetaObject::invokeMethod(toast, "close"));
        QTRY_COMPARE_WITH_TIMEOUT(toast->property("message").toString(), QStringLiteral("second"), 1'000);
        QTRY_VERIFY_WITH_TIMEOUT(toast->property("opened").toBool(), 1'000);
        QCOMPARE(strip->property("color").value<QColor>(), tokens->property("success").value<QColor>());
        auto* actionButton = root->findChild<QQuickItem*>(QStringLiteral("appToastActionButton"));
        QVERIFY(actionButton);
        QVERIFY2(actionButton->property("visible").toBool(),
                 "A toast with an action label must show its action button.");
        QVERIFY(QMetaObject::invokeMethod(actionButton, "clicked", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(!toast->property("opened").toBool(), 1'000);

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void defaultModelReadyTracksInstallState() {
        BreezeDesk::ModelManagerViewModel viewModel;
        QVERIFY2(!viewModel.defaultModelReady(),
                 "Without any installed model the default model must not report ready.");
        QSignalSpy readySpy(&viewModel, &BreezeDesk::ModelManagerViewModel::defaultModelReadyChanged);

        viewModel.updateInstalled(QStringLiteral("breeze-asr-25-q5"), true, true);
        QVERIFY(viewModel.defaultModelReady());
        QCOMPARE(readySpy.count(), 1);

        viewModel.updateInstalled(QStringLiteral("breeze-asr-25-q5"), false, false);
        QVERIFY(!viewModel.defaultModelReady());
        QCOMPARE(readySpy.count(), 2);

        viewModel.updateInstalled(QStringLiteral("breeze-asr-25-q8"), true, true);
        QVERIFY2(!viewModel.defaultModelReady(),
                 "Installing a non-default model must not report the default as ready.");
        viewModel.setDefaultModel(QStringLiteral("breeze-asr-25-q8"));
        QVERIFY(viewModel.defaultModelReady());
    }

    void glossaryProfileDeleteRequiresConfirmation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        BreezeDesk::DatabaseManager database(
            {directory.filePath(QStringLiteral("glossary.sqlite3")), 5'000, true, false});
        QVERIFY(database.initialize());
        BreezeDesk::SqliteGlossaryRepository repository(database);
        BreezeDesk::GlossaryViewModel glossaryViewModel;
        glossaryViewModel.installRepository(&repository);
        const QString profileId =
            glossaryViewModel.createProfile(QStringLiteral("Product"), QString(), QString());
        QVERIFY(!profileId.isEmpty());
        glossaryViewModel.setProperty("selectedProfileId", profileId);

        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            import BreezeDesk

            ApplicationWindow {
                id: host
                required property var glossaryVm
                width: 980
                height: 720
                visible: true

                GlossaryPage {
                    anchors.fill: parent
                    vm: host.glossaryVm
                }
            }
        )",
                          QUrl(QStringLiteral("inline:GlossaryDeleteConfirmHost.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 1'000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{QStringLiteral("glossaryVm"), QVariant::fromValue<QObject*>(&glossaryViewModel)}}));
        QVERIFY2(root, qPrintable(component.errorString() + qmlMessages.join(QLatin1Char('\n'))));

        auto* deleteButton = root->findChild<QQuickItem*>(QStringLiteral("glossaryDeleteProfileButton"));
        QObject* confirmDialog = root->findChild<QObject*>(QStringLiteral("glossaryDeleteProfileDialog"));
        QVERIFY(deleteButton);
        QVERIFY(confirmDialog);
        QVERIFY(deleteButton->property("enabled").toBool());

        QVERIFY(QMetaObject::invokeMethod(deleteButton, "clicked", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(confirmDialog->property("visible").toBool(), 1'000);
        QCOMPARE(glossaryViewModel.profiles()->rowCount(), 1);
        QVERIFY2(confirmDialog->property("destructive").toBool(),
                 "The profile delete confirmation must use the destructive dialog style.");

        QVERIFY(QMetaObject::invokeMethod(confirmDialog, "accept"));
        QTRY_COMPARE_WITH_TIMEOUT(glossaryViewModel.profiles()->rowCount(), 0, 1'000);

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }

    void recordingDialogExposesAutoTranscribeToggle() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        FakeRecorder recorder;
        BreezeDesk::ApplicationViewModel appViewModel;
        QVERIFY(!appViewModel.settings()->autoTranscribeRecording());

        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            import BreezeDesk

            ApplicationWindow {
                id: host
                required property var recorderVm
                property var settingsVm: null
                width: 640
                height: 480
                visible: true

                RecordingDialog {
                    objectName: "fixtureRecordingDialog"
                    recorder: host.recorderVm
                    settings: host.settingsVm
                }
            }
        )",
                          QUrl(QStringLiteral("inline:AutoTranscribeToggleHost.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 1'000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{QStringLiteral("recorderVm"), QVariant::fromValue<QObject*>(&recorder)}}));
        QVERIFY2(root, qPrintable(component.errorString() + qmlMessages.join(QLatin1Char('\n'))));

        auto* window = qobject_cast<QQuickWindow*>(root.data());
        QVERIFY(window);
        window->show();
        QCoreApplication::processEvents();

        QObject* dialog = root->findChild<QObject*>(QStringLiteral("fixtureRecordingDialog"));
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(), 1'000);

        QObject* toggle = root->findChild<QObject*>(QStringLiteral("recordingAutoTranscribeToggle"));
        QVERIFY(toggle);
        QVERIFY2(!toggle->property("visible").toBool(),
                 "Without settings the auto-transcribe toggle must stay hidden.");

        QVERIFY(root->setProperty("settingsVm", QVariant::fromValue<QObject*>(appViewModel.settings())));
        QTRY_VERIFY_WITH_TIMEOUT(toggle->property("visible").toBool(), 1'000);
        QVERIFY(!toggle->property("checked").toBool());

        QVERIFY(QMetaObject::invokeMethod(toggle, "click"));
        QTRY_VERIFY_WITH_TIMEOUT(appViewModel.settings()->autoTranscribeRecording(), 1'000);

        const auto failures =
            qmlMessages.filter(QRegularExpression(QStringLiteral("ReferenceError|TypeError|Binding loop")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
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

    void existingManifestModelIsRecognizedAfterServiceRecreation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        EnvironmentVariableGuard dataRootGuard(QByteArrayLiteral("BREEZEDESK_DATA_ROOT"));
        qputenv("BREEZEDESK_DATA_ROOT", directory.path().toUtf8());

        const QString modelId = QStringLiteral("breeze-asr-25-q5");
        QString installedPath;
        {
            BreezeDesk::ModelManager first;
            installedPath = first.modelPath(modelId);
            QFile installedModel(installedPath);
            QVERIFY2(installedModel.open(QIODevice::WriteOnly), qPrintable(installedModel.errorString()));
            QCOMPARE(installedModel.write("existing-model", 14), 14);
            installedModel.close();
            QVERIFY(first.isInstalled(modelId));
        }

        BreezeDesk::ModelManager restarted;
        QVERIFY(restarted.isInstalled(modelId));
        BreezeDesk::ModelManagerViewModel viewModel;
        viewModel.installServices(&restarted);

        QAbstractItemModel* models = viewModel.models();
        QVERIFY(models != nullptr);
        QModelIndex installedIndex;
        for (int row = 0; row < models->rowCount(); ++row) {
            const QModelIndex candidate = models->index(row, 0);
            if (models->data(candidate, BreezeDesk::ModelListModel::IdRole).toString() == modelId) {
                installedIndex = candidate;
                break;
            }
        }
        QVERIFY(installedIndex.isValid());
        QVERIFY(models->data(installedIndex, BreezeDesk::ModelListModel::InstalledRole).toBool());
        QCOMPARE(models->data(installedIndex, BreezeDesk::ModelListModel::StateRole).toString(),
                 QStringLiteral("Installed"));
        QCOMPARE(models->data(installedIndex, BreezeDesk::ModelListModel::ProgressRole).toDouble(), 1.0);
        QCOMPARE(restarted.modelPath(modelId), installedPath);
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
