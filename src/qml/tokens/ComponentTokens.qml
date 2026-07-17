pragma Singleton
import QtQuick

QtObject {
    readonly property int controlHeight: DesignSystem.compact ? 32 : 38
    readonly property int compactControlHeight: 30
    readonly property int clickTarget: 40
    readonly property int sidebarWidth: DesignSystem.compact ? 188 : 216
    readonly property int inspectorWidth: 292
    readonly property int cardPadding: SemanticTokens.spacingMd
    readonly property int cardSpacing: SemanticTokens.spacingMd
    readonly property int cardRadius: SemanticTokens.radiusMd
    readonly property int dialogWidth: 520
    readonly property int segmentMinHeight: DesignSystem.compact ? 72 : 88
    readonly property int waveformHeight: 118
    readonly property int focusWidth: 2
}
