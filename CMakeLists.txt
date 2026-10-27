cmake_minimum_required(VERSION 3.16)
project(klookup VERSION 0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(QT_MAJOR_VERSION 6)
set(QT_MIN_VERSION "6.0.0")

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    DBus
    Qml
)

find_package(KF6 REQUIRED COMPONENTS
    CoreAddons
    Config
    I18n
    Runner
    DBusAddons
    WidgetsAddons
    XmlGui
    Wallet
    Notifications
)

add_subdirectory(icons)
add_subdirectory(src)
add_subdirectory(providers/wikipedia)
add_subdirectory(providers/github)
add_subdirectory(providers/mangaupdates)
add_subdirectory(providers/anilist)
add_subdirectory(providers/tvmaze)
add_subdirectory(providers/lyrics_ovh)
add_subdirectory(providers/duckduckgo)
add_subdirectory(providers/openlibrary)
add_subdirectory(providers/themoviedb)
add_subdirectory(providers/omdb)
add_subdirectory(providers/myanimelist)
add_subdirectory(providers/mangadex)
add_subdirectory(providers/mangabaka)
add_subdirectory(runner)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

set(KLOOKUP_PROVIDERS
    provider_wikipedia
    provider_github
    provider_mangaupdates
    provider_anilist
    provider_tvmaze
    provider_lyrics_ovh
    provider_duckduckgo
    provider_openlibrary
    provider_themoviedb
    provider_omdb
    provider_myanimelist
    provider_mangadex
    provider_mangabaka
)

foreach(provider ${KLOOKUP_PROVIDERS})
    set_target_properties(${provider} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:klookup>/providers"
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:klookup>/providers"
    )
endforeach()

add_dependencies(klookup ${KLOOKUP_PROVIDERS})

add_custom_target(klookup_with_plugins ALL
    DEPENDS klookup
        ${KLOOKUP_PROVIDERS}
)

if(BUILD_TESTING)
    add_dependencies(klookup_with_plugins test_appicon)
    add_dependencies(klookup_with_plugins test_searchdashboard test_mainwindow test_session_lifecycle test_session_lifecycle_helper test_searchtab test_provider_network test_provider_settings_lifecycle test_provider_settings_lifecycle_helper provider_mock_alpha provider_mock_beta)
    add_dependencies(klookup_with_plugins test_clipboardinput test_clipboardpolicy)
endif()

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
