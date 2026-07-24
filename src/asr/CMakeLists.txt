add_library(breezedesk_asr STATIC
    AsrTypes.cpp
    ModelFileIntegrity.cpp
    PresetRegistry.cpp
    WhisperParameterMapper.cpp
    SegmentMetrics.cpp
    LongFormChunkPlanner.cpp
    OverlapDeduplicator.cpp
    PromptTokenBudget.cpp
    StreamingVadSegmenter.cpp
    LongFormTranscriber.cpp
    WhisperContext.cpp
    WhisperBackendInfo.cpp
    WhisperLogBridge.cpp
    WhisperSegmentCollector.cpp
    WhisperModelSession.cpp
    WhisperTranscriptionEngine.cpp
)

target_include_directories(breezedesk_asr
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(breezedesk_asr
    PUBLIC
        Qt6::Core
        breezedesk_core
)

if(BREEZEDESK_ENABLE_WHISPER)
    target_link_libraries(breezedesk_asr PRIVATE whisper)
    target_compile_definitions(breezedesk_asr PRIVATE BREEZEDESK_HAS_WHISPER=1)
    if(APPLE)
        target_compile_definitions(breezedesk_asr PRIVATE
            BREEZEDESK_WHISPER_BACKEND_METAL=1)
    elseif(WIN32 AND BREEZEDESK_WINDOWS_BACKEND STREQUAL "VULKAN")
        target_compile_definitions(breezedesk_asr PRIVATE
            BREEZEDESK_WHISPER_BACKEND_VULKAN=1)
    endif()
endif()

if(COMMAND breezedesk_enable_strict_warnings)
    breezedesk_enable_strict_warnings(breezedesk_asr)
endif()
