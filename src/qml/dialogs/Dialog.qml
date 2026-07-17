import QtQuick
import QtQuick.Controls as T

T.Dialog {
    id: control
    anchors.centerIn: T.Overlay.overlay
    width: Math.min(ComponentTokens.dialogWidth, T.Overlay.overlay.width - SemanticTokens.spacingXl * 2)
    modal: true
    dim: true
    padding: SemanticTokens.spacingLg
    closePolicy: T.Popup.CloseOnEscape
    background: Rectangle {
        color: SemanticTokens.surfaceRaised
        radius: SemanticTokens.radiusLg
        border.color: SemanticTokens.border
    }
}
