import QtQuick
import QtQuick.Controls as T

T.Button {
    id: control

    property string accessibleName: text

    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    implicitHeight: Math.max(ComponentTokens.compactControlHeight,
                             contentItem.implicitHeight + topPadding + bottomPadding)
    leftPadding: SemanticTokens.spacingXs
    rightPadding: SemanticTokens.spacingXs
    topPadding: SemanticTokens.spacingXs
    bottomPadding: SemanticTokens.spacingXs
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: SemanticTokens.fontFamily
    font.pixelSize: SemanticTokens.bodySize

    Accessible.name: accessibleName
    Accessible.role: Accessible.Link

    contentItem: Text {
        text: control.text
        color: !control.enabled ? SemanticTokens.textMuted
                                : (control.hovered || control.down
                                   ? SemanticTokens.accentStrong : SemanticTokens.accent)
        font.family: control.font.family
        font.pixelSize: control.font.pixelSize
        font.underline: control.hovered || control.activeFocus
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        maximumLineCount: 1
    }

    background: Rectangle {
        color: "transparent"
        radius: SemanticTokens.radiusSm
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 0
        border.color: SemanticTokens.focusRing
        opacity: control.enabled ? 1.0 : 0.55
    }
}
