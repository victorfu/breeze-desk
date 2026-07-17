add_library(breezedesk_cli_support STATIC
    CliForwardingPolicy.cpp
    CliTranscriptionPersistence.cpp
    ${PROJECT_SOURCE_DIR}/include/breezedesk/cli/CliForwardingPolicy.h)
target_include_directories(breezedesk_cli_support PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(breezedesk_cli_support PUBLIC
    breezedesk_database breezedesk_jobs breezedesk_transcript Qt6::Core)
target_compile_features(breezedesk_cli_support PUBLIC cxx_std_17)
breezedesk_enable_strict_warnings(breezedesk_cli_support)

qt_add_executable(breezedesk-cli
    main.cpp
    ${PROJECT_SOURCE_DIR}/include/breezedesk/cli/CliExitCode.h
    ${PROJECT_SOURCE_DIR}/resources/resources.qrc)
set_target_properties(breezedesk-cli PROPERTIES OUTPUT_NAME "${BREEZEDESK_CLI_EXECUTABLE_NAME}")
target_link_libraries(breezedesk-cli PRIVATE
    breezedesk_build_config breezedesk_cli_support breezedesk_core breezedesk_database breezedesk_jobs
    breezedesk_transcript breezedesk_audio breezedesk_models breezedesk_asr
    breezedesk_glossary breezedesk_ipc breezedesk_worker_process
    Qt6::Core Qt6::Network)
breezedesk_enable_strict_warnings(breezedesk-cli)
add_dependencies(breezedesk-cli breezedesk-asr-worker)
add_custom_command(TARGET breezedesk-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:breezedesk-asr-worker>
        $<TARGET_FILE_DIR:breezedesk-cli>/$<TARGET_FILE_NAME:breezedesk-asr-worker>
    VERBATIM)
