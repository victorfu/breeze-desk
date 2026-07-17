import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var vm
    objectName: "queuePage"
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingLg
        spacing: SemanticTokens.spacingMd
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("Job Queue")
                    color: SemanticTokens.text
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.titleSize
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("One transcription runs at a time; completed chunks are saved immediately.")
                    color: SemanticTokens.textMuted
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                }
            }
            Toggle {
                text: qsTr("Pause after current job")
                checked: root.vm.pauseAfterCurrent
                onToggled: root.vm.pauseAfterCurrent = checked
            }
            AppButton { text: qsTr("Clear Completed"); onClicked: root.vm.clearCompleted() }
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
                onCancelRequested: root.vm.cancel(id)
                onRetryRequested: root.vm.retry(id)
                onResumeRequested: root.vm.resume(id)
            }
        }
    }
}
