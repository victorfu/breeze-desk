import QtQuick
import QtQuick.Controls as T
import QtQuick.Layouts

T.Button {
    id: control

    property bool selected: false
    property string badgeText: ""

    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    implicitHeight: Math.max(ComponentTokens.clickTarget,
                             contentItem.implicitHeight + topPadding + bottomPadding)
    leftPadding: SemanticTokens.spacingMd
    rightPadding: SemanticTokens.spacingMd
    topPadding: SemanticTokens.spacingSm
    bottomPadding: SemanticTokens.spacingSm
    hoverEnabled: true

    Accessible.name: text
    Accessible.description: selected ? qsTr("Selected") : ""
    Accessible.role: Accessible.PageTab

    contentItem: RowLayout {
        spacing: SemanticTokens.spacingXs

        Text {
            objectName: control.objectName + "Label"
            text: control.text
            color: control.selected ? SemanticTokens.accentStrong : SemanticTokens.text
            font.pixelSize: SemanticTokens.bodySize
            font.weight: control.selected ? SemanticTokens.weightSemiBold
                                          : SemanticTokens.weightMedium
            maximumLineCount: 1
            elide: Text.ElideRight
        }
        Rectangle {
            visible: control.badgeText.length > 0
            Layout.preferredWidth: Math.max(20 * DesignSystem.textScale,
                                            badgeLabel.implicitWidth + SemanticTokens.spacingSm)
            Layout.preferredHeight: 20 * DesignSystem.textScale
            radius: height / 2
            color: SemanticTokens.accentMuted

            Text {
                id: badgeLabel
                anchors.centerIn: parent
                text: control.badgeText
                color: SemanticTokens.accentStrong
                font.pixelSize: SemanticTokens.captionSize
                font.weight: SemanticTokens.weightSemiBold
            }
        }
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: SemanticTokens.radiusSm
            color: control.down ? SemanticTokens.pressedTint
                 : control.hovered ? SemanticTokens.surfaceHover : "transparent"
        }
        Rectangle {
            visible: control.selected || control.activeFocus
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            width: Math.min(32 * DesignSystem.textScale, parent.width - SemanticTokens.spacingMd)
            height: 3
            radius: 2
            color: control.selected ? SemanticTokens.accentStrong : SemanticTokens.focusRing
        }
    }
}
