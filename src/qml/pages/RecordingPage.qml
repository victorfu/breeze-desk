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
    readonly property bool compactInspector: width < 1040
    property bool compactInspectorOpen: false

    onCompactInspectorChanged: if (!compactInspector) compactInspectorOpen = false

    Component {
        id: inspectorContentComponent

        ColumnLayout {
            spacing: SemanticTokens.spacingLg
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Recording")
                Text { text: qsTr("Duration: %1").arg(durationCode.text); color: SemanticTokens.text; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.bodySize }
                TimeCode { id: durationCode; visible: false; milliseconds: root.detail.durationMs }
                Text { text: qsTr("Status: %1").arg(root.detail.status); color: SemanticTokens.text; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.bodySize }
                Text { text: qsTr("Model: %1").arg(root.detail.model.length > 0 ? root.detail.model : qsTr("Not transcribed")); color: SemanticTokens.text; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.bodySize }
            }
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Transcript")
                Text { text: qsTr("%n segment(s)", "", root.transcript.segmentCount); color: SemanticTokens.text; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.bodySize }
                Toggle { text: qsTr("Auto-scroll"); checked: root.player.autoScroll; onToggled: root.player.autoScroll = checked }
            }
            InspectorSection {
                Layout.fillWidth: true
                title: qsTr("Notes")
                TextArea {
                    objectName: "notesEditor"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
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
            Layout.margins: SemanticTokens.spacingLg
            spacing: SemanticTokens.spacingMd
            ColumnLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: root.detail.title
                    color: SemanticTokens.text
                    elide: Text.ElideRight
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.titleSize
                    font.weight: Font.DemiBold
                }
                RowLayout {
                    StatusBadge { text: root.detail.status; tone: "neutral" }
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
                objectName: "recordingMainPane"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: SemanticTokens.spacingLg
                spacing: SemanticTokens.spacingMd
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: ComponentTokens.waveformHeight
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

                        ColumnLayout {
                            id: transportActions
                            objectName: "recordingTransportActions"
                            Layout.fillWidth: true
                            spacing: SemanticTokens.spacingSm

                            Row {
                                id: playbackButtons
                                objectName: "recordingPlaybackButtons"
                                Layout.alignment: Qt.AlignVCenter
                                spacing: SemanticTokens.spacingSm

                                AppButton { iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/rotate-ccw.svg"; text: qsTr("−5 s"); accessibleName: qsTr("Back 5 seconds"); onClicked: root.player.skipBackward() }
                                AppButton { iconSource: root.player.playing ? "qrc:/qt/qml/BreezeDesk/icons/lucide/pause.svg" : "qrc:/qt/qml/BreezeDesk/icons/lucide/play.svg"; text: root.player.playing ? qsTr("Pause") : qsTr("Play"); primary: true; onClicked: root.player.playPause() }
                                AppButton { iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/rotate-cw.svg"; text: qsTr("+5 s"); accessibleName: qsTr("Forward 5 seconds"); onClicked: root.player.skipForward() }
                            }

                            RowLayout {
                                id: playbackTimeline
                                objectName: "recordingPlaybackTimeline"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: SemanticTokens.spacingSm

                                TimeCode { milliseconds: root.player.position; enabled: false }
                                Slider {
                                    objectName: "playbackPositionSlider"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    from: 0
                                    to: Math.max(1, root.player.duration)
                                    value: root.player.position
                                    Accessible.name: qsTr("Playback position")
                                    onMoved: root.player.position = value
                                }
                                TimeCode { milliseconds: root.player.duration; enabled: false }
                            }
                        }

                        RowLayout {
                            id: transportOptions
                            objectName: "recordingTransportOptions"
                            Layout.fillWidth: true
                            spacing: SemanticTokens.spacingSm

                            AppComboBox {
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
                            Slider {
                                objectName: "volumeSlider"
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
                    RowLayout {
                        objectName: "recordingTranscriptSearchRow"
                        Layout.fillWidth: true
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
                        Layout.fillWidth: true
                        spacing: SemanticTokens.spacingSm
                        AppButton { objectName: "recordingPreviousButton"; text: qsTr("Previous"); enabled: root.transcript.segmentCount > 0; onClicked: root.transcript.selectedIndex = root.transcript.findPrevious(root.transcript.selectedIndex) }
                        AppButton { objectName: "recordingNextButton"; text: qsTr("Next"); enabled: root.transcript.segmentCount > 0; onClicked: root.transcript.selectedIndex = root.transcript.findNext(root.transcript.selectedIndex) }
                        Item { Layout.fillWidth: true }
                        AppButton { objectName: "recordingUndoButton"; text: qsTr("Undo"); enabled: root.transcript.canUndo; onClicked: root.transcript.undo() }
                        AppButton { objectName: "recordingRedoButton"; text: qsTr("Redo"); enabled: root.transcript.canRedo; onClicked: root.transcript.redo() }
                        AppButton { objectName: "recordingSaveButton"; text: root.transcript.dirty ? qsTr("Save Changes") : qsTr("Saved"); enabled: root.transcript.dirty; onClicked: root.transcript.save() }
                    }
                }
                EmptyState {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.transcript.segmentCount === 0
                    title: qsTr("No transcript yet")
                    description: qsTr("Add this recording to the queue. Partial segments will appear here as each long-form unit completes.")
                    actionText: qsTr("Add to Queue")
                    onActionTriggered: root.vm.enqueueTranscription(root.vm.activeRecordingId)
                }
                ListView {
                    id: segmentList
                    objectName: "segmentList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.transcript.segmentCount > 0
                    model: root.transcript.segments
                    spacing: SemanticTokens.spacingSm
                    clip: true
                    reuseItems: true
                    cacheBuffer: height
                    keyNavigationEnabled: true
                    ScrollBar.vertical: ScrollBar { }
                    delegate: SegmentEditor {
                        width: ListView.view.width
                        modelIndex: index
                        editingLocked: root.transcript.editingLocked
                        selected: root.transcript.selectedIndex === index || root.transcript.activePlaybackIndex === index
                        onSelectedRequested: root.transcript.selectedIndex = index
                        onSeekRequested: root.player.position = position
                        onTextEdited: root.transcript.editText(index, text)
                        onSplitRequested: root.transcript.splitAt(index, root.player.position)
                        onMergePreviousRequested: root.transcript.mergePrevious(index)
                        onMergeNextRequested: root.transcript.mergeNext(index)
                        onDeleteRequested: root.transcript.remove(index)
                        onReviewedRequested: root.transcript.markReviewed(index, reviewed)
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
