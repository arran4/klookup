find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_appicon test_appicon.cpp)
target_link_libraries(test_appicon Qt6::Test klookup_core)
target_compile_definitions(test_appicon PRIVATE ICON_SOURCE_DIR="${CMAKE_SOURCE_DIR}/icons")
add_test(NAME test_appicon COMMAND test_appicon)

add_executable(test_clipboardpolicy test_clipboardpolicy.cpp)
target_link_libraries(test_clipboardpolicy Qt6::Test klookup_core)
add_test(NAME test_clipboardpolicy COMMAND test_clipboardpolicy)

add_executable(test_clipboardinput test_clipboardinput.cpp)
target_link_libraries(test_clipboardinput Qt6::Test klookup_core)
add_test(NAME test_clipboardinput COMMAND test_clipboardinput)

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

add_executable(test_resultactions test_resultactions.cpp)
target_link_libraries(test_resultactions Qt6::Test klookup_core)
add_test(NAME test_resultactions COMMAND test_resultactions)

add_executable(test_sourcelink test_sourcelink.cpp)
target_link_libraries(test_sourcelink Qt6::Test klookup_core)
add_test(NAME test_sourcelink COMMAND test_sourcelink)
add_executable(test_provider_mangadex test_provider_mangadex.cpp ../providers/mangadex/provider_mangadex.cpp)
target_link_libraries(test_provider_mangadex Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network)
add_test(NAME test_provider_mangadex COMMAND test_provider_mangadex)

add_executable(test_provider_mangabaka test_provider_mangabaka.cpp ../providers/mangabaka/provider_mangabaka.cpp)
target_link_libraries(test_provider_mangabaka Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network)
add_test(NAME test_provider_mangabaka COMMAND test_provider_mangabaka)

add_executable(test_provider_network test_provider_network.cpp ../providers/duckduckgo/provider_duckduckgo.cpp)
target_link_libraries(test_provider_network Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network)
add_test(NAME test_provider_network COMMAND test_provider_network)

