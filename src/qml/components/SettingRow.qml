import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root

    property string label
    property string description
    property real controlColumnWidth: ComponentTokens.inspectorWidth
    default property alias control: controlSlot.data

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    spacing: SemanticTokens.spacingLg

    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
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
            wrapMode: Text.WrapAnywhere
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.captionSize
        }
    }

    RowLayout {
        id: controlSlot
        visible: children.length > 0
        Layout.minimumWidth: root.controlColumnWidth
        Layout.preferredWidth: root.controlColumnWidth
        Layout.maximumWidth: root.controlColumnWidth
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        spacing: 0
    }
}
