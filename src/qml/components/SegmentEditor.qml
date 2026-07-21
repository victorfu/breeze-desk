pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as T
import QtQuick.Layouts

Rectangle {
    id: root

    required property int proxyRow
    required property int startMs
    required property int endMs
    required property string originalText
    required property string editedText
    required property bool lowConfidence
    required property bool edited
    required property bool glossaryReplacement
    required property var glossaryAudit
    required property bool reviewed
    required property bool editingLocked
    property bool selected: false
    property bool editing: false
    readonly property int modelIndex: proxyRow

    signal selectedRequested(int index)
    signal seekRequested(int position)
    signal textEdited(int index, string text)
    signal splitRequested(int index)
    signal mergePreviousRequested(int index)
    signal mergeNextRequested(int index)
    signal deleteRequested(int index)
    signal reviewedRequested(int index, bool reviewed)
    signal glossaryReplacementRequested(int index, int replacementIndex, bool applied)

    readonly property int contentPadding: SemanticTokens.spacingXs
    readonly property int actionButtonSize: 32
    readonly property int actionIconSize: 18

    onSelectedChanged: {
        if (!selected) {
            editing = false
        }
    }
    readonly property int timeColumnWidth: Math.max(
                                               80,
                                               Math.ceil(SemanticTokens.captionSize * 5.8
                                                         + SemanticTokens.spacingSm))

    objectName: "segmentEditor"
    implicitHeight: Math.max(48, body.implicitHeight + contentPadding * 2)
    color: selected ? SemanticTokens.accentMuted : "transparent"
    radius: 0
    border.width: 0
    Accessible.name: qsTr("Transcript segment from %1 to %2").arg(startCode.text).arg(endCode.text)
    Accessible.description: lowConfidence
                            ? qsTr("Low-confidence transcript segment")
                            : qsTr("Transcript segment")
    Accessible.role: Accessible.ListItem
    Accessible.selected: selected
    Accessible.onPressAction: root.selectedRequested(root.modelIndex)

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        propagateComposedEvents: true
        onClicked: function(mouse) {
            root.selectedRequested(root.modelIndex)
            mouse.accepted = false
        }
    }

    ColumnLayout {
        id: body

        anchors.fill: parent
        anchors.margins: root.contentPadding
        spacing: root.selected ? SemanticTokens.spacingXs : 0

        RowLayout {
            Layout.fillWidth: true
            spacing: SemanticTokens.spacingSm

            ColumnLayout {
                id: timeColumn

                objectName: "segmentTimeColumn"
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: root.timeColumnWidth
                Layout.minimumWidth: root.timeColumnWidth
                Layout.maximumWidth: root.timeColumnWidth
                spacing: 0

                TimeCode {
                    id: startCode
                    objectName: "segmentStartTimeCode"

                    Layout.fillWidth: true
                    padding: 2
                    implicitHeight: Math.max(22, contentItem.implicitHeight + topPadding + bottomPadding)
                    milliseconds: root.startMs
                    onSeekRequested: function(position) {
                        root.selectedRequested(root.modelIndex)
                        root.seekRequested(position)
                    }
                }

                TimeCode {
                    id: endCode
                    objectName: "segmentEndTimeCode"

                    Layout.fillWidth: true
                    padding: 2
                    implicitHeight: Math.max(22, contentItem.implicitHeight + topPadding + bottomPadding)
                    milliseconds: root.endMs
                    onSeekRequested: function(position) {
                        root.selectedRequested(root.modelIndex)
                        root.seekRequested(position)
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 0

                T.TextArea {
                    id: editor

                    objectName: "segmentTextEditor"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: Math.max(28, contentHeight + topPadding + bottomPadding)
                    leftPadding: SemanticTokens.spacingXs
                    rightPadding: SemanticTokens.spacingXs
                    topPadding: SemanticTokens.spacingXs
                    bottomPadding: SemanticTokens.spacingXs
                    text: root.editedText
                    readOnly: root.editingLocked || !root.editing
                    activeFocusOnPress: root.editing
                    color: SemanticTokens.text
                    selectionColor: SemanticTokens.accent
                    selectedTextColor: SemanticTokens.textOnAccent
                    wrapMode: TextEdit.Wrap
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                    Accessible.name: qsTr("Segment text")
                    Accessible.description: qsTr("Transcript text from %1 to %2")
                                                .arg(startCode.text).arg(endCode.text)
                    background: Rectangle {
                        color: editor.activeFocus ? SemanticTokens.surfaceRaised : "transparent"
                        radius: SemanticTokens.radiusSm
                        border.width: editor.activeFocus ? ComponentTokens.focusWidth : 0
                        border.color: SemanticTokens.focusRing
                    }
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            root.selectedRequested(root.modelIndex)
                        } else {
                            if (text !== root.editedText) {
                                root.textEdited(root.modelIndex, text)
                            }
                            root.editing = false
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: root.selectedRequested(root.modelIndex)
                        onDoubleTapped: {
                            if (!root.editingLocked) {
                                root.editing = true
                                editor.forceActiveFocus()
                            }
                        }
                    }
                    HoverHandler {
                        enabled: !root.editing
                        cursorShape: Qt.PointingHandCursor
                    }
                }

                RowLayout {
                    id: statusRow

                    objectName: "segmentStatusRow"
                    visible: root.edited || root.glossaryReplacement
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: SemanticTokens.spacingXs

                    StatusBadge {
                        visible: root.edited
                        text: qsTr("Edited")
                        tone: "accent"
                    }
                    StatusBadge {
                        visible: root.glossaryReplacement
                        text: qsTr("Glossary")
                        tone: "success"
                    }
                }
            }

            Loader {
                id: actionsLoader

                Layout.alignment: Qt.AlignVCenter
                active: root.selected
                visible: active

                sourceComponent: Row {
                    objectName: "segmentActionsRow"
                    spacing: 0

                    IconButton {
                        objectName: "segmentEditButton"
                        iconSource: root.editing
                                    ? "qrc:/qt/qml/BreezeDesk/icons/lucide/check.svg"
                                    : "qrc:/qt/qml/BreezeDesk/icons/lucide/pencil.svg"
                        accessibleName: root.editing ? qsTr("Done Editing") : qsTr("Edit Text")
                        iconColor: !enabled ? SemanticTokens.textMuted
                                 : root.editing ? SemanticTokens.accentStrong
                                 : SemanticTokens.text
                        iconSize: root.actionIconSize
                        implicitWidth: root.actionButtonSize
                        implicitHeight: root.actionButtonSize
                        enabled: !root.editingLocked
                        // A pointer click must not blur the editor before onClicked
                        // decides whether this action finishes the current edit.
                        focusPolicy: Qt.TabFocus
                        onClicked: {
                            if (root.editing) {
                                root.editing = false
                                root.forceActiveFocus()
                            } else {
                                root.editing = true
                                editor.forceActiveFocus()
                            }
                        }
                    }
                    IconButton {
                        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/scissors.svg"
                        accessibleName: qsTr("Split")
                        iconSize: root.actionIconSize
                        implicitWidth: root.actionButtonSize
                        implicitHeight: root.actionButtonSize
                        enabled: !root.editingLocked
                        onClicked: root.splitRequested(root.modelIndex)
                    }
                    IconButton {
                        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/arrow-up-to-line.svg"
                        accessibleName: qsTr("Merge with Previous")
                        iconSize: root.actionIconSize
                        implicitWidth: root.actionButtonSize
                        implicitHeight: root.actionButtonSize
                        enabled: !root.editingLocked
                        onClicked: root.mergePreviousRequested(root.modelIndex)
                    }
                    IconButton {
                        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/arrow-down-to-line.svg"
                        accessibleName: qsTr("Merge with Next")
                        iconSize: root.actionIconSize
                        implicitWidth: root.actionButtonSize
                        implicitHeight: root.actionButtonSize
                        enabled: !root.editingLocked
                        onClicked: root.mergeNextRequested(root.modelIndex)
                    }
                    RemoveButton {
                        objectName: "segmentDeleteButton"
                        accessibleName: qsTr("Delete segment")
                        iconSize: root.actionIconSize
                        implicitWidth: root.actionButtonSize
                        implicitHeight: root.actionButtonSize
                        enabled: !root.editingLocked
                        onClicked: root.deleteRequested(root.modelIndex)
                    }
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            active: root.selected && root.glossaryAudit.length > 0
            visible: active

            sourceComponent: ColumnLayout {
                spacing: SemanticTokens.spacingXs

                Text {
                    text: qsTr("Glossary replacements")
                    color: SemanticTokens.textMuted
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.captionSize
                    font.weight: Font.DemiBold
                }

                Repeater {
                    model: root.glossaryAudit
                    delegate: RowLayout {
                        id: auditRow

                        required property int index
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: SemanticTokens.spacingXs

                        Text {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: qsTr("%1 → %2").arg(auditRow.modelData.originalText)
                                                      .arg(auditRow.modelData.canonicalText)
                            color: SemanticTokens.text
                            elide: Text.ElideRight
                            font.family: SemanticTokens.fontFamily
                            font.pixelSize: SemanticTokens.captionSize
                        }
                        StatusBadge {
                            text: auditRow.modelData.applied ? qsTr("Applied") : qsTr("Reverted")
                            tone: auditRow.modelData.applied ? "success" : "neutral"
                        }
                        AppButton {
                            text: auditRow.modelData.applied ? qsTr("Undo") : qsTr("Apply")
                            enabled: !root.editingLocked
                            onClicked: root.glossaryReplacementRequested(
                                           root.modelIndex, auditRow.index, !auditRow.modelData.applied)
                        }
                    }
                }
            }
        }
    }

    Item {
        id: lowConfidenceMarker

        objectName: "segmentLowConfidenceMarker"
        visible: root.lowConfidence
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 8
        Accessible.ignored: true

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            color: SemanticTokens.warning
        }

        HoverHandler {
            id: markerHover
        }
        T.ToolTip.visible: markerHover.hovered
        T.ToolTip.text: qsTr("Low confidence")
        T.ToolTip.delay: 500
    }

    Rectangle {
        id: separator

        objectName: "segmentSeparator"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: root.selected ? SemanticTokens.accent : SemanticTokens.border
    }
}
