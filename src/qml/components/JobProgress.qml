import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    required property string jobId
    required property string title
    required property string jobState
    required property string stage
    required property real progress
    required property string errorMessage
    required property bool canCancel
    required property bool canRetry
    required property bool canResume
    signal cancelRequested(string id)
    signal retryRequested(string id)
    signal resumeRequested(string id)
    implicitHeight: content.implicitHeight + SemanticTokens.spacingLg * 2
    color: SemanticTokens.surface
    radius: ComponentTokens.cardRadius
    border.color: SemanticTokens.border
    Accessible.name: title + ", " + jobState + ", " + Math.round(progress * 100) + "%"
    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingLg
        spacing: SemanticTokens.spacingSm
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: root.title
                color: SemanticTokens.text
                elide: Text.ElideRight
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.bodySize
                font.weight: Font.DemiBold
            }
            StatusBadge {
                text: root.jobState
                tone: root.jobState === "Failed" ? "danger" : root.jobState === "Completed" ? "success" : "accent"
            }
        }
        AppProgressBar { Layout.fillWidth: true; value: root.progress }
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: root.errorMessage.length > 0 ? root.errorMessage : root.stage
                color: root.errorMessage.length > 0 ? SemanticTokens.danger : SemanticTokens.textMuted
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.captionSize
            }
            Item { Layout.fillWidth: true }
            AppButton { visible: root.canCancel; text: qsTr("Cancel"); onClicked: root.cancelRequested(root.jobId) }
            AppButton { visible: root.canRetry; text: qsTr("Retry"); onClicked: root.retryRequested(root.jobId) }
            AppButton { visible: root.canResume; text: qsTr("Resume"); onClicked: root.resumeRequested(root.jobId) }
        }
    }
}
