import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: control

    required property var recorder
    property var settings: null
    property string errorMessage: ""

    signal startRequested()
    signal recordingCompleted(string path)

    title: qsTr("Record from Microphone")
    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/mic.svg"
    standardButtons: Dialog.NoButton
    closePolicy: recorder.recording ? Popup.NoAutoClose : Popup.CloseOnEscape

    function deviceIndex(deviceId) {
        for (let index = 0; index < recorder.inputDevices.length; ++index) {
            if (recorder.inputDevices[index].id === deviceId)
                return index
        }
        return recorder.inputDevices.length > 0 ? 0 : -1
    }

    onOpened: {
        errorMessage = ""
    }

    Connections {
        target: control.recorder

        function onRecordingError(message) {
            control.errorMessage = message
        }

        function onRecordingFinished(path) {
            control.recordingCompleted(path)
            control.close()
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: SemanticTokens.spacingLg

        SettingRow {
            Layout.fillWidth: true
            label: qsTr("Input device")
            description: qsTr("Recordings are saved as local PCM WAV files.")

            AppComboBox {
                id: deviceSelector
                Layout.preferredWidth: 260
                enabled: !control.recorder.recording && control.recorder.inputDevices.length > 0
                accessibleName: qsTr("Microphone input device")
                model: control.recorder.inputDevices
                textRole: "description"
                valueRole: "id"
                currentIndex: control.deviceIndex(control.recorder.selectedDeviceId)
                onActivated: control.recorder.selectedDeviceId = currentValue
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: statusColumn.implicitHeight + SemanticTokens.spacingLg * 2
            color: SemanticTokens.surfaceMuted
            radius: SemanticTokens.radiusMd

            ColumnLayout {
                id: statusColumn
                anchors.fill: parent
                anchors.margins: SemanticTokens.spacingLg
                spacing: SemanticTokens.spacingMd

                RowLayout {
                    Layout.fillWidth: true
                    StatusBadge {
                        text: control.recorder.paused ? qsTr("Paused")
                                                     : control.recorder.recording ? qsTr("Recording")
                                                                                  : qsTr("Ready")
                        tone: control.recorder.recording && !control.recorder.paused ? "accent" : "neutral"
                    }
                    Item { Layout.fillWidth: true }
                    TimeCode {
                        milliseconds: control.recorder.durationMs
                        enabled: false
                        Accessible.name: qsTr("Recording duration")
                    }
                }

                Text {
                    text: qsTr("Input level")
                    color: SemanticTokens.textMuted
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.captionSize
                }
                AppProgressBar {
                    Layout.fillWidth: true
                    value: Math.max(0, Math.min(1, control.recorder.level))
                    Accessible.name: qsTr("Microphone input level")
                }
            }
        }

        Toggle {
            objectName: "recordingAutoTranscribeToggle"
            Layout.fillWidth: true
            visible: control.settings !== null
            text: qsTr("Transcribe automatically after recording")
            checked: control.settings ? control.settings.autoTranscribeRecording : false
            onToggled: if (control.settings) control.settings.autoTranscribeRecording = checked
        }

        Text {
            Layout.fillWidth: true
            visible: control.errorMessage.length > 0
            text: control.errorMessage
            color: SemanticTokens.danger
            wrapMode: Text.Wrap
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.bodySize
            Accessible.role: Accessible.AlertMessage
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton {
                visible: !control.recorder.recording
                text: qsTr("Start Recording")
                primary: true
                enabled: control.recorder.inputDevices.length > 0
                onClicked: {
                    control.errorMessage = ""
                    control.startRequested()
                }
            }
            AppButton {
                visible: control.recorder.recording
                text: control.recorder.paused ? qsTr("Resume") : qsTr("Pause")
                onClicked: control.recorder.paused ? control.recorder.resume() : control.recorder.pause()
            }
            AppButton {
                visible: control.recorder.recording
                text: qsTr("Stop and Save")
                primary: true
                onClicked: control.recorder.stop()
            }
            AppButton {
                visible: !control.recorder.recording
                text: qsTr("Close")
                onClicked: control.close()
            }
        }
    }
}
