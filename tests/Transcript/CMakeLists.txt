qt_add_executable(tst_Transcript tst_Transcript.cpp)
target_link_libraries(tst_Transcript PRIVATE breezedesk_transcript breezedesk_jobs breezedesk_ui
    Qt6::Test Qt6::Sql)
breezedesk_enable_strict_warnings(tst_Transcript)
add_test(NAME Transcript.EditorRepositoryExport COMMAND tst_Transcript)
set_tests_properties(Transcript.EditorRepositoryExport PROPERTIES LABELS "Transcript;Unit")
