import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property string title
    default property alias content: body.data
    spacing: SemanticTokens.spacingSm
    Text {
        text: root.title
        color: SemanticTokens.text
        font.pixelSize: SemanticTokens.captionSize
        font.weight: SemanticTokens.weightSemiBold
        font.capitalization: Font.AllUppercase
    }
    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: SemanticTokens.spacingSm
    }
}
