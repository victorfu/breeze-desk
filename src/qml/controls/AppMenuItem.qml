import QtQuick
import QtQuick.Controls as T

T.MenuItem {
    id: control

    implicitWidth: Math.max(220, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: ComponentTokens.clickTarget
    leftPadding: SemanticTokens.spacingMd
    rightPadding: SemanticTokens.spacingMd
    font.family: SemanticTokens.fontFamily
    font.pixelSize: SemanticTokens.bodySize
    hoverEnabled: true
    Accessible.name: text

    contentItem: Text {
        leftPadding: control.checkable ? SemanticTokens.spacingLg : 0
        text: control.text
        color: SemanticTokens.text
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        visible: control.checkable && control.checked
        x: SemanticTokens.spacingSm
        y: (control.height - height) / 2
        text: "✓"
        color: SemanticTokens.accent
        font.family: SemanticTokens.fontFamily
        font.pixelSize: SemanticTokens.bodySize
        font.weight: Font.DemiBold
    }

    background: Rectangle {
        radius: SemanticTokens.radiusSm
        color: control.down || control.highlighted ? SemanticTokens.accentMuted : "transparent"
    }
}
