import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control
    property bool selected: false
    property string badgeText: ""
    property url iconSource
    implicitHeight: ComponentTokens.clickTarget
    padding: SemanticTokens.spacingMd
    Accessible.name: text
    Accessible.description: selected ? qsTr("Selected") : ""
    background: Rectangle {
        radius: SemanticTokens.radiusSm
        color: control.selected ? SemanticTokens.accentMuted
                                : (control.hovered ? SemanticTokens.surfaceMuted : "transparent")
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 0
        border.color: SemanticTokens.focusRing
    }
    contentItem: RowLayout {
        spacing: SemanticTokens.spacingSm
        AppIcon {
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            source: control.iconSource
            iconSize: 20
            color: control.selected ? SemanticTokens.accentStrong : SemanticTokens.textMuted
        }
        Text {
            Layout.fillWidth: true
            text: control.text
            color: SemanticTokens.text
            elide: Text.ElideRight
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.bodySize
            font.weight: control.selected ? Font.DemiBold : Font.Normal
        }
        Text {
            id: badge
            visible: control.badgeText.length > 0
            text: control.badgeText
            color: SemanticTokens.textMuted
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.captionSize
        }
    }
}
