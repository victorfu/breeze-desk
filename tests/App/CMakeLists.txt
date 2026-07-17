qt_add_executable(tst_MaintenanceController tst_MaintenanceController.cpp)
target_link_libraries(tst_MaintenanceController PRIVATE
    breezedesk_maintenance breezedesk_database breezedesk_ipc breezedesk_update
    Qt6::Test Qt6::Sql)
breezedesk_enable_strict_warnings(tst_MaintenanceController)
add_test(NAME App.MaintenanceController COMMAND tst_MaintenanceController)
set_tests_properties(App.MaintenanceController PROPERTIES LABELS "App;Unit")

qt_add_executable(tst_ModelTestController
    tst_ModelTestController.cpp
    ${PROJECT_SOURCE_DIR}/resources/resources.qrc)
target_link_libraries(tst_ModelTestController PRIVATE
    breezedesk_model_test breezedesk_models breezedesk_ipc Qt6::Test)
add_dependencies(tst_ModelTestController breezedesk-asr-worker)
breezedesk_enable_strict_warnings(tst_ModelTestController)
add_test(NAME App.ModelTestController COMMAND tst_ModelTestController)
set_tests_properties(App.ModelTestController PROPERTIES LABELS "App;Models;Unit")

qt_add_executable(tst_WorkerRegistry tst_WorkerRegistry.cpp)
target_link_libraries(tst_WorkerRegistry PRIVATE breezedesk_platform Qt6::Test)
breezedesk_enable_strict_warnings(tst_WorkerRegistry)
add_test(NAME App.WorkerRegistry COMMAND tst_WorkerRegistry)
set_tests_properties(App.WorkerRegistry PROPERTIES LABELS "App;Unit;Platform")

qt_add_executable(tst_InstallSourceClassifier tst_InstallSourceClassifier.cpp)
target_link_libraries(tst_InstallSourceClassifier PRIVATE breezedesk_platform Qt6::Test)
breezedesk_enable_strict_warnings(tst_InstallSourceClassifier)
add_test(NAME App.InstallSourceClassifier COMMAND tst_InstallSourceClassifier)
set_tests_properties(App.InstallSourceClassifier PROPERTIES LABELS "App;Unit;Platform")

qt_add_executable(tst_UpdateCoordinator tst_UpdateCoordinator.cpp)
target_link_libraries(tst_UpdateCoordinator PRIVATE breezedesk_update breezedesk_platform Qt6::Test)
breezedesk_enable_strict_warnings(tst_UpdateCoordinator)
add_test(NAME App.UpdateCoordinator COMMAND tst_UpdateCoordinator)
set_tests_properties(App.UpdateCoordinator PROPERTIES LABELS "App;Unit;Update")
