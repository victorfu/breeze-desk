add_library(breezedesk_jobs STATIC
    JobQueue.cpp
    JobRecoveryService.cpp
    JobStateMachine.cpp
    SqliteJobRepository.cpp
    TranscriptionJob.cpp
)

target_include_directories(breezedesk_jobs PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(breezedesk_jobs PUBLIC breezedesk_database Qt6::Core Qt6::Sql)
target_compile_features(breezedesk_jobs PUBLIC cxx_std_17)
breezedesk_enable_strict_warnings(breezedesk_jobs)
