import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property string title: qsTr("Something went wrong")
    property string description
    property string actionText: qsTr("Retry")
    signal actionTriggered
    spacing: SemanticTokens.spacingMd
    Accessible.name: title + ". " + description
    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: 40
        Layout.preferredHeight: 40
        radius: 20
        color: SemanticTokens.danger
        Text {
            anchors.centerIn: parent
            text: "!"
            color: SemanticTokens.textOnAccent
            font.bold: true
            font.pixelSize: SemanticTokens.headingSize
        }
    }
    Text {
        Layout.fillWidth: true
        text: root.title
        horizontalAlignment: Text.AlignHCenter
        color: SemanticTokens.text
        font.family: SemanticTokens.fontFamily
        font.pixelSize: SemanticTokens.headingSize
    }
    Text {
        Layout.fillWidth: true
        text: root.description
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: SemanticTokens.textMuted
        font.family: SemanticTokens.fontFamily
        font.pixelSize: SemanticTokens.bodySize
    }
    AppButton {
        Layout.alignment: Qt.AlignHCenter
        text: root.actionText
        onClicked: root.actionTriggered()
    }
}
