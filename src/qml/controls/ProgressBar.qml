import QtQuick
import QtQuick.Controls as T

T.ProgressBar {
    id: control
    implicitHeight: 8
    Accessible.name: qsTr("Progress")
    Accessible.description: qsTr("%1 percent").arg(Math.round(value * 100))
    background: Rectangle { color: SemanticTokens.border; radius: height / 2 }
    contentItem: Item {
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: height / 2
            color: SemanticTokens.accent
        }
    }
}
