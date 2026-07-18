import QtQuick

IconButton {
    id: control

    readonly property url removeIconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/trash-2.svg"

    iconSource: removeIconSource
    iconColor: control.enabled ? SemanticTokens.danger : SemanticTokens.textMuted
}
