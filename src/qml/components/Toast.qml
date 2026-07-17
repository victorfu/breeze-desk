import QtQuick
import QtQuick.Controls as T

T.Popup {
    id: control
    property string message
    signal dismissed
    modal: false
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    padding: SemanticTokens.spacingMd
    background: Rectangle {
        color: SemanticTokens.surfaceRaised
        radius: SemanticTokens.radiusMd
        border.color: SemanticTokens.borderStrong
    }
    contentItem: Row {
        Accessible.name: control.message
        Accessible.role: Accessible.AlertMessage
        spacing: SemanticTokens.spacingMd
        Text {
            text: control.message
            color: SemanticTokens.text
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.bodySize
            wrapMode: Text.Wrap
            width: Math.min(420, implicitWidth)
        }
        AppButton {
            text: qsTr("Dismiss")
            Accessible.name: qsTr("Dismiss notification")
            onClicked: { control.close(); control.dismissed() }
        }
    }
    Timer {
        interval: 5000
        running: control.opened
        onTriggered: { control.close(); control.dismissed() }
    }
}
