if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(BREEZEDESK_OUTPUT_NAME "${BREEZEDESK_DEBUG_EXECUTABLE_NAME}")
    set(BREEZEDESK_BUNDLE_ID "${BREEZEDESK_DEBUG_BUNDLE_ID}")
else()
    set(BREEZEDESK_OUTPUT_NAME "${BREEZEDESK_RELEASE_EXECUTABLE_NAME}")
    set(BREEZEDESK_BUNDLE_ID "${BREEZEDESK_RELEASE_BUNDLE_ID}")
endif()

set(BREEZEDESK_SPARKLE_INFO_PLIST_KEYS "")
if(APPLE AND BREEZEDESK_ENABLE_UPDATES)
    set(_breezedesk_appcast_url "${BREEZEDESK_APPCAST_URL}")
    string(REPLACE "&" "&amp;" _breezedesk_appcast_url "${_breezedesk_appcast_url}")
    string(REPLACE "<" "&lt;" _breezedesk_appcast_url "${_breezedesk_appcast_url}")
    string(REPLACE ">" "&gt;" _breezedesk_appcast_url "${_breezedesk_appcast_url}")
    string(REPLACE "\"" "&quot;" _breezedesk_appcast_url "${_breezedesk_appcast_url}")
    string(REPLACE "'" "&apos;" _breezedesk_appcast_url "${_breezedesk_appcast_url}")
    set(BREEZEDESK_SPARKLE_INFO_PLIST_KEYS
        "  <key>SUFeedURL</key><string>${_breezedesk_appcast_url}</string>\n  <key>SUPublicEDKey</key><string>${BREEZEDESK_EDDSA_PUBLIC_KEY}</string>\n  <key>SUEnableAutomaticChecks</key><false/>\n  <key>SUAutomaticallyUpdate</key><false/>\n  <key>SUEnableSystemProfiling</key><false/>")
endif()

add_library(breezedesk_worker_process STATIC
    WorkerProcessManager.cpp
    TranscriptionCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/include/breezedesk/app/WorkerProcessManager.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/app/TranscriptionCoordinator.h)
target_link_libraries(breezedesk_worker_process
    PUBLIC breezedesk_build_config breezedesk_ipc Qt6::Core
    PRIVATE breezedesk_audio breezedesk_models breezedesk_database breezedesk_jobs breezedesk_glossary
            breezedesk_transcript breezedesk_asr breezedesk_settings breezedesk_platform Qt6::Concurrent)
target_compile_definitions(breezedesk_worker_process PRIVATE
    BREEZEDESK_DEV_WORKER_PATH="$<TARGET_FILE:breezedesk-asr-worker>")
add_dependencies(breezedesk_worker_process breezedesk-asr-worker)
breezedesk_enable_strict_warnings(breezedesk_worker_process)

add_library(breezedesk_maintenance STATIC
    MaintenanceController.cpp
    ${PROJECT_SOURCE_DIR}/include/breezedesk/app/MaintenanceController.h)
target_include_directories(breezedesk_maintenance PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(breezedesk_maintenance
    PUBLIC breezedesk_build_config Qt6::Core
    PRIVATE breezedesk_core breezedesk_database breezedesk_audio breezedesk_ipc
            breezedesk_update Qt6::Concurrent)
target_compile_features(breezedesk_maintenance PUBLIC cxx_std_17)
breezedesk_enable_strict_warnings(breezedesk_maintenance)

add_library(breezedesk_model_test STATIC
    ModelTestController.cpp
    ${PROJECT_SOURCE_DIR}/include/breezedesk/app/ModelTestController.h)
target_include_directories(breezedesk_model_test PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(breezedesk_model_test
    PUBLIC breezedesk_build_config Qt6::Core
    PRIVATE breezedesk_core breezedesk_ipc breezedesk_models)
target_compile_features(breezedesk_model_test PUBLIC cxx_std_17)
breezedesk_enable_strict_warnings(breezedesk_model_test)

set(BREEZEDESK_APP_SOURCES
    main.cpp
    ${PROJECT_SOURCE_DIR}/resources/resources.qrc)

if(WIN32)
    if(BREEZEDESK_WINDOWS_ICON_PATH)
        if(NOT EXISTS "${BREEZEDESK_WINDOWS_ICON_PATH}")
            message(FATAL_ERROR
                "BREEZEDESK_WINDOWS_ICON_PATH does not exist: ${BREEZEDESK_WINDOWS_ICON_PATH}")
        endif()
        file(TO_CMAKE_PATH "${BREEZEDESK_WINDOWS_ICON_PATH}" _breezedesk_windows_icon_path)
        set(BREEZEDESK_WINDOWS_ICON_RESOURCE
            "IDI_BREEZEDESK_ICON ICON DISCARDABLE \"${_breezedesk_windows_icon_path}\"")
    else()
        set(BREEZEDESK_WINDOWS_ICON_RESOURCE "")
    endif()
    configure_file(${PROJECT_SOURCE_DIR}/cmake/breezedesk.rc.in
                   ${CMAKE_CURRENT_BINARY_DIR}/breezedesk.rc @ONLY)
    list(APPEND BREEZEDESK_APP_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/breezedesk.rc)
endif()

qt_add_executable(BreezeDesk MACOSX_BUNDLE WIN32 ${BREEZEDESK_APP_SOURCES})
set_target_properties(BreezeDesk PROPERTIES
    OUTPUT_NAME "${BREEZEDESK_OUTPUT_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${BREEZEDESK_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "${BREEZEDESK_OUTPUT_NAME}"
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/cmake/Info.plist.in"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
target_link_libraries(BreezeDesk PRIVATE
    breezedesk_build_config
    breezedesk_core breezedesk_ipc breezedesk_database breezedesk_audio
    breezedesk_models breezedesk_glossary breezedesk_transcript breezedesk_jobs
    breezedesk_asr breezedesk_platform breezedesk_settings breezedesk_update breezedesk_ui
    breezedesk_worker_process breezedesk_maintenance
    breezedesk_model_test
    Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets)
breezedesk_enable_strict_warnings(BreezeDesk)
add_dependencies(BreezeDesk breezedesk-asr-worker)

if(APPLE)
    add_custom_command(TARGET BreezeDesk POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:breezedesk-asr-worker>
            $<TARGET_BUNDLE_CONTENT_DIR:BreezeDesk>/MacOS/$<TARGET_FILE_NAME:breezedesk-asr-worker>
        VERBATIM)
else()
    add_custom_command(TARGET BreezeDesk POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:breezedesk-asr-worker>
            $<TARGET_FILE_DIR:BreezeDesk>/$<TARGET_FILE_NAME:breezedesk-asr-worker>
        VERBATIM)
endif()

qt_add_translations(BreezeDesk
    TS_FILES
        ${PROJECT_SOURCE_DIR}/translations/breezedesk_en.ts
        ${PROJECT_SOURCE_DIR}/translations/breezedesk_zh_TW.ts
    RESOURCE_PREFIX /i18n)
