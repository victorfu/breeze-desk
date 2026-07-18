if(NOT BREEZEDESK_BUILD_TESTS)
    return()
endif()

qt_add_executable(tst_QmlSmoke
    tst_QmlSmoke.cpp
    ${PROJECT_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(tst_QmlSmoke PRIVATE breezedesk_ui Qt6::Test Qt6::Quick Qt6::Qml)
target_compile_definitions(tst_QmlSmoke PRIVATE BREEZEDESK_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
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
    ${PROJECT_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(tst_JobQueueViewModel PRIVATE breezedesk_ui Qt6::Test Qt6::Quick Qt6::Qml)
breezedesk_enable_strict_warnings(tst_JobQueueViewModel)
add_test(NAME Qml.JobQueueViewModel COMMAND tst_JobQueueViewModel)
set_tests_properties(Qml.JobQueueViewModel PROPERTIES LABELS "Qml;Unit"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic")
