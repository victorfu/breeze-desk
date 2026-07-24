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
    signal transcribeRequested(string id)
    signal trashRequested(string id)
    signal renameRequested(string id, string title)
    signal revealRequested(string id)
    signal relinkRequested(string id)
    signal editTagsRequested(string id, var tags)
    signal reviewRequested(string id, bool reviewed)
    readonly property string displayedStatus: UiText.recordingStatus(status)
    readonly property int metadataRowHeight: 24
    implicitHeight: Math.max(72, contentItem.implicitHeight + topPadding + bottomPadding)
    padding: ComponentTokens.cardPadding
    Accessible.name: title + ", " + displayedStatus
    Accessible.description: qsTr("Open recording details")
    onClicked: openRequested(recordingId)
    Keys.onReturnPressed: openRequested(recordingId)
    Keys.onEnterPressed: openRequested(recordingId)
    background: Rectangle {
        color: control.hovered || control.highlighted ? SemanticTokens.surfaceMuted
                                                      : SemanticTokens.surface
        radius: ComponentTokens.cardRadius
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 1
        border.color: control.activeFocus ? SemanticTokens.focusRing
                    : control.highlighted ? SemanticTokens.borderStrong : SemanticTokens.border
    }
    contentItem: Item {
        id: cardContent

        implicitHeight: Math.max(details.implicitHeight, actionRow.implicitHeight)

        Rectangle {
            id: mediaMarker
            anchors.left: parent.left
            anchors.top: parent.top
            width: 36
            height: 36
            radius: SemanticTokens.radiusMd
            color: SemanticTokens.accentMuted
            Rectangle {
                anchors.centerIn: parent
                width: 16
                height: 3
                radius: 2
                color: SemanticTokens.accentStrong
            }
        }

        ColumnLayout {
            id: details
            anchors.left: mediaMarker.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: SemanticTokens.spacingSm
            anchors.rightMargin: actionRow.implicitWidth + SemanticTokens.spacingSm
            spacing: SemanticTokens.spacingXs

            Text {
                Layout.fillWidth: true
                text: control.title
                color: SemanticTokens.text
                elide: Text.ElideRight
                font.pixelSize: SemanticTokens.bodySize
                font.weight: SemanticTokens.weightSemiBold
            }

            RowLayout {
                id: metadata
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: SemanticTokens.spacingXs

                TimeCode {
                    Layout.minimumWidth: implicitWidth
                    milliseconds: control.durationMs
                    enabled: false
                }
                StatusBadge {
                    Layout.minimumWidth: implicitWidth
                    text: control.displayedStatus
                    tone: control.status === "Completed" ? "success" : "neutral"
                }
                StatusBadge {
                    Layout.minimumWidth: implicitWidth
                    text: control.reviewState.toLowerCase() === "reviewed"
                          ? qsTr("Reviewed") : qsTr("Unreviewed")
                    tone: control.reviewState.toLowerCase() === "reviewed" ? "success" : "neutral"
                }
                Text {
                    visible: control.modelName.length > 0
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: implicitWidth
                    Layout.preferredHeight: control.metadataRowHeight
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    text: control.modelName
                    verticalAlignment: Text.AlignVCenter
                    color: SemanticTokens.textMuted
                    font.pixelSize: SemanticTokens.captionSize
                }
                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: implicitWidth
                    Layout.preferredHeight: control.metadataRowHeight
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    text: UiText.shortDateTime(control.createdAt)
                    verticalAlignment: Text.AlignVCenter
                    color: SemanticTokens.textMuted
                    font.pixelSize: SemanticTokens.captionSize
                }
            }

            Text {
                Layout.fillWidth: true
                visible: control.tags.length > 0
                text: control.tags.join(" · ")
                color: SemanticTokens.textMuted
                elide: Text.ElideRight
                font.pixelSize: SemanticTokens.captionSize
                Accessible.name: qsTr("Tags: %1").arg(text)
            }

            AppProgressBar {
                Layout.fillWidth: true
                visible: control.progress > 0 && control.progress < 1
                value: control.progress
            }
        }

        RowLayout {
            id: actionRow
            objectName: "recordingActionRow"
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 0

            IconButton {
                id: actionsButton
                objectName: "recordingActionsButton"
                accessibleName: qsTr("Actions for %1").arg(control.title)
                iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/ellipsis.svg"

                onClicked: actionsMenu.popup(
                               actionsButton,
                               actionsButton.width - actionsMenu.implicitWidth,
                               actionsButton.height + SemanticTokens.spacingXs)
            }
            RemoveButton {
                objectName: "recordingTrashButton"
                accessibleName: qsTr("Move %1 to Trash").arg(control.title)
                onClicked: control.trashRequested(control.recordingId)
            }
        }

        AppMenu {
            id: actionsMenu
            AppMenuItem {
                objectName: "recordingOpenMenuItem"
                text: qsTr("Open Recording")
                onTriggered: control.openRequested(control.recordingId)
            }
            AppMenuItem {
                objectName: "recordingTranscribeMenuItem"
                text: qsTr("Transcribe")
                onTriggered: control.transcribeRequested(control.recordingId)
            }
            AppMenuSeparator { }
            AppMenuItem {
                text: qsTr("Rename…")
                onTriggered: control.renameRequested(control.recordingId, control.title)
            }
            AppMenuItem {
                text: qsTr("Show in Finder / Explorer")
                onTriggered: control.revealRequested(control.recordingId)
            }
            AppMenuItem {
                text: control.sourceMissing ? qsTr("Relink Missing Source…") : qsTr("Relink Source…")
                onTriggered: control.relinkRequested(control.recordingId)
            }
            AppMenuSeparator { }
            AppMenuItem {
                text: qsTr("Edit Tags…")
                onTriggered: control.editTagsRequested(control.recordingId, control.tags)
            }
            AppMenuItem {
                text: control.reviewState.toLowerCase() === "reviewed"
                      ? qsTr("Mark as Unreviewed") : qsTr("Mark as Reviewed")
                onTriggered: control.reviewRequested(
                                 control.recordingId,
                                 control.reviewState.toLowerCase() !== "reviewed")
            }
        }
    }
}
