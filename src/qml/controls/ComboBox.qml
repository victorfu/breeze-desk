import QtQuick
import QtQuick.Controls as T

T.ComboBox {
    id: control
    property string accessibleName: ""
    implicitHeight: ComponentTokens.controlHeight
    leftPadding: SemanticTokens.spacingMd
    rightPadding: SemanticTokens.spacingLg
    font.family: SemanticTokens.fontFamily
    font.pixelSize: SemanticTokens.bodySize
    Accessible.name: accessibleName
    contentItem: Text {
        text: control.displayText
        color: SemanticTokens.text
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        color: SemanticTokens.surface
        radius: SemanticTokens.radiusSm
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 1
        border.color: control.activeFocus ? SemanticTokens.focusRing : SemanticTokens.border
    }
}
