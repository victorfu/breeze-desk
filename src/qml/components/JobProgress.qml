import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    required property string jobId
    required property string title
    required property string jobState
    required property string stage
    required property real progress
    required property string errorMessage
    required property bool canCancel
    required property bool canRetry
    required property bool canResume
    required property bool isRunningNow
    required property int queuePosition
    required property int waitingAhead
    required property int currentChunk
    required property int totalChunks
    required property string latestPartialText
    required property var eventTimeline
    required property bool canMoveUp
    required property bool canMoveDown
    required property bool canRemove

    signal cancelRequested(string id)
    signal retryRequested(string id)
    signal resumeRequested(string id)
    signal moveUpRequested(string id)
    signal moveDownRequested(string id)
    signal reorderRequested(string id, int destination)
    signal removeRequested(string id)

    property bool timelineExpanded: false
    readonly property string displayedJobState: UiText.jobState(jobState)
    readonly property bool queueDragActive: queueDragHandler.active

    function userFacingStatus() {
        if (root.errorMessage === "This build does not include whisper.cpp"
                || root.errorMessage === "whisper.cpp is disabled in this build") {
            return qsTr("Speech recognition is unavailable in this build. Use a build that includes whisper.cpp, then retry this job.")
        }
        return root.errorMessage.length > 0 ? root.errorMessage : UiText.jobStage(root.stage)
    }

    function queueSummary() {
        if (root.queuePosition < 0)
            return ""
        const position = qsTr("Queue position %1").arg(root.queuePosition + 1)
        if (root.waitingAhead === 0)
            return position + " · " + qsTr("Next in queue")
        if (root.waitingAhead === 1)
            return position + " · " + qsTr("1 job ahead")
        return position + " · " + qsTr("%1 jobs ahead").arg(root.waitingAhead)
    }

    function chunkSummary() {
        if (root.totalChunks <= 0)
            return ""
        if (root.currentChunk <= 0)
            return qsTr("%1 chunks").arg(root.totalChunks)
        return qsTr("Chunk %1 of %2").arg(root.currentChunk).arg(root.totalChunks)
    }

    objectName: "jobProgressCard"
    implicitHeight: content.implicitHeight + SemanticTokens.spacingLg * 2
    z: queueDragActive ? 2 : 0
    opacity: queueDragActive ? 0.88 : 1.0
    color: SemanticTokens.surface
    radius: ComponentTokens.cardRadius
    border.color: isRunningNow ? SemanticTokens.accent : SemanticTokens.border
    border.width: isRunningNow ? 2 : 1
    activeFocusOnTab: jobState === "Queued"
    Accessible.name: title + ", " + displayedJobState + ", " + Math.round(progress * 100) + "%"
    Accessible.description: [userFacingStatus(), queueSummary(), chunkSummary()].filter(
                                function(value) { return value.length > 0 }).join(". ")
    Drag.active: queueDragHandler.active
    Drag.source: root
    Drag.keys: ["breezedesk-queued-job"]
    Drag.supportedActions: Qt.MoveAction
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2
    onJobIdChanged: timelineExpanded = false
    Keys.onPressed: function(event) {
        if (root.jobState !== "Queued" || !(event.modifiers & Qt.ControlModifier))
            return
        if (event.key === Qt.Key_Up && root.canMoveUp) {
            root.moveUpRequested(root.jobId)
            event.accepted = true
        } else if (event.key === Qt.Key_Down && root.canMoveDown) {
            root.moveDownRequested(root.jobId)
            event.accepted = true
        }
    }

    DropArea {
        anchors.fill: parent
        keys: ["breezedesk-queued-job"]
        onEntered: function(drag) {
            const sourceCard = drag.source
            if (root.jobState !== "Queued" || !sourceCard || sourceCard === root
                    || sourceCard.jobState !== "Queued"
                    || sourceCard.queuePosition === root.queuePosition) {
                return
            }
            sourceCard.reorderRequested(sourceCard.jobId, root.queuePosition)
        }
        onDropped: function(drop) { drop.acceptProposedAction() }
    }

    ColumnLayout {
        id: content

        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingLg
        spacing: SemanticTokens.spacingSm

        RowLayout {
            Layout.fillWidth: true
            spacing: SemanticTokens.spacingSm

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: SemanticTokens.spacingXs

                Text {
                    Layout.fillWidth: true
                    text: root.title
                    color: SemanticTokens.text
                    elide: Text.ElideRight
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                    font.weight: Font.DemiBold
                }
                Text {
                    id: queueMetadata
                    objectName: "jobQueueMetadata"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.queueSummary()
                    color: SemanticTokens.textMuted
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.captionSize
                }
            }

            StatusBadge {
                visible: root.isRunningNow
                text: qsTr("Running now")
                tone: "accent"
            }
            StatusBadge {
                text: root.displayedJobState
                tone: root.jobState === "Failed" ? "danger"
                      : root.jobState === "Completed" ? "success"
                      : root.isRunningNow ? "accent" : "neutral"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: SemanticTokens.spacingSm

            AppProgressBar {
                Layout.fillWidth: true
                value: root.progress
            }
            Text {
                text: Math.round(root.progress * 100) + "%"
                color: SemanticTokens.textMuted
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.captionSize
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: SemanticTokens.spacingSm

            Text {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.userFacingStatus()
                color: root.errorMessage.length > 0 ? SemanticTokens.danger : SemanticTokens.textMuted
                wrapMode: Text.WordWrap
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.captionSize
                Accessible.role: root.errorMessage.length > 0
                                 ? Accessible.AlertMessage : Accessible.StaticText
            }
            Text {
                id: chunkStatus
                objectName: "jobChunkStatus"
                visible: text.length > 0
                text: root.chunkSummary()
                color: SemanticTokens.textMuted
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.captionSize
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: root.latestPartialText.length > 0
            implicitHeight: partialColumn.implicitHeight + SemanticTokens.spacingSm * 2
            color: SemanticTokens.surfaceMuted
            radius: SemanticTokens.radiusSm

            ColumnLayout {
                id: partialColumn
                anchors.fill: parent
                anchors.margins: SemanticTokens.spacingSm
                spacing: SemanticTokens.spacingXs

                Text {
                    text: qsTr("Latest partial transcript")
                    color: SemanticTokens.textMuted
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.captionSize
                    font.weight: Font.DemiBold
                }
                Text {
                    objectName: "jobLatestPartialText"
                    Layout.fillWidth: true
                    text: root.latestPartialText
                    color: SemanticTokens.text
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                }
            }
        }

        AppButton {
            id: timelineToggle
            objectName: "jobTimelineToggle"
            visible: root.eventTimeline.length > 0
            Layout.alignment: Qt.AlignLeft
            text: root.timelineExpanded ? qsTr("Hide activity") : qsTr("Show activity (%1)").arg(root.eventTimeline.length)
            accessibleName: text
            onClicked: root.timelineExpanded = !root.timelineExpanded
        }

        ColumnLayout {
            id: eventTimelineColumn
            objectName: "jobEventTimeline"
            Layout.fillWidth: true
            visible: root.timelineExpanded && root.eventTimeline.length > 0
            spacing: SemanticTokens.spacingXs

            Repeater {
                model: root.eventTimeline
                delegate: RowLayout {
                    id: eventRow
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: SemanticTokens.spacingSm

                    Text {
                        Layout.alignment: Qt.AlignTop
                        text: UiText.shortDateTime(eventRow.modelData.timestamp)
                        color: SemanticTokens.textMuted
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.captionSize
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 0

                        Text {
                            Layout.fillWidth: true
                            text: UiText.jobState(eventRow.modelData.title)
                            color: eventRow.modelData.severity === "error"
                                   ? SemanticTokens.danger : SemanticTokens.text
                            wrapMode: Text.WordWrap
                            font.family: SemanticTokens.fontFamily
                            font.pixelSize: SemanticTokens.captionSize
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: UiText.jobStage(eventRow.modelData.detail)
                            color: SemanticTokens.textMuted
                            wrapMode: Text.WordWrap
                            font.family: SemanticTokens.fontFamily
                            font.pixelSize: SemanticTokens.captionSize
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.jobState === "Queued" || root.canCancel || root.canRetry
                     || root.canResume || root.canRemove
            spacing: SemanticTokens.spacingXs

            RowLayout {
                visible: root.jobState === "Queued"
                spacing: 0

                Item {
                    id: dragHandle
                    objectName: "jobDragHandle"
                    Layout.preferredWidth: ComponentTokens.clickTarget
                    Layout.preferredHeight: ComponentTokens.clickTarget
                    Accessible.name: qsTr("Drag %1 to reorder the queue").arg(root.title)
                    Accessible.role: Accessible.Button

                    AppIcon {
                        anchors.centerIn: parent
                        source: "qrc:/qt/qml/BreezeDesk/icons/lucide/list-ordered.svg"
                        iconSize: 20
                        color: SemanticTokens.textMuted
                    }

                    DragHandler {
                        id: queueDragHandler
                        enabled: root.jobState === "Queued"
                        target: root
                        acceptedButtons: Qt.LeftButton
                        xAxis.enabled: false
                        onActiveChanged: {
                            if (!active && root.ListView.view)
                                root.ListView.view.forceLayout()
                        }
                    }
                }
                IconButton {
                    objectName: "jobMoveUpButton"
                    accessibleName: qsTr("Move %1 up in queue").arg(root.title)
                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/chevron-up.svg"
                    enabled: root.canMoveUp
                    onClicked: root.moveUpRequested(root.jobId)
                }
                IconButton {
                    objectName: "jobMoveDownButton"
                    accessibleName: qsTr("Move %1 down in queue").arg(root.title)
                    iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/chevron-down.svg"
                    enabled: root.canMoveDown
                    onClicked: root.moveDownRequested(root.jobId)
                }
            }

            Item { Layout.fillWidth: true }
            AppButton {
                visible: root.canCancel
                text: qsTr("Cancel")
                onClicked: root.cancelRequested(root.jobId)
            }
            AppButton {
                visible: root.canRetry
                text: qsTr("Retry")
                onClicked: root.retryRequested(root.jobId)
            }
            AppButton {
                visible: root.canResume
                text: qsTr("Resume")
                onClicked: root.resumeRequested(root.jobId)
            }
            AppButton {
                objectName: "jobRemoveButton"
                visible: root.canRemove
                text: qsTr("Remove")
                accessibleName: qsTr("Remove %1 permanently").arg(root.title)
                danger: true
                onClicked: root.removeRequested(root.jobId)
            }
        }
    }
}
