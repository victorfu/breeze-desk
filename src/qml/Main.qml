import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    width: 1280
    height: 820
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    title: window.vm.displayName
    color: SemanticTokens.window

    palette.window: SemanticTokens.surfaceRaised
    palette.windowText: SemanticTokens.text
    palette.base: SemanticTokens.surface
    palette.alternateBase: SemanticTokens.surfaceMuted
    palette.text: SemanticTokens.text
    palette.button: SemanticTokens.surface
    palette.buttonText: SemanticTokens.text
    palette.highlight: SemanticTokens.accentMuted
    palette.highlightedText: SemanticTokens.text
    palette.placeholderText: SemanticTokens.textMuted
    palette.mid: SemanticTokens.surfaceMuted
    palette.dark: SemanticTokens.borderStrong
    palette.shadow: SemanticTokens.shadow

    property var injectedApplicationViewModel: null
    property var injectedRecorder: null
    property var injectedMaintenance: null
    readonly property var vm: injectedApplicationViewModel ? injectedApplicationViewModel : ownedApplicationViewModel

    ApplicationViewModel { id: ownedApplicationViewModel }

    function applyAppearanceSettings() {
        DesignSystem.theme = vm.settings.theme === "Dark" ? DesignSystem.Dark
                           : vm.settings.theme === "Light" ? DesignSystem.Light : DesignSystem.System
        DesignSystem.textScale = vm.settings.textScale
        DesignSystem.compact = vm.settings.compactMode
    }

    function openRecordingDialog() {
        if (recordingDialogLoader.item)
            recordingDialogLoader.item.open()
        else
            vm.showToast(qsTr("Microphone recording is unavailable in this build."))
    }

    function exportFormat(index) {
        const formats = ["txt", "md", "srt", "vtt", "json", "csv"]
        return formats[Math.max(0, Math.min(formats.length - 1, index))]
    }

    function requestQuit() {
        if (injectedRecorder && injectedRecorder.recording)
            recordingQuitDialog.open()
        else if (vm.jobQueue.activeCount > 0)
            quitDialog.open()
        else
            Qt.quit()
    }

    property var pendingToasts: []

    function showToast(message, severity, actionText, action) {
        if (pendingToasts.length >= 3)
            pendingToasts.shift()
        pendingToasts.push({ message: message, severity: severity || "info",
                             actionText: actionText || "", action: action || null })
        if (!toast.opened)
            presentNextToast()
    }

    function presentNextToast() {
        if (pendingToasts.length === 0)
            return
        const next = pendingToasts.shift()
        toast.message = next.message
        toast.severity = next.severity
        toast.actionText = next.actionText
        toast.action = next.action
        toast.open()
    }

    Component.onCompleted: applyAppearanceSettings()

    onClosing: function(close) {
        if (vm.settings.closeBehavior === "MinimizeToTray") {
            close.accepted = false
            window.hide()
        } else if (vm.settings.closeBehavior === "Quit") {
            close.accepted = false
            window.requestQuit()
        }
    }

    Connections {
        target: window.vm.settings
        function onThemeChanged() { window.applyAppearanceSettings() }
        function onAppearanceChanged() { window.applyAppearanceSettings() }
    }

    Connections {
        target: window.vm
        function onToastMessageChanged() {
            if (window.vm.toastMessage.length > 0) {
                window.showToast(window.vm.toastMessage)
                window.vm.dismissToast()
            }
        }
        function onOpenImportDialogRequested() { importDialog.open() }
        function onExportRequested(recordingId) {
            if (recordingId.length > 0)
                exportDialog.open()
        }
    }

    Connections {
        target: window.vm.diagnostics
        function onExportRequested(includePaths) {
            if (diagnosticsDialogLoader.item) {
                diagnosticsDialogLoader.item.includePaths = includePaths
                diagnosticsDialogLoader.item.open()
            } else {
                window.vm.showToast(qsTr("Diagnostics export is unavailable in this build."))
            }
        }
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls) {
                window.vm.importUrls(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }

    ShortcutRegistry {
        anchors.fill: parent
        onImportTriggered: importDialog.open()
        onRecordingTriggered: window.openRecordingDialog()
        onPlayPauseTriggered: if (window.vm.currentPage === "Recording" && !window.activeFocusItem) window.vm.player.playPause()
        onSearchTriggered: {
            if (window.vm.currentPage === "Recording") {
                recordingPage.focusTranscriptSearch()
            } else {
                window.vm.navigate("Library")
            }
        }
        onSaveTriggered: if (window.vm.currentPage === "Recording") window.vm.transcript.save()
        onExportTriggered: window.vm.exportActiveRecording()
        onUndoTriggered: if (window.vm.currentPage === "Recording") window.vm.transcript.undo()
        onRedoTriggered: if (window.vm.currentPage === "Recording") window.vm.transcript.redo()
        onSettingsTriggered: window.vm.navigate("Settings")
    }

    ColumnLayout {
        id: shellLayout
        objectName: "shellLayout"
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: topBar
            objectName: "mainTopBar"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(60, topBarContent.implicitHeight + SemanticTokens.spacingSm * 2)
            color: SemanticTokens.surface

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: SemanticTokens.border
            }

            RowLayout {
                id: topBarContent
                objectName: "topBarContent"
                anchors.fill: parent
                anchors.leftMargin: SemanticTokens.spacingLg
                anchors.rightMargin: SemanticTokens.spacingLg
                anchors.topMargin: SemanticTokens.spacingSm
                anchors.bottomMargin: SemanticTokens.spacingSm
                spacing: SemanticTokens.spacingSm

                RowLayout {
                    id: brandRow
                    objectName: "topBarBrandRow"
                    Layout.maximumWidth: 230 * DesignSystem.textScale
                    spacing: SemanticTokens.spacingSm

                    Item {
                        id: brandLogo
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30

                        Image {
                            objectName: "topBarBrandLogo"
                            anchors.fill: parent
                            source: "qrc:/qt/qml/BreezeDesk/icons/breezedesk-sidebar.png"
                            sourceSize.width: Math.ceil(width * Math.max(1, Screen.devicePixelRatio))
                            sourceSize.height: Math.ceil(height * Math.max(1, Screen.devicePixelRatio))
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            Accessible.name: qsTr("%1 logo").arg(window.vm.displayName)
                        }
                    }
                    Text {
                        id: brandText
                        objectName: "topBarBrandText"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: window.vm.displayName
                        color: SemanticTokens.text
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        wrapMode: Text.NoWrap
                        font.pixelSize: SemanticTokens.bodySize
                        font.weight: SemanticTokens.weightSemiBold
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 24
                    color: SemanticTokens.border
                }

                RowLayout {
                    id: topNavigation
                    objectName: "topNavigation"
                    spacing: 0

                    TopNavigationItem {
                        objectName: "topLibraryNavigation"
                        text: qsTr("Library")
                        selected: ["Library", "Recording", "Trash"].includes(window.vm.currentPage)
                        onClicked: window.vm.navigate("Library")
                    }
                    TopNavigationItem {
                        objectName: "topGlossaryNavigation"
                        text: qsTr("Name Dictionary")
                        selected: window.vm.currentPage === "Glossary"
                        onClicked: window.vm.navigate("Glossary")
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.minimumWidth: SemanticTokens.spacingSm
                }

                TopNavigationItem {
                    objectName: "topActivityNavigation"
                    text: qsTr("Activity")
                    badgeText: window.vm.jobQueue.activeCount > 0
                               ? window.vm.jobQueue.activeCount.toString() : ""
                    selected: window.vm.currentPage === "Queue"
                    onClicked: window.vm.navigate("Queue")
                }
                IconButton {
                    objectName: "topSettingsButton"
                    accessibleName: qsTr("Settings")
                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/settings.svg"
                    iconColor: window.vm.currentPage === "Settings" || window.vm.currentPage === "Models"
                               ? SemanticTokens.accentStrong : SemanticTokens.text
                    onClicked: window.vm.navigate("Settings")
                }
            }
        }

        StackLayout {
            id: pages
            objectName: "pageStack"
            Layout.minimumWidth: 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            currentIndex: window.vm.currentPage === "Queue" ? 1
                        : window.vm.currentPage === "Trash" ? 2
                        : window.vm.currentPage === "Models" ? 3
                        : window.vm.currentPage === "Glossary" ? 4
                        : window.vm.currentPage === "Settings" ? 5
                        : window.vm.currentPage === "Recording" ? 6 : 0
            LibraryPage {
                vm: window.vm.library
                app: window.vm
                onImportRequested: importDialog.open()
                onFolderImportRequested: importFolderDialog.open()
                onRecordingRequested: window.openRecordingDialog()
                onTrashRequested: window.vm.navigate("Trash")
                onToastRequested: function(message, severity, actionText, action) {
                    window.showToast(message, severity, actionText, action)
                }
            }
            QueuePage { vm: window.vm.jobQueue; onBackRequested: window.vm.navigate("Library") }
            TrashPage { vm: window.vm.library; onBackRequested: window.vm.navigate("Library") }
            ModelsPage {
                vm: window.vm.modelManager
                onBackRequested: window.vm.navigate("Settings")
                onCustomImportRequested: customModelDialog.open()
            }
            GlossaryPage { vm: window.vm.glossary }
            SettingsPage {
                vm: window.vm.settings
                diagnostics: window.vm.diagnostics
                onManageModelsRequested: window.vm.navigate("Models")
            }
            RecordingPage { id: recordingPage; vm: window.vm }
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import Audio or Video")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("Media files (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.opus *.mp4 *.mov *.mkv *.webm)"),
            qsTr("All files (*)")
        ]
        onAccepted: window.vm.importUrls(selectedFiles)
    }

    FolderDialog {
        id: importFolderDialog
        objectName: "importFolderDialog"
        title: qsTr("Import Media Folder")
        onAccepted: window.vm.importFolder(selectedFolder)
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export Transcript")
        fileMode: FileDialog.SaveFile
        nameFilters: [
            qsTr("Plain text (*.txt)"),
            qsTr("Markdown (*.md)"),
            qsTr("SubRip subtitles (*.srt)"),
            qsTr("WebVTT subtitles (*.vtt)"),
            qsTr("JSON (*.json)"),
            qsTr("CSV (*.csv)")
        ]
        onAccepted: window.vm.exportActiveRecordingTo(
                        selectedFile,
                        window.exportFormat(selectedNameFilter.index),
                        false)
    }

    FileDialog {
        id: customModelDialog
        title: qsTr("Import a whisper.cpp GGML model")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("GGML model (*.bin)")]
        onAccepted: window.vm.modelManager.importCustom(selectedFile)
    }


    Loader {
        id: recordingDialogLoader
        active: window.injectedRecorder !== null
        sourceComponent: Component {
            RecordingDialog {
                recorder: window.injectedRecorder
                settings: window.vm.settings
                onStartRequested: window.vm.startRecording()
            }
        }
    }

    Loader {
        id: diagnosticsDialogLoader
        active: window.injectedMaintenance !== null
        sourceComponent: Component {
            DiagnosticsExportDialog {
                onExportRequested: function(destination, includePersonalPaths) {
                    window.injectedMaintenance.exportDiagnosticsToUrl(destination,
                                                                      includePersonalPaths)
                }
            }
        }
    }

    AppDialog {
        id: recordingQuitDialog
        objectName: "recordingQuitDialog"
        title: qsTr("Recording is still in progress")
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/mic.svg"
        standardButtons: Dialog.NoButton
        ColumnLayout {
            width: parent.width
            spacing: SemanticTokens.spacingLg
            Text {
                Layout.fillWidth: true
                text: qsTr("Stop and save the recording before quitting so it remains available in your library.")
                color: SemanticTokens.text
                wrapMode: Text.Wrap
                font.pixelSize: SemanticTokens.bodySize
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    objectName: "continueRecordingButton"
                    text: qsTr("Continue Recording")
                    onClicked: recordingQuitDialog.close()
                }
                AppButton {
                    objectName: "stopRecordingAndQuitButton"
                    text: qsTr("Stop, Save, and Quit")
                    primary: true
                    onClicked: {
                        if (window.injectedRecorder && window.injectedRecorder.stop()) {
                            recordingQuitDialog.close()
                            Qt.callLater(window.requestQuit)
                        }
                    }
                }
            }
        }
    }

    AppDialog {
        id: quitDialog
        objectName: "transcriptionQuitDialog"
        title: qsTr("Transcription is still running")
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/pause.svg"
        standardButtons: Dialog.NoButton
        ColumnLayout {
            width: parent.width
            spacing: SemanticTokens.spacingLg
            Text {
                Layout.fillWidth: true
                text: qsTr("Completed chunks are safe. Quitting now will mark the active job as interrupted so it can be resumed later.")
                color: SemanticTokens.text
                wrapMode: Text.Wrap
                font.pixelSize: SemanticTokens.bodySize
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton { text: qsTr("Continue in Background"); onClicked: { quitDialog.close(); window.hide() } }
                AppButton { text: qsTr("Quit and Resume Later"); primary: true; onClicked: Qt.quit() }
            }
        }
    }

    Toast {
        id: toast
        objectName: "appToast"
        x: window.width - width - SemanticTokens.spacingLg
        y: window.height - height - SemanticTokens.spacingLg
        onClosed: Qt.callLater(window.presentNextToast)
    }
}
