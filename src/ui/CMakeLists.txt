set(BREEZEDESK_UI_HEADERS
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/ApplicationViewModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/DesignSystem.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/DiagnosticsViewModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/GlossaryViewModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/JobListModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/JobQueueViewModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/LibraryViewModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/ModelManagerViewModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/PlayerViewModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/RecordingDetailViewModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/RecordingListModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/SettingsViewModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/TranscriptSegmentModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/TranscriptRevisionModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/TranscriptRevisionViewModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/TranscriptViewModel.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/UiRegistration.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ui/WaveformItem.h
)

add_library(breezedesk_ui STATIC
    ${BREEZEDESK_UI_HEADERS}
    ApplicationViewModel.cpp
    DesignSystem.cpp
    DiagnosticsViewModel.cpp
    GlossaryViewModel.cpp
    JobListModel.cpp
    JobQueueViewModel.cpp
    LibraryViewModel.cpp
    ModelManagerViewModel.cpp
    PlayerViewModel.cpp
    RecordingDetailViewModel.cpp
    RecordingListModel.cpp
    SettingsViewModel.cpp
    TranscriptSegmentModel.cpp
    TranscriptRevisionModel.cpp
    TranscriptRevisionViewModel.cpp
    TranscriptViewModel.cpp
    UiRegistration.cpp
    WaveformItem.cpp
)

target_include_directories(breezedesk_ui
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(breezedesk_ui
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Multimedia
        breezedesk_database
    PRIVATE
        breezedesk_audio
        breezedesk_jobs
        breezedesk_models
        breezedesk_settings
        breezedesk_glossary
        breezedesk_transcript
        Qt6::Concurrent
)

target_compile_features(breezedesk_ui PUBLIC cxx_std_17)
breezedesk_enable_strict_warnings(breezedesk_ui)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(breezedesk_ui PRIVATE -Wno-variadic-macro-arguments-omitted)
endif()

add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../qml
    ${CMAKE_CURRENT_BINARY_DIR}/qml
)
