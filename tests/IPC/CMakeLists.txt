add_executable(tst_ipc_frame_codec tst_FrameCodec.cpp)
target_link_libraries(tst_ipc_frame_codec PRIVATE Qt6::Test breezedesk_ipc)
add_test(NAME IPC.FrameCodec COMMAND tst_ipc_frame_codec)
set_tests_properties(IPC.FrameCodec PROPERTIES LABELS "IPC;unit")

add_executable(tst_ipc_worker_transport tst_WorkerTransport.cpp)
target_link_libraries(tst_ipc_worker_transport PRIVATE Qt6::Test breezedesk_ipc)
add_test(NAME IPC.WorkerTransport COMMAND tst_ipc_worker_transport)
set_tests_properties(IPC.WorkerTransport PROPERTIES LABELS "IPC;unit")

add_executable(tst_ipc_worker_process tst_WorkerProcess.cpp)
target_link_libraries(tst_ipc_worker_process PRIVATE Qt6::Test breezedesk_ipc)
target_compile_definitions(tst_ipc_worker_process PRIVATE
    BREEZEDESK_TEST_WORKER_PATH="$<TARGET_FILE:breezedesk-asr-worker>")
if(BREEZEDESK_ENABLE_WHISPER)
    if(BREEZEDESK_WHISPER_CPP_SOURCE_DIR)
        set(_breezedesk_test_vad_model
            "${BREEZEDESK_WHISPER_CPP_SOURCE_DIR}/models/for-tests-silero-v6.2.0-ggml.bin")
    elseif(DEFINED whisper_cpp_SOURCE_DIR)
        set(_breezedesk_test_vad_model
            "${whisper_cpp_SOURCE_DIR}/models/for-tests-silero-v6.2.0-ggml.bin")
    endif()
    if(DEFINED _breezedesk_test_vad_model AND EXISTS "${_breezedesk_test_vad_model}")
        target_compile_definitions(tst_ipc_worker_process PRIVATE
            BREEZEDESK_TEST_VAD_MODEL_PATH="${_breezedesk_test_vad_model}")
    endif()
endif()
add_dependencies(tst_ipc_worker_process breezedesk-asr-worker)
add_test(NAME IPC.WorkerProcess COMMAND tst_ipc_worker_process)
set_tests_properties(IPC.WorkerProcess PROPERTIES LABELS "IPC;integration")

add_executable(tst_ipc_worker_process_manager tst_WorkerProcessManager.cpp)
target_link_libraries(tst_ipc_worker_process_manager PRIVATE
    Qt6::Test breezedesk_worker_process)
add_dependencies(tst_ipc_worker_process_manager breezedesk-asr-worker)
add_test(NAME IPC.WorkerProcessManager COMMAND tst_ipc_worker_process_manager)
set_tests_properties(IPC.WorkerProcessManager PROPERTIES LABELS "IPC;integration")

add_executable(single_instance_race_helper single_instance_race_helper.cpp)
target_link_libraries(single_instance_race_helper PRIVATE Qt6::Core breezedesk_ipc)

add_executable(tst_ipc_single_instance tst_SingleInstanceGuard.cpp)
target_link_libraries(tst_ipc_single_instance PRIVATE Qt6::Test breezedesk_ipc)
add_dependencies(tst_ipc_single_instance single_instance_race_helper)
add_test(NAME IPC.SingleInstance COMMAND tst_ipc_single_instance)
set_tests_properties(IPC.SingleInstance PROPERTIES LABELS "IPC;unit")

add_executable(tst_cli_forwarding_policy tst_CliForwardingPolicy.cpp)
target_link_libraries(tst_cli_forwarding_policy PRIVATE Qt6::Test breezedesk_cli_support)
add_test(NAME IPC.CliForwardingPolicy COMMAND tst_cli_forwarding_policy)
set_tests_properties(IPC.CliForwardingPolicy PROPERTIES LABELS "IPC;CLI;unit")

foreach(target IN ITEMS
        tst_ipc_frame_codec
        tst_ipc_worker_transport
        tst_ipc_worker_process
        tst_ipc_worker_process_manager
        single_instance_race_helper
        tst_ipc_single_instance
        tst_cli_forwarding_policy)
    if(COMMAND breezedesk_enable_strict_warnings)
        breezedesk_enable_strict_warnings(${target})
    endif()
endforeach()
