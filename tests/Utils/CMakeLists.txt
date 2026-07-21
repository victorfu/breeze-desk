qt_add_executable(tst_CoreUtilities tst_CoreUtilities.cpp)
target_link_libraries(tst_CoreUtilities PRIVATE breezedesk_core Qt6::Test)
breezedesk_enable_strict_warnings(tst_CoreUtilities)
add_test(NAME Utils.CoreUtilities COMMAND tst_CoreUtilities)
set_tests_properties(Utils.CoreUtilities PROPERTIES LABELS "Utils;Unit")

qt_add_executable(tst_ApplicationLogger tst_ApplicationLogger.cpp)
target_link_libraries(tst_ApplicationLogger PRIVATE breezedesk_core Qt6::Test)
breezedesk_enable_strict_warnings(tst_ApplicationLogger)
add_test(NAME Utils.ApplicationLogger COMMAND tst_ApplicationLogger
    -o -,txt
    -o "${CMAKE_BINARY_DIR}/Testing/Temporary/Utils.ApplicationLogger.txt,txt")
set_tests_properties(Utils.ApplicationLogger PROPERTIES LABELS "Utils;Unit;Privacy")
