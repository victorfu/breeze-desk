import QtQuick
import QtQuick.Controls as T

T.TextField {
    id: control
    property string accessibleName: placeholderText
    implicitHeight: ComponentTokens.controlHeight
    leftPadding: SemanticTokens.spacingMd
    rightPadding: SemanticTokens.spacingMd
    hoverEnabled: true
    color: SemanticTokens.text
    placeholderTextColor: SemanticTokens.textMuted
    selectionColor: SemanticTokens.accentMuted
    selectedTextColor: SemanticTokens.text
    font.pixelSize: SemanticTokens.bodySize
    Accessible.name: accessibleName
    background: Rectangle {
        color: SemanticTokens.surface
        radius: SemanticTokens.radiusSm
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 1
        border.color: control.activeFocus ? SemanticTokens.focusRing
                    : control.hovered ? SemanticTokens.borderStrong : SemanticTokens.border
        Behavior on border.color {
            ColorAnimation { duration: SemanticTokens.animationFast; easing.type: SemanticTokens.easeStandard }
        }
    }
}
