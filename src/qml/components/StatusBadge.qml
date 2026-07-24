import QtQuick

Rectangle {
    id: root
    property string text
    property string tone: "neutral"
    readonly property color toneColor: tone === "success" ? SemanticTokens.success
                                      : tone === "warning" ? SemanticTokens.warning
                                      : tone === "danger" ? SemanticTokens.danger
                                      : tone === "accent" ? SemanticTokens.accentStrong
                                      : SemanticTokens.textMuted
    implicitWidth: label.implicitWidth + SemanticTokens.spacingMd
    implicitHeight: 24
    radius: height / 2
    color: "transparent"
    border.color: toneColor
    Accessible.name: text
    Accessible.role: Accessible.StaticText
    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.toneColor
        font.pixelSize: SemanticTokens.captionSize
        font.weight: SemanticTokens.weightSemiBold
    }
}
