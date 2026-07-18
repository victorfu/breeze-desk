import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control
    property int milliseconds: 0
    signal seekRequested(int position)
    implicitHeight: Math.max(control.enabled ? 28 : 22,
                             contentItem.implicitHeight + topPadding + bottomPadding)
    Layout.minimumHeight: implicitHeight
    padding: SemanticTokens.spacingXs
    text: formatTime(milliseconds)
    Accessible.name: enabled ? qsTr("Seek to %1").arg(text) : text
    font.family: SemanticTokens.fixedFontFamily
    font.pixelSize: SemanticTokens.captionSize
    function formatTime(value) {
        return UiText.timecode(value)
    }
    onClicked: seekRequested(milliseconds)
    HoverHandler {
        enabled: control.enabled
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        margin: Math.max(0, (ComponentTokens.clickTarget - control.height) / 2)
        onTapped: function(eventPoint) {
            if (!control.contains(eventPoint.position))
                control.seekRequested(control.milliseconds)
        }
    }
    background: Rectangle {
        color: control.down ? SemanticTokens.accentMuted
             : control.hovered ? SemanticTokens.hoverTint : "transparent"
        radius: SemanticTokens.radiusSm
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 0
        border.color: SemanticTokens.focusRing
    }
    contentItem: Text {
        text: control.text
        color: control.enabled ? SemanticTokens.accentStrong : SemanticTokens.textMuted
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
