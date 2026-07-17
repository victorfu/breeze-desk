import QtQuick
import QtQuick.Controls as T

T.Button {
    id: control
    property string accessibleName: text
    property bool primary: false
    property url iconSource
    implicitHeight: ComponentTokens.controlHeight
    implicitWidth: Math.max(92, contentItem.implicitWidth + SemanticTokens.spacingLg * 2)
    padding: SemanticTokens.spacingSm
    font.family: SemanticTokens.fontFamily
    font.pixelSize: SemanticTokens.bodySize
    Accessible.name: accessibleName
    Accessible.role: Accessible.Button
    contentItem: Item {
        implicitWidth: buttonContents.implicitWidth
        implicitHeight: buttonContents.implicitHeight
        Row {
            id: buttonContents
            anchors.centerIn: parent
            spacing: SemanticTokens.spacingXs
            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: String(control.iconSource).length > 0
                source: control.iconSource
                iconSize: 18
                color: control.primary ? SemanticTokens.textOnAccent : SemanticTokens.text
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                color: control.primary ? SemanticTokens.textOnAccent : SemanticTokens.text
                font: control.font
                elide: Text.ElideRight
            }
        }
    }
    background: Rectangle {
        radius: SemanticTokens.radiusSm
        color: control.primary
               ? (control.down ? SemanticTokens.accentStrong : SemanticTokens.accent)
               : (control.down ? SemanticTokens.surfaceMuted : SemanticTokens.surface)
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 1
        border.color: control.activeFocus ? SemanticTokens.focusRing : SemanticTokens.border
        opacity: control.enabled ? 1.0 : 0.5
        Behavior on color { ColorAnimation { duration: SemanticTokens.animationFast } }
    }
}
