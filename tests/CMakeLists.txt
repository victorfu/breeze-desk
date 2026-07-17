add_subdirectory(Utils)
add_subdirectory(Database)
add_subdirectory(Jobs)
add_subdirectory(Glossary)
add_subdirectory(Transcript)
add_subdirectory(Settings)
add_subdirectory(IPC)
add_subdirectory(Asr)
add_subdirectory(Audio)
add_subdirectory(Models)
add_subdirectory(Library)
add_subdirectory(App)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Qml/CMakeLists.txt")
    add_subdirectory(Qml)
endif()

add_custom_target(breezedesk_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    USES_TERMINAL)
