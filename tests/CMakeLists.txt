find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(test_searchdashboard test_searchdashboard.cpp)
target_link_libraries(test_searchdashboard Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui)
add_test(NAME test_searchdashboard COMMAND test_searchdashboard)
