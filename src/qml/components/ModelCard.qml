import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property string modelId
    required property string displayName
    required property string description
    required property string quantization
    required property double fileSize
    required property string licenseName
    required property bool recommended
    required property bool defaultCandidate
    required property bool installed
    required property bool loaded
    required property bool isDefault
    required property string modelState
    required property real progress
    required property url licenseUrl
    required property url sourceUrl
    signal downloadRequested(string id)
    signal pauseRequested(string id)
    signal resumeRequested(string id)
    signal cancelRequested(string id)
    signal deleteRequested(string id)
    signal verifyRequested(string id)
    signal testRequested(string id)
    signal defaultRequested(string id)
    signal licenseRequested(url url)
    signal sourceRequested(url url)
    readonly property string displayedModelState: UiText.modelState(modelState)
    readonly property string displayedDescription: UiText.modelDescription(modelId, description)
    implicitHeight: card.implicitHeight + SemanticTokens.spacingLg * 2
    color: SemanticTokens.surface
    radius: ComponentTokens.cardRadius
    border.color: SemanticTokens.border
    Accessible.name: displayName + ", " + displayedModelState
    ColumnLayout {
        id: card
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingLg
        spacing: SemanticTokens.spacingMd
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: SemanticTokens.spacingXs
                RowLayout {
                    Text {
                        text: root.displayName
                        color: SemanticTokens.text
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.headingSize
                        font.weight: Font.DemiBold
                    }
                    StatusBadge { visible: root.recommended; text: qsTr("Recommended"); tone: "accent" }
                    StatusBadge { visible: root.isDefault; text: qsTr("Default"); tone: "success" }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.displayedDescription
                    color: SemanticTokens.textMuted
                    wrapMode: Text.Wrap
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                }
            }
            StatusBadge { text: root.quantization; tone: "neutral" }
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("%1 GB").arg((root.fileSize / 1000000000).toFixed(2))
                color: SemanticTokens.textMuted
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.captionSize
            }
            Text {
                text: qsTr("License: %1").arg(root.licenseName)
                color: SemanticTokens.textMuted
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.captionSize
            }
            AppLinkButton {
                objectName: "modelLicenseLink"
                text: qsTr("License")
                accessibleName: qsTr("Open model license")
                enabled: root.licenseUrl.toString().length > 0
                onClicked: root.licenseRequested(root.licenseUrl)
            }
            AppLinkButton {
                objectName: "modelSourceLink"
                text: qsTr("Source")
                accessibleName: qsTr("Open model source")
                enabled: root.sourceUrl.toString().length > 0
                onClicked: root.sourceRequested(root.sourceUrl)
            }
            Item { Layout.fillWidth: true }
            StatusBadge {
                text: root.loaded ? qsTr("Loaded") : root.installed ? qsTr("Installed") : qsTr("Not installed")
                tone: root.loaded || root.installed ? "success" : "neutral"
            }
        }
        DownloadProgress {
            Layout.fillWidth: true
            visible: root.modelState === "Downloading" || root.modelState === "Requested"
                     || root.modelState === "Paused" || root.modelState === "Verifying"
                     || root.modelState === "Testing"
            value: root.progress
            statusText: root.displayedModelState
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton {
                visible: !root.installed && root.modelState !== "Downloading"
                text: root.modelState === "Paused" ? qsTr("Resume") : qsTr("Download")
                primary: root.recommended
                onClicked: root.modelState === "Paused" ? root.resumeRequested(root.modelId) : root.downloadRequested(root.modelId)
            }
            AppButton {
                visible: root.modelState === "Downloading"
                text: qsTr("Pause")
                onClicked: root.pauseRequested(root.modelId)
            }
            AppButton {
                visible: root.modelState === "Downloading" || root.modelState === "Requested"
                         || root.modelState === "Paused"
                text: qsTr("Cancel")
                onClicked: root.cancelRequested(root.modelId)
            }
            AppButton {
                visible: root.installed
                enabled: root.modelState !== "Testing"
                text: qsTr("Verify")
                onClicked: root.verifyRequested(root.modelId)
            }
            AppButton {
                visible: root.installed && root.defaultCandidate
                enabled: root.modelState !== "Testing"
                text: qsTr("Test")
                onClicked: root.testRequested(root.modelId)
            }
            AppButton {
                visible: root.installed && root.defaultCandidate && !root.isDefault
                text: qsTr("Set Default")
                onClicked: root.defaultRequested(root.modelId)
            }
            Item { Layout.fillWidth: true }
            RemoveButton {
                objectName: "modelDeleteButton"
                visible: root.installed
                enabled: !root.loaded && root.modelState !== "Testing" && root.modelState !== "Verifying"
                accessibleName: root.loaded ? qsTr("Model is in use and cannot be deleted") : qsTr("Delete model")
                onClicked: root.deleteRequested(root.modelId)
            }
        }
    }
}
