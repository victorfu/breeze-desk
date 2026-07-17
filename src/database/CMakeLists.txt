add_library(breezedesk_database STATIC
    DatabaseManager.cpp
    DatabaseSearchService.cpp
    SqliteRecordingRepository.cpp
)

target_include_directories(breezedesk_database
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(breezedesk_database
    PUBLIC
        breezedesk_core
        Qt6::Core
        Qt6::Sql
)

target_compile_features(breezedesk_database PUBLIC cxx_std_17)
breezedesk_enable_strict_warnings(breezedesk_database)
