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
    readonly property string displayedStatus: UiText.recordingStatus(status)
    implicitHeight: Math.max(72, contentItem.implicitHeight + topPadding + bottomPadding)
    padding: SemanticTokens.spacingSm
    Accessible.name: title + ", " + displayedStatus
    Accessible.description: qsTr("Open recording details")
    onClicked: openRequested(recordingId)
    background: Rectangle {
        color: control.hovered ? SemanticTokens.surfaceMuted : SemanticTokens.surface
        radius: ComponentTokens.cardRadius
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 1
        border.color: control.activeFocus ? SemanticTokens.focusRing : SemanticTokens.border
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
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.bodySize
                font.weight: Font.DemiBold
            }

            Flow {
                id: metadata
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                spacing: SemanticTokens.spacingXs

                TimeCode {
                    milliseconds: control.durationMs
                    enabled: false
                }
                StatusBadge {
                    text: control.displayedStatus
                    tone: control.status === "Completed" ? "success" : "neutral"
                }
                StatusBadge {
                    text: control.reviewState.toLowerCase() === "reviewed"
                          ? qsTr("Reviewed") : qsTr("Unreviewed")
                    tone: control.reviewState.toLowerCase() === "reviewed" ? "success" : "neutral"
                }
                Text {
                    visible: control.modelName.length > 0
                    text: control.modelName
                    height: metadataRowHeight
                    verticalAlignment: Text.AlignVCenter
                    color: SemanticTokens.textMuted
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.captionSize
                    readonly property real metadataRowHeight: 28
                }
                Text {
                    text: UiText.shortDateTime(control.createdAt)
                    height: 28
                    verticalAlignment: Text.AlignVCenter
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

                ToolTip.visible: hovered
                ToolTip.text: accessibleName
                ToolTip.delay: 500

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
