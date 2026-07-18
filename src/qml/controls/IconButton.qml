import QtQuick
import QtQuick.Controls as T

T.Button {
    id: control
    required property string accessibleName
    property url iconSource
    property color iconColor: control.enabled ? SemanticTokens.text : SemanticTokens.textMuted
    property color hoverTintColor: SemanticTokens.hoverTint
    property color pressedTintColor: SemanticTokens.pressedTint
    property string toolTipText: accessibleName
    property int iconSize: 20
    implicitWidth: ComponentTokens.clickTarget
    implicitHeight: ComponentTokens.clickTarget
    hoverEnabled: true
    Accessible.name: accessibleName
    Accessible.role: Accessible.Button
    T.ToolTip.visible: control.hovered && control.toolTipText.length > 0
    T.ToolTip.text: control.toolTipText
    T.ToolTip.delay: 500
    contentItem: AppIcon {
        source: control.iconSource
        iconSize: control.iconSize
        color: control.iconColor
    }
    background: Rectangle {
        radius: SemanticTokens.radiusSm
        color: control.down ? control.pressedTintColor
             : control.hovered ? control.hoverTintColor : "transparent"
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 0
        border.color: SemanticTokens.focusRing
        Behavior on color { ColorAnimation { duration: SemanticTokens.animationFast } }
    }
}
