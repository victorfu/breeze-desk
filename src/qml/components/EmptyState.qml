import QtQuick
import QtQuick.Layouts

Item {
    id: root
    property string title
    property string description
    property string actionText
    property url iconSource: ""
    signal actionTriggered
    implicitWidth: 400
    implicitHeight: content.implicitHeight
    Accessible.name: title + ". " + description
    ColumnLayout {
        id: content
        anchors.centerIn: parent
        width: Math.min(420, root.width - SemanticTokens.spacingXl * 2)
        spacing: SemanticTokens.spacingMd
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            radius: 24
            color: SemanticTokens.accentMuted
            AppIcon {
                visible: String(root.iconSource).length > 0
                anchors.centerIn: parent
                source: root.iconSource
                iconSize: 22
                color: SemanticTokens.accentStrong
            }
            Rectangle {
                visible: String(root.iconSource).length === 0
                anchors.centerIn: parent
                width: 18
                height: 3
                radius: 2
                color: SemanticTokens.accentStrong
            }
        }
        Text {
            Layout.fillWidth: true
            text: root.title
            horizontalAlignment: Text.AlignHCenter
            color: SemanticTokens.text
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.headingSize
            font.weight: Font.DemiBold
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
            visible: root.actionText.length > 0
            text: root.actionText
            primary: true
            onClicked: root.actionTriggered()
        }
    }
}
