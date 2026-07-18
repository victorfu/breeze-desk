import QtQuick
import QtQuick.Controls as T

T.ComboBox {
    id: control
    property string accessibleName: ""

    implicitHeight: ComponentTokens.controlHeight
    hoverEnabled: true
    leftPadding: SemanticTokens.spacingMd
    rightPadding: SemanticTokens.spacingXl
    font.family: SemanticTokens.fontFamily
    font.pixelSize: SemanticTokens.bodySize
    Accessible.name: accessibleName

    contentItem: Text {
        text: control.displayText
        color: SemanticTokens.text
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: AppIcon {
        objectName: "appComboBoxIndicator"
        x: control.width - width - SemanticTokens.spacingSm
        y: Math.round((control.height - height) / 2)
        width: 16
        height: 16
        iconSize: 16
        source: control.popup.visible
                ? "qrc:/qt/qml/BreezeDesk/icons/lucide/chevron-up.svg"
                : "qrc:/qt/qml/BreezeDesk/icons/lucide/chevron-down.svg"
        color: control.enabled ? SemanticTokens.textMuted : SemanticTokens.borderStrong
    }

    background: Rectangle {
        objectName: "appComboBoxSurface"
        color: control.down || control.popup.visible ? SemanticTokens.surfaceMuted
             : control.hovered ? SemanticTokens.surfaceHover : SemanticTokens.surface
        radius: SemanticTokens.radiusSm
        border.width: control.activeFocus ? ComponentTokens.focusWidth : 1
        border.color: control.activeFocus ? SemanticTokens.focusRing : SemanticTokens.border
        Behavior on color { ColorAnimation { duration: SemanticTokens.animationFast } }
    }

    delegate: T.ItemDelegate {
        id: option
        required property int index

        width: ListView.view ? ListView.view.width : control.width
        implicitHeight: ComponentTokens.controlHeight
        leftPadding: SemanticTokens.spacingMd
        rightPadding: SemanticTokens.spacingMd
        highlighted: control.highlightedIndex === index
        hoverEnabled: true
        Accessible.name: control.textAt(index)

        contentItem: Text {
            text: control.textAt(option.index)
            color: SemanticTokens.text
            font.family: control.font.family
            font.pixelSize: control.font.pixelSize
            font.weight: control.currentIndex === option.index ? Font.DemiBold : Font.Normal
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: SemanticTokens.radiusSm
            color: option.down || option.highlighted || option.hovered
                   ? SemanticTokens.accentMuted : "transparent"
        }
    }

    popup: T.Popup {
        y: control.height + SemanticTokens.spacingXs
        width: Math.max(control.width, 160)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                         320 * DesignSystem.textScale)
        topMargin: SemanticTokens.spacingSm
        bottomMargin: SemanticTokens.spacingSm
        padding: SemanticTokens.spacingXs
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollIndicator.vertical: T.ScrollIndicator { }
        }

        background: Rectangle {
            objectName: "appComboBoxPopupSurface"
            implicitWidth: 160
            color: SemanticTokens.surfaceRaised
            radius: SemanticTokens.radiusMd
            border.width: 1
            border.color: SemanticTokens.border
        }
    }
}
