import QtQuick
import QtQuick.Controls.impl as ControlsImpl

Item {
    id: root
    property url source
    property color color: SemanticTokens.text
    property int iconSize: 20
    implicitWidth: iconSize
    implicitHeight: iconSize
    Accessible.ignored: true

    ControlsImpl.IconImage {
        anchors.fill: parent
        source: root.source
        sourceSize.width: Math.ceil(width * Math.max(1, Screen.devicePixelRatio))
        sourceSize.height: Math.ceil(height * Math.max(1, Screen.devicePixelRatio))
        color: root.color
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
}
