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
                            color: parent.highlighted ? SemanticTokens.accentMuted
                                 : parent.hovered ? SemanticTokens.hoverTint : "transparent"
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
                    RemoveButton {
                        id: deleteProfileButton
                        objectName: "glossaryDeleteProfileButton"
                        Layout.row: profileActions.stacked ? 1 : 0
                        Layout.column: profileActions.stacked ? 0 : 1
                        Layout.alignment: Qt.AlignRight
                        enabled: root.vm.selectedProfileId.length > 0
                        accessibleName: qsTr("Delete glossary profile")
                        onClicked: confirmDeleteProfile.open()
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
                iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/book-open.svg"
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
                        RemoveButton {
                            objectName: "glossaryDeleteTermButton"
                            accessibleName: qsTr("Delete glossary term %1").arg(canonicalText)
                            onClicked: root.vm.deleteTerm(termId)
                        }
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
        objectName: "glossaryProfileDialog"
        title: qsTr("New Glossary Profile")
        standardButtons: Dialog.NoButton

        function clearFields() {
            profileName.clear()
            profileDescription.clear()
            profileContext.clear()
        }

        function createProfile() {
            if (profileName.text.trim().length === 0)
                return

            if (root.vm.createProfile(profileName.text.trim(),
                                      profileDescription.text.trim(),
                                      profileContext.text.trim()).length > 0) {
                profileDialog.close()
            }
        }

        onOpened: profileName.forceActiveFocus()
        onClosed: clearFields()

        background: Rectangle {
            objectName: "glossaryProfileDialogSurface"
            color: SemanticTokens.surfaceRaised
            radius: SemanticTokens.radiusLg
            border.width: 1
            border.color: SemanticTokens.border
        }

        header: Rectangle {
            objectName: "glossaryProfileDialogHeader"
            implicitHeight: profileHeaderLayout.implicitHeight + SemanticTokens.spacingLg * 2
            color: SemanticTokens.surfaceRaised

            RowLayout {
                id: profileHeaderLayout
                anchors.fill: parent
                anchors.margins: SemanticTokens.spacingLg
                spacing: SemanticTokens.spacingMd

                Rectangle {
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    Layout.alignment: Qt.AlignTop
                    color: SemanticTokens.accentMuted
                    radius: SemanticTokens.radiusMd

                    AppIcon {
                        anchors.centerIn: parent
                        source: "qrc:/qt/qml/BreezeDesk/icons/lucide/book-open.svg"
                        color: SemanticTokens.accent
                        iconSize: 22
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: SemanticTokens.spacingXs

                    Text {
                        Layout.fillWidth: true
                        text: profileDialog.title
                        color: SemanticTokens.text
                        wrapMode: Text.WordWrap
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.headingSize
                        font.weight: Font.DemiBold
                        Accessible.role: Accessible.Heading
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Profiles keep project context and important names scoped to a meeting or recording.")
                        color: SemanticTokens.textMuted
                        wrapMode: Text.WordWrap
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.captionSize
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: SemanticTokens.border
            }
        }

        ColumnLayout {
            id: profileDialogContent
            objectName: "glossaryProfileDialogContent"
            width: parent.width
            spacing: SemanticTokens.spacingMd

            ColumnLayout {
                Layout.fillWidth: true
                spacing: SemanticTokens.spacingXs
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Profile name")
                    color: SemanticTokens.text
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                    font.weight: Font.DemiBold
                }
                AppTextField {
                    id: profileName
                    objectName: "glossaryProfileNameField"
                    Layout.fillWidth: true
                    accessibleName: qsTr("Profile name")
                    placeholderText: qsTr("Profile name")
                    onAccepted: profileDialog.createProfile()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: SemanticTokens.spacingXs
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Description")
                    color: SemanticTokens.text
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                    font.weight: Font.DemiBold
                }
                AppTextField {
                    id: profileDescription
                    objectName: "glossaryProfileDescriptionField"
                    Layout.fillWidth: true
                    accessibleName: qsTr("Description")
                    placeholderText: qsTr("Description")
                    onAccepted: profileDialog.createProfile()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: SemanticTokens.spacingXs
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Project context")
                    color: SemanticTokens.text
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                    font.weight: Font.DemiBold
                }
                AppTextField {
                    id: profileContext
                    objectName: "glossaryProfileContextField"
                    Layout.fillWidth: true
                    accessibleName: qsTr("Project context")
                    placeholderText: qsTr("Project or meeting context")
                    onAccepted: profileDialog.createProfile()
                }
            }
        }

        footer: Rectangle {
            objectName: "glossaryProfileDialogFooter"
            implicitHeight: profileFooterLayout.implicitHeight + SemanticTokens.spacingMd * 2
            color: SemanticTokens.surfaceRaised

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: SemanticTokens.border
            }

            RowLayout {
                id: profileFooterLayout
                anchors.fill: parent
                anchors.margins: SemanticTokens.spacingMd
                spacing: SemanticTokens.spacingSm

                Item { Layout.fillWidth: true }
                AppButton {
                    objectName: "glossaryProfileCancelButton"
                    text: qsTr("Cancel")
                    onClicked: profileDialog.close()
                }
                AppButton {
                    objectName: "glossaryProfileCreateButton"
                    text: qsTr("New Profile")
                    primary: true
                    enabled: profileName.text.trim().length > 0
                    onClicked: profileDialog.createProfile()
                }
            }
        }
    }
    AppDialog {
        id: termDialog
        objectName: "glossaryTermDialog"
        title: qsTr("Add Glossary Term")
        standardButtons: Dialog.NoButton

        function clearFields() {
            canonicalText.clear()
            aliasText.clear()
            priority.value = 80
        }

        function addTerm() {
            if (canonicalText.text.trim().length === 0)
                return

            const aliases = aliasText.text.length > 0 ? aliasText.text.split(",").map(function(value) { return value.trim() }) : []
            if (root.vm.addTerm(canonicalText.text.trim(), aliases, priority.value).length > 0)
                termDialog.close()
        }

        onOpened: canonicalText.forceActiveFocus()
        onClosed: clearFields()

        background: Rectangle {
            color: SemanticTokens.surfaceRaised
            radius: SemanticTokens.radiusLg
            border.width: 1
            border.color: SemanticTokens.border
        }

        header: Rectangle {
            implicitHeight: termHeaderLayout.implicitHeight + SemanticTokens.spacingLg * 2
            color: SemanticTokens.surfaceRaised

            RowLayout {
                id: termHeaderLayout
                anchors.fill: parent
                anchors.margins: SemanticTokens.spacingLg
                spacing: SemanticTokens.spacingMd

                Rectangle {
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    Layout.alignment: Qt.AlignTop
                    color: SemanticTokens.accentMuted
                    radius: SemanticTokens.radiusMd

                    AppIcon {
                        anchors.centerIn: parent
                        source: "qrc:/qt/qml/BreezeDesk/icons/lucide/book-open.svg"
                        color: SemanticTokens.accent
                        iconSize: 22
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: SemanticTokens.spacingXs

                    Text {
                        Layout.fillWidth: true
                        text: termDialog.title
                        color: SemanticTokens.text
                        wrapMode: Text.WordWrap
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.headingSize
                        font.weight: Font.DemiBold
                        Accessible.role: Accessible.Heading
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Explicit aliases can be applied conservatively and remain auditable.")
                        color: SemanticTokens.textMuted
                        wrapMode: Text.WordWrap
                        font.family: SemanticTokens.fontFamily
                        font.pixelSize: SemanticTokens.captionSize
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: SemanticTokens.border
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: SemanticTokens.spacingMd

            ColumnLayout {
                Layout.fillWidth: true
                spacing: SemanticTokens.spacingXs
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Canonical term")
                    color: SemanticTokens.text
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                    font.weight: Font.DemiBold
                }
                AppTextField {
                    id: canonicalText
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
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                    font.weight: Font.DemiBold
                }
                AppTextField {
                    id: aliasText
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
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.bodySize
                    font.weight: Font.DemiBold
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: SemanticTokens.spacingMd
                    AppSlider {
                        id: priority
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
                    text: qsTr("Cancel")
                    onClicked: termDialog.close()
                }
                AppButton {
                    text: qsTr("Add Term")
                    primary: true
                    enabled: canonicalText.text.trim().length > 0
                    onClicked: termDialog.addTerm()
                }
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
    AppDialog {
        id: confirmDeleteProfile
        objectName: "glossaryDeleteProfileDialog"
        title: qsTr("Delete glossary profile permanently?")
        iconSource: "qrc:/qt/qml/BreezeDesk/icons/lucide/trash-2.svg"
        destructive: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: root.vm.deleteProfile(root.vm.selectedProfileId)
        Text {
            width: parent.width
            text: qsTr("This deletes the profile and all of its terms. This cannot be undone.")
            wrapMode: Text.Wrap
            color: SemanticTokens.text
            font.family: SemanticTokens.fontFamily
            font.pixelSize: SemanticTokens.bodySize
        }
    }
}
