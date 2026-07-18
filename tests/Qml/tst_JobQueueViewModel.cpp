#include "breezedesk/ui/JobListModel.h"
#include "breezedesk/ui/JobQueueViewModel.h"
#include "breezedesk/ui/UiRegistration.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QtTest>

using namespace BreezeDesk;

namespace {
JobListModel* jobModel(JobQueueViewModel& viewModel) {
    return qobject_cast<JobListModel*>(viewModel.jobs());
}

int rowForId(JobListModel* model, const QString& id) {
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->data(model->index(row), JobListModel::IdRole).toString() == id) {
            return row;
        }
    }
    return -1;
}

QVariant valueFor(JobListModel* model, const QString& id, const JobListModel::Role role) {
    const int row = rowForId(model, id);
    return row < 0 ? QVariant{} : model->data(model->index(row), role);
}
} // namespace

class JobQueueViewModelTest final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void exposesRunningQueueTelemetryAndTimeline();
    void reordersOnlyQueuedJobsUsingQueuedPositions();
    void clearFinishedAndHideMatchPersistenceSemantics();
    void enhancedJobCardRendersAndExposesAccessibleActions();
    void queuedCardCanBeDragReordered();
};

void JobQueueViewModelTest::initTestCase() {
    registerUiTypes();
}

void JobQueueViewModelTest::exposesRunningQueueTelemetryAndTimeline() {
    JobQueueViewModel viewModel;
    JobListModel* model = jobModel(viewModel);
    QVERIFY(model);
    const QString allocatedId = viewModel.allocateJobId();
    QVERIFY(!allocatedId.isEmpty());
    QVERIFY(viewModel.empty()); // Persist-first allocation must not add an optimistic row.
    QVERIFY(!viewModel.containsJob(allocatedId));

    const QString running = viewModel.enqueue(QStringLiteral("recording"), QStringLiteral("Running job"));
    const QString interrupted =
        viewModel.enqueue(QStringLiteral("recording"), QStringLiteral("Interrupted job"));
    const QString waiting = viewModel.enqueue(QStringLiteral("recording"), QStringLiteral("Waiting job"));
    QVERIFY(viewModel.containsJob(running));

    viewModel.updateJob(running, QStringLiteral("recording"), QStringLiteral("Running job"),
                        QStringLiteral("Transcribing"), QStringLiteral("Transcribing"), 0.42);
    viewModel.updateJob(interrupted, QStringLiteral("recording"), QStringLiteral("Interrupted job"),
                        QStringLiteral("Interrupted"), QStringLiteral("Transcribing"), 0.25,
                        QStringLiteral("Worker exited"));
    viewModel.updateJobTelemetry(running, 2, 5, QStringLiteral("Latest partial words"));
    const QDateTime eventTime = QDateTime::fromString(QStringLiteral("2026-07-18T08:00:00Z"), Qt::ISODate);
    viewModel.appendJobEvent(running, QStringLiteral("Chunk saved"), QStringLiteral("Chunk 1 of 5"),
                             QStringLiteral("info"), eventTime);

    QCOMPARE(viewModel.runningJobId(), running);
    QCOMPARE(viewModel.activeCount(), 2); // Running plus queued; Interrupted is not active.
    QVERIFY(valueFor(model, running, JobListModel::IsRunningNowRole).toBool());
    QVERIFY(!valueFor(model, waiting, JobListModel::IsRunningNowRole).toBool());
    QCOMPARE(valueFor(model, interrupted, JobListModel::QueuePositionRole).toInt(), -1);
    QCOMPARE(valueFor(model, waiting, JobListModel::QueuePositionRole).toInt(), 0);
    QCOMPARE(valueFor(model, waiting, JobListModel::WaitingAheadRole).toInt(), 0);
    QCOMPARE(valueFor(model, running, JobListModel::CurrentChunkRole).toInt(), 2);
    QCOMPARE(valueFor(model, running, JobListModel::TotalChunksRole).toInt(), 5);
    QCOMPARE(valueFor(model, running, JobListModel::LatestPartialTextRole).toString(),
             QStringLiteral("Latest partial words"));
    QVERIFY(viewModel.isWritingTranscript(running));
    QVERIFY(!viewModel.isWritingTranscript(interrupted));

    const QVariantList timeline = valueFor(model, running, JobListModel::EventTimelineRole).toList();
    QCOMPARE(timeline.size(), 1);
    const QVariantMap latest = timeline.constLast().toMap();
    QCOMPARE(latest.value(QStringLiteral("title")).toString(), QStringLiteral("Chunk saved"));
    QCOMPARE(latest.value(QStringLiteral("timestamp")).toDateTime(), eventTime);
}

