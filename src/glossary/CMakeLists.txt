add_library(breezedesk_glossary STATIC
    GlossaryPostProcessor.cpp
    GlossarySerializer.cpp
    PromptComposer.cpp
    SqliteGlossaryRepository.cpp
)

target_include_directories(breezedesk_glossary PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(breezedesk_glossary PUBLIC breezedesk_database Qt6::Core Qt6::Sql)
target_compile_features(breezedesk_glossary PUBLIC cxx_std_17)
breezedesk_enable_strict_warnings(breezedesk_glossary)
