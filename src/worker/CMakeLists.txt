add_executable(breezedesk-asr-worker
    main.cpp
)

target_link_libraries(breezedesk-asr-worker
    PRIVATE
        Qt6::Core
        Qt6::Network
        breezedesk_build_config
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

if(WIN32)
    # Keep raw developer builds runnable without relying on an IDE-provided PATH.
    # Release packaging still uses windeployqt to collect the full application runtime.
    add_custom_command(TARGET breezedesk-asr-worker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt6::Core>
            $<TARGET_FILE:Qt6::Network>
            $<TARGET_FILE_DIR:breezedesk-asr-worker>
        VERBATIM)
endif()

if(COMMAND breezedesk_enable_strict_warnings)
    breezedesk_enable_strict_warnings(breezedesk-asr-worker)
endif()
