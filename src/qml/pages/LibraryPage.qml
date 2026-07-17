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
    objectName: "libraryPage"
    readonly property int headerStackWidth: 760
    readonly property int toolbarStackWidth: 840
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingLg
        spacing: SemanticTokens.spacingMd
        GridLayout {
            id: libraryHeader
            objectName: "libraryHeader"
            Layout.fillWidth: true
            columns: stacked ? 1 : 2
            columnSpacing: SemanticTokens.spacingMd
            rowSpacing: SemanticTokens.spacingSm
            // Keep the breakpoint independent of the layout's implicit size. Child
            // widths change when the grid changes columns, so feeding them back into
            // this decision can make the layout oscillate indefinitely.
            readonly property bool stacked: width < root.headerStackWidth * DesignSystem.textScale
            ColumnLayout {
                id: titleBlock
                Layout.row: 0
                Layout.column: 0
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Library")
                    color: SemanticTokens.text
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.titleSize
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Your offline recordings and transcripts")
                    color: SemanticTokens.textMuted
                    wrapMode: Text.WordWrap
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                }
            }
            RowLayout {
                id: headerActions
                objectName: "libraryHeaderActions"
                Layout.row: libraryHeader.stacked ? 1 : 0
                Layout.column: libraryHeader.stacked ? 0 : 1
                Layout.fillWidth: libraryHeader.stacked
                Layout.minimumWidth: 0
                spacing: SemanticTokens.spacingSm
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
                    font.family: SemanticTokens.fontFamily
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
            ScrollBar.vertical: ScrollBar { }
            delegate: RecordingCard {
                width: ListView.view.width
                onOpenRequested: root.vm.selectedRecordingId = id
                onTrashRequested: root.vm.moveToTrash(id)
                onRenameRequested: function(id, title) {
                    root.pendingRecordingId = id
                    renameField.text = title
                    renameDialog.open()
                }
                onRevealRequested: root.app.revealRecording(id)
                onRelinkRequested: function(id) {
                    root.pendingRecordingId = id
                    relinkDialog.open()
                }
                onEditTagsRequested: function(id, tags) {
                    root.pendingRecordingId = id
                    tagsField.text = tags.join(", ")
                    tagsDialog.open()
                }
                onReviewRequested: function(id, reviewed) {
                    root.vm.setReviewState(id, reviewed)
                }
            }
        }
    }

    property string pendingRecordingId: ""

    AppDialog {
        id: renameDialog
        objectName: "renameRecordingDialog"
        title: qsTr("Rename Recording")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.vm.rename(root.pendingRecordingId, renameField.text)
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
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.vm.setTagsText(root.pendingRecordingId, tagsField.text)
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
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.captionSize
            }
        }
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
