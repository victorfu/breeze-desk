pragma ComponentBehavior: Bound

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
        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Trash")
            subtitle: qsTr("Original source files are never deleted. Permanent delete removes only application-managed data and cache.")
        }
        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.vm.trashEmpty
            iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/trash-2.svg"
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
                id: trashRow
                required property string recordingId
                required property string title
                width: ListView.view.width
                height: Math.max(76, trashRowLayout.implicitHeight + SemanticTokens.spacingMd * 2)
                color: SemanticTokens.surface
                radius: SemanticTokens.radiusMd
                border.color: SemanticTokens.border
                RowLayout {
                    id: trashRowLayout
                    anchors.fill: parent
                    anchors.margins: SemanticTokens.spacingMd
                    Text {
                        Layout.fillWidth: true
                        text: trashRow.title
                        color: SemanticTokens.text
                        elide: Text.ElideRight
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.bodySize
                    }
                    IconButton {
                        objectName: "trashRestoreButton"
                        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/archive-restore.svg"
                        accessibleName: qsTr("Restore %1").arg(trashRow.title)
                        onClicked: root.vm.restore(trashRow.recordingId)
                    }
                    RemoveButton {
                        objectName: "trashDeletePermanentlyButton"
                        accessibleName: qsTr("Delete %1 permanently").arg(trashRow.title)
                        onClicked: { root.pendingDeleteId = trashRow.recordingId; confirmDelete.open() }
                    }
                }
            }
        }
    }
    AppDialog {
        id: confirmDelete
        title: qsTr("Delete recording permanently?")
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/trash-2.svg"
        destructive: true
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
