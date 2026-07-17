add_executable(tst_asr_core tst_AsrCore.cpp)
target_link_libraries(tst_asr_core PRIVATE Qt6::Test breezedesk_asr)
add_test(NAME Asr.Core COMMAND tst_asr_core)
set_tests_properties(Asr.Core PROPERTIES LABELS "ASR;unit;long-form")

if(COMMAND breezedesk_enable_strict_warnings)
    breezedesk_enable_strict_warnings(tst_asr_core)
endif()

if(BREEZEDESK_ENABLE_MODEL_INTEGRATION_TESTS)
    if(NOT BREEZEDESK_ENABLE_WHISPER)
        message(FATAL_ERROR
            "BREEZEDESK_ENABLE_MODEL_INTEGRATION_TESTS requires BREEZEDESK_ENABLE_WHISPER")
    endif()

    if(BREEZEDESK_WHISPER_CPP_SOURCE_DIR)
        set(_breezedesk_whisper_test_source "${BREEZEDESK_WHISPER_CPP_SOURCE_DIR}")
    elseif(DEFINED whisper_cpp_SOURCE_DIR)
        set(_breezedesk_whisper_test_source "${whisper_cpp_SOURCE_DIR}")
    endif()
    if(NOT DEFINED _breezedesk_whisper_test_source
       OR NOT EXISTS "${_breezedesk_whisper_test_source}/samples/jfk.wav")
        message(FATAL_ERROR
            "The pinned whisper.cpp samples/jfk.wav fixture is required for the model integration test")
    endif()

    set(BREEZEDESK_TEST_MODEL_PATH
        "${CMAKE_BINARY_DIR}/test-models/ggml-tiny.en-q5_1.bin"
        CACHE FILEPATH "Predownloaded tiny whisper.cpp model for integration tests")
    add_executable(tst_asr_model_integration tst_ModelIntegration.cpp)
    target_link_libraries(tst_asr_model_integration PRIVATE
        Qt6::Test
        breezedesk_ipc)
    target_compile_definitions(tst_asr_model_integration PRIVATE
        BREEZEDESK_TEST_WORKER_PATH="$<TARGET_FILE:breezedesk-asr-worker>"
        BREEZEDESK_TEST_MODEL_DEFAULT_PATH="${BREEZEDESK_TEST_MODEL_PATH}"
        BREEZEDESK_TEST_AUDIO_SOURCE_PATH="${_breezedesk_whisper_test_source}/samples/jfk.wav")
    add_dependencies(tst_asr_model_integration breezedesk-asr-worker)
    add_test(NAME Asr.ModelIntegration COMMAND tst_asr_model_integration)
    set_tests_properties(Asr.ModelIntegration PROPERTIES
        LABELS "ASR;integration;model"
        TIMEOUT 240)
    if(COMMAND breezedesk_enable_strict_warnings)
        breezedesk_enable_strict_warnings(tst_asr_model_integration)
    endif()
endif()
