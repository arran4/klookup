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
    GlobalAccel
    Config
    I18n
    Runner
    DBusAddons
    WidgetsAddons
    XmlGui
    Wallet
    Notifications
)

find_package(ZLIB REQUIRED)

add_subdirectory(icons)
add_subdirectory(src)
add_subdirectory(providers/wikipedia)
add_subdirectory(providers/github)
add_subdirectory(providers/mangaupdates)
add_subdirectory(providers/anilist)
add_subdirectory(providers/duckduckgo)
add_subdirectory(providers/openlibrary)
add_subdirectory(providers/themoviedb)
add_subdirectory(providers/omdb)
add_subdirectory(providers/myanimelist)
add_subdirectory(providers/mangadex)
add_subdirectory(providers/mangabaka)
add_subdirectory(providers/spotify)
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
    provider_duckduckgo
    provider_openlibrary
    provider_themoviedb
    provider_omdb
    provider_myanimelist
    provider_mangadex
    provider_mangabaka
    provider_spotify
)

string(REPLACE ";" "," builtin_native_ids "${KLOOKUP_PROVIDERS}")
target_compile_definitions(klookup_core PRIVATE KLOOKUP_BUILTIN_NATIVE_IDS="${builtin_native_ids}")

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
    get_property(test_targets DIRECTORY tests PROPERTY BUILDSYSTEM_TARGETS)
    get_property(registered_tests DIRECTORY tests PROPERTY TESTS)
    set(covered_test_sources)
    foreach(test_target IN LISTS test_targets)
        get_target_property(test_type ${test_target} TYPE)
        if(test_type STREQUAL "EXECUTABLE")
            add_dependencies(klookup_with_plugins ${test_target})
            if(test_target MATCHES "^test_" AND NOT test_target MATCHES "_helper$" AND NOT test_target IN_LIST registered_tests)
                message(FATAL_ERROR "Test executable omitted from CTest: ${test_target}")
            endif()
            get_target_property(test_sources ${test_target} SOURCES)
            list(APPEND covered_test_sources ${test_sources})
        endif()
    endforeach()
    file(GLOB expected_test_sources RELATIVE "${CMAKE_SOURCE_DIR}/tests" CONFIGURE_DEPENDS "tests/test_*.cpp")
    foreach(test_source IN LISTS expected_test_sources)
        if(NOT test_source IN_LIST covered_test_sources)
            message(FATAL_ERROR "Test source omitted from executable targets: ${test_source}")
        endif()
    endforeach()
endif()

install(DIRECTORY providers.d/ DESTINATION ${KDE_INSTALL_DATADIR}/klookup/providers.d)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
