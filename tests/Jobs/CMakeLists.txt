qt_add_executable(tst_Jobs tst_Jobs.cpp)
target_link_libraries(tst_Jobs PRIVATE breezedesk_jobs Qt6::Test Qt6::Sql)
breezedesk_enable_strict_warnings(tst_Jobs)
add_test(NAME Jobs.StateAndRecovery COMMAND tst_Jobs)
set_tests_properties(Jobs.StateAndRecovery PROPERTIES LABELS "Jobs;Unit")

qt_add_executable(tst_CliTranscriptionPersistence tst_CliTranscriptionPersistence.cpp)
target_link_libraries(tst_CliTranscriptionPersistence PRIVATE
    breezedesk_cli_support Qt6::Test Qt6::Sql)
breezedesk_enable_strict_warnings(tst_CliTranscriptionPersistence)
add_test(NAME Jobs.CliTranscriptionPersistence COMMAND tst_CliTranscriptionPersistence)
set_tests_properties(Jobs.CliTranscriptionPersistence PROPERTIES LABELS "Jobs;CLI;Integration")

add_executable(coordinator_worker_helper coordinator_worker_helper.cpp)
target_link_libraries(coordinator_worker_helper PRIVATE breezedesk_ipc Qt6::Core)
breezedesk_enable_strict_warnings(coordinator_worker_helper)

qt_add_executable(tst_TranscriptionCoordinator
    tst_TranscriptionCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(tst_TranscriptionCoordinator PRIVATE
    breezedesk_worker_process breezedesk_database breezedesk_jobs
    breezedesk_transcript breezedesk_models Qt6::Test Qt6::Sql)
target_compile_definitions(tst_TranscriptionCoordinator PRIVATE
    BREEZEDESK_COORDINATOR_WORKER_PATH="$<TARGET_FILE:coordinator_worker_helper>")
add_dependencies(tst_TranscriptionCoordinator coordinator_worker_helper)
breezedesk_enable_strict_warnings(tst_TranscriptionCoordinator)
add_test(NAME Jobs.TranscriptionCoordinator COMMAND tst_TranscriptionCoordinator)
set_tests_properties(Jobs.TranscriptionCoordinator PROPERTIES LABELS "Jobs;IPC;Integration")
