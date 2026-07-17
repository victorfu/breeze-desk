import QtQuick
import QtQuick.Controls

IconButton {
    id: control

    readonly property url removeIconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/trash-2.svg"
    property string toolTipText: accessibleName

    iconSource: removeIconSource
    iconColor: control.enabled ? SemanticTokens.danger : SemanticTokens.textMuted

    ToolTip.visible: control.hovered && control.toolTipText.length > 0
    ToolTip.text: control.toolTipText
    ToolTip.delay: 500
}
