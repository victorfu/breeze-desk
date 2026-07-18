import QtQuick
import QtQuick.Controls as T

T.SpinBox {
    id: control

    property string accessibleName: ""

    implicitWidth: Math.max(140, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: ComponentTokens.controlHeight
    leftPadding: downIndicator.width + SemanticTokens.spacingXs
    rightPadding: upIndicator.width + SemanticTokens.spacingXs
    hoverEnabled: true
    font.family: SemanticTokens.fontFamily
    font.pixelSize: SemanticTokens.bodySize
    Accessible.name: accessibleName

    contentItem: TextInput {
        text: control.displayText
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        color: control.enabled ? SemanticTokens.text : SemanticTokens.textMuted
        selectionColor: SemanticTokens.accentMuted
        selectedTextColor: SemanticTokens.text
        font: control.font
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        clip: true
    }

    down.indicator: Rectangle {
        id: downIndicator
        x: 0
        height: control.height
        width: ComponentTokens.controlHeight
        radius: SemanticTokens.radiusSm
        color: control.down.pressed ? SemanticTokens.pressedTint
             : control.down.hovered ? SemanticTokens.hoverTint : "transparent"
        AppIcon {
            anchors.centerIn: parent
            iconSize: 16
            source: "qrc:/qt/qml/BreezeDesk/icons/lucide/chevron-down.svg"
            color: control.enabled && control.value > control.from
                   ? SemanticTokens.text : SemanticTokens.borderStrong
        }
    }

    up.indicator: Rectangle {
        id: upIndicator
        x: control.width - width
        height: control.height
        width: ComponentTokens.controlHeight
        radius: SemanticTokens.radiusSm
        color: control.up.pressed ? SemanticTokens.pressedTint
             : control.up.hovered ? SemanticTokens.hoverTint : "transparent"
        AppIcon {
            anchors.centerIn: parent
            iconSize: 16
            source: "qrc:/qt/qml/BreezeDesk/icons/lucide/chevron-up.svg"
            color: control.enabled && control.value < control.to
                   ? SemanticTokens.text : SemanticTokens.borderStrong
        }
    }

    background: Rectangle {
        color: SemanticTokens.surface
        radius: SemanticTokens.radiusSm
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 1
        border.color: control.activeFocus ? SemanticTokens.focusRing : SemanticTokens.border
    }
}
