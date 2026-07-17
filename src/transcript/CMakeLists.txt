add_library(breezedesk_transcript STATIC
    SqliteTranscriptRepository.cpp
    TranscriptAutosave.cpp
    TranscriptEditor.cpp
    TranscriptExporter.cpp
)

target_include_directories(breezedesk_transcript PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(breezedesk_transcript PUBLIC breezedesk_database breezedesk_glossary Qt6::Core Qt6::Sql)
target_compile_features(breezedesk_transcript PUBLIC cxx_std_17)
breezedesk_enable_strict_warnings(breezedesk_transcript)
