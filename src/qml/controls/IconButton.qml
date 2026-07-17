import QtQuick
import QtQuick.Controls as T

T.Button {
    id: control
    required property string accessibleName
    property url iconSource
    property color iconColor: control.enabled ? SemanticTokens.text : SemanticTokens.textMuted
    implicitWidth: ComponentTokens.clickTarget
    implicitHeight: ComponentTokens.clickTarget
    Accessible.name: accessibleName
    Accessible.role: Accessible.Button
    contentItem: AppIcon {
        source: control.iconSource
        iconSize: 20
        color: control.iconColor
    }
    background: Rectangle {
        radius: SemanticTokens.radiusSm
        color: control.down ? SemanticTokens.surfaceMuted : "transparent"
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 0
        border.color: SemanticTokens.focusRing
    }
}