add_library(provider_mock_alpha MODULE provider_mock_alpha.cpp)
target_link_libraries(provider_mock_alpha klookup_core Qt6::Core)
target_include_directories(provider_mock_alpha PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_library(provider_mock_beta MODULE provider_mock_beta.cpp)
target_link_libraries(provider_mock_beta klookup_core Qt6::Core)
target_include_directories(provider_mock_beta PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(test_provider_settings_lifecycle_helper test_provider_settings_lifecycle_helper.cpp)
target_link_libraries(test_provider_settings_lifecycle_helper klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network Qt6::DBus KF6::CoreAddons KF6::ConfigCore KF6::ConfigGui KF6::I18n KF6::DBusAddons KF6::XmlGui KF6::Wallet KF6::Notifications)
add_dependencies(test_provider_settings_lifecycle_helper provider_mock_alpha provider_mock_beta)

set_target_properties(test_provider_settings_lifecycle_helper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/provider-settings-lifecycle"
)

# Ensure the mock providers are output directly into a 'providers' directory next to the helper executable
set_target_properties(provider_mock_alpha PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/provider-settings-lifecycle/providers"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/provider-settings-lifecycle/providers"
)
set_target_properties(provider_mock_beta PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/provider-settings-lifecycle/providers"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/provider-settings-lifecycle/providers"
)

add_executable(test_provider_settings_lifecycle test_provider_settings_lifecycle.cpp)
target_link_libraries(test_provider_settings_lifecycle Qt6::Test Qt6::Core)
target_compile_definitions(test_provider_settings_lifecycle PRIVATE HELPER_PATH="$<TARGET_FILE:test_provider_settings_lifecycle_helper>")
add_test(NAME test_provider_settings_lifecycle COMMAND test_provider_settings_lifecycle)

add_executable(test_searchcontinuation test_searchcontinuation.cpp)
target_link_libraries(test_searchcontinuation Qt6::Test klookup_core)
add_test(NAME test_searchcontinuation COMMAND test_searchcontinuation)

foreach(target test_provider_mangabaka test_provider_mangadex)
    target_compile_definitions(${target} PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
endforeach()

add_executable(test_providerauthentication test_providerauthentication.cpp)
target_link_libraries(test_providerauthentication Qt6::Test klookup_core)
add_test(NAME test_providerauthentication COMMAND test_providerauthentication)

add_executable(test_provider_wikipedia test_provider_wikipedia.cpp ../providers/wikipedia/provider_wikipedia.cpp)
target_link_libraries(test_provider_wikipedia Qt6::Test klookup_core)
add_test(NAME test_provider_wikipedia COMMAND test_provider_wikipedia)

add_executable(test_provider_spotify test_provider_spotify.cpp ../providers/spotify/provider_spotify.cpp)
target_link_libraries(test_provider_spotify Qt6::Test klookup_core KF6::ConfigCore Qt6::Core Qt6::Gui Qt6::Network)
add_test(NAME test_provider_spotify COMMAND test_provider_spotify)

add_executable(test_recentactivity test_recentactivity.cpp)
target_link_libraries(test_recentactivity Qt6::Test klookup_core)
add_test(NAME test_recentactivity COMMAND test_recentactivity)

add_executable(test_globalshortcuts test_globalshortcuts.cpp)
target_link_libraries(test_globalshortcuts Qt6::Test klookup_core)
add_test(NAME test_globalshortcuts COMMAND test_globalshortcuts)

add_executable(test_independent_windows test_independent_windows.cpp)
target_link_libraries(test_independent_windows Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network Qt6::DBus KF6::CoreAddons KF6::ConfigCore KF6::ConfigGui KF6::I18n KF6::DBusAddons KF6::XmlGui KF6::Wallet KF6::Notifications)
add_test(NAME test_independent_windows COMMAND test_independent_windows)

add_executable(test_autostart test_autostart.cpp)
target_link_libraries(test_autostart Qt6::Test klookup_core KF6::ConfigCore)
add_test(NAME test_autostart COMMAND test_autostart)

add_executable(test_resultcontent test_resultcontent.cpp)
target_link_libraries(test_resultcontent Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network Qt6::DBus KF6::CoreAddons KF6::ConfigCore KF6::ConfigGui KF6::I18n KF6::DBusAddons KF6::XmlGui KF6::Wallet KF6::Notifications)
add_test(NAME test_resultcontent COMMAND test_resultcontent)

add_executable(test_providerapiv1 test_providerapiv1.cpp)
target_link_libraries(test_providerapiv1 Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network Qt6::DBus KF6::CoreAddons KF6::ConfigCore KF6::ConfigGui KF6::I18n KF6::DBusAddons KF6::XmlGui KF6::Wallet KF6::Notifications)
target_compile_definitions(test_providerapiv1 PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME test_providerapiv1 COMMAND test_providerapiv1)

add_executable(test_providerdiscovery test_providerdiscovery.cpp)
target_link_libraries(test_providerdiscovery Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network Qt6::DBus KF6::CoreAddons KF6::ConfigCore KF6::ConfigGui KF6::I18n KF6::DBusAddons KF6::XmlGui KF6::Wallet KF6::Notifications)
target_compile_definitions(test_providerdiscovery PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME test_providerdiscovery COMMAND test_providerdiscovery)

find_program(DBUS_RUN_SESSION dbus-run-session)
if(DBUS_RUN_SESSION)
    add_executable(test_shortcut_activation test_shortcut_activation.cpp)
    target_link_libraries(test_shortcut_activation Qt6::Test Qt6::DBus)
    target_compile_definitions(test_shortcut_activation PRIVATE KLOOKUP_BINARY="$<TARGET_FILE:klookup>")
    add_dependencies(test_shortcut_activation klookup)
    add_test(NAME test_shortcut_activation COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:test_shortcut_activation>)
endif()
