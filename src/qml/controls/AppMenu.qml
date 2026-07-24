import QtQuick
import QtQuick.Controls as T

T.Menu {
    id: control

    implicitWidth: Math.max(236, implicitContentWidth + leftPadding + rightPadding)
    margins: SemanticTokens.spacingSm
    overlap: SemanticTokens.spacingXs
    padding: SemanticTokens.spacingXs
    font.pixelSize: SemanticTokens.bodySize

    palette.window: SemanticTokens.surfaceRaised
    palette.windowText: SemanticTokens.text
    palette.text: SemanticTokens.text
    palette.highlight: SemanticTokens.accentMuted
    palette.highlightedText: SemanticTokens.text
    palette.mid: SemanticTokens.border
    palette.dark: SemanticTokens.borderStrong
    palette.shadow: SemanticTokens.shadow

    enter: Transition {
        NumberAnimation {
            property: "opacity"; from: 0.0; to: 1.0
            duration: SemanticTokens.animationNormal
            easing.type: SemanticTokens.easeStandard
        }
        NumberAnimation {
            property: "scale"; from: 0.97; to: 1.0
            duration: SemanticTokens.animationNormal
            easing.type: SemanticTokens.easeStandard
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"; from: 1.0; to: 0.0
            duration: SemanticTokens.animationFast
            easing.type: SemanticTokens.easeExit
        }
    }

    background: Item {
        implicitWidth: 236
        AppShadow {
            anchors.fill: parent
            level: 2
            radius: SemanticTokens.radiusMd
        }
        Rectangle {
            objectName: "appMenuSurface"
            anchors.fill: parent
            color: SemanticTokens.surfaceRaised
            radius: SemanticTokens.radiusMd
            border.width: 1
            border.color: SemanticTokens.border
        }
    }
}
