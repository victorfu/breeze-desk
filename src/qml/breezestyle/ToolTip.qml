import QtQuick
import QtQuick.Templates as T
import BreezeDesk

// Style override for every tool tip in the application, including the shared
// instance behind the `ToolTip.visible` attached API.
T.ToolTip {
    id: control

    x: parent ? Math.round((parent.width - implicitWidth) / 2) : 0
    y: -implicitHeight - SemanticTokens.spacingSm
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    margins: SemanticTokens.spacingSm
    topPadding: SemanticTokens.spacingSm
    bottomPadding: SemanticTokens.spacingSm
    leftPadding: SemanticTokens.spacingSm + SemanticTokens.spacingXs
    rightPadding: SemanticTokens.spacingSm + SemanticTokens.spacingXs
    delay: 500
    font.pixelSize: SemanticTokens.captionSize
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
                 | T.Popup.CloseOnReleaseOutsideParent

    enter: Transition {
        NumberAnimation {
            property: "opacity"; from: 0.0; to: 1.0
            duration: SemanticTokens.animationFast
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

    contentItem: Text {
        text: control.text
        font: control.font
        color: SemanticTokens.text
        wrapMode: Text.Wrap
    }

    background: Item {
        AppShadow {
            anchors.fill: parent
            level: 2
            radius: SemanticTokens.radiusSm
        }
        Rectangle {
            anchors.fill: parent
            color: SemanticTokens.surfaceRaised
            radius: SemanticTokens.radiusSm
            border.width: 1
            border.color: SemanticTokens.border
        }
    }
}
