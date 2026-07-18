import QtQuick
import QtQuick.Controls as T

T.Popup {
    id: control
    property string message
    property string severity: "info"
    property string actionText: ""
    property var action: null
    signal dismissed
    signal actionTriggered
    readonly property color severityColor: severity === "success" ? SemanticTokens.success
                                         : severity === "danger" ? SemanticTokens.danger
                                         : SemanticTokens.accent
    readonly property url severityIconSource: severity === "success"
                                              ? "qrc:/qt/qml/BreezeDesk/icons/lucide/circle-check.svg"
                                              : severity === "danger"
                                                ? "qrc:/qt/qml/BreezeDesk/icons/lucide/circle-alert.svg"
                                                : "qrc:/qt/qml/BreezeDesk/icons/lucide/info.svg"
    modal: false
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    padding: SemanticTokens.spacingMd
    background: Rectangle {
        color: SemanticTokens.surfaceRaised
        radius: SemanticTokens.radiusMd
        border.color: SemanticTokens.borderStrong
        Rectangle {
            objectName: "appToastSeverityStrip"
            anchors.left: parent.left
            anchors.leftMargin: 1
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: SemanticTokens.radiusMd
            anchors.bottomMargin: SemanticTokens.radiusMd
            width: 3
            radius: 1.5
            color: control.severityColor
        }
    }
    contentItem: Row {
        Accessible.name: control.message
        Accessible.role: Accessible.AlertMessage
        spacing: SemanticTokens.spacingMd
        AppIcon {
            anchors.verticalCenter: parent.verticalCenter
            source: control.severityIconSource
            iconSize: 18
            color: control.severityColor
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: control.message
            color: SemanticTokens.text
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.bodySize
            wrapMode: Text.Wrap
            width: Math.min(420, implicitWidth)
        }
        AppButton {
            objectName: "appToastActionButton"
            anchors.verticalCenter: parent.verticalCenter
            visible: control.actionText.length > 0
            text: control.actionText
            onClicked: {
                control.actionTriggered()
                if (control.action)
                    control.action()
                control.close()
                control.dismissed()
            }
        }
        AppButton {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Dismiss")
            Accessible.name: qsTr("Dismiss notification")
            onClicked: { control.close(); control.dismissed() }
        }
    }
    Timer {
        interval: 5000
        running: control.opened
        onTriggered: { control.close(); control.dismissed() }
    }
}