void JobQueueViewModelTest::reordersOnlyQueuedJobsUsingQueuedPositions() {
    JobQueueViewModel viewModel;
    JobListModel* model = jobModel(viewModel);
    QVERIFY(model);

    const QString first = viewModel.enqueue(QStringLiteral("recording"), QStringLiteral("First"));
    const QString second = viewModel.enqueue(QStringLiteral("recording"), QStringLiteral("Second"));
    const QString third = viewModel.enqueue(QStringLiteral("recording"), QStringLiteral("Third"));
    QSignalSpy reordered(&viewModel, &JobQueueViewModel::reorderRequested);

    viewModel.moveDown(first);
    QCOMPARE(reordered.count(), 1);
    QCOMPARE(reordered.constFirst().at(0).toString(), first);
    QCOMPARE(reordered.constFirst().at(1).toInt(), 1);
    QCOMPARE(valueFor(model, second, JobListModel::QueuePositionRole).toInt(), 0);
    QCOMPARE(valueFor(model, first, JobListModel::QueuePositionRole).toInt(), 1);
    QVERIFY(valueFor(model, first, JobListModel::CanMoveUpRole).toBool());

    viewModel.updateJob(second, QStringLiteral("recording"), QStringLiteral("Second"),
                        QStringLiteral("Transcribing"), QStringLiteral("Transcribing"), 0.1);
    QCOMPARE(valueFor(model, first, JobListModel::QueuePositionRole).toInt(), 0);
    QCOMPARE(valueFor(model, third, JobListModel::WaitingAheadRole).toInt(), 1);

    viewModel.moveUp(third);
    QCOMPARE(reordered.count(), 2);
    QCOMPARE(reordered.at(1).at(0).toString(), third);
    QCOMPARE(reordered.at(1).at(1).toInt(), 0);
    QCOMPARE(valueFor(model, third, JobListModel::QueuePositionRole).toInt(), 0);
    QCOMPARE(valueFor(model, first, JobListModel::QueuePositionRole).toInt(), 1);

    viewModel.moveDown(first);
    QCOMPARE(reordered.count(), 2); // Already last among queued jobs.
}

