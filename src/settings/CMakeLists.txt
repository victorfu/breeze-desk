add_library(breezedesk_settings STATIC
    SettingsManagers.cpp
    SettingsStore.cpp
)

target_include_directories(breezedesk_settings PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(breezedesk_settings PUBLIC breezedesk_database Qt6::Core)
target_compile_features(breezedesk_settings PUBLIC cxx_std_17)
breezedesk_enable_strict_warnings(breezedesk_settings)
