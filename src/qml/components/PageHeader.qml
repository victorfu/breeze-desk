import QtQuick
import QtQuick.Layouts

GridLayout {
    id: root

    property string title
    property string subtitle
    property real titlePixelSize: SemanticTokens.titleSize
    property int stackWidth: 760
    property alias actionsObjectName: actionsSlot.objectName
    default property alias actions: actionsSlot.data

    // Keep the breakpoint independent of the layout's implicit size. Child
    // widths change when the grid changes columns, so feeding them back into
    // this decision can make the layout oscillate indefinitely.
    readonly property bool stacked: width < stackWidth * DesignSystem.textScale

    columns: stacked ? 1 : 2
    columnSpacing: SemanticTokens.spacingMd
    rowSpacing: SemanticTokens.spacingSm

    ColumnLayout {
        Layout.row: 0
        Layout.column: 0
        Layout.fillWidth: true
        Layout.minimumWidth: 0

        Text {
            Layout.fillWidth: true
            text: root.title
            color: SemanticTokens.text
            wrapMode: Text.WordWrap
            font.family: SemanticTokens.fontFamily
            font.pixelSize: root.titlePixelSize
            font.weight: Font.DemiBold
            Accessible.role: Accessible.Heading
        }
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: root.subtitle.length > 0
            text: root.subtitle
            color: SemanticTokens.textMuted
            wrapMode: Text.WordWrap
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.bodySize
        }
    }

    RowLayout {
        id: actionsSlot
        visible: children.length > 0
        Layout.row: root.stacked ? 1 : 0
        Layout.column: root.stacked ? 0 : 1
        Layout.fillWidth: root.stacked
        Layout.minimumWidth: 0
        spacing: SemanticTokens.spacingSm
    }
}
