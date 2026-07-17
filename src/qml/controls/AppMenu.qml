import QtQuick
import QtQuick.Controls as T

T.Menu {
    id: control

    implicitWidth: Math.max(236, implicitContentWidth + leftPadding + rightPadding)
    margins: SemanticTokens.spacingSm
    overlap: SemanticTokens.spacingXs
    padding: SemanticTokens.spacingXs
    font.family: SemanticTokens.fontFamily
    font.pixelSize: SemanticTokens.bodySize

    palette.window: SemanticTokens.surfaceRaised
    palette.windowText: SemanticTokens.text
    palette.text: SemanticTokens.text
    palette.highlight: SemanticTokens.accentMuted
    palette.highlightedText: SemanticTokens.text
    palette.mid: SemanticTokens.border
    palette.dark: SemanticTokens.borderStrong
    palette.shadow: SemanticTokens.shadow

    background: Rectangle {
        objectName: "appMenuSurface"
        implicitWidth: 236
        color: SemanticTokens.surfaceRaised
        radius: SemanticTokens.radiusMd
        border.width: 1
        border.color: SemanticTokens.border
    }
}
