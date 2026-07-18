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
    readonly property bool narrowTimeline: recordingMainPane.width < 440 * DesignSystem.textScale
    readonly property bool narrowTransportOptions: recordingMainPane.width < 440 * DesignSystem.textScale
    readonly property int compactWaveformHeight: DesignSystem.compact ? 52 : 64
    property bool compactInspectorOpen: false

    onCompactInspectorChanged: if (!compactInspector) compactInspectorOpen = false

    Component {
        id: inspectorContentComponent

        ColumnLayout {
            spacing: SemanticTokens.spacingMd
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Recording")
                TimeCode { id: durationCode; visible: false; milliseconds: root.detail.durationMs }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Duration"); color: SemanticTokens.textMuted; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.captionSize }
                    Item { Layout.fillWidth: true }
                    Text { text: durationCode.text; color: SemanticTokens.text; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.bodySize }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Status"); color: SemanticTokens.textMuted; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.captionSize }
                    Item { Layout.fillWidth: true }
                    Text { text: root.displayedRecordingStatus; color: SemanticTokens.text; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.bodySize; elide: Text.ElideRight; Layout.maximumWidth: 170 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Model"); color: SemanticTokens.textMuted; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.captionSize }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: root.detail.model.length > 0 ? root.detail.model : qsTr("Not transcribed")
                        color: SemanticTokens.text
                        font.family: SemanticTokens.fontFamily
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
                    Text { text: qsTr("%n segment(s)", "", root.transcript.segmentCount); color: SemanticTokens.text; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.bodySize }
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
                    font.family: SemanticTokens.fontFamily
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
            ColumnLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: root.detail.title
                    color: SemanticTokens.text
                    elide: Text.ElideRight
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.headingSize
                    font.weight: Font.DemiBold
                }
                RowLayout {
                    StatusBadge { text: root.displayedRecordingStatus; tone: "neutral" }
                    Text { text: root.detail.sourcePath; color: SemanticTokens.textMuted; elide: Text.ElideMiddle; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.captionSize; Layout.maximumWidth: 520 }
                }
            }
            AppButton { text: qsTr("Transcribe"); primary: true; onClicked: root.vm.enqueueTranscription(root.vm.activeRecordingId) }
            AppButton { text: qsTr("Export"); onClicked: root.vm.exportActiveRecording() }
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
                    }
                }
                Rectangle {
                    id: transportCard
                    objectName: "recordingTransportCard"
                    Layout.fillWidth: true
                    Layout.preferredHeight: transportLayout.implicitHeight + SemanticTokens.spacingSm * 2
                    color: SemanticTokens.surface
                    radius: SemanticTokens.radiusMd
                    border.color: SemanticTokens.border
                    ColumnLayout {
                        id: transportLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: SemanticTokens.spacingSm
                        spacing: SemanticTokens.spacingSm

                        GridLayout {
                            id: playbackTimeline
                            objectName: "recordingPlaybackTimeline"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.maximumWidth: transportCard.width - SemanticTokens.spacingSm * 2
                            columns: root.narrowTimeline ? 3 : 4
                            columnSpacing: SemanticTokens.spacingSm
                            rowSpacing: SemanticTokens.spacingXs

                            Row {
                                id: playbackButtons
                                objectName: "recordingPlaybackButtons"
                                Layout.columnSpan: root.narrowTimeline ? 3 : 1
                                Layout.alignment: root.narrowTimeline ? Qt.AlignHCenter : Qt.AlignVCenter
                                spacing: SemanticTokens.spacingSm

                                IconButton {
                                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/rotate-ccw.svg"
                                    accessibleName: qsTr("Back 5 seconds")
                                    ToolTip.visible: hovered
                                    ToolTip.text: accessibleName
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
                                    ToolTip.visible: hovered
                                    ToolTip.text: accessibleName
                                    onClicked: root.player.playPause()
                                }
                                IconButton {
                                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/rotate-cw.svg"
                                    accessibleName: qsTr("Forward 5 seconds")
                                    ToolTip.visible: hovered
                                    ToolTip.text: accessibleName
                                    onClicked: root.player.skipForward()
                                }
                            }

                            TimeCode { milliseconds: root.player.position; enabled: false }
                            AppSlider {
                                objectName: "playbackPositionSlider"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 160
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
                            Layout.fillWidth: true
                            columns: root.narrowTransportOptions ? 2 : 4
                            columnSpacing: SemanticTokens.spacingSm
                            rowSpacing: SemanticTokens.spacingXs

                            AppComboBox {
                                objectName: "playbackRateComboBox"
                                Layout.fillWidth: root.narrowTransportOptions
                                Layout.preferredWidth: 86
                                Accessible.name: qsTr("Playback rate")
                                model: ["0.5×", "0.75×", "1×", "1.25×", "1.5×", "2×"]
                                currentIndex: root.player.playbackRate === 0.5 ? 0 : root.player.playbackRate === 0.75 ? 1 : root.player.playbackRate === 1.25 ? 3 : root.player.playbackRate === 1.5 ? 4 : root.player.playbackRate === 2 ? 5 : 2
                                onActivated: root.player.playbackRate = [0.5, 0.75, 1.0, 1.25, 1.5, 2.0][currentIndex]
                            }
                            Toggle {
                                objectName: "muteToggle"
                                text: qsTr("Mute")
                                checked: root.player.muted
                                onToggled: root.player.muted = checked
                            }
                            AppSlider {
                                objectName: "volumeSlider"
                                Layout.fillWidth: root.narrowTransportOptions
                                Layout.preferredWidth: 92
                                from: 0
                                to: 1
                                value: root.player.volume
                                enabled: !root.player.muted
                                Accessible.name: qsTr("Volume")
                                onMoved: root.player.volume = value
                            }
                            Toggle { text: qsTr("Loop selection"); checked: root.player.loopSelection; onToggled: root.player.loopSelection = checked }
                        }
                    }
                }
                ColumnLayout {
                    id: transcriptToolbar
                    objectName: "recordingTranscriptToolbar"
                    Layout.fillWidth: true
                    spacing: SemanticTokens.spacingSm

                    StatusBadge {
                        objectName: "recordingEditingLockedBadge"
                        visible: root.transcript.editingLocked
                        text: qsTr("Live transcription — editing locked")
                        tone: "accent"
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.narrowTools ? 1 : 2
                        columnSpacing: SemanticTokens.spacingSm
                        rowSpacing: SemanticTokens.spacingXs

                        RowLayout {
                            objectName: "recordingTranscriptSearchRow"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: SemanticTokens.spacingSm
                            AppSearchField {
                                objectName: "recordingTranscriptSearch"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: root.transcript.searchText
                                placeholderText: qsTr("Find in transcript")
                                onTextEdited: root.transcript.searchText = text
                            }
                            Toggle { objectName: "recordingLowConfidenceToggle"; text: qsTr("Low confidence"); checked: root.transcript.lowConfidenceOnly; onToggled: root.transcript.lowConfidenceOnly = checked }
                        }

                        RowLayout {
                            objectName: "recordingTranscriptActionRow"
                            Layout.fillWidth: root.narrowTools
                            Layout.alignment: Qt.AlignRight
                            spacing: SemanticTokens.spacingSm
                            IconButton {
                                objectName: "recordingPreviousButton"
                                iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/chevron-up.svg"
                                accessibleName: qsTr("Previous")
                                enabled: root.transcript.visibleSegmentCount > 0
                                ToolTip.visible: hovered
                                ToolTip.text: accessibleName
                                onClicked: root.transcript.selectedIndex = root.transcript.findPrevious(root.transcript.selectedIndex)
                            }
                            IconButton {
                                objectName: "recordingNextButton"
                                iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/chevron-down.svg"
                                accessibleName: qsTr("Next")
                                enabled: root.transcript.visibleSegmentCount > 0
                                ToolTip.visible: hovered
                                ToolTip.text: accessibleName
                                onClicked: root.transcript.selectedIndex = root.transcript.findNext(root.transcript.selectedIndex)
                            }
                            Item { visible: root.narrowTools; Layout.fillWidth: root.narrowTools }
                            IconButton {
                                objectName: "recordingUndoButton"
                                iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/undo-2.svg"
                                accessibleName: qsTr("Undo")
                                enabled: root.transcript.canUndo
                                ToolTip.visible: hovered
                                ToolTip.text: accessibleName
                                onClicked: root.transcript.undo()
                            }
                            IconButton {
                                objectName: "recordingRedoButton"
                                iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/redo-2.svg"
                                accessibleName: qsTr("Redo")
                                enabled: root.transcript.canRedo
                                ToolTip.visible: hovered
                                ToolTip.text: accessibleName
                                onClicked: root.transcript.redo()
                            }
                            IconButton {
                                objectName: "recordingSaveButton"
                                iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/save.svg"
                                accessibleName: root.transcript.dirty ? qsTr("Save Changes") : qsTr("Saved")
                                enabled: root.transcript.dirty
                                ToolTip.visible: hovered
                                ToolTip.text: accessibleName
                                onClicked: root.transcript.save()
                            }
                        }
                    }
                }
                EmptyState {
                    objectName: "recordingNoTranscriptState"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.transcript.segmentCount === 0
                    title: qsTr("No transcript yet")
                    description: qsTr("Add this recording to the queue. Partial segments will appear here as each long-form unit completes.")
                    actionText: qsTr("Add to Queue")
                    onActionTriggered: root.vm.enqueueTranscription(root.vm.activeRecordingId)
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
                        objectName: "segmentEditor"
                        width: ListView.view.width
                        editingLocked: root.transcript.editingLocked
                        selected: root.transcript.selectedIndex === proxyRow
                                  || root.transcript.activePlaybackIndex === proxyRow
                        onSelectedRequested: function(segmentIndex) {
                            root.transcript.selectedIndex = segmentIndex
                        }
                        onSeekRequested: function(position) {
                            root.player.position = position
                        }
                        onTextEdited: function(segmentIndex, text) {
                            root.transcript.editText(segmentIndex, text)
                        }
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

    Item {
        id: compactInspectorOverlay
        objectName: "recordingInspectorOverlay"
        anchors.fill: parent
        visible: root.compactInspector && root.compactInspectorOpen
        z: 100

        Rectangle {
            anchors.fill: parent
            color: SemanticTokens.window
            opacity: 0.72
            MouseArea {
                anchors.fill: parent
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
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.headingSize
                        font.weight: Font.DemiBold
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
