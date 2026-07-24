import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property real value: 0
    property string statusText: ""
    property string detail: ""
    spacing: SemanticTokens.spacingXs
    AppProgressBar { Layout.fillWidth: true; value: root.value }
    RowLayout {
        Layout.fillWidth: true
        Text {
            text: root.statusText
            color: SemanticTokens.textMuted
            font.pixelSize: SemanticTokens.captionSize
        }
        Item { Layout.fillWidth: true }
        Text {
            text: root.detail.length > 0 ? root.detail : Math.round(root.value * 100) + "%"
            color: SemanticTokens.textMuted
            font.pixelSize: SemanticTokens.captionSize
        }
    }
}
