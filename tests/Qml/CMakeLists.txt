if(NOT BREEZEDESK_BUILD_TESTS)
    return()
endif()

qt_add_executable(tst_QmlSmoke
    tst_QmlSmoke.cpp
    ${PROJECT_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(tst_QmlSmoke PRIVATE breezedesk_ui Qt6::Test Qt6::Quick Qt6::Qml)
target_compile_definitions(tst_QmlSmoke PRIVATE BREEZEDESK_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
breezedesk_enable_strict_warnings(tst_QmlSmoke)
add_test(NAME Qml.Smoke COMMAND tst_QmlSmoke)
set_tests_properties(Qml.Smoke PROPERTIES LABELS "Qml;Unit"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic")
