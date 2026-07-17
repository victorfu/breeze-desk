add_library(breezedesk_ipc STATIC
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ipc/ApplicationCommand.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ipc/IAsrWorkerClient.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ipc/AsrWorkerClient.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ipc/WorkerServer.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/ipc/SingleInstanceGuard.h
    Protocol.cpp
    ApplicationCommand.cpp
    FrameCodec.cpp
    LocalEndpoint.cpp
    SingleInstanceGuard.cpp
    WorkerServer.cpp
    AsrWorkerClient.cpp
)

target_include_directories(breezedesk_ipc
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(breezedesk_ipc
    PUBLIC
        Qt6::Core
        Qt6::Network
        breezedesk_core
)

set_target_properties(breezedesk_ipc PROPERTIES AUTOMOC ON)

if(COMMAND breezedesk_enable_strict_warnings)
    breezedesk_enable_strict_warnings(breezedesk_ipc)
endif()
