pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var vm
    objectName: "glossaryPage"
    readonly property int termsHeaderStackWidth: 640

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingLg
        spacing: SemanticTokens.spacingMd

        PageHeader {
            id: glossaryHeader
            objectName: "glossaryHeader"
            actionsObjectName: "glossaryHeaderActions"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            stackWidth: root.termsHeaderStackWidth
            title: qsTr("Terms")
            subtitle: qsTr("Add canonical names and aliases so transcripts use your preferred spelling.")

            AppSearchField {
                objectName: "glossarySearchField"
                Layout.fillWidth: glossaryHeader.stacked
                Layout.minimumWidth: 160
                Layout.preferredWidth: 230
                text: root.vm.termSearch
                onTextEdited: root.vm.termSearch = text
            }
            AppButton {
                objectName: "glossaryAddTermButton"
                text: qsTr("Add Term")
                primary: true
                onClicked: termDialog.open()
            }
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: termsList.count === 0
            iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/book-open.svg"
            title: root.vm.termSearch.length > 0 ? qsTr("No matching terms") : qsTr("No terms yet")
            description: root.vm.termSearch.length > 0
                         ? qsTr("Try a different canonical name or alias.")
                         : qsTr("Add canonical names and aliases so transcripts use your preferred spelling.")
            actionText: root.vm.termSearch.length > 0 ? "" : qsTr("Add Term")
            onActionTriggered: termDialog.open()
        }

        ListView {
            id: termsList
            objectName: "glossaryTermsList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: count > 0
            model: root.vm.terms
            spacing: SemanticTokens.spacingSm
            clip: true
            reuseItems: true

            delegate: Rectangle {
                id: termCard
                required property string termId
                required property string canonicalText
                required property var aliases
                required property int priority
                required property bool termEnabled
                width: ListView.view.width
                height: Math.max(ComponentTokens.clickTarget,
                                 termRow.implicitHeight + SemanticTokens.spacingMd * 2)
                color: SemanticTokens.surface
                opacity: termEnabled ? 1.0 : 0.68
                radius: SemanticTokens.radiusMd
                border.color: SemanticTokens.border

                RowLayout {
                    id: termRow
                    anchors.fill: parent
                    anchors.margins: SemanticTokens.spacingMd
                    spacing: SemanticTokens.spacingMd

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Text {
                            Layout.fillWidth: true
                            text: termCard.canonicalText
                            color: SemanticTokens.text
                            elide: Text.ElideRight
                            font.pixelSize: SemanticTokens.bodySize
                            font.weight: SemanticTokens.weightSemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: termCard.aliases.length > 0
                                  ? qsTr("Aliases: %1").arg(termCard.aliases.join(", "))
                                  : qsTr("No aliases")
                            color: SemanticTokens.textMuted
                            elide: Text.ElideRight
                            font.pixelSize: SemanticTokens.captionSize
                        }
                    }
                    StatusBadge {
                        visible: termRow.width >= 560 * DesignSystem.textScale
                        text: qsTr("Priority %1").arg(termCard.priority)
                        tone: termCard.priority >= 80 ? "accent" : "neutral"
                    }
                    Toggle {
                        objectName: "glossaryTermToggle"
                        text: qsTr("Enabled")
                        accessibleName: qsTr("Enable glossary term %1").arg(termCard.canonicalText)
                        checked: termCard.termEnabled
                        onToggled: root.vm.setTermEnabled(termCard.termId, checked)
                    }
                    RemoveButton {
                        objectName: "glossaryDeleteTermButton"
                        accessibleName: qsTr("Delete glossary term %1").arg(termCard.canonicalText)
                        onClicked: root.vm.deleteTerm(termCard.termId)
                    }
                }
            }
        }
    }

    AppDialog {
        id: termDialog
        objectName: "glossaryTermDialog"
        surfaceObjectName: "glossaryTermDialogSurface"
        headerObjectName: "glossaryTermDialogHeader"
        title: qsTr("Add Glossary Term")
        subtitle: qsTr("Explicit aliases can be applied conservatively and remain auditable.")
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/book-open.svg"
        standardButtons: Dialog.NoButton

        function clearFields() {
            canonicalText.clear()
            aliasText.clear()
            priority.value = 80
        }

        function addTerm() {
            if (canonicalText.text.trim().length === 0)
                return

            const aliases = aliasText.text.length > 0
                            ? aliasText.text.split(",").map(function(value) { return value.trim() })
                            : []
            if (root.vm.addTerm(canonicalText.text.trim(), aliases, priority.value).length > 0)
                termDialog.close()
        }

        onOpened: canonicalText.forceActiveFocus()
        onClosed: clearFields()

        ColumnLayout {
            objectName: "glossaryTermDialogContent"
            width: parent.width
            spacing: SemanticTokens.spacingMd

            ColumnLayout {
                Layout.fillWidth: true
                spacing: SemanticTokens.spacingXs
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Canonical term")
                    color: SemanticTokens.text
                    font.pixelSize: SemanticTokens.bodySize
                    font.weight: SemanticTokens.weightSemiBold
                }
                AppTextField {
                    id: canonicalText
                    objectName: "glossaryCanonicalTermField"
                    Layout.fillWidth: true
                    accessibleName: qsTr("Canonical term")
                    placeholderText: qsTr("Canonical term")
                    onAccepted: termDialog.addTerm()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: SemanticTokens.spacingXs
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Aliases")
                    color: SemanticTokens.text
                    font.pixelSize: SemanticTokens.bodySize
                    font.weight: SemanticTokens.weightSemiBold
                }
                AppTextField {
                    id: aliasText
                    objectName: "glossaryAliasesField"
                    Layout.fillWidth: true
                    accessibleName: qsTr("Aliases")
                    placeholderText: qsTr("Aliases separated by commas")
                    onAccepted: termDialog.addTerm()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: SemanticTokens.spacingXs
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Priority")
                    color: SemanticTokens.text
                    font.pixelSize: SemanticTokens.bodySize
                    font.weight: SemanticTokens.weightSemiBold
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: SemanticTokens.spacingMd
                    AppSlider {
                        id: priority
                        objectName: "glossaryPrioritySlider"
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 10
                        value: 80
                        Accessible.name: qsTr("Term priority")
                    }
                    StatusBadge {
                        text: Math.round(priority.value).toString()
                        tone: priority.value >= 80 ? "accent" : "neutral"
                        Accessible.name: qsTr("Priority %1").arg(Math.round(priority.value))
                    }
                }
            }
        }

        footer: Rectangle {
            objectName: "glossaryTermDialogFooter"
            implicitHeight: termFooterLayout.implicitHeight + SemanticTokens.spacingMd * 2
            color: SemanticTokens.surfaceRaised

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: SemanticTokens.border
            }

            RowLayout {
                id: termFooterLayout
                anchors.fill: parent
                anchors.margins: SemanticTokens.spacingMd
                spacing: SemanticTokens.spacingSm

                Item { Layout.fillWidth: true }
                AppButton {
                    objectName: "glossaryTermCancelButton"
                    text: qsTr("Cancel")
                    onClicked: termDialog.close()
                }
                AppButton {
                    objectName: "glossaryTermCreateButton"
                    text: qsTr("Add Term")
                    primary: true
                    enabled: canonicalText.text.trim().length > 0
                    onClicked: termDialog.addTerm()
                }
            }
        }
    }
}
