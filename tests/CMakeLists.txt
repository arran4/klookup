find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(test_searchdashboard test_searchdashboard.cpp)
target_link_libraries(test_searchdashboard Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui)
add_test(NAME test_searchdashboard COMMAND test_searchdashboard)

add_executable(test_mainwindow test_mainwindow.cpp)
target_link_libraries(test_mainwindow Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network Qt6::DBus KF6::CoreAddons KF6::ConfigCore KF6::ConfigGui KF6::I18n KF6::DBusAddons KF6::XmlGui KF6::Wallet KF6::Notifications)
add_test(NAME test_mainwindow COMMAND test_mainwindow)

add_executable(test_searchtab test_searchtab.cpp)
target_link_libraries(test_searchtab Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui)
add_test(NAME test_searchtab COMMAND test_searchtab)
