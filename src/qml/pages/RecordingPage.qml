pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var vm
    objectName: "recordingPage"
    readonly property var detail: vm.recordingDetail
    readonly property var transcript: vm.transcript
    readonly property var player: vm.player
    readonly property string displayedRecordingStatus: UiText.recordingStatus(detail.status)
    readonly property bool compactInspector: width < 1040
    readonly property bool narrowTools: recordingMainPane.width < 680 * DesignSystem.textScale
    readonly property bool stackTransportOptions: recordingMainPane.width < 1040 * DesignSystem.textScale
    readonly property bool narrowTransport: recordingMainPane.width < 440 * DesignSystem.textScale
    readonly property int compactWaveformHeight: DesignSystem.compact ? 52 : 64
    readonly property var playbackRates: [0.5, 0.75, 1.0, 1.25, 1.5, 2.0]
    readonly property bool activeDraftDirty: {
        const activeSegment = segmentList.activeEditingSegment || segmentList.currentItem
        return activeSegment ? activeSegment.draftDirty : false
    }
    property bool compactInspectorOpen: false

    component ModeIconButton: IconButton {
        id: modeButton
        iconColor: checked ? SemanticTokens.accentStrong
                           : (enabled ? SemanticTokens.text : SemanticTokens.textMuted)
        background: Rectangle {
            radius: SemanticTokens.radiusSm
            color: modeButton.checked
                   ? (modeButton.down || modeButton.hovered
                      ? SemanticTokens.accentMuted : Qt.rgba(SemanticTokens.accent.r,
                                                             SemanticTokens.accent.g,
                                                             SemanticTokens.accent.b, 0.12))
                   : modeButton.down ? SemanticTokens.pressedTint
                   : modeButton.hovered ? SemanticTokens.hoverTint : "transparent"
            border.width: modeButton.activeFocus ? ComponentTokens.focusWidth : 0
            border.color: SemanticTokens.focusRing
            Behavior on color {
                ColorAnimation {
                    duration: SemanticTokens.animationFast
                    easing.type: SemanticTokens.easeStandard
                }
            }
        }
    }

    onCompactInspectorChanged: if (!compactInspector) compactInspectorOpen = false

    function requestTranscription() {
        if (!commitActiveEdit())
            return
        if (root.transcript.segmentCount > 0) {
            replaceTranscriptDialog.open()
            return
        }
        startTranscription()
    }

    function startTranscription() {
        root.vm.requestTranscription(root.vm.activeRecordingId)
    }

    function focusTranscriptSearch() {
        recordingTranscriptSearch.forceActiveFocus()
        recordingTranscriptSearch.selectAll()
    }

    function commitActiveEdit() {
        const currentSegment = segmentList.activeEditingSegment || segmentList.currentItem
        return !currentSegment || !currentSegment.commitDraft || currentSegment.commitDraft()
    }

    function saveTranscript() {
        if (!commitActiveEdit())
            return false
        return root.vm.flushActiveTranscript()
    }

    function requestExport() {
        if (!commitActiveEdit())
            return
        root.vm.exportActiveRecording()
    }

    Component {
        id: inspectorContentComponent

        ColumnLayout {
            spacing: SemanticTokens.spacingMd
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Recording")
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Duration"); color: SemanticTokens.textMuted; font.pixelSize: SemanticTokens.captionSize }
                    Item { Layout.fillWidth: true }
                    Text { text: UiText.timecode(root.detail.durationMs); color: SemanticTokens.text; font.pixelSize: SemanticTokens.bodySize }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Status"); color: SemanticTokens.textMuted; font.pixelSize: SemanticTokens.captionSize }
                    Item { Layout.fillWidth: true }
                    Text { text: root.displayedRecordingStatus; color: SemanticTokens.text; font.pixelSize: SemanticTokens.bodySize; elide: Text.ElideRight; Layout.maximumWidth: 170 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Model"); color: SemanticTokens.textMuted; font.pixelSize: SemanticTokens.captionSize }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: root.detail.model.length > 0 ? root.detail.model : qsTr("Not transcribed")
                        color: SemanticTokens.text
                        font.pixelSize: SemanticTokens.bodySize
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignRight
                        Layout.fillWidth: true
                        Layout.maximumWidth: 180
                    }
                }
            }
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Transcript")
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("%n segment(s)", "", root.transcript.segmentCount); color: SemanticTokens.text; font.pixelSize: SemanticTokens.bodySize }
                    Item { Layout.fillWidth: true }
                    Toggle { text: qsTr("Auto-scroll"); checked: root.player.autoScroll; onToggled: root.player.autoScroll = checked }
                }
            }
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Notes")
                TextArea {
                    objectName: "notesEditor"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 88
                    text: root.detail.notes
                    placeholderText: qsTr("Recording notes")
                    color: SemanticTokens.text
                    wrapMode: TextEdit.Wrap
                    font.pixelSize: SemanticTokens.bodySize
                    onActiveFocusChanged: if (!activeFocus && text !== root.detail.notes) root.detail.notes = text
                    background: Rectangle { color: SemanticTokens.surface; radius: SemanticTokens.radiusSm; border.color: SemanticTokens.border }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            objectName: "recordingHeader"
            Layout.fillWidth: true
            Layout.leftMargin: SemanticTokens.spacingMd
            Layout.rightMargin: SemanticTokens.spacingMd
            Layout.topMargin: SemanticTokens.spacingSm
            Layout.bottomMargin: SemanticTokens.spacingSm
            spacing: SemanticTokens.spacingSm
            AppLinkButton {
                objectName: "recordingBackButton"
                text: qsTr("← Library")
                accessibleName: qsTr("Back to Library")
                onClicked: {
                    if (root.commitActiveEdit())
                        root.vm.navigate("Library")
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: root.detail.title
                    color: SemanticTokens.text
                    elide: Text.ElideRight
                    font.pixelSize: SemanticTokens.headingSize
                    font.weight: SemanticTokens.weightSemiBold
                }
                RowLayout {
                    StatusBadge { text: root.displayedRecordingStatus; tone: "neutral" }
                    Text { text: root.detail.sourcePath; color: SemanticTokens.textMuted; elide: Text.ElideMiddle; font.pixelSize: SemanticTokens.captionSize; Layout.maximumWidth: 520 }
                }
            }
            BusyIndicator {
                objectName: "recordingModelDownloadSpinner"
                visible: root.vm.modelManager.defaultModelDownloadActive
                running: visible
                implicitWidth: 28
                implicitHeight: 28
                Accessible.name: qsTr("Downloading transcription model")
            }
            AppButton {
                objectName: "recordingTranscribeButton"
                text: root.vm.modelManager.defaultModelDownloadActive
                      ? qsTr("Downloading Q5_K…")
                      : root.transcript.editingLocked
                        ? qsTr("Transcribing…")
                        : root.transcript.segmentCount > 0
                          ? qsTr("Transcribe Again…") : qsTr("Start Transcription")
                enabled: !root.vm.modelManager.defaultModelDownloadActive
                         && !root.transcript.editingLocked
                primary: true
                onClicked: root.requestTranscription()
            }
            AppButton { text: qsTr("Export"); onClicked: root.requestExport() }
            AppButton {
                objectName: "recordingInspectorButton"
                visible: root.compactInspector
                text: qsTr("Details")
                accessibleName: qsTr("Show recording details")
                onClicked: root.compactInspectorOpen = true
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: SemanticTokens.border }
        RowLayout {
            objectName: "recordingWorkspace"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            ColumnLayout {
                id: recordingMainPane
                objectName: "recordingMainPane"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: SemanticTokens.spacingMd
                spacing: SemanticTokens.spacingSm
                Rectangle {
                    objectName: "recordingWaveformCard"
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.compactWaveformHeight
                    color: SemanticTokens.surfaceMuted
                    radius: SemanticTokens.radiusMd
                    WaveformItem {
                        id: waveform
                        anchors.fill: parent
                        anchors.margins: SemanticTokens.spacingSm
                        peaks: root.player.waveformPeaks
                        durationMs: root.player.duration
                        positionMs: root.player.position
                        selectionStartMs: root.player.selectionStart
                        selectionEndMs: root.player.selectionEnd
                        waveformColor: SemanticTokens.waveform
                        cursorColor: SemanticTokens.playhead
                        selectionColor: SemanticTokens.selection
                        Accessible.name: qsTr("Recording waveform")
                        onSeekRequested: function(positionMs) { root.player.position = positionMs }
                        onSelectionRequested: function(startMs, endMs) { root.player.selectionStart = startMs; root.player.selectionEnd = endMs }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }
                Rectangle {
                    id: transportCard
                    objectName: "recordingTransportCard"
                    Layout.fillWidth: true
                    Layout.preferredHeight: transportLayout.implicitHeight + SemanticTokens.spacingSm * 2
                    color: SemanticTokens.surface
                    radius: SemanticTokens.radiusLg
                    border.color: SemanticTokens.border
                    GridLayout {
                        id: transportLayout
                        anchors.fill: parent
                        anchors.margins: SemanticTokens.spacingSm
                        columns: root.narrowTransport ? 1 : root.stackTransportOptions ? 2 : 3
                        columnSpacing: SemanticTokens.spacingMd
                        rowSpacing: SemanticTokens.spacingXs

                        Rectangle {
                            objectName: "recordingPlaybackButtonSurface"
                            Layout.row: 0
                            Layout.column: 0
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: playbackButtons.implicitWidth + SemanticTokens.spacingXs * 2
                            Layout.preferredHeight: 44 * DesignSystem.textScale
                            color: SemanticTokens.surfaceMuted
                            radius: SemanticTokens.radiusMd
                            border.width: 1
                            border.color: SemanticTokens.border

                            RowLayout {
                                id: playbackButtons
                                objectName: "recordingPlaybackButtons"
                                anchors.centerIn: parent
                                spacing: SemanticTokens.spacingXs

                                IconButton {
                                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/rotate-ccw.svg"
                                    accessibleName: qsTr("Back 5 seconds")
                                    onClicked: root.player.skipBackward()
                                }
                                AppButton {
                                    objectName: "recordingPlayPauseButton"
                                    implicitWidth: ComponentTokens.clickTarget
                                    implicitHeight: ComponentTokens.clickTarget
                                    iconSize: 20
                                    iconSource: root.player.playing
                                                ? "qrc:/qt/qml/BreezeDesk/icons/lucide/pause.svg"
                                                : "qrc:/qt/qml/BreezeDesk/icons/lucide/play.svg"
                                    text: ""
                                    accessibleName: root.player.playing ? qsTr("Pause") : qsTr("Play")
                                    primary: true
                                    toolTipText: accessibleName + " · Space"
                                    onClicked: root.player.playPause()
                                }
                                IconButton {
                                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/rotate-cw.svg"
                                    accessibleName: qsTr("Forward 5 seconds")
                                    onClicked: root.player.skipForward()
                                }
                            }
                        }

                        RowLayout {
                            id: playbackTimeline
                            objectName: "recordingPlaybackTimeline"
                            Layout.row: root.narrowTransport ? 1 : 0
                            Layout.column: root.narrowTransport ? 0 : 1
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: SemanticTokens.spacingXs
                            TimeCode { milliseconds: root.player.position; enabled: false }
                            AppSlider {
                                objectName: "playbackPositionSlider"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 160 * DesignSystem.textScale
                                from: 0
                                to: Math.max(1, root.player.duration)
                                value: root.player.position
                                Accessible.name: qsTr("Playback position")
                                onMoved: root.player.position = value
                            }
                            TimeCode { milliseconds: root.player.duration; enabled: false }
                        }

                        GridLayout {
                            id: transportOptions
                            objectName: "recordingTransportOptions"
                            Layout.row: root.narrowTransport ? 2 : root.stackTransportOptions ? 1 : 0
                            Layout.column: root.stackTransportOptions ? 0 : 2
                            Layout.columnSpan: root.stackTransportOptions && !root.narrowTransport ? 2 : 1
                            Layout.fillWidth: root.stackTransportOptions
                            Layout.alignment: root.stackTransportOptions ? Qt.AlignLeft : Qt.AlignRight
                            columns: 4
                            columnSpacing: SemanticTokens.spacingXs
                            rowSpacing: SemanticTokens.spacingXs

                            AppComboBox {
                                objectName: "playbackRateComboBox"
                                Layout.preferredWidth: 78 * DesignSystem.textScale
                                Accessible.name: qsTr("Playback rate")
                                model: ["0.5×", "0.75×", "1×", "1.25×", "1.5×", "2×"]
                                currentIndex: root.playbackRates.indexOf(root.player.playbackRate) >= 0
                                              ? root.playbackRates.indexOf(root.player.playbackRate) : 2
                                onActivated: root.player.playbackRate = root.playbackRates[currentIndex]
                            }
                            ModeIconButton {
                                objectName: "muteToggle"
                                checked: root.player.muted
                                iconSource: checked
                                            ? "qrc:/qt/qml/BreezeDesk/icons/lucide/volume-x.svg"
                                            : "qrc:/qt/qml/BreezeDesk/icons/lucide/volume-2.svg"
                                accessibleName: checked ? qsTr("Unmute") : qsTr("Mute")
                                onClicked: root.player.muted = !root.player.muted
                            }
                            AppSlider {
                                objectName: "volumeSlider"
                                Layout.preferredWidth: 104 * DesignSystem.textScale
                                from: 0
                                to: 1
                                value: root.player.volume
                                enabled: !root.player.muted
                                Accessible.name: qsTr("Volume")
                                onMoved: root.player.volume = value
                            }
                            ModeIconButton {
                                objectName: "recordingLoopSelectionButton"
                                checked: root.player.loopSelection
                                iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/repeat-2.svg"
                                accessibleName: checked ? qsTr("Disable loop selection")
                                                        : qsTr("Loop selection")
                                onClicked: root.player.loopSelection = !root.player.loopSelection
                            }
                        }
                    }
                }
                Rectangle {
                    id: transcriptToolbar
                    objectName: "recordingTranscriptToolbar"
                    Layout.fillWidth: true
                    Layout.preferredHeight: transcriptTools.implicitHeight + SemanticTokens.spacingSm
                    color: SemanticTokens.surfaceMuted
                    radius: SemanticTokens.radiusMd
                    border.width: 1
                    border.color: SemanticTokens.border

                    GridLayout {
                        id: transcriptTools
                        objectName: "recordingTranscriptTools"
                        anchors.fill: parent
                        anchors.margins: SemanticTokens.spacingXs
                        columns: 1
                        columnSpacing: SemanticTokens.spacingSm
                        rowSpacing: SemanticTokens.spacingXs

                        GridLayout {
                            id: transcriptCommands
                            objectName: "recordingTranscriptCommands"
                            Layout.row: 0
                            Layout.column: 0
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            columns: root.narrowTools ? 1 : 2
                            columnSpacing: SemanticTokens.spacingSm
                            rowSpacing: SemanticTokens.spacingXs

                            RowLayout {
                                objectName: "recordingTranscriptSearchRow"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: SemanticTokens.spacingXs
                                AppSearchField {
                                    id: recordingTranscriptSearch
                                    objectName: "recordingTranscriptSearch"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: root.transcript.searchText
                                    placeholderText: qsTr("Find in transcript")
                                    onTextEdited: root.transcript.searchText = text
                                }
                                IconButton {
                                    objectName: "recordingPreviousButton"
                                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/chevron-up.svg"
                                    accessibleName: qsTr("Previous")
                                    enabled: root.transcript.visibleSegmentCount > 0
                                    onClicked: root.transcript.selectedIndex = root.transcript.findPrevious(root.transcript.selectedIndex)
                                }
                                IconButton {
                                    objectName: "recordingNextButton"
                                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/chevron-down.svg"
                                    accessibleName: qsTr("Next")
                                    enabled: root.transcript.visibleSegmentCount > 0
                                    onClicked: root.transcript.selectedIndex = root.transcript.findNext(root.transcript.selectedIndex)
                                }
                                Toggle {
                                    objectName: "recordingLowConfidenceToggle"
                                    text: qsTr("Low-confidence only")
                                    checked: root.transcript.lowConfidenceOnly
                                    onToggled: root.transcript.lowConfidenceOnly = checked
                                }
                            }

                            RowLayout {
                                objectName: "recordingTranscriptActionRow"
                                Layout.fillWidth: root.narrowTools
                                Layout.alignment: Qt.AlignRight
                                spacing: SemanticTokens.spacingXs
                                StatusBadge {
                                    objectName: "recordingEditingLockedBadge"
                                    visible: root.transcript.editingLocked
                                    text: qsTr("Transcribing…")
                                    tone: "accent"
                                }
                                Item { visible: root.narrowTools; Layout.fillWidth: root.narrowTools }
                                IconButton {
                                    objectName: "recordingUndoButton"
                                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/undo-2.svg"
                                    accessibleName: qsTr("Undo")
                                    toolTipText: accessibleName + " · Ctrl+Z"
                                    enabled: root.transcript.canUndo
                                    onClicked: root.transcript.undo()
                                }
                                IconButton {
                                    objectName: "recordingRedoButton"
                                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/redo-2.svg"
                                    accessibleName: qsTr("Redo")
                                    toolTipText: accessibleName + " · Ctrl+Shift+Z"
                                    enabled: root.transcript.canRedo
                                    onClicked: root.transcript.redo()
                                }
                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.leftMargin: SemanticTokens.spacingXs
                                    Layout.rightMargin: SemanticTokens.spacingXs
                                    implicitWidth: 1
                                    implicitHeight: 24 * DesignSystem.textScale
                                    color: SemanticTokens.border
                                }
                                IconButton {
                                    objectName: "recordingCopyTranscriptButton"
                                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/copy.svg"
                                    accessibleName: qsTr("Copy Transcript")
                                    enabled: root.transcript.segmentCount > 0
                                    onClicked: {
                                        if (!root.commitActiveEdit())
                                            return
                                        root.vm.copyToClipboard(root.transcript.fullText())
                                        root.vm.showToast(qsTr("Transcript copied to clipboard."))
                                    }
                                }
                                IconButton {
                                    objectName: "recordingSaveButton"
                                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/save.svg"
                                    accessibleName: root.transcript.dirty || root.activeDraftDirty
                                                    ? qsTr("Save Changes") : qsTr("Saved")
                                    toolTipText: accessibleName + " · Ctrl+S"
                                    enabled: root.transcript.dirty || root.activeDraftDirty
                                    onClicked: root.saveTranscript()
                                }
                            }
                        }
                    }
                }
                EmptyState {
                    objectName: "recordingNoTranscriptState"
                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/list-ordered.svg"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.transcript.segmentCount === 0
                    title: qsTr("No completed transcript yet")
                    description: root.transcript.editingLocked
                                 ? qsTr("Transcription is in progress. The transcript will appear when processing completes.")
                                 : qsTr("Start transcription to create an editable transcript.")
                    actionText: root.transcript.editingLocked ? "" : qsTr("Start Transcription")
                    onActionTriggered: root.requestTranscription()
                }
                EmptyState {
                    objectName: "recordingNoMatchesState"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.transcript.segmentCount > 0
                             && root.transcript.visibleSegmentCount === 0
                    title: qsTr("No matching segments")
                    description: qsTr("Clear the search or low-confidence filter to show the transcript.")
                    actionText: qsTr("Clear Filters")
                    onActionTriggered: {
                        root.transcript.searchText = ""
                        root.transcript.lowConfidenceOnly = false
                    }
                }
                ListView {
                    id: segmentList
                    objectName: "segmentList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.transcript.visibleSegmentCount > 0
                    model: root.transcript.segments
                    spacing: 0
                    clip: true
                    reuseItems: true
                    cacheBuffer: height
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    currentIndex: root.transcript.selectedIndex
                    property var activeEditingSegment: null
                    onCurrentIndexChanged: {
                        if (activeFocus && currentIndex >= 0
                                && root.transcript.selectedIndex !== currentIndex) {
                            root.transcript.selectedIndex = currentIndex
                        }
                    }
                    Keys.onReturnPressed: if (currentIndex >= 0)
                                              root.transcript.selectedIndex = currentIndex
                    Keys.onEnterPressed: if (currentIndex >= 0)
                                             root.transcript.selectedIndex = currentIndex
                    ScrollBar.vertical: ScrollBar { }
                    delegate: SegmentEditor {
                        id: segmentDelegate
                        objectName: "segmentEditor"
                        width: ListView.view.width
                        editingLocked: root.transcript.editingLocked
                        ListView.onPooled: {
                            if (segmentList.activeEditingSegment === segmentDelegate)
                                segmentList.activeEditingSegment = null
                            editing = false
                        }
                        onEditingChanged: {
                            if (editing) {
                                segmentList.activeEditingSegment = segmentDelegate
                            } else if (segmentList.activeEditingSegment === segmentDelegate) {
                                segmentList.activeEditingSegment = null
                            }
                        }
                        selected: root.transcript.selectedIndex === proxyRow
                                  || root.transcript.activePlaybackIndex === proxyRow
                        onSelectedRequested: function(segmentIndex) {
                            if (root.commitActiveEdit())
                                root.transcript.selectedIndex = segmentIndex
                        }
                        onSeekRequested: function(position) {
                            root.player.position = position
                        }
                        onTextEdited: function(editor, segmentId, text) {
                            editor.resolveCommit(root.transcript.editTextById(segmentId, text))
                        }
                        onDraftChanged: root.vm.scheduleActiveTranscriptAutosave()
                        onSplitRequested: function(segmentIndex) {
                            root.transcript.splitAt(segmentIndex, root.player.position)
                        }
                        onMergePreviousRequested: function(segmentIndex) {
                            root.transcript.mergePrevious(segmentIndex)
                        }
                        onMergeNextRequested: function(segmentIndex) {
                            root.transcript.mergeNext(segmentIndex)
                        }
                        onDeleteRequested: function(segmentIndex) {
                            root.transcript.remove(segmentIndex)
                        }
                        onReviewedRequested: function(segmentIndex, reviewed) {
                            root.transcript.markReviewed(segmentIndex, reviewed)
                        }
                        onGlossaryReplacementRequested: function(segmentIndex, replacementIndex, applied) {
                            root.transcript.setGlossaryReplacementApplied(segmentIndex, replacementIndex,
                                                                         applied)
                        }
                    }
                    Connections {
                        target: root.transcript
                        function onActivePlaybackIndexChanged() {
                            if (root.player.autoScroll && root.transcript.activePlaybackIndex >= 0)
                                segmentList.positionViewAtIndex(root.transcript.activePlaybackIndex, ListView.Contain)
                        }
                    }
                }
            }
            Rectangle {
                id: desktopInspector
                objectName: "recordingInspector"
                visible: !root.compactInspector
                Layout.preferredWidth: visible ? ComponentTokens.inspectorWidth : 0
                Layout.minimumWidth: visible ? ComponentTokens.inspectorWidth : 0
                Layout.maximumWidth: visible ? ComponentTokens.inspectorWidth : 0
                Layout.fillHeight: true
                color: SemanticTokens.surfaceMuted
                Loader {
                    anchors.fill: parent
                    anchors.margins: SemanticTokens.spacingMd
                    active: !root.compactInspector
                    sourceComponent: inspectorContentComponent
                }
            }
        }
    }

    AppDialog {
        id: replaceTranscriptDialog
        objectName: "replaceTranscriptDialog"
        title: qsTr("Replace the current transcript?")
        subtitle: qsTr("Transcribing this recording again will replace the current transcript when the new one is ready.")
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/list-ordered.svg"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: root.startTranscription()
    }

    Item {
        id: compactInspectorOverlay
        objectName: "recordingInspectorOverlay"
        anchors.fill: parent
        visible: root.compactInspector && root.compactInspectorOpen
        z: 100

        Rectangle {
            anchors.fill: parent
            color: SemanticTokens.scrim
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                Accessible.name: qsTr("Close recording details")
                onClicked: root.compactInspectorOpen = false
            }
        }

        Rectangle {
            id: compactInspectorPanel
            objectName: "recordingCompactInspector"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: Math.min(ComponentTokens.inspectorWidth + SemanticTokens.spacingLg * 2,
                            root.width - SemanticTokens.spacingLg * 2)
            color: SemanticTokens.surfaceMuted
            border.color: SemanticTokens.border

            MouseArea { anchors.fill: parent }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: SemanticTokens.spacingMd
                spacing: SemanticTokens.spacingMd
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Details")
                        color: SemanticTokens.text
                        font.pixelSize: SemanticTokens.headingSize
                        font.weight: SemanticTokens.weightSemiBold
                    }
                    AppButton {
                        objectName: "recordingInspectorCloseButton"
                        text: qsTr("Close")
                        onClicked: root.compactInspectorOpen = false
                    }
                }
                Loader {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: root.compactInspector
                    sourceComponent: inspectorContentComponent
                }
            }
        }
    }
}
