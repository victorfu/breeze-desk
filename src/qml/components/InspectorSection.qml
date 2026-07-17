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
        font.family: SemanticTokens.fontFamily
        font.pixelSize: SemanticTokens.captionSize
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
    }
    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: SemanticTokens.spacingSm
    }
}
