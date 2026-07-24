pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var vm
    objectName: "queuePage"
    readonly property int headerStackWidth: 760
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
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingLg
        spacing: SemanticTokens.spacingMd
        PageHeader {
            objectName: "queueHeader"
            actionsObjectName: "queueHeaderActions"
            Layout.fillWidth: true
            stackWidth: root.headerStackWidth
            title: qsTr("Job Queue")
            subtitle: qsTr("One transcription runs at a time; completed chunks are saved immediately.")
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
            description: qsTr("Open a recording from Library and add it to the transcription queue.")
        }
        ListView {
            objectName: "jobList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.vm.empty
            model: root.vm.jobs
            spacing: SemanticTokens.spacingSm
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
        subtitle: qsTr("Its transcript version, partial results, and activity history will be deleted. This cannot be undone.")
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
        subtitle: qsTr("Completed and cancelled transcript versions and their activity history will be deleted. This cannot be undone.")
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/trash-2.svg"
        destructive: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: root.vm.clearCompleted()
    }
}
