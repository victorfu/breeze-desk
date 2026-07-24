pragma Singleton
import QtQuick

QtObject {
    readonly property color window: DesignSystem.dark ? PrimitiveTokens.gray950 : PrimitiveTokens.gray25
    readonly property color surface: DesignSystem.dark ? PrimitiveTokens.gray900 : PrimitiveTokens.white
    readonly property color surfaceMuted: DesignSystem.dark ? PrimitiveTokens.gray950 : PrimitiveTokens.gray50
    readonly property color surfaceHover: DesignSystem.dark ? PrimitiveTokens.gray800 : PrimitiveTokens.gray100
    readonly property color surfaceRaised: DesignSystem.dark ? PrimitiveTokens.gray850 : PrimitiveTokens.white
    readonly property color border: DesignSystem.dark ? PrimitiveTokens.gray750 : PrimitiveTokens.gray200
    readonly property color borderStrong: DesignSystem.dark ? PrimitiveTokens.gray700 : PrimitiveTokens.gray400
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
    readonly property color hoverTint: DesignSystem.dark ? "#1AFFFFFF" : "#14171717"
    readonly property color pressedTint: DesignSystem.dark ? "#29FFFFFF" : "#21171717"
    readonly property color dangerHoverTint: "#1FC83D4B"
    readonly property color dangerPressedTint: "#33C83D4B"
    readonly property color focusRing: DesignSystem.dark ? PrimitiveTokens.blue300 : PrimitiveTokens.blue600
    readonly property color scrim: DesignSystem.dark ? "#99000000" : "#66171717"
    readonly property color shadow: PrimitiveTokens.black

    // Elevation.  A dark canvas swallows a black shadow, so the dark theme needs
    // markedly higher opacity than the light theme to read as the same lift.
    readonly property color shadowColor: PrimitiveTokens.shadowBase
    readonly property real elevation1Blur: PrimitiveTokens.blurTight
    readonly property int elevation1OffsetY: PrimitiveTokens.shadowOffsetTight
    readonly property real elevation1Opacity: DesignSystem.dark ? 0.28 : 0.06
    readonly property real elevation2Blur: PrimitiveTokens.blurMedium
    readonly property int elevation2OffsetY: PrimitiveTokens.shadowOffsetMedium
    readonly property real elevation2Opacity: DesignSystem.dark ? 0.40 : 0.10
    readonly property real elevation3Blur: PrimitiveTokens.blurWide
    readonly property int elevation3OffsetY: PrimitiveTokens.shadowOffsetWide
    readonly property real elevation3Opacity: DesignSystem.dark ? 0.52 : 0.14

    // There is deliberately no `fontFamily` token.  QML's font grouped property
    // exposes only a single `family`, and assigning it collapses QFont's family
    // list to one entry -- which would drop the CJK fallback chain that
    // DesignSystem::applyApplicationFontFallback() installs on the application
    // font.  Text items simply inherit that font instead.
    readonly property string fixedFontFamily: DesignSystem.systemFixedFontFamily
    readonly property real microSize: PrimitiveTokens.fontSizeMicro * DesignSystem.textScale
    readonly property real captionSize: PrimitiveTokens.fontSizeCaption * DesignSystem.textScale
    readonly property real bodySize: PrimitiveTokens.fontSizeBody * DesignSystem.textScale
    readonly property real subheadingSize: PrimitiveTokens.fontSizeSubheading * DesignSystem.textScale
    readonly property real headingSize: PrimitiveTokens.fontSizeHeading * DesignSystem.textScale
    readonly property real titleSize: PrimitiveTokens.fontSizeTitle * DesignSystem.textScale
    readonly property real titleLargeSize: PrimitiveTokens.fontSizeTitleLarge * DesignSystem.textScale
    readonly property int weightNormal: Font.Normal
    readonly property int weightMedium: Font.Medium
    readonly property int weightSemiBold: Font.DemiBold
    readonly property real lineHeightTight: PrimitiveTokens.lineHeightTight
    readonly property real lineHeightNormal: PrimitiveTokens.lineHeightNormal
    readonly property real trackingTight: PrimitiveTokens.trackingTight
    readonly property real trackingNormal: PrimitiveTokens.trackingNormal

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
    readonly property int animationSlow: PrimitiveTokens.durationSlow
    readonly property int easeStandard: PrimitiveTokens.easeStandard
    readonly property int easeEmphasis: PrimitiveTokens.easeEmphasis
    readonly property int easeExit: PrimitiveTokens.easeExit
}
