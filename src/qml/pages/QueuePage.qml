import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var vm
    objectName: "queuePage"
    readonly property int headerStackWidth: 760
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingLg
        spacing: SemanticTokens.spacingMd
        GridLayout {
            id: queueHeader
            objectName: "queueHeader"
            Layout.fillWidth: true
            columns: stacked ? 1 : 2
            columnSpacing: SemanticTokens.spacingMd
            rowSpacing: SemanticTokens.spacingSm
            readonly property bool stacked: width < root.headerStackWidth * DesignSystem.textScale
            ColumnLayout {
                id: titleBlock
                Layout.row: 0
                Layout.column: 0
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Job Queue")
                    color: SemanticTokens.text
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.titleSize
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("One transcription runs at a time; completed chunks are saved immediately.")
                    color: SemanticTokens.textMuted
                    wrapMode: Text.WordWrap
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                }
            }
            RowLayout {
                id: headerActions
                objectName: "queueHeaderActions"
                Layout.row: queueHeader.stacked ? 1 : 0
                Layout.column: queueHeader.stacked ? 0 : 1
                Layout.fillWidth: queueHeader.stacked
                Layout.minimumWidth: 0
                spacing: SemanticTokens.spacingSm
                Toggle {
                    objectName: "queuePauseAfterCurrentToggle"
                    text: qsTr("Pause after current job")
                    checked: root.vm.pauseAfterCurrent
                    onToggled: root.vm.pauseAfterCurrent = checked
                }
                RemoveButton {
                    objectName: "queueClearCompletedButton"
                    accessibleName: qsTr("Clear Completed")
                    onClicked: root.vm.clearCompleted()
                }
            }
        }
        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.vm.empty
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
            ScrollBar.vertical: ScrollBar { }
            delegate: JobProgress {
                width: ListView.view.width
                onCancelRequested: function(id) { root.vm.cancel(id) }
                onRetryRequested: function(id) { root.vm.retry(id) }
                onResumeRequested: function(id) { root.vm.resume(id) }
                onRemoveRequested: function(id) { root.vm.remove(id) }
            }
        }
    }
}
