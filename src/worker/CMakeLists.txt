add_executable(breezedesk-asr-worker
    main.cpp
)

target_link_libraries(breezedesk-asr-worker
    PRIVATE
        Qt6::Core
        Qt6::Network
        breezedesk_build_config
        breezedesk_core
        breezedesk_ipc
        breezedesk_asr
)

if(BREEZEDESK_ENABLE_WHISPER)
    # Keep this explicit: the native worker itself is a direct libwhisper consumer.
    target_link_libraries(breezedesk-asr-worker PRIVATE whisper)
endif()

set_target_properties(breezedesk-asr-worker PROPERTIES
    AUTOMOC ON
    OUTPUT_NAME "${BREEZEDESK_WORKER_EXECUTABLE_NAME}")

if(COMMAND breezedesk_enable_strict_warnings)
    breezedesk_enable_strict_warnings(breezedesk-asr-worker)
endif()
