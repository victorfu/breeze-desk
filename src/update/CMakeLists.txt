set(BREEZEDESK_UPDATE_SOURCES NullUpdateService.cpp UpdateCoordinator.cpp)
if(APPLE)
    list(APPEND BREEZEDESK_UPDATE_SOURCES SparkleUpdateService_mac.mm)
elseif(WIN32)
    list(APPEND BREEZEDESK_UPDATE_SOURCES WinSparkleUpdateService_win.cpp)
endif()

add_library(breezedesk_update STATIC
    ${PROJECT_SOURCE_DIR}/include/breezedesk/update/IUpdateService.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/update/NullUpdateService.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/update/UpdateCoordinator.h
    ${BREEZEDESK_UPDATE_SOURCES})
target_link_libraries(breezedesk_update
    PUBLIC breezedesk_build_config Qt6::Core
    PRIVATE breezedesk_platform)
if(APPLE)
    target_link_libraries(breezedesk_update PRIVATE "-framework Foundation")
endif()
breezedesk_enable_strict_warnings(breezedesk_update)
