import QtQuick
import QtQuick.Controls as T
import QtQuick.Layouts

T.Button {
    id: control
    property string accessibleName: text
    property bool primary: false
    property bool danger: false
    property string toolTipText: ""
    property url iconSource
    property int contentAlignment: Qt.AlignHCenter
    property int iconSize: 18
    property int contentSpacing: SemanticTokens.spacingXs
    implicitHeight: Math.max(ComponentTokens.controlHeight,
                             contentItem.implicitHeight + topPadding + bottomPadding)
    implicitWidth: Math.max(92, contentItem.implicitWidth + SemanticTokens.spacingLg * 2)
    padding: SemanticTokens.spacingSm
    font.family: SemanticTokens.fontFamily
    font.pixelSize: SemanticTokens.bodySize
    hoverEnabled: true
    Accessible.name: accessibleName
    Accessible.role: Accessible.Button
    T.ToolTip.visible: control.hovered && control.toolTipText.length > 0
    T.ToolTip.text: control.toolTipText
    T.ToolTip.delay: 500
    contentItem: Item {
        implicitWidth: buttonContents.implicitWidth
        implicitHeight: buttonContents.implicitHeight
        clip: true
        RowLayout {
            id: buttonContents
            anchors.fill: parent
            // Keep spacing between the icon and label only.  Applying RowLayout
            // spacing to the centering fillers also makes compact icon-only
            // buttons wider than their content area and clips the icon.
            spacing: 0
            Item {
                visible: control.contentAlignment === Qt.AlignHCenter
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
            AppIcon {
                id: icon
                objectName: control.objectName.length > 0 ? control.objectName + "Icon" : ""
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: control.iconSize
                Layout.preferredHeight: control.iconSize
                visible: String(control.iconSource).length > 0
                source: control.iconSource
                iconSize: control.iconSize
                color: control.primary || control.danger ? SemanticTokens.textOnAccent : SemanticTokens.text
            }
            Text {
                id: label
                visible: text.length > 0
                Layout.alignment: Qt.AlignVCenter
                Layout.fillWidth: control.contentAlignment !== Qt.AlignHCenter
                Layout.minimumWidth: 0
                Layout.preferredWidth: implicitWidth
                Layout.leftMargin: icon.visible ? control.contentSpacing : 0
                text: control.text
                color: control.primary || control.danger ? SemanticTokens.textOnAccent : SemanticTokens.text
                font: control.font
                elide: Text.ElideRight
                maximumLineCount: 1
            }
            Item {
                visible: control.contentAlignment === Qt.AlignHCenter
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
        }
    }
    background: Rectangle {
        radius: SemanticTokens.radiusSm
        color: control.danger
               ? (control.down || control.hovered ? SemanticTokens.dangerStrong : SemanticTokens.danger)
               : control.primary
                 ? (control.down || control.hovered ? SemanticTokens.accentStrong : SemanticTokens.accent)
                 : (control.down || control.hovered ? SemanticTokens.surfaceHover : SemanticTokens.surface)
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 1
        border.color: control.activeFocus ? SemanticTokens.focusRing : SemanticTokens.border
        opacity: control.enabled ? 1.0 : 0.5
        Behavior on color { ColorAnimation { duration: SemanticTokens.animationFast } }
    }
}