void JobQueueViewModelTest::clearFinishedAndHideMatchPersistenceSemantics() {
    JobQueueViewModel viewModel;
    JobListModel* model = jobModel(viewModel);
    QVERIFY(model);

    const QString completed = viewModel.enqueue(QStringLiteral("recording"), QStringLiteral("Completed"));
    const QString cancelled = viewModel.enqueue(QStringLiteral("recording"), QStringLiteral("Cancelled"));
    const QString failed = viewModel.enqueue(QStringLiteral("recording"), QStringLiteral("Failed"));
    const QString interrupted = viewModel.enqueue(QStringLiteral("recording"), QStringLiteral("Interrupted"));
    viewModel.updateJob(completed, QStringLiteral("recording"), QStringLiteral("Completed"),
                        QStringLiteral("Completed"), QStringLiteral("Completed"), 1.0);
    viewModel.updateJob(cancelled, QStringLiteral("recording"), QStringLiteral("Cancelled"),
                        QStringLiteral("Cancelled"), QStringLiteral("Transcribing"), 0.3);
    viewModel.updateJob(failed, QStringLiteral("recording"), QStringLiteral("Failed"),
                        QStringLiteral("Failed"), QStringLiteral("LoadingModel"), 0.2,
                        QStringLiteral("Model failed"));
    viewModel.updateJob(interrupted, QStringLiteral("recording"), QStringLiteral("Interrupted"),
                        QStringLiteral("Interrupted"), QStringLiteral("Transcribing"), 0.6);
    QCOMPARE(viewModel.activeCount(), 0);

    QSignalSpy clearRequested(&viewModel, &JobQueueViewModel::clearCompletedRequested);
    viewModel.clearCompleted();
    QCOMPARE(clearRequested.count(), 1);
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(rowForId(model, completed), -1);
    QCOMPARE(rowForId(model, cancelled), -1);
    QVERIFY(rowForId(model, failed) >= 0);
    QVERIFY(rowForId(model, interrupted) >= 0);

    QSignalSpy hidden(&viewModel, &JobQueueViewModel::removeRequested);
    viewModel.hide(failed);
    QCOMPARE(hidden.count(), 1);
    QCOMPARE(hidden.constFirst().constFirst().toString(), failed);
    QCOMPARE(rowForId(model, failed), -1);

    viewModel.hide(interrupted);
    QCOMPARE(hidden.count(), 2);
    QCOMPARE(hidden.at(1).constFirst().toString(), interrupted);
    QCOMPARE(rowForId(model, interrupted), -1);
}

void JobQueueViewModelTest::enhancedJobCardRendersAndExposesAccessibleActions() {
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import QtQuick.Layouts
        import BreezeDesk

        ApplicationWindow {
            width: 720
            height: 640
            visible: true
            property int moveUpRequests: 0
            property int moveDownRequests: 0

            JobProgress {
                id: queued
                objectName: "queuedJobCard"
                anchors.left: parent.left
                anchors.right: parent.right
                jobId: "queued-job"
                title: "Queued fixture"
                jobState: "Queued"
                stage: "Preparing"
                progress: 0.4
                errorMessage: ""
                canCancel: true
                canRetry: false
                canResume: false
                isRunningNow: false
                queuePosition: 1
                waitingAhead: 1
                currentChunk: 2
                totalChunks: 5
                latestPartialText: "Latest partial words"
                eventTimeline: [{ timestamp: new Date(0), title: "Queued", detail: "Preparing", severity: "info" }]
                canMoveUp: true
                canMoveDown: true
                canHide: false
                onMoveUpRequested: moveUpRequests += 1
                onMoveDownRequested: moveDownRequests += 1
            }
        }
    )",
                      QUrl(QStringLiteral("inline:JobProgressEnhancementTest.qml")));
    QTRY_VERIFY(component.status() != QQmlComponent::Loading);
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));

    auto* card = root->findChild<QQuickItem*>(QStringLiteral("queuedJobCard"));
    QVERIFY(card);
    auto* queueMetadata = card->findChild<QQuickItem*>(QStringLiteral("jobQueueMetadata"));
    auto* chunkStatus = card->findChild<QQuickItem*>(QStringLiteral("jobChunkStatus"));
    auto* partialText = card->findChild<QQuickItem*>(QStringLiteral("jobLatestPartialText"));
    auto* timelineToggle = card->findChild<QQuickItem*>(QStringLiteral("jobTimelineToggle"));
    auto* timeline = card->findChild<QQuickItem*>(QStringLiteral("jobEventTimeline"));
    auto* dragHandle = card->findChild<QQuickItem*>(QStringLiteral("jobDragHandle"));
    auto* moveUp = card->findChild<QQuickItem*>(QStringLiteral("jobMoveUpButton"));
    auto* moveDown = card->findChild<QQuickItem*>(QStringLiteral("jobMoveDownButton"));
    QVERIFY(queueMetadata);
    QVERIFY(chunkStatus);
    QVERIFY(partialText);
    QVERIFY(timelineToggle);
    QVERIFY(timeline);
    QVERIFY(dragHandle);
    QVERIFY(dragHandle->isVisible());
    QVERIFY(moveUp);
    QVERIFY(moveDown);
    QVERIFY(queueMetadata->property("text").toString().contains(QStringLiteral("2")));
    QVERIFY(chunkStatus->property("text").toString().contains(QStringLiteral("2")));
    QCOMPARE(partialText->property("text").toString(), QStringLiteral("Latest partial words"));
    QVERIFY(!timeline->isVisible());

    QVERIFY(QMetaObject::invokeMethod(timelineToggle, "clicked", Qt::DirectConnection));
    QTRY_VERIFY(timeline->isVisible());
    QVERIFY(QMetaObject::invokeMethod(moveUp, "clicked", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(moveDown, "clicked", Qt::DirectConnection));
    QCOMPARE(root->property("moveUpRequests").toInt(), 1);
    QCOMPARE(root->property("moveDownRequests").toInt(), 1);
}

