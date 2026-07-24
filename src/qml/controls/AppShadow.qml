import QtQuick
import QtQuick.Effects

// Drop shadow for a rounded surface.  Anchor it to the same geometry as the
// surface and draw the surface on top:
//
//     background: Item {
//         AppShadow { anchors.fill: parent; level: 3; radius: SemanticTokens.radiusLg }
//         Rectangle { anchors.fill: parent; radius: SemanticTokens.radiusLg; ... }
//     }
//
// The MultiEffect paints a hidden caster rectangle plus its shadow; the real
// surface then covers the caster pixels exactly, leaving only the shadow
// visible.  The caster uses the surface colour so the antialiased rim of the
// surface never lets a foreign colour bleed through.
Item {
    id: root

    // 1 = resting card, 2 = menu / popover, 3 = dialog / toast.
    property int level: 2
    property real radius: SemanticTokens.radiusMd
    property color surfaceColor: SemanticTokens.surfaceRaised

    Rectangle {
        id: caster
        anchors.fill: parent
        radius: root.radius
        color: root.surfaceColor
        visible: false
    }

    MultiEffect {
        anchors.fill: caster
        source: caster
        shadowEnabled: true
        shadowColor: SemanticTokens.shadowColor
        shadowBlur: root.level === 1 ? SemanticTokens.elevation1Blur
                  : root.level === 3 ? SemanticTokens.elevation3Blur
                                     : SemanticTokens.elevation2Blur
        shadowVerticalOffset: root.level === 1 ? SemanticTokens.elevation1OffsetY
                            : root.level === 3 ? SemanticTokens.elevation3OffsetY
                                               : SemanticTokens.elevation2OffsetY
        shadowOpacity: root.level === 1 ? SemanticTokens.elevation1Opacity
                     : root.level === 3 ? SemanticTokens.elevation3Opacity
                                        : SemanticTokens.elevation2Opacity
    }
}
