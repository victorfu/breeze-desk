import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    required property var vm
    objectName: "glossaryPage"
    readonly property int narrowPanelStackWidth: 260
    readonly property int profileActionsStackWidth: 210
    readonly property int termsHeaderStackWidth: 640
    RowLayout {
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacingLg
        spacing: SemanticTokens.spacingMd
        Rectangle {
            id: profilesPanel
            objectName: "glossaryProfilesPanel"
            Layout.preferredWidth: 260
            Layout.minimumWidth: 240
            Layout.fillHeight: true
            color: SemanticTokens.surfaceMuted
            radius: SemanticTokens.radiusMd
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: SemanticTokens.spacingMd
                spacing: SemanticTokens.spacingSm
                GridLayout {
                    id: profilesHeader
                    objectName: "glossaryProfilesHeader"
                    Layout.fillWidth: true
                    columns: stacked ? 1 : 2
                    columnSpacing: SemanticTokens.spacingSm
                    rowSpacing: SemanticTokens.spacingXs
                    readonly property bool stacked: width < root.narrowPanelStackWidth
                                                           * DesignSystem.textScale
                    Text {
                        id: profilesTitle
                        Layout.row: 0
                        Layout.column: 0
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: qsTr("Glossary Profiles")
                        color: SemanticTokens.text
                        wrapMode: Text.WordWrap
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.headingSize
                        font.weight: Font.DemiBold
                    }
                    AppButton {
                        id: newProfileButton
                        objectName: "glossaryNewProfileButton"
                        Layout.row: profilesHeader.stacked ? 1 : 0
                        Layout.column: profilesHeader.stacked ? 0 : 1
                        Layout.fillWidth: profilesHeader.stacked
                        text: qsTr("New")
                        onClicked: profileDialog.open()
                    }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.vm.profiles
                    clip: true
                    spacing: SemanticTokens.spacingXs
                    delegate: ItemDelegate {
                        required property string profileId
                        required property string name
                        required property int termCount
                        width: ListView.view.width
                        highlighted: root.vm.selectedProfileId === profileId
                        text: name
                        Accessible.name: qsTr("%1, %n term(s)", "", termCount).arg(name)
                        onClicked: root.vm.selectedProfileId = profileId
                        background: Rectangle {
                            radius: SemanticTokens.radiusSm
                            color: parent.highlighted ? SemanticTokens.accentMuted : "transparent"
                            border.width: parent.activeFocus ? ComponentTokens.focusWidth : 0
                            border.color: SemanticTokens.focusRing
                        }
                        contentItem: Column {
                            Text {
                                width: parent.width
                                text: name
                                color: SemanticTokens.text
                                elide: Text.ElideRight
                                font.family: SemanticTokens.fontFamily
                                font.pixelSize: SemanticTokens.bodySize
                            }
                            Text {
                                text: qsTr("%n term(s)", "", termCount)
                                color: SemanticTokens.textMuted
                                font.family: SemanticTokens.fontFamily
                                font.pixelSize: SemanticTokens.captionSize
                            }
                        }
                    }
                }
                GridLayout {
                    id: profileActions
                    objectName: "glossaryProfileActions"
                    Layout.fillWidth: true
                    columns: stacked ? 1 : 2
                    columnSpacing: SemanticTokens.spacingSm
                    rowSpacing: SemanticTokens.spacingXs
                    readonly property bool stacked: width < root.profileActionsStackWidth
                                                           * DesignSystem.textScale
                    AppButton {
                        id: duplicateProfileButton
                        objectName: "glossaryDuplicateProfileButton"
                        Layout.fillWidth: true
                        enabled: root.vm.selectedProfileId.length > 0
                        text: qsTr("Duplicate")
                        onClicked: root.vm.duplicateProfile(root.vm.selectedProfileId)
                    }
                    AppButton {
                        id: deleteProfileButton
                        objectName: "glossaryDeleteProfileButton"
                        Layout.row: profileActions.stacked ? 1 : 0
                        Layout.column: profileActions.stacked ? 0 : 1
                        Layout.fillWidth: true
                        enabled: root.vm.selectedProfileId.length > 0
                        text: qsTr("Delete")
                        onClicked: root.vm.deleteProfile(root.vm.selectedProfileId)
                    }
                }
                GridLayout {
                    id: profileTransferActions
                    objectName: "glossaryProfileTransferActions"
                    Layout.fillWidth: true
                    columns: stacked ? 1 : 2
                    columnSpacing: SemanticTokens.spacingSm
                    rowSpacing: SemanticTokens.spacingXs
                    readonly property bool stacked: width < root.narrowPanelStackWidth
                                                           * DesignSystem.textScale
                    AppButton {
                        id: importProfileButton
                        Layout.fillWidth: true
                        text: qsTr("Import")
                        onClicked: glossaryImportDialog.open()
                    }
                    AppButton {
                        id: exportJsonButton
                        Layout.row: profileTransferActions.stacked ? 1 : 0
                        Layout.column: profileTransferActions.stacked ? 0 : 1
                        Layout.fillWidth: true
                        enabled: root.vm.selectedProfileId.length > 0
                        text: qsTr("Export JSON")
                        onClicked: glossaryJsonExportDialog.open()
                    }
                    AppButton {
                        Layout.row: profileTransferActions.stacked ? 2 : 1
                        Layout.column: 0
                        Layout.columnSpan: profileTransferActions.stacked ? 1 : 2
                        Layout.fillWidth: true
                        enabled: root.vm.selectedProfileId.length > 0
                        text: qsTr("Export CSV")
                        onClicked: glossaryCsvExportDialog.open()
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            spacing: SemanticTokens.spacingMd
            GridLayout {
                id: glossaryHeader
                objectName: "glossaryHeader"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                columns: stacked ? 1 : 2
                columnSpacing: SemanticTokens.spacingMd
                rowSpacing: SemanticTokens.spacingSm
                readonly property bool stacked: width < root.termsHeaderStackWidth
                                                       * DesignSystem.textScale
                ColumnLayout {
                    id: termsTitleBlock
                    Layout.row: 0
                    Layout.column: 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Terms")
                        color: SemanticTokens.text
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.titleSize
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: qsTr("Explicit aliases can be applied conservatively and remain auditable.")
                        color: SemanticTokens.textMuted
                        wrapMode: Text.WordWrap
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.bodySize
                    }
                }
                RowLayout {
                    id: termHeaderActions
                    objectName: "glossaryHeaderActions"
                    Layout.row: glossaryHeader.stacked ? 1 : 0
                    Layout.column: glossaryHeader.stacked ? 0 : 1
                    Layout.fillWidth: glossaryHeader.stacked
                    Layout.minimumWidth: 0
                    spacing: SemanticTokens.spacingSm
                    AppSearchField {
                        objectName: "glossarySearchField"
                        Layout.fillWidth: glossaryHeader.stacked
                        Layout.minimumWidth: 160
                        Layout.preferredWidth: 230
                        enabled: root.vm.selectedProfileId.length > 0
                        text: root.vm.termSearch
                        onTextEdited: root.vm.termSearch = text
                    }
                    AppButton {
                        objectName: "glossaryAddTermButton"
                        enabled: root.vm.selectedProfileId.length > 0
                        text: qsTr("Add Term")
                        primary: true
                        onClicked: termDialog.open()
                    }
                }
            }
            EmptyState {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.vm.selectedProfileId.length === 0
                title: qsTr("Create a glossary profile")
                description: qsTr("Profiles keep project context and important names scoped to a meeting or recording.")
                actionText: qsTr("New Profile")
                onActionTriggered: profileDialog.open()
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.vm.selectedProfileId.length > 0
                model: root.vm.terms
                spacing: SemanticTokens.spacingSm
                clip: true
                reuseItems: true
                delegate: Rectangle {
                    required property string termId
                    required property string canonicalText
                    required property var aliases
                    required property int priority
                    required property bool termEnabled
                    width: ListView.view.width
                    height: Math.max(68, termRow.implicitHeight + SemanticTokens.spacingMd * 2)
                    color: SemanticTokens.surface
                    radius: SemanticTokens.radiusMd
                    border.color: SemanticTokens.border
                    RowLayout {
                        id: termRow
                        anchors.fill: parent
                        anchors.margins: SemanticTokens.spacingMd
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Text {
                                Layout.fillWidth: true
                                text: canonicalText
                                color: SemanticTokens.text
                                elide: Text.ElideRight
                                font.family: SemanticTokens.fontFamily
                                font.pixelSize: SemanticTokens.bodySize
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                text: aliases.length > 0 ? qsTr("Aliases: %1").arg(aliases.join(", ")) : qsTr("No aliases")
                                color: SemanticTokens.textMuted
                                elide: Text.ElideRight
                                font.family: SemanticTokens.fontFamily
                                font.pixelSize: SemanticTokens.captionSize
                            }
                        }
                        StatusBadge { text: qsTr("Priority %1").arg(priority); tone: priority >= 80 ? "accent" : "neutral" }
                        Toggle { text: qsTr("Enabled"); checked: termEnabled; onToggled: root.vm.setTermEnabled(termId, checked) }
                        AppButton { text: qsTr("Delete"); onClicked: root.vm.deleteTerm(termId) }
                    }
                }
            }
            InspectorSection {
                visible: root.vm.selectedProfileId.length > 0
                Layout.fillWidth: true
                title: qsTr("Prompt Preview")
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: preview.implicitHeight + SemanticTokens.spacingMd * 2
                    color: SemanticTokens.surfaceMuted
                    radius: SemanticTokens.radiusSm
                    Text {
                        id: preview
                        anchors.fill: parent
                        anchors.margins: SemanticTokens.spacingMd
                        text: root.vm.promptPreview.length > 0 ? root.vm.promptPreview : qsTr("No enabled terms are selected for this prompt.")
                        color: SemanticTokens.textMuted
                        wrapMode: Text.Wrap
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.bodySize
                    }
                }
                Text {
                    text: qsTr("Estimated tokens: %1 / %2").arg(root.vm.promptTokenCount).arg(root.vm.promptTokenMaximum)
                    color: SemanticTokens.textMuted
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.captionSize
                }
            }
        }
    }
    AppDialog {
        id: profileDialog
        title: qsTr("New Glossary Profile")
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            if (root.vm.createProfile(profileName.text, profileDescription.text, profileContext.text).length > 0) {
                profileName.clear(); profileDescription.clear(); profileContext.clear()
            }
        }
        ColumnLayout {
            width: parent.width
            AppTextField { id: profileName; Layout.fillWidth: true; Accessible.name: qsTr("Profile name"); placeholderText: qsTr("Profile name") }
            AppTextField { id: profileDescription; Layout.fillWidth: true; Accessible.name: qsTr("Description"); placeholderText: qsTr("Description") }
            AppTextField { id: profileContext; Layout.fillWidth: true; Accessible.name: qsTr("Project context"); placeholderText: qsTr("Project or meeting context") }
        }
    }
    AppDialog {
        id: termDialog
        title: qsTr("Add Glossary Term")
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            const aliases = aliasText.text.length > 0 ? aliasText.text.split(",").map(function(value) { return value.trim() }) : []
            if (root.vm.addTerm(canonicalText.text, aliases, priority.value).length > 0) {
                canonicalText.clear(); aliasText.clear(); priority.value = 80
            }
        }
        ColumnLayout {
            width: parent.width
            AppTextField { id: canonicalText; Layout.fillWidth: true; Accessible.name: qsTr("Canonical term"); placeholderText: qsTr("Canonical term") }
            AppTextField { id: aliasText; Layout.fillWidth: true; Accessible.name: qsTr("Aliases"); placeholderText: qsTr("Aliases separated by commas") }
            RowLayout {
                Text { text: qsTr("Priority"); color: SemanticTokens.text; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.bodySize }
                Slider { id: priority; Layout.fillWidth: true; from: 0; to: 100; stepSize: 10; value: 80; Accessible.name: qsTr("Term priority") }
                Text { text: priority.value; color: SemanticTokens.textMuted; font.family: SemanticTokens.fontFamily; font.pixelSize: SemanticTokens.bodySize }
            }
        }
    }
    FileDialog {
        id: glossaryImportDialog
        title: qsTr("Import Glossary")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Glossary files (*.json *.csv)"), qsTr("All files (*)")]
        onAccepted: root.vm.importFile(selectedFile)
    }
    FileDialog {
        id: glossaryJsonExportDialog
        title: qsTr("Export Glossary as JSON")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("JSON file (*.json)")]
        defaultSuffix: "json"
        onAccepted: root.vm.exportFile(selectedFile, "json")
    }
    FileDialog {
        id: glossaryCsvExportDialog
        title: qsTr("Export Glossary Terms as CSV")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("CSV file (*.csv)")]
        defaultSuffix: "csv"
        onAccepted: root.vm.exportFile(selectedFile, "csv")
    }
}
