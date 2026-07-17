import QtQuick
import QtQuick.Effects

Item {
    id: root
    property url source
    property color color: SemanticTokens.text
    property int iconSize: 20
    implicitWidth: iconSize
    implicitHeight: iconSize
    Accessible.ignored: true

    Image {
        id: sourceImage
        anchors.fill: parent
        source: root.source
        sourceSize.width: Math.ceil(width * Math.max(1, Screen.devicePixelRatio))
        sourceSize.height: Math.ceil(height * Math.max(1, Screen.devicePixelRatio))
        fillMode: Image.PreserveAspectFit
        smooth: true
        visible: false
    }

    MultiEffect {
        anchors.fill: sourceImage
        source: sourceImage
        colorization: 1.0
        colorizationColor: root.color
    }
}
