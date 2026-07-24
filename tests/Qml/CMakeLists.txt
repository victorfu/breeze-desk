if(NOT BREEZEDESK_BUILD_TESTS)
    return()
endif()

get_target_property(_breezedesk_qmake Qt6::qmake IMPORTED_LOCATION)
execute_process(
    COMMAND "${_breezedesk_qmake}" -query QT_INSTALL_PLUGINS
    OUTPUT_VARIABLE _breezedesk_qt_plugins_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _breezedesk_qt_plugins_result)
if(NOT _breezedesk_qt_plugins_result EQUAL 0
        OR NOT IS_DIRECTORY "${_breezedesk_qt_plugins_dir}")
    message(FATAL_ERROR
        "Unable to locate the Qt plugins directory required by headless QML tests")
endif()
file(TO_CMAKE_PATH "${_breezedesk_qt_plugins_dir}" _breezedesk_qt_plugins_dir)

qt_add_executable(tst_QmlSmoke
    tst_QmlSmoke.cpp
    QmlTestApplication.h
    ${PROJECT_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(tst_QmlSmoke PRIVATE breezedesk_ui Qt6::Test Qt6::Quick Qt6::Qml)
target_compile_definitions(tst_QmlSmoke PRIVATE
    BREEZEDESK_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    BREEZEDESK_QT_PLUGINS_DIR="${_breezedesk_qt_plugins_dir}")
qt_add_translations(tst_QmlSmoke
    TS_FILES
        ${PROJECT_SOURCE_DIR}/translations/breezedesk_en.ts
        ${PROJECT_SOURCE_DIR}/translations/breezedesk_zh_TW.ts
    RESOURCE_PREFIX /i18n-test)
breezedesk_enable_strict_warnings(tst_QmlSmoke)
add_test(NAME Qml.Smoke COMMAND tst_QmlSmoke)
set_tests_properties(Qml.Smoke PROPERTIES LABELS "Qml;Unit"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic")

qt_add_executable(tst_JobQueueViewModel
    tst_JobQueueViewModel.cpp
    QmlTestApplication.h
    ${PROJECT_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(tst_JobQueueViewModel PRIVATE breezedesk_ui Qt6::Test Qt6::Quick Qt6::Qml)
target_compile_definitions(tst_JobQueueViewModel PRIVATE
    BREEZEDESK_QT_PLUGINS_DIR="${_breezedesk_qt_plugins_dir}")
breezedesk_enable_strict_warnings(tst_JobQueueViewModel)
add_test(NAME Qml.JobQueueViewModel COMMAND tst_JobQueueViewModel)
set_tests_properties(Qml.JobQueueViewModel PROPERTIES LABELS "Qml;Unit"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic")
