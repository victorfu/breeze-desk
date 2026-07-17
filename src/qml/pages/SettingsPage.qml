import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    required property var vm
    required property var diagnostics
    objectName: "settingsPage"

    readonly property real contentMaximumWidth: ComponentTokens.inspectorWidth * 3
                                                + SemanticTokens.spacingLg * 2
    readonly property real pageMargin: SemanticTokens.spacingLg

    function deviceIndex(devices, selectedId) {
        for (let index = 0; index < devices.length; ++index) {
            if (devices[index].id === selectedId)
                return index
        }
        return 0
    }
    ScrollView {
        id: settingsScroll
        objectName: "settingsScroll"
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: settingsViewport.implicitHeight
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Item {
            id: settingsViewport
            objectName: "settingsViewport"
            width: settingsScroll.availableWidth
            height: implicitHeight
            implicitHeight: settingsContent.implicitHeight + root.pageMargin * 2

        ColumnLayout {
            id: settingsContent
            objectName: "settingsContent"
            width: Math.max(0, Math.min(root.contentMaximumWidth,
                                        settingsViewport.width - root.pageMargin * 2))
            x: Math.max(root.pageMargin, (settingsViewport.width - width) / 2)
            y: root.pageMargin
            spacing: SemanticTokens.spacingLg
            Text {
                Layout.fillWidth: true
                text: qsTr("Settings")
                color: SemanticTokens.text
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.titleSize
                font.weight: Font.DemiBold
            }
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("General")
                SettingRow {
                    label: qsTr("Interface language")
                    description: qsTr("Transcript text is never transformed when the UI language changes.")
                    AppComboBox {
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Interface language")
                        model: [qsTr("繁體中文"), qsTr("English")]
                        currentIndex: root.vm.language === "zh_TW" ? 0 : 1
                        onActivated: root.vm.language = currentIndex === 0 ? "zh_TW" : "en"
                    }
                }
                SettingRow {
                    label: qsTr("Launch at startup")
                    description: qsTr("Uses the native startup mechanism when supported.")
                    Toggle { checked: root.vm.launchAtStartup; onToggled: root.vm.launchAtStartup = checked }
                }
                SettingRow {
                    label: qsTr("Close behavior")
                    AppComboBox {
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Close behavior")
                        model: [qsTr("Minimize to tray"), qsTr("Close window"), qsTr("Quit")]
                        currentIndex: root.vm.closeBehavior === "MinimizeToTray" ? 0 : root.vm.closeBehavior === "CloseWindow" ? 1 : 2
                        onActivated: root.vm.closeBehavior = currentIndex === 0 ? "MinimizeToTray" : currentIndex === 1 ? "CloseWindow" : "Quit"
                    }
                }
                SettingRow {
                    label: qsTr("Import behavior")
                    AppComboBox {
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Import behavior")
                        model: [qsTr("Reference original"), qsTr("Copy into managed storage")]
                        currentIndex: root.vm.importBehavior === "ReferenceOriginal" ? 0 : 1
                        onActivated: root.vm.importBehavior = currentIndex === 0 ? "ReferenceOriginal" : "CopyManaged"
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: SemanticTokens.border }
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Appearance")
                SettingRow {
                    label: qsTr("Theme")
                    AppComboBox {
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Theme")
                        model: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
                        currentIndex: root.vm.theme === "System" ? 0 : root.vm.theme === "Light" ? 1 : 2
                        onActivated: root.vm.theme = currentIndex === 0 ? "System" : currentIndex === 1 ? "Light" : "Dark"
                    }
                }
                SettingRow {
                    label: qsTr("Text size")
                    Slider {
                        Layout.fillWidth: true
                        from: 0.8; to: 1.5; stepSize: 0.1; value: root.vm.textScale
                        Accessible.name: qsTr("Text size")
                        onMoved: root.vm.textScale = value
                    }
                }
                SettingRow { label: qsTr("Compact mode"); Toggle { checked: root.vm.compactMode; onToggled: root.vm.compactMode = checked } }
                SettingRow {
                    label: qsTr("Waveform density")
                    AppComboBox {
                        Layout.fillWidth: true
                        model: [qsTr("Sparse"), qsTr("Balanced"), qsTr("Dense")]
                        currentIndex: root.vm.waveformDensity === "Sparse" ? 0 : root.vm.waveformDensity === "Balanced" ? 1 : 2
                        onActivated: root.vm.waveformDensity = currentIndex === 0 ? "Sparse" : currentIndex === 1 ? "Balanced" : "Dense"
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: SemanticTokens.border }
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Transcription")
                SettingRow {
                    label: qsTr("Default model")
                    AppComboBox {
                        Layout.fillWidth: true
                        model: [qsTr("Breeze-ASR-25 Q5"), qsTr("Breeze-ASR-25 Q8")]
                        currentIndex: root.vm.defaultModel === "breeze-asr-25-q5" ? 0 : 1
                        onActivated: root.vm.defaultModel = currentIndex === 0 ? "breeze-asr-25-q5" : "breeze-asr-25-q8"
                    }
                }
                SettingRow {
                    label: qsTr("Language")
                    AppComboBox { Layout.fillWidth: true; model: [qsTr("Chinese (zh)"), qsTr("Automatic")]; currentIndex: root.vm.transcriptionLanguage === "zh" ? 0 : 1; onActivated: root.vm.transcriptionLanguage = currentIndex === 0 ? "zh" : "auto" }
                }
                SettingRow {
                    label: qsTr("Preset")
                    AppComboBox { Layout.fillWidth: true; model: [qsTr("Fast"), qsTr("Balanced"), qsTr("Accurate")]; currentIndex: root.vm.preset === "Fast" ? 0 : root.vm.preset === "Balanced" ? 1 : 2; onActivated: root.vm.preset = currentIndex === 0 ? "Fast" : currentIndex === 1 ? "Balanced" : "Accurate" }
                }
                SettingRow { label: qsTr("Silero VAD"); description: qsTr("Finds speech boundaries before long recordings are divided into resumable units."); Toggle { checked: root.vm.vadEnabled; onToggled: root.vm.vadEnabled = checked } }
                SettingRow {
                    label: qsTr("Initial prompt")
                    description: qsTr("Uses the selected glossary profile, project context, and the previous chunk within the model token budget.")
                    AppComboBox {
                        Layout.fillWidth: true
                        model: [qsTr("Glossary and context"), qsTr("Disabled")]
                        currentIndex: root.vm.initialPromptBehavior === "Disabled" ? 1 : 0
                        onActivated: root.vm.initialPromptBehavior = currentIndex === 1 ? "Disabled" : "GlossaryAndContext"
                    }
                }
                SettingRow {
                    label: qsTr("Backend")
                    AppComboBox { Layout.fillWidth: true; model: ["Auto", "CPU", "Metal", "Vulkan", "CUDA"]; currentIndex: model.indexOf(root.vm.backend); onActivated: root.vm.backend = model[currentIndex] }
                }
                SettingRow { label: qsTr("Flash attention"); Toggle { checked: root.vm.flashAttention; onToggled: root.vm.flashAttention = checked } }
                SettingRow { label: qsTr("Token timestamps"); Toggle { checked: root.vm.tokenTimestamps; onToggled: root.vm.tokenTimestamps = checked } }
                SettingRow {
                    label: qsTr("Worker threads")
                    SpinBox { Layout.fillWidth: true; from: 1; to: 64; value: root.vm.threadCount; editable: true; Accessible.name: qsTr("Worker threads"); onValueModified: root.vm.threadCount = value }
                }
                SettingRow {
                    label: qsTr("Low-confidence threshold")
                    Slider { Layout.fillWidth: true; from: 0; to: 1; stepSize: 0.05; value: root.vm.lowConfidenceThreshold; Accessible.name: qsTr("Low-confidence threshold"); onMoved: root.vm.lowConfidenceThreshold = value }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: SemanticTokens.border }
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Audio")
                SettingRow {
                    label: qsTr("Microphone")
                    AppComboBox {
                        Layout.fillWidth: true
                        model: root.vm.microphoneDevices
                        textRole: "description"
                        valueRole: "id"
                        currentIndex: root.deviceIndex(root.vm.microphoneDevices, root.vm.microphoneDevice)
                        Accessible.name: qsTr("Microphone")
                        onActivated: root.vm.microphoneDevice = currentValue
                    }
                }
                SettingRow {
                    label: qsTr("Playback device")
                    AppComboBox {
                        Layout.fillWidth: true
                        model: root.vm.playbackDevices
                        textRole: "description"
                        valueRole: "id"
                        currentIndex: root.deviceIndex(root.vm.playbackDevices, root.vm.playbackDevice)
                        Accessible.name: qsTr("Playback device")
                        onActivated: root.vm.playbackDevice = currentValue
                    }
                }
                SettingRow { label: qsTr("Recording format"); AppComboBox { Layout.fillWidth: true; model: [qsTr("PCM WAV")]; Accessible.name: qsTr("Recording format") } }
                SettingRow {
                    label: qsTr("Transcribe new recordings automatically")
                    description: qsTr("Starts a queued transcription after microphone recording stops.")
                    Toggle {
                        checked: root.vm.autoTranscribeRecording
                        Accessible.name: qsTr("Transcribe new recordings automatically")
                        onToggled: root.vm.autoTranscribeRecording = checked
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: SemanticTokens.border }
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Storage")
                SettingRow { label: qsTr("Application data"); description: root.vm.storagePath; AppButton { text: qsTr("Choose"); onClicked: storageFolderDialog.open() } }
                SettingRow { label: qsTr("Export directory"); description: root.vm.exportPath; AppButton { text: qsTr("Choose"); onClicked: exportFolderDialog.open() } }
                SettingRow {
                    label: qsTr("Managed media")
                    AppComboBox { Layout.fillWidth: true; model: [qsTr("Reference original"), qsTr("Copy managed media")]; currentIndex: root.vm.managedMediaPolicy === "ReferenceOriginal" ? 0 : 1; onActivated: root.vm.managedMediaPolicy = currentIndex === 0 ? "ReferenceOriginal" : "CopyManaged" }
                }
                SettingRow { label: qsTr("Cache"); AppButton { text: qsTr("Clear Cache"); onClicked: root.vm.clearCache() } }
                SettingRow { label: qsTr("Database backup"); AppButton { text: qsTr("Back Up Now"); onClicked: root.vm.backupDatabase() } }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: SemanticTokens.border }
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Updates")
                SettingRow { label: qsTr("Automatic update checks"); description: qsTr("Disabled by default. No update check runs during startup."); Toggle { checked: root.vm.automaticUpdates; onToggled: root.vm.automaticUpdates = checked } }
                SettingRow {
                    label: qsTr("Update channel")
                    description: qsTr("Stable")
                }
                SettingRow { label: qsTr("Version %1").arg(root.vm.appVersion); AppButton { text: qsTr("Check Now"); onClicked: root.vm.checkForUpdates() } }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: SemanticTokens.border }
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Privacy")
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Audio and transcripts are processed locally. This app has no accounts, telemetry, analytics, crash upload, cloud ASR, or cloud AI. The only network activities are model downloads you start and optional app update checks.")
                    color: SemanticTokens.text
                    wrapMode: Text.Wrap
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: SemanticTokens.border }
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Open Source Licenses")
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Application and whisper.cpp: MIT · Qt: LGPL · FFmpeg: LGPL · Lucide: ISC")
                    color: SemanticTokens.text
                    wrapMode: Text.Wrap
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Complete third-party notices, license texts, and FFmpeg build/source information are included with every packaged copy.")
                    color: SemanticTokens.textMuted
                    wrapMode: Text.Wrap
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.captionSize
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: SemanticTokens.border }
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Diagnostics")
                Text { text: qsTr("Qt %1 · %2 · %3").arg(root.diagnostics.qtVersion).arg(root.diagnostics.osDescription).arg(root.diagnostics.cpuArchitecture); color: SemanticTokens.textMuted; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.bodySize }
                Text { text: qsTr("Backend: %1 (actual: %2)").arg(root.diagnostics.selectedBackend).arg(root.diagnostics.actualBackend); color: SemanticTokens.textMuted; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.bodySize }
                RowLayout {
                    AppButton { text: qsTr("Refresh"); onClicked: root.diagnostics.refresh() }
                    AppButton { text: qsTr("Export Sanitized Diagnostics"); onClicked: root.diagnostics.exportDiagnostics(false) }
                }
            }
            Item { Layout.preferredHeight: SemanticTokens.spacingLg }
        }
        }
    }
    FolderDialog {
        id: storageFolderDialog
        title: qsTr("Choose Application Data Folder")
        onAccepted: root.vm.setStorageFolder(selectedFolder)
    }
    FolderDialog {
        id: exportFolderDialog
        title: qsTr("Choose Default Export Folder")
        onAccepted: root.vm.setExportFolder(selectedFolder)
    }
}
