import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var vm
    signal customImportRequested
    objectName: "modelsPage"
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingLg
        spacing: SemanticTokens.spacingMd
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("Models")
                    color: SemanticTokens.text
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.titleSize
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("Models are downloaded only when you request them. Checksums are verified before loading.")
                    color: SemanticTokens.textMuted
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                }
            }
            AppButton { text: qsTr("Import GGML Model"); onClicked: root.customImportRequested() }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: backendRow.implicitHeight + SemanticTokens.spacingMd * 2
            color: SemanticTokens.surfaceMuted
            radius: SemanticTokens.radiusMd
            RowLayout {
                id: backendRow
                anchors.fill: parent
                anchors.margins: SemanticTokens.spacingMd
                Text {
                    text: qsTr("Selected backend: %1").arg(root.vm.selectedBackend)
                    color: SemanticTokens.text
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("Actual backend: %1").arg(root.vm.actualBackend)
                    color: SemanticTokens.textMuted
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                }
                Text {
                    text: qsTr("whisper.cpp: %1").arg(root.vm.runtimeVersion)
                    color: SemanticTokens.textMuted
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                }
            }
        }
        ListView {
            objectName: "modelList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.vm.models
            spacing: SemanticTokens.spacingMd
            clip: true
            reuseItems: true
            ScrollBar.vertical: ScrollBar { }
            delegate: ModelCard {
                width: ListView.view.width
                onDownloadRequested: root.vm.download(id)
                onPauseRequested: root.vm.pause(id)
                onResumeRequested: root.vm.resume(id)
                onCancelRequested: root.vm.cancel(id)
                onDeleteRequested: root.vm.remove(id)
                onVerifyRequested: root.vm.verify(id)
                onTestRequested: root.vm.testModel(id)
                onDefaultRequested: root.vm.setDefaultModel(id)
                onLicenseRequested: function(url) { Qt.openUrlExternally(url) }
                onSourceRequested: function(url) { Qt.openUrlExternally(url) }
            }
        }
    }
}
