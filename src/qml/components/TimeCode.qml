import QtQuick
import QtQuick.Controls

Button {
    id: control
    property int milliseconds: 0
    signal seekRequested(int position)
    implicitHeight: 28
    padding: SemanticTokens.spacingXs
    text: formatTime(milliseconds)
    Accessible.name: qsTr("Seek to %1").arg(text)
    font.family: "monospace"
    font.pixelSize: SemanticTokens.captionSize
    function formatTime(value) {
        const total = Math.max(0, Math.floor(value / 1000))
        const hours = Math.floor(total / 3600)
        const minutes = Math.floor((total % 3600) / 60)
        const seconds = total % 60
        return (hours > 0 ? String(hours).padStart(2, "0") + ":" : "")
             + String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0")
    }
    onClicked: seekRequested(milliseconds)
    background: Rectangle {
        color: control.down ? SemanticTokens.accentMuted : "transparent"
        radius: SemanticTokens.radiusSm
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 0
        border.color: SemanticTokens.focusRing
    }
    contentItem: Text {
        text: control.text
        color: SemanticTokens.accentStrong
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
