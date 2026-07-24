import QtQuick
import QtQuick.Templates as T
import BreezeDesk

// Style override for every scroll bar in the application: a thin overlay
// thumb on a transparent rail that widens under the pointer and fades out
// when scrolling stops.  The hit area stays a constant 12px so layouts never
// shift; only the drawn thumb grows.
T.ScrollBar {
    id: control

    readonly property bool expanded: control.hovered || control.pressed
    readonly property real thumbThickness: expanded ? 8 : 4

    implicitWidth: control.vertical ? 12 : 0
    implicitHeight: control.horizontal ? 12 : 0
    padding: 2
    minimumSize: 0.1
    visible: control.policy !== T.ScrollBar.AlwaysOff
    hoverEnabled: true

    opacity: control.policy === T.ScrollBar.AlwaysOn
             || (control.active && control.size < 1.0) ? 1.0 : 0.0
    Behavior on opacity {
        NumberAnimation { duration: SemanticTokens.animationNormal; easing.type: SemanticTokens.easeStandard }
    }

    contentItem: Item {
        implicitWidth: control.vertical ? 12 : 0
        implicitHeight: control.horizontal ? 12 : 0

        Rectangle {
            // Vertical: hug the right edge, stretch top-to-bottom, animate width.
            // Horizontal: hug the bottom edge, stretch left-to-right, animate height.
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.top: control.vertical ? parent.top : undefined
            anchors.left: control.horizontal ? parent.left : undefined
            width: control.vertical ? control.thumbThickness : 0
            height: control.horizontal ? control.thumbThickness : 0
            radius: Math.min(width, height) / 2
            color: control.pressed ? SemanticTokens.textMuted : SemanticTokens.borderStrong
            Behavior on width {
                enabled: control.vertical
                NumberAnimation { duration: SemanticTokens.animationFast; easing.type: SemanticTokens.easeStandard }
            }
            Behavior on height {
                enabled: control.horizontal
                NumberAnimation { duration: SemanticTokens.animationFast; easing.type: SemanticTokens.easeStandard }
            }
            Behavior on color {
                ColorAnimation { duration: SemanticTokens.animationFast; easing.type: SemanticTokens.easeStandard }
            }
        }
    }
}
