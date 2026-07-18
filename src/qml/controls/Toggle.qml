import QtQuick
import QtQuick.Controls as T

T.Switch {
    id: control
    property string accessibleName: text
    implicitHeight: ComponentTokens.clickTarget
    hoverEnabled: true
    Accessible.name: accessibleName
    font.family: SemanticTokens.fontFamily
    font.pixelSize: SemanticTokens.bodySize
    contentItem: Text {
        leftPadding: control.indicator.width + SemanticTokens.spacingMd
        text: control.text
        color: control.enabled ? SemanticTokens.text : SemanticTokens.textMuted
        font: control.font
        elide: Text.ElideRight
        maximumLineCount: 1
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Rectangle {
        x: 0
        anchors.verticalCenter: parent.verticalCenter
        implicitWidth: 42
        implicitHeight: 24
        radius: 12
        color: control.checked
               ? (control.hovered && control.enabled ? SemanticTokens.accentStrong : SemanticTokens.accent)
               : (control.hovered && control.enabled ? SemanticTokens.textMuted : SemanticTokens.borderStrong)
        opacity: control.enabled ? 1.0 : 0.5
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 0
        border.color: SemanticTokens.focusRing
        Behavior on color { ColorAnimation { duration: SemanticTokens.animationFast } }
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
