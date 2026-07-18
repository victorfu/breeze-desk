set(BREEZEDESK_QML_SINGLETONS
    tokens/PrimitiveTokens.qml
    tokens/SemanticTokens.qml
    tokens/ComponentTokens.qml
    helpers/UiText.qml
)
set_source_files_properties(${BREEZEDESK_QML_SINGLETONS} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set(BREEZEDESK_ICON ${PROJECT_SOURCE_DIR}/resources/icons/breezedesk.png)
set(BREEZEDESK_MENUBAR_ICON ${PROJECT_SOURCE_DIR}/resources/icons/breezedesk-menubar.png)
set_source_files_properties(${BREEZEDESK_ICON} PROPERTIES QT_RESOURCE_ALIAS icons/breezedesk.png)
set_source_files_properties(${BREEZEDESK_MENUBAR_ICON} PROPERTIES
    QT_RESOURCE_ALIAS icons/breezedesk-menubar.png)
set(BREEZEDESK_LUCIDE_ICON_NAMES
    library list-ordered trash-2 box book-open settings
    play pause file-input mic rotate-ccw rotate-cw
    chevron-up chevron-down undo-2 redo-2 save ellipsis
    check info circle-check circle-alert copy
    scissors arrow-up-to-line arrow-down-to-line archive-restore)
set(BREEZEDESK_LUCIDE_ICONS)
foreach(icon_name IN LISTS BREEZEDESK_LUCIDE_ICON_NAMES)
    set(icon_path "${PROJECT_SOURCE_DIR}/resources/icons/lucide/${icon_name}.svg")
    set_source_files_properties(${icon_path} PROPERTIES
        QT_RESOURCE_ALIAS "icons/lucide/${icon_name}.svg")
    list(APPEND BREEZEDESK_LUCIDE_ICONS ${icon_path})
endforeach()

qt_add_qml_module(breezedesk_ui
    URI BreezeDesk
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/BreezeDesk
    NO_PLUGIN
    NO_GENERATE_QMLTYPES
    QML_FILES
        Main.qml
        ${BREEZEDESK_QML_SINGLETONS}
        controls/AppIcon.qml
        controls/AppButton.qml
        controls/AppMenu.qml
        controls/AppMenuItem.qml
        controls/AppMenuSeparator.qml
        controls/AppLinkButton.qml
        controls/AppComboBox.qml
        controls/AppProgressBar.qml
        controls/AppSearchField.qml
        controls/AppSlider.qml
        controls/AppTextField.qml
        controls/ComboBox.qml
        controls/IconButton.qml
        controls/RemoveButton.qml
        controls/ProgressBar.qml
        controls/SearchField.qml
        controls/Slider.qml
        controls/ShortcutRegistry.qml
        controls/TextField.qml
        controls/Toggle.qml
        dialogs/Dialog.qml
        dialogs/AppDialog.qml
        dialogs/DiagnosticsExportDialog.qml
        dialogs/ModelRequiredDialog.qml
        dialogs/RecordingDialog.qml
        components/DownloadProgress.qml
        components/EmptyState.qml
        components/InspectorSection.qml
        components/JobProgress.qml
        components/ModelCard.qml
        components/RecordingCard.qml
        components/SegmentEditor.qml
        components/SettingRow.qml
        components/SidebarItem.qml
        components/StatusBadge.qml
        components/TimeCode.qml
        components/Toast.qml
        pages/GlossaryPage.qml
        pages/LibraryPage.qml
        pages/ModelsPage.qml
        pages/QueuePage.qml
        pages/RecordingPage.qml
        pages/SettingsPage.qml
        pages/TrashPage.qml
    RESOURCES
        ${BREEZEDESK_ICON}
        ${BREEZEDESK_MENUBAR_ICON}
        ${BREEZEDESK_LUCIDE_ICONS}
)
