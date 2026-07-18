import QtQuick
import QtQuick.Templates as T

T.Slider {
    id: control

    implicitWidth: 160
    implicitHeight: ComponentTokens.clickTarget
    leftPadding: sliderHandle.implicitWidth / 2
    rightPadding: sliderHandle.implicitWidth / 2
    hoverEnabled: true

    background: Rectangle {
        id: sliderTrack
        objectName: "appSliderTrack"
        x: control.leftPadding
        y: control.topPadding + Math.round((control.availableHeight - height) / 2)
        width: control.availableWidth
        height: 6
        radius: height / 2
        color: SemanticTokens.borderStrong
        opacity: control.enabled ? 1 : 0.55

        Rectangle {
            objectName: "appSliderProgress"
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: SemanticTokens.accent
        }
    }

    handle: Rectangle {
        id: sliderHandle
        objectName: "appSliderHandle"
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + Math.round((control.availableHeight - height) / 2)
        implicitWidth: 20
        implicitHeight: 20
        radius: width / 2
        color: control.enabled ? SemanticTokens.surfaceRaised : SemanticTokens.surfaceMuted
        border.width: control.activeFocus || control.pressed ? 3 : 2
        border.color: control.activeFocus ? SemanticTokens.focusRing
                                            : (control.enabled ? SemanticTokens.accent
                                                               : SemanticTokens.borderStrong)

        Rectangle {
            anchors.centerIn: parent
            width: 6
            height: 6
            radius: width / 2
            color: control.enabled ? SemanticTokens.accent : SemanticTokens.borderStrong
        }
    }
}
