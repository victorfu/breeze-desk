import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property int modelIndex
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
    signal selectedRequested(int index)
    signal seekRequested(int position)
    signal textEdited(int index, string text)
    signal splitRequested(int index)
    signal mergePreviousRequested(int index)
    signal mergeNextRequested(int index)
    signal deleteRequested(int index)
    signal reviewedRequested(int index, bool reviewed)
    signal glossaryReplacementRequested(int index, int replacementIndex, bool applied)
    implicitHeight: Math.max(ComponentTokens.segmentMinHeight, body.implicitHeight + SemanticTokens.spacingMd * 2)
    color: selected ? SemanticTokens.accentMuted : SemanticTokens.surface
    radius: SemanticTokens.radiusMd
    border.width: selected ? ComponentTokens.focusWidth : 1
    border.color: selected ? SemanticTokens.accent : SemanticTokens.border
    Accessible.name: qsTr("Transcript segment from %1").arg(startCode.text)
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        propagateComposedEvents: true
        onClicked: function(mouse) { root.selectedRequested(root.modelIndex); mouse.accepted = false }
    }
    RowLayout {
        id: body
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingMd
        spacing: SemanticTokens.spacingMd
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: SemanticTokens.spacingXs
            TimeCode { id: startCode; milliseconds: root.startMs; onSeekRequested: root.seekRequested(position) }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "–"
                color: SemanticTokens.textMuted
            }
            TimeCode { milliseconds: root.endMs; onSeekRequested: root.seekRequested(position) }
        }
        TextArea {
            id: editor
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: root.editedText
            readOnly: root.editingLocked
            color: SemanticTokens.text
            selectionColor: SemanticTokens.accent
            selectedTextColor: SemanticTokens.textOnAccent
            wrapMode: TextEdit.Wrap
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.bodySize
            Accessible.name: qsTr("Segment text")
            background: Rectangle {
                color: editor.activeFocus ? SemanticTokens.surfaceRaised : "transparent"
                radius: SemanticTokens.radiusSm
                border.width: editor.activeFocus ? ComponentTokens.focusWidth : 0
                border.color: SemanticTokens.focusRing
            }
            onActiveFocusChanged: if (!activeFocus && text !== root.editedText) root.textEdited(root.modelIndex, text)
        }
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: SemanticTokens.spacingXs
            StatusBadge { visible: root.lowConfidence; text: qsTr("Low confidence"); tone: "warning" }
            StatusBadge { visible: root.edited; text: qsTr("Edited"); tone: "accent" }
            StatusBadge { visible: root.glossaryReplacement; text: qsTr("Glossary"); tone: "success" }
            Toggle {
                text: qsTr("Reviewed")
                checked: root.reviewed
                enabled: !root.editingLocked
                onToggled: root.reviewedRequested(root.modelIndex, checked)
            }
            RowLayout {
                visible: root.selected
                AppButton { text: qsTr("Split"); enabled: !root.editingLocked; onClicked: root.splitRequested(root.modelIndex) }
                AppButton { text: qsTr("Merge Prev"); enabled: !root.editingLocked; onClicked: root.mergePreviousRequested(root.modelIndex) }
                AppButton { text: qsTr("Merge Next"); enabled: !root.editingLocked; onClicked: root.mergeNextRequested(root.modelIndex) }
                AppButton { text: qsTr("Delete"); enabled: !root.editingLocked; onClicked: root.deleteRequested(root.modelIndex) }
            }
            ColumnLayout {
                visible: root.selected && root.glossaryAudit.length > 0
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
                        required property int index
                        required property var modelData
                        spacing: SemanticTokens.spacingXs
                        Text {
                            Layout.preferredWidth: 180
                            text: qsTr("%1 → %2").arg(parent.modelData.originalText)
                                                      .arg(parent.modelData.canonicalText)
                            color: SemanticTokens.text
                            elide: Text.ElideRight
                            font.family: SemanticTokens.fontFamily
                            font.pixelSize: SemanticTokens.captionSize
                        }
                        StatusBadge {
                            text: parent.modelData.applied ? qsTr("Applied") : qsTr("Reverted")
                            tone: parent.modelData.applied ? "success" : "neutral"
                        }
                        AppButton {
                            text: parent.modelData.applied ? qsTr("Undo") : qsTr("Apply")
                            enabled: !root.editingLocked
                            onClicked: root.glossaryReplacementRequested(
                                           root.modelIndex, parent.index, !parent.modelData.applied)
                        }
                    }
                }
            }
        }
    }
}
