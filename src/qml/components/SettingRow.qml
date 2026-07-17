import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root
    property string label
    property string description
    default property alias control: controlSlot.data
    Layout.fillWidth: true
    spacing: SemanticTokens.spacingLg
    ColumnLayout {
        Layout.fillWidth: true
        spacing: SemanticTokens.spacingXs
        Text {
            Layout.fillWidth: true
            text: root.label
            color: SemanticTokens.text
            wrapMode: Text.Wrap
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.bodySize
        }
        Text {
            Layout.fillWidth: true
            visible: root.description.length > 0
            text: root.description
            color: SemanticTokens.textMuted
            wrapMode: Text.Wrap
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.captionSize
        }
    }
    RowLayout { id: controlSlot; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter }
}
