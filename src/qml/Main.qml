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

    property var injectedApplicationViewModel: typeof App !== "undefined" ? App : null
    property var injectedRecorder: typeof Recorder !== "undefined" ? Recorder : null
    property var injectedMaintenance: typeof Maintenance !== "undefined" ? Maintenance : null
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
        if (vm.jobQueue.activeCount > 0)
            quitDialog.open()
        else
            Qt.quit()
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
                toast.message = window.vm.toastMessage
                toast.open()
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
        onSearchTriggered: window.vm.navigate("Library")
        onSaveTriggered: window.vm.transcript.save()
        onExportTriggered: window.vm.exportActiveRecording()
        onUndoTriggered: window.vm.transcript.undo()
        onRedoTriggered: window.vm.transcript.redo()
        onSettingsTriggered: window.vm.navigate("Settings")
    }

    RowLayout {
        id: shellLayout
        objectName: "shellLayout"
        anchors.fill: parent
        spacing: 0
        Rectangle {
            id: sidebar
            objectName: "mainSidebar"
            Layout.minimumWidth: ComponentTokens.sidebarWidth
            Layout.preferredWidth: ComponentTokens.sidebarWidth
            Layout.maximumWidth: ComponentTokens.sidebarWidth
            Layout.fillHeight: true
            color: SemanticTokens.surface
            border.color: SemanticTokens.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: SemanticTokens.spacingMd
                spacing: SemanticTokens.spacingXs
                RowLayout {
                    id: brandRow
                    objectName: "sidebarBrandRow"
                    Layout.fillWidth: true
                    Layout.bottomMargin: SemanticTokens.spacingLg
                    Image {
                        source: "qrc:/qt/qml/BreezeDesk/icons/breezedesk.png"
                        sourceSize.width: 34
                        sourceSize.height: 34
                        Accessible.name: qsTr("%1 logo").arg(window.vm.displayName)
                    }
                    Text {
                        id: brandText
                        objectName: "sidebarBrandText"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: window.vm.displayName
                        color: SemanticTokens.text
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        wrapMode: Text.NoWrap
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.bodySize
                        font.weight: Font.DemiBold
                    }
                }
                SidebarItem { Layout.fillWidth: true; iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/library.svg"; text: qsTr("Library"); selected: window.vm.currentPage === "Library" || window.vm.currentPage === "Recording"; onClicked: window.vm.navigate("Library") }
                SidebarItem { Layout.fillWidth: true; iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/list-ordered.svg"; text: qsTr("Queue"); badgeText: window.vm.jobQueue.activeCount > 0 ? window.vm.jobQueue.activeCount.toString() : ""; selected: window.vm.currentPage === "Queue"; onClicked: window.vm.navigate("Queue") }
                SidebarItem { Layout.fillWidth: true; iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/trash-2.svg"; text: qsTr("Trash"); selected: window.vm.currentPage === "Trash"; onClicked: window.vm.navigate("Trash") }
                SidebarItem { Layout.fillWidth: true; iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/box.svg"; text: qsTr("Models"); selected: window.vm.currentPage === "Models"; onClicked: window.vm.navigate("Models") }
                SidebarItem { Layout.fillWidth: true; iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/book-open.svg"; text: qsTr("Glossary"); selected: window.vm.currentPage === "Glossary"; onClicked: window.vm.navigate("Glossary") }
                Item { Layout.fillHeight: true }
                AppButton { Layout.fillWidth: true; iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/file-input.svg"; text: qsTr("Import Media"); primary: true; onClicked: importDialog.open() }
                AppButton { Layout.fillWidth: true; iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/mic.svg"; text: qsTr("Start Recording"); onClicked: window.openRecordingDialog() }
                SidebarItem { Layout.fillWidth: true; iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/settings.svg"; text: qsTr("Settings"); selected: window.vm.currentPage === "Settings"; onClicked: window.vm.navigate("Settings") }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: SemanticTokens.spacingSm
                    text: qsTr("Offline by default")
                    color: SemanticTokens.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.captionSize
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
            }
            QueuePage { vm: window.vm.jobQueue }
            TrashPage { vm: window.vm.library }
            ModelsPage { vm: window.vm.modelManager; onCustomImportRequested: customModelDialog.open() }
            GlossaryPage { vm: window.vm.glossary }
            SettingsPage { vm: window.vm.settings; diagnostics: window.vm.diagnostics }
            RecordingPage { vm: window.vm }
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
        id: quitDialog
        title: qsTr("Transcription is still running")
        standardButtons: Dialog.NoButton
        ColumnLayout {
            width: parent.width
            spacing: SemanticTokens.spacingLg
            Text {
                Layout.fillWidth: true
                text: qsTr("Completed chunks are safe. Quitting now will mark the active job as interrupted so it can be resumed later.")
                color: SemanticTokens.text
                wrapMode: Text.Wrap
                font.family: SemanticTokens.fontFamily
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
        x: window.width - width - SemanticTokens.spacingLg
        y: window.height - height - SemanticTokens.spacingLg
        onDismissed: window.vm.dismissToast()
    }
}
