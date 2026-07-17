add_library(breezedesk_models STATIC
    ${PROJECT_SOURCE_DIR}/include/breezedesk/models/ModelManifest.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/models/IModelRepository.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/models/ModelDownloadOperation.h
    ${PROJECT_SOURCE_DIR}/include/breezedesk/models/ModelManager.h
    ModelManifest.cpp
    ModelDownloadOperation.cpp
    ModelManager.cpp)

target_link_libraries(breezedesk_models
    PUBLIC breezedesk_build_config Qt6::Core
    PRIVATE breezedesk_core Qt6::Network Qt6::Concurrent)
breezedesk_enable_strict_warnings(breezedesk_models)
