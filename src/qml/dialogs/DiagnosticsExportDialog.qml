import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

AppDialog {
    id: control

    property bool includePaths: false
    signal exportRequested(url destination, bool includePersonalPaths)

    title: qsTr("Export Diagnostics")
    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/settings.svg"
    standardButtons: Dialog.NoButton

    ColumnLayout {
        width: parent.width
        spacing: SemanticTokens.spacingLg

        Text {
            Layout.fillWidth: true
            text: qsTr("The diagnostics archive contains application and worker logs, versions, and sanitized system information. It never includes audio, transcripts, or glossary terms.")
            color: SemanticTokens.text
            wrapMode: Text.Wrap
            font.pixelSize: SemanticTokens.bodySize
        }

        Toggle {
            Layout.fillWidth: true
            text: qsTr("Include personal file paths")
            checked: control.includePaths
            Accessible.description: qsTr("Off by default. Enable only when file paths are needed for troubleshooting.")
            onToggled: control.includePaths = checked
        }

        Text {
            Layout.fillWidth: true
            visible: control.includePaths
            text: qsTr("Personal paths may contain your user name and folder names. Review the archive before sharing it.")
            color: SemanticTokens.warning
            wrapMode: Text.Wrap
            font.pixelSize: SemanticTokens.captionSize
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancel")
                onClicked: control.close()
            }
            AppButton {
                text: qsTr("Choose Destination")
                primary: true
                onClicked: destinationDialog.open()
            }
        }
    }

    FileDialog {
        id: destinationDialog
        title: qsTr("Save Diagnostics Archive")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("ZIP archive (*.zip)")]
        defaultSuffix: "zip"
        onAccepted: {
            control.exportRequested(selectedFile, control.includePaths)
            control.close()
        }
    }
}
