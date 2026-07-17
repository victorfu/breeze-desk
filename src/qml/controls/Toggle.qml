import QtQuick
import QtQuick.Controls as T

T.Switch {
    id: control
    implicitHeight: ComponentTokens.clickTarget
    Accessible.name: text
    font.family: SemanticTokens.fontFamily
    font.pixelSize: SemanticTokens.bodySize
    contentItem: Text {
        leftPadding: control.indicator.width + SemanticTokens.spacingMd
        text: control.text
        color: SemanticTokens.text
        font: control.font
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Rectangle {
        x: 0
        anchors.verticalCenter: parent.verticalCenter
        implicitWidth: 42
        implicitHeight: 24
        radius: 12
        color: control.checked ? SemanticTokens.accent : SemanticTokens.borderStrong
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 0
        border.color: SemanticTokens.focusRing
        Rectangle {
            width: 18
            height: 18
            radius: 9
            y: 3
            x: control.checked ? parent.width - width - 3 : 3
            color: SemanticTokens.textOnAccent
            Behavior on x { NumberAnimation { duration: SemanticTokens.animationFast } }
        }
    }
}
