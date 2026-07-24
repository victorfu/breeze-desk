pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var vm
    signal customImportRequested
    objectName: "modelsPage"
    readonly property int responsiveStackWidth: 760

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingLg
        spacing: SemanticTokens.spacingMd
        PageHeader {
            objectName: "modelsHeader"
            actionsObjectName: "modelsHeaderActions"
            Layout.fillWidth: true
            stackWidth: root.responsiveStackWidth
            title: qsTr("Models")
            subtitle: qsTr("Models are downloaded only when you request them. Checksums are verified before loading.")
            AppButton {
                objectName: "modelsImportButton"
                text: qsTr("Import GGML Model")
                onClicked: root.customImportRequested()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: backendRow.implicitHeight + SemanticTokens.spacingMd * 2
            color: SemanticTokens.surfaceMuted
            radius: SemanticTokens.radiusMd
            GridLayout {
                id: backendRow
                objectName: "modelsBackendSummary"
                anchors.fill: parent
                anchors.margins: SemanticTokens.spacingMd
                columns: stacked ? 1 : 3
                columnSpacing: SemanticTokens.spacingMd
                rowSpacing: SemanticTokens.spacingXs
                readonly property bool stacked: width < root.responsiveStackWidth
                                                       * DesignSystem.textScale
                Text {
                    id: selectedBackend
                    objectName: "modelsPreferredCompute"
                    Layout.row: 0
                    Layout.column: 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Preferred local compute: %1")
                              .arg(UiText.computeBackendName(root.vm.selectedBackend))
                    color: SemanticTokens.text
                    wrapMode: Text.WordWrap
                    font.pixelSize: SemanticTokens.bodySize
                }
                Text {
                    id: actualBackend
                    objectName: "modelsCurrentCompute"
                    Layout.row: backendRow.stacked ? 1 : 0
                    Layout.column: backendRow.stacked ? 0 : 1
                    Layout.fillWidth: backendRow.stacked
                    Layout.minimumWidth: 0
                    text: qsTr("Currently using: %1")
                              .arg(UiText.activeComputeName(root.vm.actualBackend))
                    color: SemanticTokens.textMuted
                    wrapMode: Text.WordWrap
                    font.pixelSize: SemanticTokens.bodySize
                }
                Text {
                    id: runtimeVersion
                    Layout.row: backendRow.stacked ? 2 : 0
                    Layout.column: backendRow.stacked ? 0 : 2
                    Layout.fillWidth: backendRow.stacked
                    Layout.minimumWidth: 0
                    text: qsTr("whisper.cpp: %1").arg(root.vm.runtimeVersion)
                    color: SemanticTokens.textMuted
                    wrapMode: Text.WordWrap
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
                onDownloadRequested: function(id) { root.vm.download(id) }
                onPauseRequested: function(id) { root.vm.pause(id) }
                onResumeRequested: function(id) { root.vm.resume(id) }
                onCancelRequested: function(id) { root.vm.cancel(id) }
                onDeleteRequested: function(id) { root.vm.remove(id) }
                onVerifyRequested: function(id) { root.vm.verify(id) }
                onTestRequested: function(id) { root.vm.testModel(id) }
                onDefaultRequested: function(id) { root.vm.setDefaultModel(id) }
                onLicenseRequested: function(url) { Qt.openUrlExternally(url) }
                onSourceRequested: function(url) { Qt.openUrlExternally(url) }
            }
        }
    }
}
