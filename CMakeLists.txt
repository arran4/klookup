cmake_minimum_required(VERSION 3.16)

# Authoritative version determination:
# 1. Explicit user/packager override: -DKLOOKUP_VERSION=x.y.z
# 2. VERSION file in source root
# 3. Exact git tag if building in a git tree
# 4. Fallback default version
set(KLOOKUP_DEFAULT_VERSION "0.1.0")

if(DEFINED KLOOKUP_VERSION AND NOT KLOOKUP_VERSION STREQUAL "")
    set(_KLOOKUP_DETECTED_VERSION "${KLOOKUP_VERSION}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _KLOOKUP_FILE_VERSION)
    list(GET _KLOOKUP_FILE_VERSION 0 _KLOOKUP_FIRST_LINE)
    string(STRIP "${_KLOOKUP_FIRST_LINE}" _KLOOKUP_FILE_VERSION)
    if(NOT _KLOOKUP_FILE_VERSION STREQUAL "")
        set(_KLOOKUP_DETECTED_VERSION "${_KLOOKUP_FILE_VERSION}")
    endif()
endif()

if(NOT DEFINED _KLOOKUP_DETECTED_VERSION AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _GIT_EXACT_TAG
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_GIT_EXACT_TAG MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+.*)$")
            set(_KLOOKUP_DETECTED_VERSION "${CMAKE_MATCH_1}")
        endif()
    endif()
endif()

if(NOT DEFINED _KLOOKUP_DETECTED_VERSION)
    set(_KLOOKUP_DETECTED_VERSION "${KLOOKUP_DEFAULT_VERSION}")
endif()

string(REGEX REPLACE "^v" "" KLOOKUP_VERSION_STRING "${_KLOOKUP_DETECTED_VERSION}")

if(KLOOKUP_VERSION_STRING MATCHES "^([0-9]+(\\.[0-9]+(\\.[0-9]+)?)?)")
    set(KLOOKUP_NUMERIC_VERSION "${CMAKE_MATCH_1}")
else()
    set(KLOOKUP_NUMERIC_VERSION "${KLOOKUP_DEFAULT_VERSION}")
endif()

set(KLOOKUP_VERSION_STRING "${KLOOKUP_VERSION_STRING}" CACHE STRING "Authoritative KLookup version string" FORCE)

project(klookup VERSION ${KLOOKUP_NUMERIC_VERSION} LANGUAGES CXX)

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
add_subdirectory(providers/duckduckgo)
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
    provider_duckduckgo
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
