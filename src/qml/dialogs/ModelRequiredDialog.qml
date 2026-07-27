import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: control
    property var app
    objectName: "modelRequiredDialog"
    title: qsTr("No transcription model installed")
    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/box.svg"
    standardButtons: Dialog.NoButton
    ColumnLayout {
        width: parent.width
        spacing: SemanticTokens.spacingLg
        Text {
            Layout.fillWidth: true
            text: qsTr("The recommended model will download automatically when you start transcription for the first time.")
            color: SemanticTokens.text
            wrapMode: Text.Wrap
            font.pixelSize: SemanticTokens.bodySize
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "modelRequiredCancelButton"
                text: qsTr("Cancel")
                onClicked: control.close()
            }
            AppButton {
                objectName: "modelRequiredGoToModelsButton"
                text: qsTr("Manage Models")
                primary: true
                onClicked: {
                    control.close()
                    control.app.navigate("Models")
                }
            }
        }
    }
}
