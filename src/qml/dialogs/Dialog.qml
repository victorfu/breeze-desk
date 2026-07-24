import QtQuick
import QtQuick.Controls as T
import QtQuick.Layouts

T.Dialog {
    id: control
    property string subtitle: ""
    property url iconSource: ""
    property bool destructive: false
    property alias surfaceObjectName: surfaceRect.objectName
    property alias headerObjectName: headerRect.objectName
    property alias footerObjectName: footerBox.objectName

    anchors.centerIn: T.Overlay.overlay
    width: Math.min(ComponentTokens.dialogWidth, T.Overlay.overlay.width - SemanticTokens.spacingXl * 2)
    modal: true
    dim: true
    spacing: 0
    padding: SemanticTokens.spacingLg
    closePolicy: T.Popup.CloseOnEscape

    palette.window: SemanticTokens.surfaceRaised
    palette.windowText: SemanticTokens.text
    palette.base: SemanticTokens.surface
    palette.text: SemanticTokens.text
    palette.button: SemanticTokens.surface
    palette.buttonText: SemanticTokens.text
    palette.highlight: SemanticTokens.accentMuted
    palette.highlightedText: SemanticTokens.text
    palette.mid: SemanticTokens.surfaceMuted
    palette.dark: SemanticTokens.borderStrong
    palette.shadow: SemanticTokens.shadow

    enter: Transition {
        NumberAnimation {
            property: "opacity"; from: 0.0; to: 1.0
            duration: SemanticTokens.animationNormal
            easing.type: SemanticTokens.easeStandard
        }
        NumberAnimation {
            property: "scale"; from: 0.96; to: 1.0
            duration: SemanticTokens.animationSlow
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

    background: Item {
        AppShadow {
            anchors.fill: parent
            level: 3
            radius: SemanticTokens.radiusLg
        }
        Rectangle {
            id: surfaceRect
            objectName: "appDialogSurface"
            anchors.fill: parent
            color: SemanticTokens.surfaceRaised
            radius: SemanticTokens.radiusLg
            border.width: 1
            border.color: SemanticTokens.border
        }
    }

    header: Rectangle {
        id: headerRect
        objectName: "appDialogHeader"
        visible: control.title.length > 0
        implicitHeight: visible ? dialogHeaderLayout.implicitHeight + SemanticTokens.spacingLg * 2 : 0
        color: SemanticTokens.surfaceRaised
        radius: SemanticTokens.radiusLg

        RowLayout {
            id: dialogHeaderLayout
            anchors.fill: parent
            anchors.margins: SemanticTokens.spacingLg
            spacing: SemanticTokens.spacingMd

            Rectangle {
                Layout.preferredWidth: 42
                Layout.preferredHeight: 42
                Layout.alignment: Qt.AlignTop
                color: SemanticTokens.accentMuted
                radius: SemanticTokens.radiusMd

                AppIcon {
                    visible: String(control.iconSource).length > 0
                    anchors.centerIn: parent
                    source: control.iconSource
                    color: SemanticTokens.accent
                    iconSize: 22
                }
                Rectangle {
                    visible: String(control.iconSource).length === 0
                    anchors.centerIn: parent
                    width: 18
                    height: 4
                    radius: 2
                    color: SemanticTokens.accent
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: SemanticTokens.spacingXs

                Text {
                    Layout.fillWidth: true
                    text: control.title
                    color: SemanticTokens.text
                    wrapMode: Text.WordWrap
                    font.pixelSize: SemanticTokens.headingSize
                    font.weight: SemanticTokens.weightSemiBold
                    Accessible.role: Accessible.Heading
                }
                Text {
                    Layout.fillWidth: true
                    visible: control.subtitle.length > 0
                    text: control.subtitle
                    color: SemanticTokens.textMuted
                    wrapMode: Text.WordWrap
                    font.pixelSize: SemanticTokens.captionSize
                    lineHeight: SemanticTokens.lineHeightNormal
                    lineHeightMode: Text.ProportionalHeight
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: SemanticTokens.border
        }
    }

    footer: T.DialogButtonBox {
        id: footerBox
        objectName: "appDialogFooter"
        visible: count > 0
        spacing: SemanticTokens.spacingSm
        padding: SemanticTokens.spacingMd
        alignment: Qt.AlignRight

        delegate: AppButton {
            readonly property bool acceptish: T.DialogButtonBox.buttonRole === T.DialogButtonBox.AcceptRole
                                              || T.DialogButtonBox.buttonRole === T.DialogButtonBox.YesRole
                                              || T.DialogButtonBox.buttonRole === T.DialogButtonBox.ApplyRole
            primary: !control.destructive && acceptish
            danger: control.destructive && acceptish
        }

        background: Rectangle {
            color: SemanticTokens.surfaceRaised
            radius: SemanticTokens.radiusLg

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: SemanticTokens.border
            }
        }
    }

    T.Overlay.modal: Rectangle {
        color: SemanticTokens.scrim
    }
}
