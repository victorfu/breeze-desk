import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var vm
    objectName: "trashPage"
    property string pendingDeleteId: ""
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingLg
        spacing: SemanticTokens.spacingMd
        Text {
            text: qsTr("Trash")
            color: SemanticTokens.text
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.titleSize
            font.weight: Font.DemiBold
        }
        Text {
            text: qsTr("Original source files are never deleted. Permanent delete removes only application-managed data and cache.")
            color: SemanticTokens.textMuted
            wrapMode: Text.Wrap
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.bodySize
        }
        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.vm.trashEmpty
            title: qsTr("Trash is empty")
            description: qsTr("Recordings moved here can be restored until permanently deleted.")
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.vm.trashEmpty
            model: root.vm.trash
            spacing: SemanticTokens.spacingSm
            clip: true
            reuseItems: true
            delegate: Rectangle {
                required property string recordingId
                required property string title
                width: ListView.view.width
                height: 76
                color: SemanticTokens.surface
                radius: SemanticTokens.radiusMd
                border.color: SemanticTokens.border
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: SemanticTokens.spacingMd
                    Text {
                        Layout.fillWidth: true
                        text: title
                        color: SemanticTokens.text
                        elide: Text.ElideRight
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.bodySize
                    }
                    AppButton { text: qsTr("Restore"); onClicked: root.vm.restore(recordingId) }
                    AppButton {
                        text: qsTr("Delete Permanently")
                        onClicked: { root.pendingDeleteId = recordingId; confirmDelete.open() }
                    }
                }
            }
        }
    }
    AppDialog {
        id: confirmDelete
        implicitHeight: 220
        height: 220
        title: qsTr("Delete recording permanently?")
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: root.vm.deletePermanently(root.pendingDeleteId)
        Text {
            width: parent.width
            text: qsTr("The transcript, managed media copy, and cache will be deleted. Your original source file stays untouched.")
            wrapMode: Text.Wrap
            color: SemanticTokens.text
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.bodySize
        }
    }
}
