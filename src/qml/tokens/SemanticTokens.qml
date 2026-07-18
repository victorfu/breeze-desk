pragma Singleton
import QtQuick

QtObject {
    readonly property color window: DesignSystem.dark ? PrimitiveTokens.gray900 : PrimitiveTokens.gray25
    readonly property color surface: DesignSystem.dark ? PrimitiveTokens.gray800 : PrimitiveTokens.white
    readonly property color surfaceMuted: DesignSystem.dark ? PrimitiveTokens.gray900 : PrimitiveTokens.gray50
    readonly property color surfaceHover: DesignSystem.dark ? "#343B48" : PrimitiveTokens.gray100
    readonly property color surfaceRaised: DesignSystem.dark ? "#303641" : PrimitiveTokens.white
    readonly property color border: DesignSystem.dark ? "#434A58" : PrimitiveTokens.gray200
    readonly property color borderStrong: DesignSystem.dark ? PrimitiveTokens.gray600 : PrimitiveTokens.gray400
    readonly property color text: DesignSystem.dark ? PrimitiveTokens.gray25 : PrimitiveTokens.black
    readonly property color textMuted: DesignSystem.dark ? PrimitiveTokens.gray400 : PrimitiveTokens.gray600
    readonly property color textOnAccent: PrimitiveTokens.white
    readonly property color accent: DesignSystem.dark ? PrimitiveTokens.blue300 : PrimitiveTokens.blue500
    readonly property color accentStrong: DesignSystem.dark ? PrimitiveTokens.blue500 : PrimitiveTokens.blue600
    readonly property color accentMuted: DesignSystem.dark ? PrimitiveTokens.blue700 : PrimitiveTokens.blue100
    readonly property color success: PrimitiveTokens.green500
    readonly property color warning: PrimitiveTokens.amber500
    readonly property color danger: PrimitiveTokens.red500
    readonly property color dangerStrong: PrimitiveTokens.red600
    readonly property color waveform: DesignSystem.dark ? PrimitiveTokens.blue300 : PrimitiveTokens.blue500
    readonly property color playhead: PrimitiveTokens.red500
    readonly property color selection: DesignSystem.dark ? "#664B70E2" : "#334B70E2"
    readonly property color hoverTint: DesignSystem.dark ? "#1AFFFFFF" : "#14111318"
    readonly property color pressedTint: DesignSystem.dark ? "#29FFFFFF" : "#21111318"
    readonly property color dangerHoverTint: "#1FC83D4B"
    readonly property color dangerPressedTint: "#33C83D4B"
    readonly property color focusRing: DesignSystem.dark ? PrimitiveTokens.blue300 : PrimitiveTokens.blue600
    readonly property color shadow: PrimitiveTokens.black

    readonly property string fontFamily: DesignSystem.systemFontFamily
    readonly property string fixedFontFamily: DesignSystem.systemFixedFontFamily
    readonly property real bodySize: 14 * DesignSystem.textScale
    readonly property real captionSize: 12 * DesignSystem.textScale
    readonly property real titleSize: 22 * DesignSystem.textScale
    readonly property real headingSize: 17 * DesignSystem.textScale

    readonly property int spacingXs: PrimitiveTokens.space1
    readonly property int spacingSm: PrimitiveTokens.space2
    readonly property int spacingMd: PrimitiveTokens.space4
    readonly property int spacingLg: PrimitiveTokens.space6
    readonly property int spacingXl: PrimitiveTokens.space8
    readonly property int radiusSm: PrimitiveTokens.radiusSmall
    readonly property int radiusMd: PrimitiveTokens.radiusMedium
    readonly property int radiusLg: PrimitiveTokens.radiusLarge
    readonly property int animationFast: PrimitiveTokens.durationFast
    readonly property int animationNormal: PrimitiveTokens.durationNormal
}
