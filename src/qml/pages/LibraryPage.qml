pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    required property var vm
    required property var app
    signal importRequested
    signal folderImportRequested
    signal toastRequested(string message, string severity, string actionText, var action)
    objectName: "libraryPage"
    readonly property int headerStackWidth: 760
    readonly property int toolbarStackWidth: 840
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingLg
        spacing: SemanticTokens.spacingMd
        PageHeader {
            id: libraryHeader
            objectName: "libraryHeader"
            actionsObjectName: "libraryHeaderActions"
            Layout.fillWidth: true
            stackWidth: root.headerStackWidth
            title: qsTr("Library")
            subtitle: qsTr("Your offline recordings and transcripts")
            AppSearchField {
                objectName: "librarySearchField"
                Layout.fillWidth: libraryHeader.stacked
                Layout.minimumWidth: 160
                Layout.preferredWidth: 240
                text: root.vm.searchText
                onTextEdited: root.vm.searchText = text
            }
            AppButton {
                objectName: "libraryImportButton"
                iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/file-input.svg"
                text: qsTr("Import Files")
                primary: true
                onClicked: root.importRequested()
            }
            AppButton {
                objectName: "libraryOpenFolderButton"
                text: qsTr("Open Folder")
                onClicked: root.folderImportRequested()
            }
        }
        GridLayout {
            id: libraryToolbar
            objectName: "libraryToolbar"
            Layout.fillWidth: true
            columns: stacked ? 1 : 2
            columnSpacing: SemanticTokens.spacingMd
            rowSpacing: SemanticTokens.spacingSm
            readonly property bool stacked: root.app.folderImportRunning
                                             && width < root.toolbarStackWidth
                                                        * DesignSystem.textScale
            RowLayout {
                id: filterControls
                objectName: "libraryFilterControls"
                Layout.row: 0
                Layout.column: 0
                Layout.fillWidth: libraryToolbar.stacked
                spacing: SemanticTokens.spacingSm
                AppComboBox {
                    objectName: "librarySortControl"
                    Layout.fillWidth: libraryToolbar.stacked
                    Layout.minimumWidth: 150
                    Layout.preferredWidth: 170
                    Accessible.name: qsTr("Sort recordings")
                    model: [qsTr("Newest first"), qsTr("Oldest first"), qsTr("Title A–Z"), qsTr("Title Z–A")]
                    currentIndex: root.vm.sortMode === "Oldest" ? 1
                                  : root.vm.sortMode === "TitleAZ" ? 2
                                  : root.vm.sortMode === "TitleZA" ? 3 : 0
                    onActivated: root.vm.sortMode = ["Newest", "Oldest", "TitleAZ", "TitleZA"][currentIndex]
                }
                AppComboBox {
                    objectName: "libraryReviewFilter"
                    Layout.fillWidth: libraryToolbar.stacked
                    Layout.minimumWidth: 150
                    Layout.preferredWidth: 170
                    Accessible.name: qsTr("Filter by review state")
                    model: [qsTr("All recordings"), qsTr("Reviewed"), qsTr("Unreviewed")]
                    currentIndex: root.vm.reviewFilter === "Reviewed" ? 1
                                  : root.vm.reviewFilter === "Unreviewed" ? 2 : 0
                    onActivated: root.vm.reviewFilter = ["All", "Reviewed", "Unreviewed"][currentIndex]
                }
            }
            RowLayout {
                id: importProgress
                objectName: "libraryImportProgress"
                Layout.row: libraryToolbar.stacked ? 1 : 0
                Layout.column: libraryToolbar.stacked ? 0 : 1
                Layout.fillWidth: libraryToolbar.stacked
                Layout.alignment: Qt.AlignRight
                visible: root.app.folderImportRunning
                spacing: SemanticTokens.spacingSm
                Item { Layout.fillWidth: true; visible: libraryToolbar.stacked }
                BusyIndicator {
                    visible: root.app.folderImportTotal === 0
                    running: visible
                    implicitWidth: 24
                    implicitHeight: 24
                    Accessible.name: qsTr("Scanning folder")
                }
                AppProgressBar {
                    Layout.preferredWidth: 150
                    visible: root.app.folderImportTotal > 0
                    value: root.app.folderImportTotal > 0
                           ? root.app.folderImportCompleted / root.app.folderImportTotal : 0
                }
                Text {
                    Layout.minimumWidth: 0
                    text: root.app.folderImportTotal > 0
                          ? qsTr("Importing %1 of %2…")
                                .arg(root.app.folderImportCompleted)
                                .arg(root.app.folderImportTotal)
                          : qsTr("Scanning folder…")
                    color: SemanticTokens.textMuted
                    elide: Text.ElideRight
                    font.pixelSize: SemanticTokens.captionSize
                }
                AppButton { text: qsTr("Cancel"); onClicked: root.app.cancelFolderImport() }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: SemanticTokens.border }
        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.vm.empty
            iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/file-input.svg"
            title: root.vm.searchText.length > 0 ? qsTr("No matching recordings") : qsTr("Import your first recording")
            description: root.vm.searchText.length > 0
                         ? qsTr("Try a different title, tag, or note.")
                         : qsTr("Audio and video remain on this computer. %1 prepares them for offline transcription.").arg(root.app.displayName)
            actionText: root.vm.searchText.length > 0 ? "" : qsTr("Choose Files")
            onActionTriggered: root.importRequested()
        }
        ListView {
            id: list
            objectName: "recordingList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.vm.empty
            model: root.vm.recordings
            spacing: SemanticTokens.spacingSm
            clip: true
            reuseItems: true
            keyNavigationEnabled: true
            activeFocusOnTab: true
            Keys.onReturnPressed: if (currentItem) currentItem.clicked()
            Keys.onEnterPressed: if (currentItem) currentItem.clicked()
            ScrollBar.vertical: ScrollBar { }
            delegate: RecordingCard {
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                onOpenRequested: function(recordingId) {
                    root.vm.activateRecording(recordingId)
                }
                onTranscribeRequested: function(recordingId) {
                    if (root.app.modelManager.defaultModelReady)
                        root.app.enqueueTranscription(recordingId)
                    else
                        modelRequiredDialog.open()
                }
                onTrashRequested: function(recordingId) {
                    const libraryVm = root.vm
                    libraryVm.moveToTrash(recordingId)
                    root.toastRequested(qsTr("Moved to Trash."), "info", qsTr("Undo"),
                                        function() { libraryVm.restore(recordingId) })
                }
                onRenameRequested: function(recordingId, title) {
                    root.pendingRecordingId = recordingId
                    renameField.text = title
                    renameDialog.open()
                }
                onRevealRequested: function(recordingId) {
                    root.app.revealRecording(recordingId)
                }
                onRelinkRequested: function(recordingId) {
                    root.pendingRecordingId = recordingId
                    relinkDialog.open()
                }
                onEditTagsRequested: function(recordingId, tags) {
                    root.pendingRecordingId = recordingId
                    tagsField.text = tags.join(", ")
                    tagsDialog.open()
                }
                onReviewRequested: function(recordingId, reviewed) {
                    root.vm.setReviewState(recordingId, reviewed)
                }
            }
        }
    }

    property string pendingRecordingId: ""

    AppDialog {
        id: renameDialog
        objectName: "renameRecordingDialog"
        title: qsTr("Rename Recording")
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/library.svg"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.vm.rename(root.pendingRecordingId, renameField.text)
        onOpened: renameField.forceActiveFocus()
        onClosed: root.pendingRecordingId = ""
        AppTextField {
            id: renameField
            width: parent.width
            placeholderText: qsTr("Recording title")
            Accessible.name: qsTr("Recording title")
            onAccepted: if (text.trim().length > 0) renameDialog.accept()
        }
    }

    AppDialog {
        id: tagsDialog
        objectName: "editRecordingTagsDialog"
        title: qsTr("Edit Tags")
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/book-open.svg"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.vm.setTagsText(root.pendingRecordingId, tagsField.text)
        onOpened: tagsField.forceActiveFocus()
        onClosed: root.pendingRecordingId = ""
        ColumnLayout {
            width: parent.width
            spacing: SemanticTokens.spacingSm
            AppTextField {
                id: tagsField
                Layout.fillWidth: true
                placeholderText: qsTr("meeting, product, customer")
                Accessible.name: qsTr("Comma-separated recording tags")
                onAccepted: tagsDialog.accept()
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Separate tags with commas.")
                color: SemanticTokens.textMuted
                wrapMode: Text.WordWrap
                font.pixelSize: SemanticTokens.captionSize
            }
        }
    }

    ModelRequiredDialog {
        id: modelRequiredDialog
        app: root.app
    }

    FileDialog {
        id: relinkDialog
        objectName: "relinkRecordingDialog"
        title: qsTr("Relink Recording Source")
        fileMode: FileDialog.OpenFile
        nameFilters: [
            qsTr("Media files (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.opus *.mp4 *.mov *.mkv *.webm)")
        ]
        onAccepted: {
            root.vm.relinkSource(root.pendingRecordingId, selectedFile)
            root.pendingRecordingId = ""
        }
        onRejected: root.pendingRecordingId = ""
    }
}
