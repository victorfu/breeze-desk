pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var vm
    signal backRequested
    objectName: "queuePage"
    readonly property int headerStackWidth: 760
    readonly property real contentMaximumWidth: 1440
    property string pendingRemoveJobId: ""
    property string pendingRemoveJobTitle: ""

    function requestJobRemoval(jobId, title) {
        pendingRemoveJobId = jobId
        pendingRemoveJobTitle = title
        removeJobDialog.open()
    }

    function clearPendingJobRemoval() {
        pendingRemoveJobId = ""
        pendingRemoveJobTitle = ""
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: SemanticTokens.spacingLg
        anchors.bottomMargin: SemanticTokens.spacingLg
        width: Math.max(0, Math.min(root.contentMaximumWidth,
                                    root.width - SemanticTokens.spacingLg * 2))
        spacing: SemanticTokens.spacingMd
        AppLinkButton {
            objectName: "queueBackButton"
            Layout.alignment: Qt.AlignLeft
            text: qsTr("← Library")
            accessibleName: qsTr("Back to Library")
            onClicked: root.backRequested()
        }
        PageHeader {
            objectName: "queueHeader"
            actionsObjectName: "queueHeaderActions"
            Layout.fillWidth: true
            stackWidth: root.headerStackWidth
            title: qsTr("Job Queue")
            subtitle: qsTr("Transcriptions run one at a time. You can continue using the app while they finish.")
            Toggle {
                objectName: "queuePauseAfterCurrentToggle"
                text: qsTr("Pause after current job")
                checked: root.vm.pauseAfterCurrent
                onToggled: root.vm.pauseAfterCurrent = checked
            }
            RemoveButton {
                objectName: "queueClearCompletedButton"
                accessibleName: qsTr("Permanently remove completed and cancelled jobs")
                onClicked: removeFinishedDialog.open()
            }
        }
        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.vm.empty
            iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/list-ordered.svg"
            title: qsTr("The queue is empty")
            description: qsTr("New automatic transcriptions and manual retries will appear here.")
        }
        ListView {
            objectName: "jobList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.vm.empty
            model: root.vm.jobs
            spacing: SemanticTokens.spacingXs
            clip: true
            reuseItems: true
            keyNavigationEnabled: true
            activeFocusOnTab: true
            ScrollBar.vertical: ScrollBar { }
            delegate: JobProgress {
                width: ListView.view.width
                onCancelRequested: function(id) { root.vm.cancel(id) }
                onRetryRequested: function(id) { root.vm.retry(id) }
                onResumeRequested: function(id) { root.vm.resume(id) }
                onMoveUpRequested: function(id) { root.vm.moveUp(id) }
                onMoveDownRequested: function(id) { root.vm.moveDown(id) }
                onReorderRequested: function(id, destination) { root.vm.reorder(id, destination) }
                onRemoveRequested: function(id) { root.requestJobRemoval(id, title) }
            }
        }
    }

    AppDialog {
        id: removeJobDialog
        objectName: "queueRemoveJobDialog"
        title: root.pendingRemoveJobTitle.length > 0
               ? qsTr("Remove %1 permanently?").arg(root.pendingRemoveJobTitle)
               : qsTr("Remove this job permanently?")
        subtitle: qsTr("Its partial transcript and activity history will be deleted. This cannot be undone.")
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/trash-2.svg"
        destructive: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            root.vm.remove(root.pendingRemoveJobId)
            root.clearPendingJobRemoval()
        }
        onRejected: root.clearPendingJobRemoval()
    }

    AppDialog {
        id: removeFinishedDialog
        objectName: "queueRemoveFinishedDialog"
        title: qsTr("Remove finished jobs permanently?")
        subtitle: qsTr("Completed and cancelled activity history will be deleted. Finished transcripts remain in the Library.")
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/trash-2.svg"
        destructive: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: root.vm.clearCompleted()
    }
}
