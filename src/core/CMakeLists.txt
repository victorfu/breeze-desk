add_library(breezedesk_core STATIC
    ApplicationLogger.cpp
    Error.cpp
    LoggingCategories.cpp
    StoragePaths.cpp
    TemporaryFileJanitor.cpp
    TimeUtils.cpp
    TextUtils.cpp
)

target_include_directories(breezedesk_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(breezedesk_core
    PUBLIC
        breezedesk_build_config
        Qt6::Core
)

target_compile_features(breezedesk_core PUBLIC cxx_std_17)
breezedesk_enable_strict_warnings(breezedesk_core)
