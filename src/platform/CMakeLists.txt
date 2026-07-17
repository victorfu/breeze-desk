set(BREEZEDESK_PLATFORM_SOURCES
    InstallSourceClassifier.cpp
    PlatformCapabilities.cpp
    WorkerRegistry.cpp)

if(APPLE)
    list(APPEND BREEZEDESK_PLATFORM_SOURCES PlatformService_mac.mm)
elseif(WIN32)
    list(APPEND BREEZEDESK_PLATFORM_SOURCES PlatformService_win.cpp)
else()
    list(APPEND BREEZEDESK_PLATFORM_SOURCES PlatformService_generic.cpp)
endif()

add_library(breezedesk_platform STATIC
    ${PROJECT_SOURCE_DIR}/include/breezedesk/platform/PlatformCapabilities.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/platform/InstallSourceClassifier.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/platform/WorkerRegistry.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/platform/IPlatformService.h
    ${BREEZEDESK_PLATFORM_SOURCES})
target_link_libraries(breezedesk_platform PUBLIC breezedesk_build_config Qt6::Core Qt6::Gui)

if(APPLE)
    target_link_libraries(breezedesk_platform PRIVATE
        "-framework AppKit" "-framework AVFoundation" "-framework ServiceManagement")
elseif(WIN32)
    target_link_libraries(breezedesk_platform PRIVATE shell32 advapi32)
endif()

breezedesk_enable_strict_warnings(breezedesk_platform)
