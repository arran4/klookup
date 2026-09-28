find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(test_searchdashboard test_searchdashboard.cpp)
target_link_libraries(test_searchdashboard Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui)
add_test(NAME test_searchdashboard COMMAND test_searchdashboard)

add_executable(test_mainwindow test_mainwindow.cpp)
target_link_libraries(test_mainwindow Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network Qt6::DBus KF6::CoreAddons KF6::ConfigCore KF6::ConfigGui KF6::I18n KF6::DBusAddons KF6::XmlGui KF6::Wallet KF6::Notifications)
add_test(NAME test_mainwindow COMMAND test_mainwindow)

add_executable(test_session_lifecycle test_session_lifecycle.cpp)
target_link_libraries(test_session_lifecycle Qt6::Test Qt6::Core)
add_test(NAME test_session_lifecycle COMMAND test_session_lifecycle)

add_executable(test_session_lifecycle_helper session_lifecycle_helper.cpp)
target_link_libraries(test_session_lifecycle_helper klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network Qt6::DBus KF6::CoreAddons KF6::ConfigCore KF6::ConfigGui KF6::I18n KF6::DBusAddons KF6::XmlGui KF6::Wallet KF6::Notifications)

add_executable(test_searchtab test_searchtab.cpp)
target_link_libraries(test_searchtab Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui)
add_test(NAME test_searchtab COMMAND test_searchtab)
add_executable(test_provider_network test_provider_network.cpp ../providers/duckduckgo/provider_duckduckgo.cpp)
target_link_libraries(test_provider_network Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network)
add_test(NAME test_provider_network COMMAND test_provider_network)

add_library(provider_mock_alpha MODULE provider_mock_alpha.cpp)
target_link_libraries(provider_mock_alpha klookup_core Qt6::Core)
target_include_directories(provider_mock_alpha PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_library(provider_mock_beta MODULE provider_mock_beta.cpp)
target_link_libraries(provider_mock_beta klookup_core Qt6::Core)
target_include_directories(provider_mock_beta PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Ensure the mock providers are output directly into a 'providers' directory next to the helper executable
set_target_properties(provider_mock_alpha PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:test_provider_settings_lifecycle_helper>/providers"
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:test_provider_settings_lifecycle_helper>/providers"
)
set_target_properties(provider_mock_beta PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:test_provider_settings_lifecycle_helper>/providers"
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:test_provider_settings_lifecycle_helper>/providers"
)

add_executable(test_provider_settings_lifecycle_helper test_provider_settings_lifecycle_helper.cpp)
target_link_libraries(test_provider_settings_lifecycle_helper klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network Qt6::DBus KF6::CoreAddons KF6::ConfigCore KF6::ConfigGui KF6::I18n KF6::DBusAddons KF6::XmlGui KF6::Wallet KF6::Notifications)
add_dependencies(test_provider_settings_lifecycle_helper provider_mock_alpha provider_mock_beta)

add_executable(test_provider_settings_lifecycle test_provider_settings_lifecycle.cpp)
target_link_libraries(test_provider_settings_lifecycle Qt6::Test Qt6::Core)
add_test(NAME test_provider_settings_lifecycle COMMAND test_provider_settings_lifecycle)
