import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ItemDelegate {
    id: control
    required property string recordingId
    required property string title
    required property int durationMs
    required property var createdAt
    required property string status
    required property string modelName
    required property var tags
    required property string reviewState
    required property real progress
    required property bool sourceMissing
    signal openRequested(string id)
    signal trashRequested(string id)
    signal renameRequested(string id, string title)
    signal revealRequested(string id)
    signal relinkRequested(string id)
    signal editTagsRequested(string id, var tags)
    signal reviewRequested(string id, bool reviewed)
    implicitHeight: 116
    padding: SemanticTokens.spacingMd
    Accessible.name: title + ", " + status
    onClicked: openRequested(recordingId)
    background: Rectangle {
        color: control.hovered ? SemanticTokens.surfaceMuted : SemanticTokens.surface
        radius: ComponentTokens.cardRadius
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 1
        border.color: control.activeFocus ? SemanticTokens.focusRing : SemanticTokens.border
    }
    contentItem: RowLayout {
        spacing: SemanticTokens.spacingMd
        Rectangle {
            Layout.preferredWidth: 42
            Layout.preferredHeight: 42
            radius: SemanticTokens.radiusMd
            color: SemanticTokens.accentMuted
            Rectangle {
                anchors.centerIn: parent
                width: 18
                height: 3
                radius: 2
                color: SemanticTokens.accentStrong
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: SemanticTokens.spacingXs
            Text {
                Layout.fillWidth: true
                text: control.title
                color: SemanticTokens.text
                elide: Text.ElideRight
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.bodySize
                font.weight: Font.DemiBold
            }
            RowLayout {
                spacing: SemanticTokens.spacingSm
                TimeCode { milliseconds: control.durationMs; enabled: false }
                StatusBadge { text: control.status; tone: control.status === "Completed" ? "success" : "neutral" }
                StatusBadge {
                    text: control.reviewState.toLowerCase() === "reviewed"
                          ? qsTr("Reviewed") : qsTr("Unreviewed")
                    tone: control.reviewState.toLowerCase() === "reviewed" ? "success" : "neutral"
                }
                Text {
                    visible: control.modelName.length > 0
                    text: control.modelName
                    color: SemanticTokens.textMuted
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.captionSize
                }
                Text {
                    text: Qt.locale().toString(control.createdAt, Locale.ShortFormat)
                    color: SemanticTokens.textMuted
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.captionSize
                }
            }
            Text {
                Layout.fillWidth: true
                visible: control.tags.length > 0
                text: control.tags.join(" · ")
                color: SemanticTokens.textMuted
                elide: Text.ElideRight
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.captionSize
                Accessible.name: qsTr("Tags: %1").arg(text)
            }
            AppProgressBar {
                Layout.fillWidth: true
                visible: control.progress > 0 && control.progress < 1
                value: control.progress
            }
        }
        ColumnLayout {
            spacing: SemanticTokens.spacingXs
            AppButton {
                id: actionsButton
                text: qsTr("Actions")
                Accessible.name: qsTr("Actions for %1").arg(control.title)
                onClicked: actionsMenu.popup()
            }
            AppButton {
                text: qsTr("Trash")
                Accessible.name: qsTr("Move %1 to Trash").arg(control.title)
                onClicked: control.trashRequested(control.recordingId)
            }
        }
        Menu {
            id: actionsMenu
            MenuItem {
                text: qsTr("Rename…")
                onTriggered: control.renameRequested(control.recordingId, control.title)
            }
            MenuItem {
                text: qsTr("Show in Finder / Explorer")
                onTriggered: control.revealRequested(control.recordingId)
            }
            MenuItem {
                text: control.sourceMissing ? qsTr("Relink Missing Source…") : qsTr("Relink Source…")
                onTriggered: control.relinkRequested(control.recordingId)
            }
            MenuSeparator { }
            MenuItem {
                text: qsTr("Edit Tags…")
                onTriggered: control.editTagsRequested(control.recordingId, control.tags)
            }
            MenuItem {
                text: control.reviewState.toLowerCase() === "reviewed"
                      ? qsTr("Mark as Unreviewed") : qsTr("Mark as Reviewed")
                onTriggered: control.reviewRequested(
                                 control.recordingId,
                                 control.reviewState.toLowerCase() !== "reviewed")
            }
        }
    }
}