void JobQueueViewModelTest::queuedCardCanBeDragReordered() {
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import BreezeDesk

        ApplicationWindow {
            width: 720
            height: 760
            visible: true
            property int reorderRequests: 0
            property int requestedDestination: -1

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 8

                JobProgress {
                    objectName: "firstQueuedCard"
                    width: parent.width
                    jobId: "first"
                    title: "First queued fixture"
                    jobState: "Queued"
                    stage: "Preparing"
                    progress: 0
                    errorMessage: ""
                    canCancel: true
                    canRetry: false
                    canResume: false
                    isRunningNow: false
                    queuePosition: 0
                    waitingAhead: 0
                    currentChunk: 0
                    totalChunks: 0
                    latestPartialText: ""
                    eventTimeline: []
                    canMoveUp: false
                    canMoveDown: true
                    canHide: false
                    onReorderRequested: function(id, destination) {
                        reorderRequests += 1
                        requestedDestination = destination
                    }
                }

                JobProgress {
                    objectName: "secondQueuedCard"
                    width: parent.width
                    jobId: "second"
                    title: "Second queued fixture"
                    jobState: "Queued"
                    stage: "Preparing"
                    progress: 0
                    errorMessage: ""
                    canCancel: true
                    canRetry: false
                    canResume: false
                    isRunningNow: false
                    queuePosition: 1
                    waitingAhead: 1
                    currentChunk: 0
                    totalChunks: 0
                    latestPartialText: ""
                    eventTimeline: []
                    canMoveUp: true
                    canMoveDown: false
                    canHide: false
                }
            }
        }
    )",
                      QUrl(QStringLiteral("inline:JobProgressDragTest.qml")));
    QTRY_VERIFY(component.status() != QQmlComponent::Loading);
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));

    auto* window = qobject_cast<QQuickWindow*>(root.get());
    auto* firstCard = root->findChild<QQuickItem*>(QStringLiteral("firstQueuedCard"));
    auto* secondCard = root->findChild<QQuickItem*>(QStringLiteral("secondQueuedCard"));
    auto* dragHandle = firstCard == nullptr
                           ? nullptr
                           : firstCard->findChild<QQuickItem*>(QStringLiteral("jobDragHandle"));
    QVERIFY(window);
    QVERIFY(firstCard);
    QVERIFY(secondCard);
    QVERIFY(dragHandle);
    QTRY_VERIFY(window->isExposed());

    const QPoint start = dragHandle
                             ->mapToScene(QPointF(dragHandle->width() / 2, dragHandle->height() / 2))
                             .toPoint();
    const QPoint target =
        secondCard->mapToScene(QPointF(secondCard->width() / 2, secondCard->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window, start + QPoint(0, 24), 50);
    QTest::mouseMove(window, target, 100);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, target);

    QTRY_COMPARE(root->property("reorderRequests").toInt(), 1);
    QCOMPARE(root->property("requestedDestination").toInt(), 1);
}

QTEST_MAIN(JobQueueViewModelTest)
#include "tst_JobQueueViewModel.moc"
