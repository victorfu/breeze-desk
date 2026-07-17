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
    required property bool canRemove
    signal cancelRequested(string id)
    signal retryRequested(string id)
    signal resumeRequested(string id)
    signal removeRequested(string id)

    function userFacingStatus() {
        if (root.errorMessage === "This build does not include whisper.cpp"
                || root.errorMessage === "whisper.cpp is disabled in this build") {
            return qsTr("Speech recognition is unavailable in this build. Use a build that includes whisper.cpp, then retry this job.")
        }
        return root.errorMessage.length > 0 ? root.errorMessage : root.stage
    }

    implicitHeight: content.implicitHeight + SemanticTokens.spacingLg * 2
    color: SemanticTokens.surface
    radius: ComponentTokens.cardRadius
    border.color: SemanticTokens.border
    Accessible.name: title + ", " + jobState + ", " + Math.round(progress * 100) + "%"
    Accessible.description: userFacingStatus()
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
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.userFacingStatus()
            color: root.errorMessage.length > 0 ? SemanticTokens.danger : SemanticTokens.textMuted
            wrapMode: Text.WordWrap
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.captionSize
            Accessible.role: root.errorMessage.length > 0 ? Accessible.AlertMessage : Accessible.StaticText
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.canCancel || root.canRetry || root.canResume || root.canRemove
            Item { Layout.fillWidth: true }
            AppButton { visible: root.canCancel; text: qsTr("Cancel"); onClicked: root.cancelRequested(root.jobId) }
            AppButton { visible: root.canRetry; text: qsTr("Retry"); onClicked: root.retryRequested(root.jobId) }
            AppButton { visible: root.canResume; text: qsTr("Resume"); onClicked: root.resumeRequested(root.jobId) }
            RemoveButton {
                objectName: "jobRemoveButton"
                visible: root.canRemove
                accessibleName: qsTr("Remove %1 from queue").arg(root.title)
                onClicked: root.removeRequested(root.jobId)
            }
        }
    }
}
