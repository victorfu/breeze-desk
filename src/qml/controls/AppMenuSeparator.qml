import QtQuick
import QtQuick.Controls as T

T.MenuSeparator {
    id: control

    padding: SemanticTokens.spacingXs
    verticalPadding: SemanticTokens.spacingSm

    contentItem: Rectangle {
        implicitWidth: 220
        implicitHeight: 1
        color: SemanticTokens.border
    }
}
