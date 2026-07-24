import QtQuick
import "." as Controls

Controls.AppTextField {
    id: control
    Accessible.name: qsTr("Search")
    placeholderText: qsTr("Search")
    inputMethodHints: Qt.ImhNoPredictiveText
    leftPadding: 38 * DesignSystem.textScale
    rightPadding: clearButton.visible ? 38 * DesignSystem.textScale : SemanticTokens.spacingMd

    Controls.AppIcon {
        anchors.left: parent.left
        anchors.leftMargin: SemanticTokens.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        width: 16 * DesignSystem.textScale
        height: width
        iconSize: 16
        source: "qrc:/qt/qml/BreezeDesk/icons/lucide/search.svg"
        color: control.activeFocus ? SemanticTokens.accentStrong : SemanticTokens.textMuted
    }

    Controls.IconButton {
        id: clearButton
        objectName: control.objectName.length > 0
                    ? control.objectName + "ClearButton" : "searchFieldClearButton"
        anchors.right: parent.right
        anchors.rightMargin: 3
        anchors.verticalCenter: parent.verticalCenter
        width: 32 * DesignSystem.textScale
        height: width
        visible: control.text.length > 0
        iconSize: 16
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/x.svg"
        accessibleName: qsTr("Clear search")
        onClicked: {
            control.clear()
            control.textEdited()
            control.forceActiveFocus()
        }
    }
}
