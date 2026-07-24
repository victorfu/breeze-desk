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
    readonly property var revisions: vm.transcriptRevisions
    readonly property var player: vm.player
    readonly property string displayedRecordingStatus: UiText.recordingStatus(detail.status)
    readonly property bool compactInspector: width < 1040
    readonly property bool narrowTools: recordingMainPane.width < 680 * DesignSystem.textScale
    readonly property bool stackTranscriptTools: recordingMainPane.width < 1160 * DesignSystem.textScale
    readonly property bool stackTransportOptions: recordingMainPane.width < 1040 * DesignSystem.textScale
    readonly property bool narrowTransport: recordingMainPane.width < 440 * DesignSystem.textScale
    readonly property int compactWaveformHeight: DesignSystem.compact ? 52 : 64
    readonly property var playbackRates: [0.5, 0.75, 1.0, 1.25, 1.5, 2.0]
    property bool compactInspectorOpen: false
    property string pendingDeleteRevisionId: ""
    property var pendingDeleteRevision: ({})

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
        root.vm.requestTranscription(root.vm.activeRecordingId)
    }

    function focusTranscriptSearch() {
        recordingTranscriptSearch.forceActiveFocus()
        recordingTranscriptSearch.selectAll()
    }

    function requestRevisionDeletion(jobId) {
        root.pendingDeleteRevisionId = jobId
        root.pendingDeleteRevision = root.vm.transcriptRevisionDetails(jobId)
        deleteRevisionDialog.open()
    }

    AppDialog {
        id: revisionHistoryDialog
        objectName: "transcriptRevisionHistoryDialog"
        title: qsTr("Transcript history")
        subtitle: qsTr("Each transcription creates a separate version. Viewing a version does not replace the latest completed transcript.")
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/list-ordered.svg"
        standardButtons: Dialog.Close
        width: Math.min(640 * DesignSystem.textScale,
                        Overlay.overlay.width - SemanticTokens.spacingXl * 2)

        ColumnLayout {
            width: parent.width
            spacing: SemanticTokens.spacingSm

            Text {
                Layout.fillWidth: true
                visible: root.revisions.count === 0
                text: qsTr("No transcript versions are available yet.")
                color: SemanticTokens.textMuted
                wrapMode: Text.WordWrap
                font.pixelSize: SemanticTokens.bodySize
            }

            ListView {
                id: revisionHistoryList
                objectName: "transcriptRevisionHistoryList"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(Math.max(contentHeight, 80 * DesignSystem.textScale),
                                                 420 * DesignSystem.textScale)
                visible: root.revisions.count > 0
                model: root.revisions.revisions
                spacing: SemanticTokens.spacingSm
                clip: true
                reuseItems: true
                ScrollBar.vertical: ScrollBar { }

                delegate: Rectangle {
                    id: revisionRow
                    required property string jobId
                    required property int revisionNumber
                    required property string jobState
                    required property var createdAt
                    required property int segmentCount
                    required property bool hasManualEdits
                    required property bool hasProvisionalSegments
                    required property string latestText
                    required property bool isActiveRevision
                    required property bool isSelectedRevision
                    required property bool isRunning
                    required property bool canDelete
                    required property string modelId
                    required property string backend
                    required property string language
                    required property string preset
                    required property string errorMessage

                    width: ListView.view.width
                    height: Math.max(104 * DesignSystem.textScale,
                                     revisionRowLayout.implicitHeight + SemanticTokens.spacingMd * 2)
                    color: isSelectedRevision ? SemanticTokens.accentMuted : SemanticTokens.surface
                    radius: SemanticTokens.radiusMd
                    border.color: isSelectedRevision ? SemanticTokens.accent : SemanticTokens.border

                    RowLayout {
                        id: revisionRowLayout
                        anchors.fill: parent
                        anchors.margins: SemanticTokens.spacingMd
                        spacing: SemanticTokens.spacingSm

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: SemanticTokens.spacingXs

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: SemanticTokens.spacingXs
                                Text {
                                    text: qsTr("Version %1").arg(revisionRow.revisionNumber)
                                    color: SemanticTokens.text
                                    font.pixelSize: SemanticTokens.bodySize
                                    font.weight: SemanticTokens.weightSemiBold
                                }
                                StatusBadge {
                                    text: UiText.jobState(revisionRow.jobState)
                                    tone: revisionRow.jobState === "Completed" ? "success"
                                          : revisionRow.jobState === "Failed" ? "danger"
                                          : revisionRow.isRunning ? "accent" : "warning"
                                }
                                StatusBadge {
                                    visible: revisionRow.isActiveRevision
                                    text: qsTr("Latest completed")
                                    tone: "success"
                                }
                                StatusBadge {
                                    visible: revisionRow.isSelectedRevision
                                    text: qsTr("Viewing")
                                    tone: "accent"
                                }
                                Item { Layout.fillWidth: true }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("%1 · %n segment(s)", "", revisionRow.segmentCount)
                                      .arg(UiText.shortDateTime(revisionRow.createdAt))
                                color: SemanticTokens.textMuted
                                elide: Text.ElideRight
                                font.pixelSize: SemanticTokens.captionSize
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: revisionRow.modelId.length > 0 || revisionRow.backend.length > 0
                                         || revisionRow.language.length > 0
                                         || revisionRow.preset.length > 0
                                text: [revisionRow.modelId,
                                       revisionRow.backend,
                                       revisionRow.language.length > 0
                                           ? qsTr("Language: %1").arg(revisionRow.language) : "",
                                       revisionRow.preset.length > 0
                                           ? qsTr("Preset: %1").arg(revisionRow.preset) : ""].filter(function(value) {
                                    return value.length > 0
                                }).join(" · ")
                                color: SemanticTokens.textMuted
                                elide: Text.ElideRight
                                font.pixelSize: SemanticTokens.captionSize
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: revisionRow.latestText.length > 0
                                text: revisionRow.latestText
                                color: SemanticTokens.text
                                elide: Text.ElideRight
                                font.pixelSize: SemanticTokens.captionSize
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: revisionRow.errorMessage.length > 0
                                text: revisionRow.errorMessage
                                color: SemanticTokens.danger
                                elide: Text.ElideRight
                                font.pixelSize: SemanticTokens.captionSize
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: revisionRow.hasManualEdits || revisionRow.hasProvisionalSegments
                                text: revisionRow.hasManualEdits && revisionRow.hasProvisionalSegments
                                      ? qsTr("Contains manual edits and partial results")
                                      : revisionRow.hasManualEdits ? qsTr("Contains manual edits")
                                                                   : qsTr("Contains partial results")
                                color: SemanticTokens.warning
                                elide: Text.ElideRight
                                font.pixelSize: SemanticTokens.captionSize
                            }
                        }

                        AppButton {
                            objectName: "viewTranscriptRevisionButton"
                            text: revisionRow.isSelectedRevision ? qsTr("Viewing") : qsTr("View")
                            enabled: !revisionRow.isSelectedRevision
                            onClicked: {
                                root.vm.selectTranscriptRevision(revisionRow.jobId)
                                if (root.revisions.selectedJobId === revisionRow.jobId)
                                    revisionHistoryDialog.close()
                            }
                        }
                        RemoveButton {
                            objectName: "deleteTranscriptRevisionButton"
                            visible: revisionRow.canDelete
                            accessibleName: qsTr("Delete version %1 permanently")
                                            .arg(revisionRow.revisionNumber)
                            onClicked: root.requestRevisionDeletion(revisionRow.jobId)
                        }
                    }
                }
            }
        }
    }

    AppDialog {
        id: deleteRevisionDialog
        objectName: "deleteTranscriptRevisionDialog"
        title: root.pendingDeleteRevision.revisionNumber === undefined
               ? qsTr("Delete transcript version permanently?")
               : qsTr("Delete version %1 permanently?")
                 .arg(root.pendingDeleteRevision.revisionNumber)
        subtitle: qsTr("This cannot be undone.")
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/trash-2.svg"
        destructive: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            root.vm.deleteTranscriptRevision(root.pendingDeleteRevisionId)
            root.pendingDeleteRevisionId = ""
            root.pendingDeleteRevision = ({})
        }
        onRejected: {
            root.pendingDeleteRevisionId = ""
            root.pendingDeleteRevision = ({})
        }

        ColumnLayout {
            width: parent.width
            spacing: SemanticTokens.spacingSm
            Text {
                Layout.fillWidth: true
                text: qsTr("The transcript segments and processing record for this version will be permanently deleted.")
                wrapMode: Text.WordWrap
                color: SemanticTokens.text
                font.pixelSize: SemanticTokens.bodySize
            }
            Text {
                Layout.fillWidth: true
                visible: root.pendingDeleteRevision.hasManualEdits === true
                text: qsTr("This version contains manual edits. Those edits will also be lost.")
                wrapMode: Text.WordWrap
                color: SemanticTokens.danger
                font.pixelSize: SemanticTokens.bodySize
                font.weight: SemanticTokens.weightSemiBold
            }
            Text {
                objectName: "deleteDirtyTranscriptRevisionWarning"
                Layout.fillWidth: true
                visible: root.pendingDeleteRevisionId === root.revisions.selectedJobId
                         && root.transcript.dirty
                text: qsTr("This is the version you are viewing, and it has unsaved changes. Those changes will be discarded.")
                wrapMode: Text.WordWrap
                color: SemanticTokens.danger
                font.pixelSize: SemanticTokens.bodySize
                font.weight: SemanticTokens.weightSemiBold
            }
        }
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
                      ? qsTr("Downloading Q5_K…") : qsTr("Transcribe")
                enabled: !root.vm.modelManager.defaultModelDownloadActive
                primary: true
                onClicked: root.requestTranscription()
            }
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
                        readonly property bool hasRevisions: root.revisions.count > 0
                        readonly property bool stacked: hasRevisions
                                                        && (root.stackTranscriptTools
                                                            || root.transcript.editingLocked)
                        columns: stacked || !hasRevisions ? 1 : 2
                        columnSpacing: SemanticTokens.spacingSm
                        rowSpacing: SemanticTokens.spacingXs

                        RowLayout {
                            objectName: "recordingTranscriptRevisionBar"
                            visible: transcriptTools.hasRevisions
                            Layout.row: 0
                            Layout.column: 0
                            Layout.fillWidth: transcriptTools.stacked
                            Layout.minimumWidth: 0
                            spacing: SemanticTokens.spacingXs
                            AppComboBox {
                                id: transcriptRevisionPicker
                                objectName: "transcriptRevisionPicker"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 220 * DesignSystem.textScale
                                Layout.minimumWidth: 170 * DesignSystem.textScale
                                Layout.maximumWidth: 260 * DesignSystem.textScale
                                accessibleName: qsTr("Transcript version")
                                model: root.revisions.revisions
                                textRole: "displayLabel"
                                valueRole: "jobId"
                                currentIndex: root.revisions.selectedIndex
                                onActivated: {
                                    root.vm.selectTranscriptRevision(String(currentValue))
                                    Qt.callLater(function() {
                                        transcriptRevisionPicker.currentIndex = root.revisions.selectedIndex
                                    })
                                }
                            }
                            StatusBadge {
                                visible: root.revisions.followingLive
                                         && root.revisions.selectedRevisionRunning
                                text: qsTr("Following live")
                                tone: "accent"
                            }
                            StatusBadge {
                                objectName: "recordingEditingLockedBadge"
                                visible: root.transcript.editingLocked
                                text: qsTr("Live transcription — editing locked")
                                tone: "accent"
                            }
                            StatusBadge {
                                objectName: "transcriptNewVersionBadge"
                                visible: root.revisions.hasNewerRevision
                                         && !root.revisions.selectionPinned
                                text: qsTr("New version available")
                                tone: "accent"
                            }
                            AppButton {
                                objectName: "followLiveTranscriptButton"
                                visible: root.revisions.selectionPinned
                                text: qsTr("Follow latest")
                                primary: true
                                onClicked: root.vm.followLiveTranscript()
                            }
                            IconButton {
                                objectName: "transcriptHistoryButton"
                                iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/history.svg"
                                accessibleName: qsTr("Transcript history")
                                onClicked: revisionHistoryDialog.open()
                            }
                        }

                        GridLayout {
                            id: transcriptCommands
                            objectName: "recordingTranscriptCommands"
                            Layout.row: transcriptTools.stacked ? 1 : 0
                            Layout.column: transcriptTools.stacked || !transcriptTools.hasRevisions ? 0 : 1
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
                                        root.vm.copyToClipboard(root.transcript.fullText())
                                        root.vm.showToast(qsTr("Transcript copied to clipboard."))
                                    }
                                }
                                IconButton {
                                    objectName: "recordingSaveButton"
                                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/save.svg"
                                    accessibleName: root.transcript.dirty ? qsTr("Save Changes") : qsTr("Saved")
                                    toolTipText: accessibleName + " · Ctrl+S"
                                    enabled: root.transcript.dirty
                                    onClicked: root.transcript.save()
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
                    title: root.revisions.selectedJobId.length === 0
                           ? qsTr("No completed transcript yet")
                           : qsTr("This transcript version is empty")
                    description: root.revisions.selectedJobId.length === 0
                                 ? qsTr("Add this recording to the queue. Partial segments will appear here while the new version is transcribed.")
                                 : qsTr("This version did not produce any transcript segments.")
                    actionText: root.revisions.selectedJobId.length === 0 ? qsTr("Add to Queue") : ""
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
                        ListView.onPooled: editing = false
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
