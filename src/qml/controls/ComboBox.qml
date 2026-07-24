import QtQuick
import QtQuick.Controls as T

T.ComboBox {
    id: control
    property string accessibleName: ""

    implicitHeight: ComponentTokens.controlHeight
    hoverEnabled: true
    leftPadding: SemanticTokens.spacingMd
    rightPadding: SemanticTokens.spacingXl
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
        // Tracks the popup's *intent* rather than `visible`, which lingers for
        // the duration of the exit transition and would leave the chevron
        // pointing up while the popup fades out.
        property bool popupShown: false
        Connections {
            target: control.popup
            function onAboutToShow() { control.indicator.popupShown = true }
            function onAboutToHide() { control.indicator.popupShown = false }
        }
        x: control.width - width - SemanticTokens.spacingSm
        y: Math.round((control.height - height) / 2)
        width: 16
        height: 16
        iconSize: 16
        source: popupShown
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
        border.color: control.activeFocus ? SemanticTokens.focusRing
                    : control.hovered ? SemanticTokens.borderStrong : SemanticTokens.border
        Behavior on color { ColorAnimation { duration: SemanticTokens.animationFast; easing.type: SemanticTokens.easeStandard } }
        Behavior on border.color {
            ColorAnimation { duration: SemanticTokens.animationFast; easing.type: SemanticTokens.easeStandard }
        }
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
            font.pixelSize: control.font.pixelSize
            font.weight: control.currentIndex === option.index ? SemanticTokens.weightSemiBold : SemanticTokens.weightNormal
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

        enter: Transition {
            NumberAnimation {
                property: "opacity"; from: 0.0; to: 1.0
                duration: SemanticTokens.animationNormal
                easing.type: SemanticTokens.easeStandard
            }
            NumberAnimation {
                property: "scale"; from: 0.97; to: 1.0
                duration: SemanticTokens.animationNormal
                easing.type: SemanticTokens.easeStandard
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"; from: 1.0; to: 0.0
                duration: SemanticTokens.animationFast
                easing.type: SemanticTokens.easeExit
            }
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollIndicator.vertical: T.ScrollIndicator { }
        }

        background: Item {
            implicitWidth: 160
            AppShadow {
                anchors.fill: parent
                level: 2
                radius: SemanticTokens.radiusMd
            }
            Rectangle {
                objectName: "appComboBoxPopupSurface"
                anchors.fill: parent
                color: SemanticTokens.surfaceRaised
                radius: SemanticTokens.radiusMd
                border.width: 1
                border.color: SemanticTokens.border
            }
        }
    }
}
