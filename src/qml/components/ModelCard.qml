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
    readonly property bool downloadActionAvailable: !root.installed
                                                    && (root.modelState === "NotInstalled"
                                                        || root.modelState === "Paused"
                                                        || root.modelState === "Cancelled"
                                                        || root.modelState === "Failed")
    readonly property bool downloadBusy: root.modelState === "Requested"
                                         || root.modelState === "Downloading"
                                         || root.modelState === "Verifying"
    readonly property int summaryStackWidth: 760
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
        GridLayout {
            id: summary
            objectName: "modelSummaryLayout"
            Layout.fillWidth: true
            columns: stacked ? 1 : 2
            columnSpacing: SemanticTokens.spacingMd
            rowSpacing: SemanticTokens.spacingSm
            readonly property bool stacked: width < root.summaryStackWidth
                                                      * DesignSystem.textScale

            ColumnLayout {
                Layout.row: 0
                Layout.column: 0
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: SemanticTokens.spacingXs

                Text {
                    objectName: "modelDisplayName"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: root.displayName
                    color: SemanticTokens.text
                    elide: Text.ElideNone
                    wrapMode: Text.WordWrap
                    font.pixelSize: SemanticTokens.headingSize
                    font.weight: SemanticTokens.weightSemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: root.displayedDescription
                    color: SemanticTokens.textMuted
                    wrapMode: Text.Wrap
                    font.pixelSize: SemanticTokens.bodySize
                }
            }

            Flow {
                id: badges
                objectName: "modelBadges"
                Layout.row: summary.stacked ? 1 : 0
                Layout.column: summary.stacked ? 0 : 1
                Layout.fillWidth: summary.stacked
                Layout.minimumWidth: summary.stacked ? 0 : implicitWidth
                Layout.alignment: summary.stacked ? Qt.AlignLeft : Qt.AlignTop | Qt.AlignRight
                spacing: SemanticTokens.spacingSm

                StatusBadge {
                    objectName: "modelRecommendedBadge"
                    visible: root.recommended
                    text: qsTr("Recommended")
                    tone: "accent"
                }
                StatusBadge {
                    objectName: "modelDefaultBadge"
                    visible: root.isDefault
                    text: qsTr("Default")
                    tone: "success"
                }
                StatusBadge {
                    objectName: "modelFailureBadge"
                    visible: root.modelState === "Failed"
                    text: root.displayedModelState
                    tone: "danger"
                }
                StatusBadge {
                    objectName: "modelQuantizationBadge"
                    text: root.quantization
                    tone: "neutral"
                }
                StatusBadge {
                    objectName: "modelInstallBadge"
                    text: root.loaded ? qsTr("Loaded")
                                      : root.installed ? qsTr("Installed") : qsTr("Not installed")
                    tone: root.loaded || root.installed ? "success" : "neutral"
                }
            }
        }
        Flow {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            spacing: SemanticTokens.spacingSm
            Text {
                height: ComponentTokens.compactControlHeight
                verticalAlignment: Text.AlignVCenter
                text: qsTr("%1 GB").arg((root.fileSize / 1000000000).toFixed(2))
                color: SemanticTokens.textMuted
                font.pixelSize: SemanticTokens.captionSize
            }
            Text {
                height: ComponentTokens.compactControlHeight
                verticalAlignment: Text.AlignVCenter
                text: qsTr("License: %1").arg(root.licenseName)
                color: SemanticTokens.textMuted
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
            objectName: "modelDownloadSpinnerRow"
            Layout.fillWidth: true
            visible: root.downloadBusy
            spacing: SemanticTokens.spacingSm

            BusyIndicator {
                objectName: "modelDownloadSpinner"
                running: parent.visible
                implicitWidth: 24
                implicitHeight: 24
                Accessible.name: qsTr("Downloading model")
            }
            Text {
                Layout.fillWidth: true
                text: root.displayedModelState
                color: SemanticTokens.textMuted
                font.pixelSize: SemanticTokens.captionSize
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: SemanticTokens.spacingSm
            Flow {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                spacing: SemanticTokens.spacingSm
                AppButton {
                    objectName: "modelDownloadButton"
                    visible: root.downloadActionAvailable
                    enabled: root.downloadActionAvailable
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
            }
            RemoveButton {
                objectName: "modelDeleteButton"
                Layout.alignment: Qt.AlignTop
                visible: root.installed
                enabled: !root.loaded && root.modelState !== "Testing" && root.modelState !== "Verifying"
                accessibleName: root.loaded ? qsTr("Model is in use and cannot be deleted") : qsTr("Delete model")
                onClicked: root.deleteRequested(root.modelId)
            }
        }
    }
}
