qt_add_executable(tst_LibraryWorkflows tst_LibraryWorkflows.cpp)
target_link_libraries(tst_LibraryWorkflows PRIVATE
    breezedesk_ui breezedesk_platform breezedesk_database Qt6::Test Qt6::Sql)
breezedesk_enable_strict_warnings(tst_LibraryWorkflows)
add_test(NAME Library.Workflows COMMAND tst_LibraryWorkflows)
set_tests_properties(Library.Workflows PROPERTIES LABELS "Library;Unit")
