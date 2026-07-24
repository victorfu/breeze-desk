pragma Singleton
import QtQuick

QtObject {
    readonly property color white: "#FFFFFF"
    readonly property color black: "#171717"

    // Neutral grey ramp.  Values are deliberately untinted so the blue accent
    // stays the only chromatic element in the interface.  Higher number = darker;
    // gray700 and below are only ever used by the dark theme.
    readonly property color gray25: "#FAFAFB"
    readonly property color gray50: "#F5F5F5"
    readonly property color gray100: "#EAEAEA"
    readonly property color gray200: "#D9D9D9"
    readonly property color gray400: "#949494"
    readonly property color gray600: "#575757"
    readonly property color gray700: "#4E4E4E"
    readonly property color gray750: "#3A3A3A"
    readonly property color gray800: "#323232"
    readonly property color gray850: "#2A2A2A"
    readonly property color gray900: "#242424"
    readonly property color gray950: "#1E1E1E"

    readonly property color blue100: "#DFE8FF"
    readonly property color blue300: "#93ACF5"
    readonly property color blue500: "#4B70E2"
    readonly property color blue600: "#365BC8"
    readonly property color blue700: "#28469D"
    readonly property color teal500: "#198A86"
    readonly property color green500: "#23885A"
    readonly property color amber500: "#B76B00"
    readonly property color red500: "#C83D4B"
    readonly property color red600: "#A83240"

    // Shadows are cast in pure black and shaped by opacity alone, so a single
    // base colour works for both themes.
    readonly property color shadowBase: "#000000"

    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 20
    readonly property int space6: 24
    readonly property int space8: 32
    readonly property int space10: 40
    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 16

    readonly property int durationFast: 110
    readonly property int durationNormal: 180
    readonly property int durationSlow: 240

    // MultiEffect expresses blur as a 0..1 fraction of its maximum radius.
    readonly property real blurTight: 0.25
    readonly property real blurMedium: 0.45
    readonly property real blurWide: 0.75
    readonly property int shadowOffsetTight: 1
    readonly property int shadowOffsetMedium: 3
    readonly property int shadowOffsetWide: 8

    readonly property int easeStandard: Easing.OutCubic
    readonly property int easeEmphasis: Easing.OutBack
    readonly property int easeExit: Easing.InQuad

    readonly property int fontSizeMicro: 11
    readonly property int fontSizeCaption: 12
    readonly property int fontSizeBody: 14
    readonly property int fontSizeSubheading: 15
    readonly property int fontSizeHeading: 17
    readonly property int fontSizeTitle: 22
    readonly property int fontSizeTitleLarge: 28

    readonly property real lineHeightTight: 1.3
    readonly property real lineHeightNormal: 1.5
    readonly property real trackingTight: -0.2
    readonly property real trackingNormal: 0.0
}
